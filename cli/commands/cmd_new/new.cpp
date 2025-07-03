#include "new.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

namespace WFX::CLI {

// For 'Logger'
using namespace WFX::Utils;

static void CreateFile(const fs::path& path, const std::string& content) {
    std::ofstream outFile(path);
    if(!outFile)
        Logger::GetInstance().Fatal("[WFX]: Failed to create file: ", path);

    outFile << content;
    Logger::GetInstance().Info("[WFX]: Created: ", path);
}

static void ScaffoldProject(const std::string& projectName) {
    const fs::path base = fs::current_path() / projectName;

    // 1. Create core project folders
    fs::create_directories(base / "src");
    fs::create_directories(base / "public");
    fs::create_directories(base / "templates");
    fs::create_directories(base / "wfx");

    // 2. Create essential config and main file
    CreateFile(base / "wfx.toml", R"([Network]
recv_buffer_max              = 16384   # Max total recv buffer size per connection (in bytes)
recv_buffer_incr             = 4096    # Buffer growth step (in bytes)
header_reserve_hint          = 512     # Initial header allocation hint size
max_header_size              = 8192    # Max total size of all headers
max_header_count             = 64      # Max number of headers allowed
max_body_size                = 8192    # Max size of request body
max_connections              = 100000  # Max total concurrent connections
max_connections_per_ip       = 20      # Per-IP connection cap
max_request_burst_per_ip     = 10      # Initial request tokens per IP
max_requests_per_ip_per_sec  = 5       # Refill rate (tokens per second per IP)

[Windows]
accept_slots       = 4096    # Number of pre-allocated AcceptEx contexts
connection_threads = 4       # IOCP worker thread count
request_threads    = 2       # Threads executing user handlers

[Linux]
worker_connections = 4096    # Max simultaneous epoll-based worker connections
)");

    CreateFile(base / "main.cpp", R"(#include <wfx/app.hpp>

GET("/", [](const Request& req, Response& res) {
    res.Text("Hello, WFX!");
});
)");

    // 3. Create example template and static asset
    CreateFile(base / "templates/index.html", R"(<html><body><h1>Hello from WFX Template</h1></body></html>)");
    CreateFile(base / "public/style.css", "body { font-family: sans-serif; }");
    CreateFile(base / "public/script.js", "console.log(\"Hello from WFX\")");

    Logger::GetInstance().Info("[WFX]: Project '", projectName, "' created successfully!");
}

int CreateProject(const std::string& projectName) {
    const std::filesystem::path projectPath = std::filesystem::current_path() / projectName;

    if(fs::exists(projectPath))
        Logger::GetInstance().Fatal("[WFX]: Project already exists: ", projectPath);

    ScaffoldProject(projectName);
    return 0;
}

}  // namespace WFX::New