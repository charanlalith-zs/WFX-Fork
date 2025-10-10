#include "doctor.hpp"

#include "utils/logger/logger.hpp"
#include <fstream>
#include <cstdlib>
#include <string>
#include <filesystem>
#include <algorithm>
#include <array>

#ifdef _WIN32
    #define NULL_DEVICE " >nul 2>&1"
#else
    #define NULL_DEVICE " >/dev/null 2>&1"
#endif

namespace WFX::CLI {

struct CompilerConfig {
    const char* id;
    const char* display;
    const char* command;
    const char* linker;
    const char* cargs;
    const char* largs;
    const char* objFlag;
    const char* dllFlag;
};

#if defined(_MSC_VER)
constexpr CompilerConfig BUILD_COMPILER{
    "msvc", "MSVC", "cl", "link",
    "/std:c++17 /O2 /GL /GS- /GR- /EHsc /MD /I. /Iwfx/include /Iwfx /c",
    "/DLL /LTCG /OPT:REF /DEBUG:NONE",
    "/Fo:", "/OUT:"
};
#elif defined(__MINGW32__) || defined(__MINGW64__)
constexpr CompilerConfig BUILD_COMPILER{
    "g++-mingw", "G++ (MinGW)", "g++", "g++",
    "-std=c++17 -O2 -flto -ffunction-sections -fdata-sections "
    "-fvisibility=hidden -fvisibility-inlines-hidden -I. -Iwfx/include -Iwfx -c",
    "-shared -flto -Wl,--gc-sections -Wl,--strip-all",
    "-o ", "-o "
};
#elif defined(__clang__)
constexpr CompilerConfig BUILD_COMPILER{
    "clang++", "Clang++", "clang++", "clang++",
    "-std=c++17 -O2 -flto -fvisibility=hidden -fvisibility-inlines-hidden "
    "-ffunction-sections -fdata-sections -I. -Iwfx/include -Iwfx -c",
    "-shared -fPIC -flto -Wl,--gc-sections -Wl,--strip-all",
    "-o ", "-o "
};
#elif defined(__GNUC__)
constexpr CompilerConfig BUILD_COMPILER{
    "g++", "G++", "g++", "g++",
    "-std=c++17 -O2 -flto -fvisibility=hidden -fvisibility-inlines-hidden "
    "-ffunction-sections -fdata-sections -I. -Iwfx/include -Iwfx -c",
    "-shared -fPIC -flto -Wl,--gc-sections -Wl,--strip-all",
    "-o ", "-o "
};
#else
#error "[WFX Doctor]: Unsupported compiler. (__VERSION__: " __VERSION__ ")"
#endif

static std::string RunCommand(const std::string& cmd)
{
    std::array<char, 256> buffer{};
    std::string result;

#ifdef _WIN32
    FILE* pipe = _popen((cmd + " 2>&1").c_str(), "r");
#else
    FILE* pipe = popen((cmd + " 2>&1").c_str(), "r");
#endif
    if(!pipe) return result;

    while(fgets(buffer.data(), static_cast<int>(buffer.size()), pipe))
        result += buffer.data();

#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    return result;
}

static bool IsCompilerAvailable(const std::string& binary)
{
#ifdef _WIN32
    return std::system(("where " + binary + NULL_DEVICE).c_str()) == 0;
#else
    return std::system(("which " + binary + NULL_DEVICE).c_str()) == 0;
#endif
}

#ifdef _WIN32
static std::pair<std::string, std::string> TryMSVCCompilerAndLinker()
{
    constexpr const char* vswhere = R"("C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe")";
    std::string output = RunCommand(std::string(vswhere) +
        " -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath");

    output.erase(std::remove(output.begin(), output.end(), '\r'), output.end());
    output.erase(std::remove(output.begin(), output.end(), '\n'), output.end());
    if(output.empty()) return {"", ""};

    std::filesystem::path base = output;
    base /= "VC\\Tools\\MSVC";

    if(!std::filesystem::exists(base)) return {"", ""};

    for(const auto& entry : std::filesystem::directory_iterator(base)) {
        std::filesystem::path binDir = entry.path() / "bin\\Hostx64\\x64";
        std::string cl   = (binDir / "cl.exe").string();
        std::string link = (binDir / "link.exe").string();

        if(std::filesystem::exists(cl) && std::filesystem::exists(link))
            return {cl, link};
    }

    return {"", ""};
}
#endif

int WFXDoctor()
{
    auto& logger = WFX::Utils::Logger::GetInstance();
    logger.Info("----------------------------------------------");
    logger.Info("[Doctor]: Checking for build compiler presence");
    logger.Info("----------------------------------------------");

    std::string compiler = BUILD_COMPILER.command;
    std::string linker   = BUILD_COMPILER.linker;

#ifdef _WIN32
    if(std::string(BUILD_COMPILER.id) == "msvc") {
        auto [resolvedCompiler, resolvedLinker] = TryMSVCCompilerAndLinker();
        if(resolvedCompiler.empty() || resolvedLinker.empty()) {
            logger.Error("[X] Failed to locate MSVC tools. Please open Developer Command Prompt or add MSVC to PATH");
            return 1;
        }

        compiler = resolvedCompiler;
        linker   = resolvedLinker;

        logger.Info("[+] MSVC compiler found at: ", compiler);
        logger.Info("[+] MSVC linker found at: ", linker);
    }
#endif

    // Detect absolute or relative paths (e.g., "C:/...", "./cl.exe")
    bool isPath = compiler.find('/') != std::string::npos || compiler.find('\\') != std::string::npos;
    bool exists = isPath && std::filesystem::exists(compiler);

    // Only run IsCompilerAvailable if it's not already resolved by a valid path
    if(!exists && !IsCompilerAvailable(compiler)) {
        logger.Error("[X] Compiler '", BUILD_COMPILER.id, "' not found on this system");
        logger.Info("[!] Please install it or adjust your PATH");
        return 1;
    }

    // Always quote compiler for command execution (in case path contains spaces)
    std::string quotedCompiler = "\"" + compiler + "\"";
    std::string versionCmd     = (std::string(BUILD_COMPILER.id) == "msvc")
                                    ? quotedCompiler : quotedCompiler + " --version";
    std::string version        = RunCommand(versionCmd);

    // Extract first line only
    std::string versionLine = version.substr(0, version.find('\n'));
    logger.Info("[+] Detected: [", BUILD_COMPILER.display, ": ", versionLine, ']');

    std::ofstream out("toolchain.toml");
    out << "[Compiler]\n";
    out << "name    = \"" << BUILD_COMPILER.id       << "\"\n";
    out << "ccmd    = \"" << compiler                << "\"\n";
    out << "lcmd    = \"" << linker                  << "\"\n";
    out << "cargs   = \"" << BUILD_COMPILER.cargs    << "\"\n";
    out << "largs   = \"" << BUILD_COMPILER.largs    << "\"\n";
    out << "objflag = \"" << BUILD_COMPILER.objFlag  << "\"\n";
    out << "dllflag = \"" << BUILD_COMPILER.dllFlag  << '"';

    logger.Info("[Doctor]: Saved toolchain config to toolchain.toml");
    return 0;
}

} // namespace WFX::CLI