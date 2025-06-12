#ifndef WFX_CLI_COMMANDS_DEV_HPP
#define WFX_CLI_COMMANDS_DEV_HPP

#include <string>
#include <vector>

#include "utils/logger/logger.hpp"

namespace WFX::CLI {

int RunDevServer(const std::vector<std::string>& args);

}  // namespace WFX::CLI

#endif  // WFX_CLI_COMMANDS_DEV_HPP