#ifndef WFX_WINDOWS_ACCEPT_EX_MANAGER_HPP
#define WFX_WINDOWS_ACCEPT_EX_MANAGER_HPP

#include "http/connection/http_connection.hpp"
#include "utils/logger/logger.hpp"

#include <winsock2.h>
#include <mswsock.h>
#include <array>
#include <mutex>
#include <functional>

namespace WFX::OSSpecific {

// I/O operation types for tracking overlapped operations
enum class PerIoOperationType {
    INVALID = -1,
    ARM_RECV,
    RECV,
    SEND,
    ACCEPT,
    ACCEPT_DEFERRED
};

struct PerIoBase {
    OVERLAPPED         overlapped{};
    PerIoOperationType operationType = PerIoOperationType::INVALID;
    WFXSocket          socket        = WFX_INVALID_SOCKET;

    ~PerIoBase() = default;
};

struct PerIoData : PerIoBase {
    WSABUF wsaBuf{};
};

struct PerIoContext : PerIoBase {
    char buffer[2 * (sizeof(SOCKADDR_IN) + 16)];
};

// Just to be consistent with IoBase
struct PostRecvOp : PerIoBase {};

// Used as a tag only
struct DeferredAcceptHandler : PerIoBase {
    DeferredAcceptHandler() { operationType = PerIoOperationType::ACCEPT_DEFERRED; }
};
static DeferredAcceptHandler DEFERRED_ACCEPT_HANDLER;

// Forward declare logger
using namespace WFX::Utils;

class AcceptExManager {
public:
    constexpr static int MAX_SLOTS = 4096;

    AcceptExManager() = default;

    bool Initialize(WFXSocket listenSocket, HANDLE iocp);
    void DeInitialize();
    void HandleAcceptCompletion(PerIoContext* ctx);
    void HandleSocketOptions(SOCKET);

private:
    // IMP
    LPFN_ACCEPTEX             lpfnAcceptEx             = nullptr;
    LPFN_GETACCEPTEXSOCKADDRS lpfnGetAcceptExSockaddrs = nullptr;

    WFXSocket listenSocket_;
    HANDLE    iocp_;
    Logger&   logger_ = Logger::GetInstance();

    std::array<PerIoContext, MAX_SLOTS> contexts_;
    uint64_t                            activeSlotsBits_ = 0;

    inline void SetSlot(int index);
    inline void ClearSlot(int index);
    int         GetSlotFromPointer(PerIoContext* ctx);
    bool        AssociateWithIOCP(WFXSocket sock);
    void        RepostAcceptAtSlot(int slot);
    bool        PostAcceptAtSlot(int slot);
};

} // namespace WFX::OSSpecific

#endif // WFX_WINDOWS_ACCEPT_EX_MANAGER_HPP