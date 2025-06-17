#include "Engine.hpp"
#include <iostream>
#include <string>
#include <thread>

namespace WFX::Core {

Engine::Engine()
    : connHandler_(CreateConnectionHandler())
{}

void Engine::Listen(const std::string& host, int port)
{
    if(!connHandler_->Initialize(host, port))
        logger_.Fatal("[Engine]: Failed to initialize server");

    logger_.Info("[Engine]: Listening on ", host, ':', port);

    connHandler_->Run([this](WFXSocket socket) {
        this->HandleConnection(socket);
    });
}

void Engine::Stop()
{
    connHandler_->Stop();

    logger_.Info("[Engine]: Stopped Successfully!");
}

void Engine::HandleConnection(WFXSocket socket)
{
    connHandler_->Receive(socket, [this, socket](ReceiveCallbackData data, size_t len) {
        this->HandleRequest(socket, std::move(data), len);
    });
}

void Engine::HandleRequest(WFXSocket socket, ReceiveCallbackData data, size_t length)
{
    // Hardcode the request for now, later we do the do
    std::string httpResp =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 14\r\n"
        "\r\n"
        "Hello from WFX";

    connHandler_->Write(socket, httpResp.c_str(), httpResp.size());
    connHandler_->Close(socket);
}

} // namespace WFX