#include "accept_ex_manager.hpp"

#include "utils/perf_timer/perf_timer.hpp"

namespace WFX::OSSpecific {

bool AcceptExManager::Initialize(WFXSocket listenSocket, HANDLE iocp)
{
    listenSocket_ = listenSocket;
    iocp_         = iocp;

    // Load AcceptEx and GetAcceptExSockaddrs
    GUID guidAcceptEx             = WSAID_ACCEPTEX;
    GUID guidGetAcceptExSockaddrs = WSAID_GETACCEPTEXSOCKADDRS;
    DWORD bytes                   = 0;

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

    result = WSAIoctl(
        listenSocket_,
        SIO_GET_EXTENSION_FUNCTION_POINTER,
        &guidGetAcceptExSockaddrs,
        sizeof(guidGetAcceptExSockaddrs),
        &lpfnGetAcceptExSockaddrs,
        sizeof(lpfnGetAcceptExSockaddrs),
        &bytes,
        nullptr,
        nullptr
    );

    if(result == SOCKET_ERROR || !lpfnGetAcceptExSockaddrs)
        return logger_.Error("[AcceptExManager]: Failed to load GetAcceptExSockaddrs"), false;

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

        if(ctx.socket != WFX_INVALID_SOCKET)
        {
            closesocket(ctx.socket);
            ctx.socket = WFX_INVALID_SOCKET;
        }

        ClearSlot(i);
    }

    activeSlotsBits_ = 0;
    
    logger_.Info("[AcceptExManager]: DeInitialized ", MAX_SLOTS, " AcceptEx slots");
}

void AcceptExManager::HandleAcceptCompletion(PerIoContext* ctx)
{
    const int slot   = GetSlotFromPointer(ctx);
    WFXSocket client = ctx->socket;

    if(client == WFX_INVALID_SOCKET) {
        logger_.Error("[AcceptExManager]: Invalid socket on completion.");
        RepostAcceptAtSlot(slot);
        return;
    }

    SOCKADDR* localSockaddr     = nullptr;
    SOCKADDR* remoteSockaddr    = nullptr;
    int       localSockaddrLen  = 0;
    int       remoteSockaddrLen = 0;

    // GetAcceptExSockaddrs MUST be called before setsockopt or using the client socket
    lpfnGetAcceptExSockaddrs(
        ctx->buffer,
        0,
        sizeof(SOCKADDR_IN) + 16,
        sizeof(SOCKADDR_IN) + 16,
        &localSockaddr,
        &localSockaddrLen,
        &remoteSockaddr,
        &remoteSockaddrLen
    );

    setsockopt(client, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)&listenSocket_, sizeof(listenSocket_));

    if(!AssociateWithIOCP(client)) {
        logger_.Error("[AcceptExManager]: Failed to associate accepted socket with IOCP");
        closesocket(client);
        RepostAcceptAtSlot(slot);
        return;
    }

    RepostAcceptAtSlot(slot);
 
    // Safe now to update context and options
    if(!PostQueuedCompletionStatus(iocp_, 0, (ULONG_PTR)client, &DEFERRED_ACCEPT_HANDLER.overlapped)) {
        logger_.Error("[AcceptExManager]: Failed to queue deferred accept for socket ", client);
        closesocket(client);
    }
}

void AcceptExManager::HandleSocketOptions(SOCKET client)
{
    int one = 1;
    setsockopt(client, IPPROTO_TCP, TCP_NODELAY, (char*)&one, sizeof(one));
    // setsockopt(client, SOL_SOCKET, SO_REUSEADDR, (char*)&one, sizeof(one));
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

    ctx.socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if(ctx.socket == WFX_INVALID_SOCKET) {
        logger_.Error("AcceptExManager: WSASocket failed at slot ", slot, ", err=", WSAGetLastError());
        return false;
    }

    ctx.operationType = PerIoOperationType::ACCEPT;

    DWORD bytesReceived = 0;
    BOOL result = lpfnAcceptEx(
        listenSocket_,
        ctx.socket,
        ctx.buffer,
        0,
        sizeof(SOCKADDR_IN) + 16,
        sizeof(SOCKADDR_IN) + 16,
        &bytesReceived,
        &ctx.overlapped
    );

    if(!result && WSAGetLastError() != ERROR_IO_PENDING) {
        logger_.Error("AcceptExManager: AcceptEx failed at slot ", slot, ", err=", WSAGetLastError());
        closesocket(ctx.socket);
        ctx.socket = WFX_INVALID_SOCKET;
        return false;
    }

    SetSlot(slot);
    return true;
}

} // namespace WFX::OSSpecific