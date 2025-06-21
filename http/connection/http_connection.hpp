#ifndef WFX_HTTP_CONNECTION_HANDLER_HPP
#define WFX_HTTP_CONNECTION_HANDLER_HPP

#include "utils/functional/move_only_function.hpp"

#include <string>
#include <memory>
#include <functional>

#ifdef _WIN32
    #include <WinSock2.h>
    using WFXSocket = SOCKET;
    constexpr WFXSocket WFX_INVALID_SOCKET = INVALID_SOCKET;
#else
    #include <cstdint>
    using WFXSocket = int; // On Linux/Unix, sockets are file descriptors (ints)
    constexpr WFXSocket WFX_INVALID_SOCKET = -1;
#endif

// For 'MoveOnlyFunction'
using WFX::Utils::MoveOnlyFunction;

// When connection is accepted, upper layers will recieve the connection data (Socket and IP)
// For that, we need a callback
// And for the callback, we need the data :)
struct WFXAcceptedConnectionInfo {
    virtual ~WFXAcceptedConnectionInfo() = default;
    virtual WFXSocket        GetSocket() const = 0;
    virtual std::string_view GetIp()     const = 0;
    virtual uint64_t         GetIpType() const = 0;
};

// Forward declare it so compilers won't cry
struct ConnectionContext;

using ReceiveCallback = MoveOnlyFunction<void(ConnectionContext&)>;

using AcceptedConnectionDeleter      = MoveOnlyFunction<void(WFXAcceptedConnectionInfo*)>;
using AcceptedConnectionCallbackData = std::unique_ptr<WFXAcceptedConnectionInfo, AcceptedConnectionDeleter>;
using AcceptedConnectionCallback     = MoveOnlyFunction<void(WFXSocket)>;

// Quite important
struct ConnectionContext {
    WFXSocket socket;
    
    char*  buffer        = nullptr;
    size_t bufferSize    = 0;
    size_t dataLength    = 0;
    size_t maxBufferSize = 16 * 1024; // 16KB. Can be set by the user as well
    
    ReceiveCallback onReceive;

    // Accepted connection info (moved in on accept)
    AcceptedConnectionCallbackData acceptInfo;
};

namespace WFX::Http {

// Abstraction for Windows and Linux impl
class HttpConnectionHandler {
public:
    virtual ~HttpConnectionHandler() = default;

    // Initialize sockets, bind and listen on given host:port
    virtual bool Initialize(const std::string& host, int port) = 0;

    // Set the receive callback ONCE per socket (can be overwritten if needed)
    virtual void SetReceiveCallback(WFXSocket socket, ReceiveCallback onData) = 0;

    // Read more data if required (Async)
    virtual void ResumeReceive(WFXSocket socket) = 0;

    // Write data to socket (Async)
    virtual int Write(WFXSocket socket, const char* buffer, size_t length) = 0;

    // Close a client socket
    virtual void Close(WFXSocket socket) = 0;

    // Run the main connection loop (can be used by dev/serve mode)
    virtual void Run(AcceptedConnectionCallback) = 0;

    // Shutdown the main connection loop, cleanup everything
    virtual void Stop() = 0;
};

} // namespace WFX::Http

#endif // WFX_HTTP_CONNECTION_HANDLER_HPP