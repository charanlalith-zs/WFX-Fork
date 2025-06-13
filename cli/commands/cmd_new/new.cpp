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
    CreateFile(base / "settings.wfx", R"(MIDDLEWARE = ["LoggerMiddleware", "StaticMiddleware"]
STATIC_DIR = "public"
TEMPLATE_DIR = "templates"
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