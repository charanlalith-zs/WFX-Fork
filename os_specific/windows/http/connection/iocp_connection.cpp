#include "iocp_connection.hpp"

namespace WFX::OSSpecific {

IocpConnectionHandler::IocpConnectionHandler()
    : listenSocket_(INVALID_SOCKET), iocp_(nullptr), running_(false),
      bufferPool_(1024 * 1024, [](std::size_t size) { return size * 2; })
{
    WSADATA wsaData;
    if(WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        logger_.Fatal("WSAStartup failed");

    if(LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
        logger_.Fatal("Incorrect Winsock version");
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

void IocpConnectionHandler::Receive(WFXSocket socket, RecieveCallback callback)
{
    {
        std::unique_lock<std::shared_mutex> lock(callbackMutex_);
        receiveCallbacks_[socket] = std::move(callback);
    }

    PostReceive(socket);
}

int IocpConnectionHandler::Write(int socket, const char* buffer, size_t length)
{
    void* outBuffer = bufferPool_.Lease(length);
    if(!outBuffer)
        return -1;

    memcpy(outBuffer, buffer, length);

    PerIoData* ioData = new PerIoData{};
    ZeroMemory(&ioData->overlapped, sizeof(ioData->overlapped));
    ioData->operationType = PerIoOperationType::SEND;
    ioData->buffer        = outBuffer;
    ioData->wsaBuf.buf    = static_cast<char*>(outBuffer);
    ioData->wsaBuf.len    = static_cast<ULONG>(length);

    DWORD bytesSent;
    int ret = WSASend(socket, &ioData->wsaBuf, 1, &bytesSent, 0, &ioData->overlapped, nullptr);
    if(ret == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
        SafeDeleteIoData(ioData);
        return -1;
    }

    return static_cast<int>(bytesSent);
}

void IocpConnectionHandler::Close(int socket)
{
    shutdown(socket, SD_BOTH);
    closesocket(socket);

    {
        std::unique_lock<std::shared_mutex> lock(callbackMutex_);
        receiveCallbacks_.erase(socket);
    }
}

void IocpConnectionHandler::Run(AcceptedConnectionCallback onAccepted)
{
    logger_.Info("[IOCP]: Starting connection handler...");
    running_ = true;

    if(!acceptManager_.Initialize(listenSocket_, iocp_, onAccepted))
        logger_.Fatal("[IOCP]: Failed to initialize AcceptExManager");

    logger_.Info("[IOCP]: AcceptExManager initialized with ", AcceptExManager::MAX_SLOTS, " pending accepts.");

    size_t fallbackThreads = std::thread::hardware_concurrency();
    if(!CreateWorkerThreads(fallbackThreads ? fallbackThreads : 4))
        return logger_.Fatal("[IOCP]: Failed to start worker threads.");
}

void IocpConnectionHandler::Stop()
{
    running_ = false;

    // Wake up all our sleepy ass threads and start to prepare for ANHILATION
    for(size_t i = 0; i < workerThreads_.size(); ++i)
        PostQueuedCompletionStatus(iocp_, 0, 0, nullptr);
}

void IocpConnectionHandler::PostReceive(int socket)
{
    void* buf = bufferPool_.Lease(4096);
    if(!buf) {
        Close(socket);
        return;
    }

    PerIoData* ioData = new PerIoData{};
    ZeroMemory(&ioData->overlapped, sizeof(ioData->overlapped));
    ioData->operationType = PerIoOperationType::RECV;
    ioData->buffer        = buf;
    ioData->socket        = socket;
    ioData->wsaBuf.buf    = static_cast<char*>(buf);
    ioData->wsaBuf.len    = 4096;

    DWORD flags = 0, bytesRecv = 0;
    int ret = WSARecv(socket, &ioData->wsaBuf, 1, &bytesRecv, &flags, &ioData->overlapped, nullptr);
    if(ret == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
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
        BOOL result = GetQueuedCompletionStatus(iocp_, &bytesTransferred, &key, &overlapped, INFINITE);
        if(!result || !overlapped)
            continue;

        auto*              base   = reinterpret_cast<PerIoBase*>(overlapped);
        PerIoOperationType opType = base->operationType;

        switch(opType) {
            case PerIoOperationType::RECV:
            {
                std::unique_ptr<PerIoData, PerIoDataDeleter> ioData(static_cast<PerIoData*>(base), PerIoDataDeleter{this});
                SOCKET clientSocket = ioData->socket;
                RecieveCallback callback;

                {
                    std::shared_lock<std::shared_mutex> lock(callbackMutex_);
                    auto it = receiveCallbacks_.find(clientSocket);
                    if(it != receiveCallbacks_.end())
                        callback = it->second;
                }

                if(callback)
                    callback(static_cast<char*>(ioData->buffer), bytesTransferred);

                PostReceive(clientSocket);
                break; // automatic cleanup via custom deleter
            }

            case PerIoOperationType::SEND:
            {
                std::unique_ptr<PerIoData, PerIoDataDeleter> ioData(static_cast<PerIoData*>(base), PerIoDataDeleter{this});
                break; // just cleanup, no callback
            }

            case PerIoOperationType::ACCEPT:
            {
                acceptManager_.HandleAcceptCompletion(static_cast<PerIoContext*>(base));
                break;
            }

            default:
                logger_.Warn("Unknown IOCP operation type: ", (int)opType);
                break;
        }
    }
}

bool IocpConnectionHandler::CreateWorkerThreads(size_t threadCount)
{
    for(size_t i = 0; i < threadCount; ++i)
        workerThreads_.emplace_back(&IocpConnectionHandler::WorkerLoop, this);

    return true;
}

void IocpConnectionHandler::SafeDeleteIoData(PerIoData* data)
{
    if(!data) return;
    if(data->buffer) bufferPool_.Release(data->buffer);
    delete data;
}

void IocpConnectionHandler::InternalCleanup()
{
    for(auto& t : workerThreads_)
        if(t.joinable()) t.join();

    workerThreads_.clear();

    acceptManager_.DeInitialize();

    if(listenSocket_ != WFX_INVALID_SOCKET) {
        closesocket(listenSocket_);
        listenSocket_ = WFX_INVALID_SOCKET;
    }

    if(iocp_) {
        CloseHandle(iocp_);
        iocp_ = nullptr;
    }

    receiveCallbacks_.clear();

    WSACleanup();
    logger_.Info("[IOCP]: Cleaned up Socket resources.");
}

} // namespace WFX::OSSpecific