#ifndef WFX_CLI_MAIN_HPP
#define WFX_CLI_MAIN_HPP

#include <iostream>
#include <string>
#include <vector>

#include "commands/cmd_new/new.hpp"
#include "commands/cmd_dev/dev.hpp"

namespace WFX {

int WFXEntryPoint(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: wfx <command> [options]\n";
        return 1;
    }

    std::string command = argv[1];
    std::vector<std::string> args(argv + 2, argv + argc);

    if(command == "new")
        return CLI::CreateProject(args);

    else if(command == "dev") {
        return CLI::RunDevServer(args);
    }
    else if(command == "build") {
        std::cout << "TODO: IMPL BUILD MODE\n";
        return 0;
    }
    else if(command == "serve") {
        std::cout << "TODO: IMPL SERVE MODE\n";
        return 0;
    }
    else {
        std::cerr << "Unknown command: " << command << "\n";
        return 1;
    }
}

}  // namespace WFX

int main(int argc, char* argv[]) {
    return WFX::WFXEntryPoint(argc, argv);
}

#endif  // WFX_CLI_MAIN_HPP