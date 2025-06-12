#include "accept_ex_manager.hpp"

namespace WFX::OSSpecific {

bool AcceptExManager::Initialize(WFXSocket listenSocket, HANDLE iocp, const AcceptedConnectionCallback& cb)
{
    // Sanity check
    if(!cb) return logger_.Error("[AcceptExManager]: Failed to load callback function"), false;

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
        return logger_.Error("[AcceptExManager]: Failed to load AcceptEx"), false;

    // Zero out all the data for use
    memset(contexts_.data(), 0, sizeof(contexts_));

    for(int i = 0; i < MAX_SLOTS; ++i) {
        if(!PostAcceptAtSlot(i)) {
            logger_.Error("[AcceptExManager]: Failed to initialize AcceptEx at slot ", i);
            return false;
        }
    }

    logger_.Info("[AcceptExManager]: Initialized ", MAX_SLOTS, " concurrent AcceptEx slots");
    return true;
}

void AcceptExManager::DeInitialize()
{
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
        logger_.Error("[AcceptExManager]: Invalid socket on completion.");
        RepostAcceptAtSlot(slot);
        return;
    }

    if(!AssociateWithIOCP(client)) {
        logger_.Error("[AcceptExManager]: Failed to associate accepted socket with IOCP");
        closesocket(client);
        RepostAcceptAtSlot(slot);
        return;
    }
    
    setsockopt(client, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)&listenSocket_, sizeof(listenSocket_));

    // Performance socket tuning
    int one = 1;
    setsockopt(client, IPPROTO_TCP, TCP_NODELAY, (char*)&one, sizeof(one));
    setsockopt(client, SOL_SOCKET, SO_REUSEADDR, (char*)&one, sizeof(one));

    // Requeue immediately to minimize accept latency
    RepostAcceptAtSlot(slot);

    // Call the callback to pass control to higher layers
    acceptCallback_(client);
}

inline void AcceptExManager::SetSlot(int index)
{
    activeSlotsBits_ |= (1ULL << index);
}

inline void AcceptExManager::ClearSlot(int index)
{
    activeSlotsBits_ &= ~(1ULL << index);
}

int AcceptExManager::GetSlotFromPointer(PerIoContext* ctx)
{
    // Current Address - Base Address gives us the index based on how pointer arithmetic works. Pretty cool
    return static_cast<int>(
        (reinterpret_cast<uintptr_t>(ctx) - reinterpret_cast<uintptr_t>(&contexts_[0])) / sizeof(PerIoContext)
    );
}

bool AcceptExManager::AssociateWithIOCP(WFXSocket sock)
{
    return CreateIoCompletionPort((HANDLE)sock, iocp_, (ULONG_PTR)sock, 0);
}

void AcceptExManager::RepostAcceptAtSlot(int slot)
{
    ClearSlot(slot);

    // Try original slot — fast path
    if(PostAcceptAtSlot(slot)) return;

    // Calculate fallback from available slots
    uint64_t available = ~activeSlotsBits_;
    unsigned long fallbackIdx;
    bool hasFree;

#if defined(_MSC_VER)
    hasFree = _BitScanForward64(&fallbackIdx, available);
#else
    hasFree = (available != 0);
    fallbackIdx = __builtin_ctzll(available);
#endif

    // Branchless fallback — only log if this also fails
    bool success = hasFree && PostAcceptAtSlot(static_cast<int>(fallbackIdx));

    if(!success)
        logger_.Error("AcceptExManager: All AcceptEx slots exhausted — recovery failed.");
}

bool AcceptExManager::PostAcceptAtSlot(int slot)
{
    PerIoContext& ctx = contexts_[slot];
    ctx.overlapped    = { 0 };

    ctx.acceptSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if(ctx.acceptSocket == WFX_INVALID_SOCKET) {
        logger_.Error("AcceptExManager: WSASocket failed at slot ", slot, ", err=", WSAGetLastError());
        return false;
    }

    ctx.operationType = PerIoOperationType::ACCEPT;

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