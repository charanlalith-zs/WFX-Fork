#ifndef WFX_CLI_COMMANDS_NEW_HPP
#define WFX_CLI_COMMANDS_NEW_HPP

#include "utils/logger/logger.hpp"

#include <string>
#include <vector>

namespace WFX::CLI {

int CreateProject(const std::vector<std::string>& args);

}  // namespace WFX::New

#endif  // WFX_CLI_COMMANDS_NEW_HPP