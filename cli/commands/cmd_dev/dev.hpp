#ifndef WFX_CLI_COMMANDS_DEV_HPP
#define WFX_CLI_COMMANDS_DEV_HPP

#include <string>

#include "utils/logger/logger.hpp"

namespace WFX::CLI {

int RunDevServer(const std::string& host, int port);

}  // namespace WFX::CLI

#endif  // WFX_CLI_COMMANDS_DEV_HPP