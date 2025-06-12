#include "dev.hpp"

#include <csignal>
#include <atomic>
#include <iostream>
#include <thread>
#include <chrono>

#include "engine/Engine.hpp"

namespace WFX::CLI {

// For 'Logger'
using namespace WFX::Utils;

static std::atomic<bool> shouldStop = false;

#ifdef _WIN32
#include <Windows.h>
BOOL WINAPI ConsoleHandler(DWORD signal)
{
    if(signal == CTRL_C_EVENT) {
        Logger::GetInstance().Info("[WFX]: Shutting down...");
        shouldStop = true;
        return TRUE;
    }
    return FALSE;
}
#else
void SignalHandler(int signal)
{
    if(signal == SIGINT) {
        Logger::GetInstance().Info("[WFX]: Shutting down...");
        shouldStop = true;
    }
}
#endif

int RunDevServer(const std::vector<std::string>& args)
{
    std::string host = "127.0.0.1";
    int port         = 8080;

    // Simple argument parsing: --host <host> --port <port>
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--host" && i + 1 < args.size()) {
            host = args[i + 1];
            ++i;
        } else if (args[i] == "--port" && i + 1 < args.size()) {
            try {
                port = std::stoi(args[i + 1]);
            }
            catch (...) {
                Logger::GetInstance().Fatal("[WFX]: Invalid port: ", args[i + 1]);
            }
            ++i;
        }
    }

#ifdef _WIN32
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);
#else
    std::signal(SIGINT, SignalHandler);
#endif

    WFX::Core::Engine engine;
    engine.Listen(host, port);

    Logger::GetInstance().Info("[WFX]: Dev server running at http://", host, ':', port);
    Logger::GetInstance().Info("[WFX]: Press Ctrl+C to stop.");
    
    while(!shouldStop)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    engine.Stop();
    return 0;
}

}  // namespace WFX::CLI