#include "iocp_connection.hpp"
#include "utils/perf_timer/perf_timer.hpp"

#undef max
#undef min

namespace WFX::OSSpecific {

IocpConnectionHandler::IocpConnectionHandler()
{
    WSADATA wsaData;
    if(WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        logger_.Fatal("WSAStartup failed");

    if(LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
        logger_.Fatal("Incorrect Winsock version");

    allocPool_.PreWarmAll(32);
}

IocpConnectionHandler::~IocpConnectionHandler()
{
    // Final cleanup on destruction
    InternalCleanup();
}

bool IocpConnectionHandler::Initialize(const std::string& host, int port)
{
    listenSocket_ = WSASocket(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if(listenSocket_ == INVALID_SOCKET)
        return logger_.Error("[IOCP]: WSASocket failed"), false;

    sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = inet_addr(host.c_str());
    addr.sin_port        = htons(port);

    if(bind(listenSocket_, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
        return logger_.Error("[IOCP]: bind failed"), false;

    if(listen(listenSocket_, SOMAXCONN) == SOCKET_ERROR)
        return logger_.Error("[IOCP]: listen failed"), false;

    iocp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if(!iocp_)
        return logger_.Error("[IOCP]: CreateIoCompletionPort failed"), false;

    if(!CreateIoCompletionPort((HANDLE)listenSocket_, iocp_, (ULONG_PTR)listenSocket_, 0))
        return logger_.Error("[IOCP]: IOCP association failed"), false;

    return true;
}

void IocpConnectionHandler::ResumeRecieve(WFXSocket socket)
{
    // Simply 're-arm' WSARecv
    PostReceive(socket);
}

void IocpConnectionHandler::Receive(WFXSocket socket, ReceiveCallback callback)
{
    if(!callback) {
        logger_.Error("[IOCP]: Invalid callback in 'Recieve'");
        return;
    }
    
    WFX_PROFILE_BLOCK_START(Recieve_)
    if(!receiveCallbacks_.Insert(socket, std::move(callback)))
        logger_.Fatal("[IOCP]: Failed to insert Receive Callback");

    // Hand off PostReceive to IOCP thread. sizeof(PostRecvOp) rounded to 64 bytes
    PostRecvOp* op = static_cast<PostRecvOp*>(allocPool_.Allocate(sizeof(PostRecvOp)));
    
    op->operationType = PerIoOperationType::ARM_RECV;
    op->socket        = socket;
    
    if(!PostQueuedCompletionStatus(iocp_, 0, 0, &op->overlapped)) {
        logger_.Error("[IOCP]: Failed to queue PostReceive");
        allocPool_.Free(op, sizeof(PostRecvOp));
        Close(socket);
    }
    WFX_PROFILE_BLOCK_END(Recieve_)
}

int IocpConnectionHandler::Write(WFXSocket socket, const char* buffer, size_t length)
{
    void* outBuffer = allocPool_.Allocate(length);
    if(!outBuffer)
        return -1;

    std::memcpy(outBuffer, buffer, length);

    PerIoData* ioData = static_cast<PerIoData*>(allocPool_.Allocate(sizeof(PerIoData)));

    ioData->overlapped    = { 0 };
    ioData->operationType = PerIoOperationType::SEND;
    ioData->wsaBuf.buf    = static_cast<char*>(outBuffer);
    ioData->wsaBuf.len    = static_cast<ULONG>(length);

    DWORD bytesSent;
    int ret = WSASend(socket, &ioData->wsaBuf, 1, &bytesSent, 0, &ioData->overlapped, nullptr);
    if(ret == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
        SafeDeleteIoData(ioData);
        return -1;
    }

    return static_cast<int>(length);
}

void IocpConnectionHandler::Close(WFXSocket socket)
{
    CancelIoEx((HANDLE)socket, NULL);
    shutdown(socket, SD_BOTH);
    closesocket(socket);

    if(!receiveCallbacks_.Erase(socket))
        logger_.Warn("[IOCP]: Failed to erase Receive Callback for socket: ", socket);
}

void IocpConnectionHandler::Run(AcceptedConnectionCallback onAccepted)
{
    logger_.Info("[IOCP]: Starting connection handler...");
    running_ = true;

    if(!onAccepted)
        logger_.Fatal("[IOCP]: Failed to get 'onAccepted' callback");
    acceptCallback_ = std::move(onAccepted);

    if(!acceptManager_.Initialize(listenSocket_, iocp_))
        logger_.Fatal("[IOCP]: Failed to initialize AcceptExManager");

    logger_.Info("[IOCP]: AcceptExManager initialized with ", AcceptExManager::MAX_SLOTS, " pending accepts.");

    unsigned int cores          = std::thread::hardware_concurrency();
    unsigned int iocpThreads    = std::max(2u, cores);
    unsigned int offloadThreads = std::max(2u, cores);

    if(!CreateWorkerThreads(iocpThreads, offloadThreads))
        return logger_.Fatal("[IOCP]: Failed to start worker threads.");
}

void IocpConnectionHandler::Stop()
{
    running_ = false;

    // Wake up all our sleepy ass threads and start to prepare for ANHILATION
    for(size_t i = 0; i < workerThreads_.size(); ++i)
        PostQueuedCompletionStatus(iocp_, 0, 0, nullptr);
}

void IocpConnectionHandler::PostReceive(WFXSocket socket)
{
    WFX_PROFILE_BLOCK_START(PostReceive_Lease);

    void* buf = allocPool_.Allocate(4096);
    if(!buf) {
        Close(socket);
        return;
    }

    // Ensure pages are touched — avoids kernel-to-user page fault stalls
    ((volatile char*)buf)[0] = 0;

    WFX_PROFILE_BLOCK_END(PostReceive_Lease);

    WFX_PROFILE_BLOCK_START(PostReceive_New);

    PerIoData* ioData = static_cast<PerIoData*>(allocPool_.Allocate(sizeof(PerIoData)));
    ioData->overlapped    = { 0 };
    ioData->operationType = PerIoOperationType::RECV;
    ioData->socket        = socket;
    ioData->wsaBuf.buf    = static_cast<char*>(buf);
    ioData->wsaBuf.len    = 4096;

    WFX_PROFILE_BLOCK_END(PostReceive_New);

    DWORD flags = 0, bytesRecv = 0;
    int ret = WSARecv(socket, &ioData->wsaBuf, 1, &bytesRecv, &flags, &ioData->overlapped, nullptr);
    if(ret == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
        logger_.Warn("[IOCP]: WSARecv immediate failure: ", WSAGetLastError());
        SafeDeleteIoData(ioData);
        Close(socket);
    }
}

void IocpConnectionHandler::WorkerLoop()
{
    DWORD       bytesTransferred;
    ULONG_PTR   key;
    OVERLAPPED* overlapped;

    // One quick protection, OVERLAPPED must be the first thing in the structs we use
    static_assert(offsetof(PerIoBase, overlapped) == 0, "OVERLAPPED must be first!");

    while(running_) {
        BOOL result = GetQueuedCompletionStatus(iocp_, &bytesTransferred, &key, &overlapped, 1000);
        if(!result || !overlapped)
            continue;

        auto*              base   = reinterpret_cast<PerIoBase*>(overlapped);
        PerIoOperationType opType = base->operationType;

        switch(opType) {
            case PerIoOperationType::ARM_RECV:
            {
                std::unique_ptr<PostRecvOp, PostRecvOpDeleter> op(static_cast<PostRecvOp*>(base), PostRecvOpDeleter{this});
                PostReceive(op->socket);
                break;
            }

            case PerIoOperationType::RECV:
            {
                WFX_PROFILE_BLOCK_START(RECV_Handle);
                std::unique_ptr<PerIoData, PerIoDataDeleter> ioData(static_cast<PerIoData*>(base), PerIoDataDeleter{this});
                SOCKET clientSocket = ioData->socket;

                // FILTER ZERO-LENGTH (clean disconnect)
                if(bytesTransferred <= 0) {
                    Close(clientSocket);
                    break;
                }

                ReceiveCallback callback;
                if(!receiveCallbacks_.Get(clientSocket, callback))
                    logger_.Fatal("[IOCP]: Failed to get Receive Callback");
                
                // Take ownership of buffer before we free ioData
                char*  rawBuf = ioData->wsaBuf.buf;
                size_t rawLen = ioData->wsaBuf.len;

                ioData->wsaBuf.buf = nullptr;
                ioData->wsaBuf.len = 0;

                // Enqueue callback to offload thread
                offloadCallbacks_.enqueue([this, rawBuf, rawLen, cb = std::move(callback)]() mutable {
                    WFX_PROFILE_BLOCK_START(Offloaded_CB);
                    // Move buffer into callback with a custom deleter
                    std::unique_ptr<char[], std::function<void(char*)>> movedBuf(
                        rawBuf,
                        [this, rawLen](char* p) { this->allocPool_.Free(p, rawLen); }
                    );

                    cb(std::move(movedBuf), rawLen);
                    WFX_PROFILE_BLOCK_END(Offloaded_CB);
                });

                WFX_PROFILE_BLOCK_END(RECV_Handle);
                break;
            }

            case PerIoOperationType::SEND:
            {
                std::unique_ptr<PerIoData, PerIoDataDeleter> ioData(static_cast<PerIoData*>(base), PerIoDataDeleter{this});
                break; // just cleanup, no callback
            }

            case PerIoOperationType::ACCEPT:
            {
                WFX_PROFILE_BLOCK_START(ACCEPT_Handle);
                acceptManager_.HandleAcceptCompletion(static_cast<PerIoContext*>(base));
                WFX_PROFILE_BLOCK_END(ACCEPT_Handle);
                break;
            }

            case PerIoOperationType::ACCEPT_DEFERRED:
            {
                WFX_PROFILE_BLOCK_START(ACCEPT_Deferred);
                SOCKET client = static_cast<SOCKET>(key);
                acceptManager_.HandleSocketOptions(client);
                
                offloadCallbacks_.enqueue([this, client]() {
                    acceptCallback_(client);
                });

                WFX_PROFILE_BLOCK_END(ACCEPT_Deferred);
                
                break;
            }

            default:
                logger_.Warn("Unknown IOCP operation type: ", (int)opType);
                break;
        }
    }
}

bool IocpConnectionHandler::CreateWorkerThreads(unsigned int iocpThreads, unsigned int offloadThreads)
{
    // Launch IOCP worker threads
    for(unsigned int i = 0; i < iocpThreads; ++i)
        workerThreads_.emplace_back(&IocpConnectionHandler::WorkerLoop, this);

    // Launch offload callback threads
    for(unsigned int i = 0; i < offloadThreads; ++i)
        offloadThreads_.emplace_back([this]() {
            while(running_) {
                std::function<void(void)> cb;
                if(offloadCallbacks_.wait_dequeue_timed(cb, std::chrono::milliseconds(10)))
                    cb();
            }
        });

    return true;
}

void IocpConnectionHandler::SafeDeleteIoData(PerIoData* data)
{
    if(!data) return;
    if(data->wsaBuf.buf) allocPool_.Free(data->wsaBuf.buf, data->wsaBuf.len);
    allocPool_.Free(data, sizeof(PerIoData));
}

void IocpConnectionHandler::InternalCleanup()
{
    for(auto& t : workerThreads_)
        if(t.joinable()) t.join();
    workerThreads_.clear();

    for(auto& t : offloadThreads_)
        if(t.joinable()) t.join();
    offloadThreads_.clear();

    acceptManager_.DeInitialize();

    if(listenSocket_ != WFX_INVALID_SOCKET) {
        closesocket(listenSocket_);
        listenSocket_ = WFX_INVALID_SOCKET;
    }

    if(iocp_) {
        CloseHandle(iocp_);
        iocp_ = nullptr;
    }

    WSACleanup();
    logger_.Info("[IOCP]: Cleaned up Socket resources.");
}

} // namespace WFX::OSSpecific