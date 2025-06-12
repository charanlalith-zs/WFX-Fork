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
    RECV,
    SEND,
    ACCEPT
};

struct PerIoBase {
    OVERLAPPED         overlapped{};
    PerIoOperationType operationType = PerIoOperationType::INVALID;

    ~PerIoBase() = default;
};

struct PerIoData : PerIoBase {
    WFXSocket socket = WFX_INVALID_SOCKET;
    void*     buffer = nullptr;
    WSABUF    wsaBuf{};
};

struct PerIoContext : PerIoBase {
    WFXSocket                  acceptSocket = WFX_INVALID_SOCKET;
    char                       buffer[2 * (sizeof(SOCKADDR_IN) + 16)];
    AcceptedConnectionCallback callbackHandler;
};

// Forward declare logger
using namespace WFX::Utils;

class AcceptExManager {
public:
    constexpr static int MAX_SLOTS = 1024;

    AcceptExManager() = default;

    bool Initialize(WFXSocket listenSocket, HANDLE iocp, const AcceptedConnectionCallback& cb);
    void DeInitialize();
    void HandleAcceptCompletion(PerIoContext* ctx);

private:
    // IMP
    LPFN_ACCEPTEX lpfnAcceptEx = nullptr;

    WFXSocket listenSocket_;
    HANDLE    iocp_;
    Logger&   logger_ = Logger::GetInstance();

    std::array<PerIoContext, MAX_SLOTS> contexts_;
    uint64_t                            activeSlotsBits_ = 0;
    std::mutex                          reuseMutex_;
    AcceptedConnectionCallback          acceptCallback_;

    inline void SetSlot(int index);
    inline void ClearSlot(int index);
    inline int  FindFreeSlot();
    int         GetSlotFromPointer(PerIoContext* ctx);
    bool        AssociateWithIOCP(WFXSocket sock);
    void        RepostAcceptAtSlot(int slot);
    bool        PostAcceptAtSlot(int slot);
};

} // namespace WFX::OSSpecific

#endif // WFX_WINDOWS_ACCEPT_EX_MANAGER_HPP