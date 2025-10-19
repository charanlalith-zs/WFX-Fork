#include "build.hpp"

#include "config/config.hpp"
#include "engine/template_engine.hpp"
#include "utils/logger/logger.hpp"

namespace WFX::CLI {

using namespace WFX::Core; // For 'TemplateEngine', 'Config'

// Supported build types: 'templates', ...
constexpr static const char* BUILD_TEMPLATES = "templates";

int BuildProject(const std::string& buildType)
{
    if(buildType == BUILD_TEMPLATES) {
        // Template engine internally uses wfx.toml stuff, so we need to load it
        auto& config         = Config::GetInstance();
        auto& templateEngine = TemplateEngine::GetInstance();

        config.LoadCoreSettings("wfx.toml");

        templateEngine.PreCompileTemplates();
        templateEngine.SaveTemplatesToCache();

        return 0;
    }

    // Invalid type
    Logger::GetInstance().Fatal(
        "[WFX]: Wrong build type provided: ", buildType.c_str(), ". Supported types: 'templates'"
    );

    // Not that this will ever get triggered but yeah
    return -1;
}

} // namespace WFX::CLI