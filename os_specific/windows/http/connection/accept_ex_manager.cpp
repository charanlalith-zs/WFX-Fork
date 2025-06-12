#include "accept_ex_manager.hpp"

namespace WFX::OSSpecific {

bool AcceptExManager::Initialize(WFXSocket listenSocket, HANDLE iocp, const AcceptedConnectionCallback& cb)
{
    // Rest of the main stuff
    acceptCallback_ = cb;
    listenSocket_   = listenSocket;
    iocp_           = iocp;

    // AcceptEx isn't a direct part of WinSock, we need to link it during runtime basically
    GUID  guidAcceptEx = WSAID_ACCEPTEX;
    DWORD bytes        = 0;

    int result = WSAIoctl(
        listenSocket_,
        SIO_GET_EXTENSION_FUNCTION_POINTER,
        &guidAcceptEx,
        sizeof(guidAcceptEx),
        &lpfnAcceptEx,
        sizeof(lpfnAcceptEx),
        &bytes,
        nullptr,
        nullptr
    );

    if(result == SOCKET_ERROR || !lpfnAcceptEx)
        return logger_.Error("Failed to load AcceptEx"), false;

    contexts_.fill({});

    for(int i = 0; i < MAX_SLOTS; ++i) {
        if(!PostAcceptAtSlot(i)) {
            logger_.Error("AcceptExManager: Failed to initialize AcceptEx at slot ", i);
            return false;
        }
    }

    logger_.Info("[AcceptExManager]: Initialized ", MAX_SLOTS, " concurrent AcceptEx slots");
    return true;
}

void AcceptExManager::DeInitialize()
{
    std::lock_guard<std::mutex> lock(reuseMutex_);

    for(int i = 0; i < MAX_SLOTS; ++i)
    {
        if(!(activeSlotsBits_ & (1ULL << i)))
            continue;

        PerIoContext& ctx = contexts_[i];

        if(ctx.acceptSocket != WFX_INVALID_SOCKET)
        {
            closesocket(ctx.acceptSocket);
            ctx.acceptSocket = WFX_INVALID_SOCKET;
        }

        ClearSlot(i);
    }

    activeSlotsBits_ = 0;
    acceptCallback_ = nullptr;
    
    logger_.Info("[AcceptExManager]: DeInitialized ", MAX_SLOTS, " AcceptEx slots");
}

void AcceptExManager::HandleAcceptCompletion(PerIoContext* ctx)
{
    const int slot   = GetSlotFromPointer(ctx);
    WFXSocket client = ctx->acceptSocket;

    if(client == WFX_INVALID_SOCKET) {
        logger_.Error("AcceptExManager: Invalid socket on completion.");
        RepostAcceptAtSlot(slot);
        return;
    }

    if(!AssociateWithIOCP(client)) {
        logger_.Error("AcceptExManager: Failed to associate accepted socket with IOCP");
        closesocket(client);
        RepostAcceptAtSlot(slot);
        return;
    }
    
    setsockopt(client, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)&listenSocket_, sizeof(listenSocket_));

    RepostAcceptAtSlot(slot);

    if(ctx->callbackHandler)
        ctx->callbackHandler(client);
    else {
        logger_.Warn("AcceptExManager: No callback set for accepted socket.");
        closesocket(client);
    }
}

inline void AcceptExManager::SetSlot(int index)
{
    activeSlotsBits_ |= (1ULL << index);
}

inline void AcceptExManager::ClearSlot(int index)
{
    activeSlotsBits_ &= ~(1ULL << index);
}

inline int AcceptExManager::FindFreeSlot()
{
    if(~activeSlotsBits_ == 0) return -1;
    unsigned long idx;
#if defined(_MSC_VER)
    _BitScanForward64(&idx, ~activeSlotsBits_);
#else
    idx = __builtin_ctzll(~activeSlotsBits_);
#endif
    return static_cast<int>(idx);
}

int AcceptExManager::GetSlotFromPointer(PerIoContext* ctx)
{
    uintptr_t base = reinterpret_cast<uintptr_t>(&contexts_[0]);
    uintptr_t ptr = reinterpret_cast<uintptr_t>(ctx);
    return static_cast<int>((ptr - base) / sizeof(PerIoContext));
}

bool AcceptExManager::AssociateWithIOCP(WFXSocket sock)
{
    return CreateIoCompletionPort((HANDLE)sock, iocp_, (ULONG_PTR)sock, 0);
}

void AcceptExManager::RepostAcceptAtSlot(int slot)
{
    ClearSlot(slot);
    if(!PostAcceptAtSlot(slot)) {
        std::lock_guard<std::mutex> lock(reuseMutex_);
        int fallbackSlot = FindFreeSlot();
        
        if(fallbackSlot >= 0)
            PostAcceptAtSlot(fallbackSlot);
        else
            logger_.Error("AcceptExManager: All AcceptEx slots exhausted — recovery failed.");
    }
}

bool AcceptExManager::PostAcceptAtSlot(int slot)
{
    PerIoContext& ctx = contexts_[slot];

    ZeroMemory(&ctx.overlapped, sizeof(OVERLAPPED));
    ctx.acceptSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if(ctx.acceptSocket == WFX_INVALID_SOCKET) {
        logger_.Error("AcceptExManager: WSASocket failed at slot ", slot, ", err=", WSAGetLastError());
        return false;
    }

    ctx.operationType   = PerIoOperationType::ACCEPT;
    ctx.callbackHandler = acceptCallback_;

    DWORD bytesReceived = 0;
    BOOL result = lpfnAcceptEx(
        listenSocket_,
        ctx.acceptSocket,
        ctx.buffer,
        0,
        sizeof(SOCKADDR_IN) + 16,
        sizeof(SOCKADDR_IN) + 16,
        &bytesReceived,
        &ctx.overlapped
    );

    if(!result && WSAGetLastError() != ERROR_IO_PENDING) {
        logger_.Error("AcceptExManager: AcceptEx failed at slot ", slot, ", err=", WSAGetLastError());
        closesocket(ctx.acceptSocket);
        ctx.acceptSocket = WFX_INVALID_SOCKET;
        return false;
    }

    SetSlot(slot);
    return true;
}

} // namespace WFX::OSSpecific