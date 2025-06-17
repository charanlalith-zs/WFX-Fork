#ifndef WFX_WINDOWS_IOCP_CONNECTION_HANDLER_HPP
#define WFX_WINDOWS_IOCP_CONNECTION_HANDLER_HPP

#include "accept_ex_manager.hpp"
#include "third_party/concurrent_queue/blockingconcurrentqueue.h"
#include "http/connection/http_connection.hpp"
#include "utils/fixed_pool/fixed_pool.hpp"
#include "utils/hash_map/concurrent_hash_map.hpp"
#include "utils/logger/logger.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>

#include <vector>
#include <thread>
#include <atomic>
#include <shared_mutex>
#include <memory>
#include <functional>
#include <string>
#include <queue>

#pragma comment(lib, "ws2_32.lib")

namespace WFX::OSSpecific {

using namespace WFX::Utils; // For 'Logger', 'BufferPool' and 'ConcurrentHashMap'
using namespace moodycamel; // For BlockingConcurrentQueue

class IocpConnectionHandler : public WFX::Core::HttpConnectionHandler {
public:
    IocpConnectionHandler();
    ~IocpConnectionHandler();

    bool Initialize(const std::string& host, int port) override;
    void ResumeRecieve(WFXSocket) override;
    void Receive(WFXSocket, ReceiveCallback onData) override;
    int  Write(WFXSocket socket, const char* buffer, size_t length) override;
    void Close(WFXSocket socket) override;

    // Main function
    void Run(AcceptedConnectionCallback) override;
    void Stop() override;

private:
    bool CreateWorkerThreads(unsigned int iocpThreads, unsigned int offloadThreads);
    void WorkerLoop();
    void SafeDeleteIoData(PerIoData* data);
    void PostReceive(WFXSocket socket);
    void InternalCleanup();

private:
    // Helper structs used in unique_ptr deleter
    struct PerIoDataDeleter {
        IocpConnectionHandler* handler;
        
        void operator()(PerIoData* data) const {
            handler->SafeDeleteIoData(data);
        }
    };

    struct PostRecvOpDeleter {
        IocpConnectionHandler* handler;

        void operator()(PostRecvOp* data) const {
            handler->allocPool_.Free(data, sizeof(PostRecvOp));
        }
    };
    
private:
    SOCKET                     listenSocket_ = INVALID_SOCKET;
    HANDLE                     iocp_         = nullptr;
    std::mutex                 connectionMutex_;
    std::atomic<bool>          running_      = false;
    std::vector<std::thread>   workerThreads_;
    std::vector<std::thread>   offloadThreads_;
    AcceptedConnectionCallback acceptCallback_;

    Logger& logger_ = Logger::GetInstance();
    ConfigurableFixedAllocPool allocPool_{{64, 128, 4096}};
    ConcurrentHashMap<SOCKET, ReceiveCallback> receiveCallbacks_{1024 * 1024};
    BlockingConcurrentQueue<std::function<void(void)>> offloadCallbacks_;

    // Main shit
    AcceptExManager acceptManager_;
};

} // namespace WFX::OSSpecific

#endif // WFX_WINDOWS_IOCP_CONNECTION_HANDLER_HPP