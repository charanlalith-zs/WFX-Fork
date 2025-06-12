#ifndef WFX_HTTP_CONNECTION_HANDLER_HPP
#define WFX_HTTP_CONNECTION_HANDLER_HPP

#include <string>
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

// Recieve callback signature around the code
using RecieveCallback = std::function<void(const char*, size_t)>;

// When connection is accepted, upper layers will recieve the connection WFXSocket
// For that, we need a callback
using AcceptedConnectionCallback = std::function<void(WFXSocket)>;

namespace WFX::Core {

// Abstraction for Windows and Linux impl
class HttpConnectionHandler {
public:
    virtual ~HttpConnectionHandler() = default;

    // Initialize sockets, bind and listen on given host:port
    virtual bool Initialize(const std::string& host, int port) = 0;

    // Accept a new connection and return a socket descriptor or handle
    /* DEPRECATED */
    // virtual WFXSocket AcceptConnection() = 0;

    // Read data from socket (Async)
    virtual void Receive(WFXSocket, RecieveCallback onData) = 0;

    // Write data to socket (Async)
    virtual int Write(int socket, const char* buffer, size_t length) = 0;

    // Close a client socket
    virtual void Close(int socket) = 0;

    // Run the main connection loop (can be used by dev/serve mode)
    virtual void Run(AcceptedConnectionCallback) = 0;

    // Shutdown the main connection loop, cleanup everything
    virtual void Stop() = 0;
};

} // namespace WFX::Connection

#endif // WFX_HTTP_CONNECTION_HANDLER_HPP