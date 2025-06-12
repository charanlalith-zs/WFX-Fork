#ifndef WFX_ENGINE_HPP
#define WFX_ENGINE_HPP

#include "http/connection/http_connection_factory.hpp"

#include <string>

namespace WFX::Core {

// For 'Logger'
using namespace WFX::Utils;

class Engine {
public:
    Engine();
    void Listen(const std::string& host, int port);
    void Stop();

private:
    void HandleConnection(WFXSocket socket);
    void HandleRequest(WFXSocket socket, const char* data, size_t length);

    Logger& logger_ = Logger::GetInstance();

    std::unique_ptr<HttpConnectionHandler> connHandler_;
};

} // namespace WFX

#endif // WFX_ENGINE_HPP