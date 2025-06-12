#ifndef WFX_WINDOWS_IOCP_CONNECTION_HANDLER_HPP
#define WFX_WINDOWS_IOCP_CONNECTION_HANDLER_HPP

#include "accept_ex_manager.hpp"
#include "http/connection/http_connection.hpp"
#include "utils/buffer_pool/buffer_pool.hpp"
#include "utils/logger/logger.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>

#include <vector>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <shared_mutex>
#include <memory>
#include <functional>
#include <string>
#include <queue>

#pragma comment(lib, "ws2_32.lib")

namespace WFX::OSSpecific {

class IocpConnectionHandler : public WFX::Core::HttpConnectionHandler {
public:
    IocpConnectionHandler();
    ~IocpConnectionHandler();

    bool Initialize(const std::string& host, int port) override;
    void Receive(WFXSocket, RecieveCallback onData) override;
    int  Write(int socket, const char* buffer, size_t length) override;
    void Close(int socket) override;

    // Main function
    void Run(AcceptedConnectionCallback) override;
    void Stop() override;

private:
    bool CreateWorkerThreads(size_t threadCount = std::thread::hardware_concurrency());
    void WorkerLoop();
    void SafeDeleteIoData(PerIoData* data);
    void PostReceive(int socket);
    void InternalCleanup();

private:
    // Helper function used in unique_ptr deleter
    struct PerIoDataDeleter {
        IocpConnectionHandler* handler;
        
        void operator()(PerIoData* data) const {
            handler->SafeDeleteIoData(data);
        }
    };
    
private:
    SOCKET                   listenSocket_;
    HANDLE                   iocp_;
    std::vector<std::thread> workerThreads_;
    std::atomic<bool>        running_;
    std::mutex               connectionMutex_;

    WFX::Utils::BufferPool bufferPool_;
    WFX::Utils::Logger&    logger_ = WFX::Utils::Logger::GetInstance();

    std::unordered_map<SOCKET, std::function<void(const char*, size_t)>> receiveCallbacks_;
    std::shared_mutex callbackMutex_;

    // Main shit
    AcceptExManager acceptManager_;
};

} // namespace WFX::OSSpecific

#endif // WFX_WINDOWS_IOCP_CONNECTION_HANDLER_HPP