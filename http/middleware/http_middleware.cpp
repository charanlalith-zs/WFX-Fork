#include "http_middleware.hpp"
#include "utils/logger/logger.hpp"

#include <unordered_set>

namespace WFX::Http {

HttpMiddleware& HttpMiddleware::GetInstance()
{
    static HttpMiddleware middleware;
    return middleware;
}

void HttpMiddleware::RegisterMiddleware(MiddlewareName name, MiddlewareCallbackType mw)
{
    auto&& [it, inserted] = middlewareFactories_.emplace(name, std::move(mw));
    if(!inserted) {
        auto& logger = WFX::Utils::Logger::GetInstance();
        logger.Warn("[HttpMiddleware]: Duplicate registration attempt for middleware '", name, "'. Ignoring this one");
    }
}

void HttpMiddleware::RegisterPerRouteMiddleware(const TrieNode* node, MiddlewareStack mwStack)
{
    auto& logger = WFX::Utils::Logger::GetInstance();
    if(!node) {
        logger.Warn("[HttpMiddleware]: Route node is nullptr. Ignoring this one");
        return;
    }

    auto&& [it, inserted] = middlewarePerRouteCallbacks_.emplace(node, std::move(mwStack));
    if(!inserted)
        logger.Warn("[HttpMiddleware]: Duplicate registration attempt for route node '", (void*)node, "'. Ignoring this one");
}

bool HttpMiddleware::ExecuteMiddleware(const TrieNode* node, HttpRequest& req, Response& res)
{
    // Initially execute the global middleware stack
    if(!ExecuteHelper(req, res, middlewareGlobalCallbacks_))
        return false;

    // We assume that no node means no per-route middleware
    if(!node)
        return true;

    auto elem = middlewarePerRouteCallbacks_.find(node);
    
    // Node exists but no middleware exist, return true
    if(elem == middlewarePerRouteCallbacks_.end())
        return true;

    // Per route middleware exists, execute it
    return ExecuteHelper(req, res, elem->second);
}

void HttpMiddleware::LoadMiddlewareFromConfig(MiddlewareConfigOrder order)
{
    middlewareGlobalCallbacks_.clear();

    auto& logger = WFX::Utils::Logger::GetInstance();
    std::unordered_set<std::string_view> loadedNames;

    for(const auto& nameStr : order) {
        std::string_view name = nameStr;

        // Duplicate middleware name from config
        if(!loadedNames.insert(name).second) {
            logger.Warn(
                "[HttpMiddleware]: Middleware '",
                name,
                "' is listed multiple times in config. Skipping duplicate"
            );
            continue;
        }

        auto it = middlewareFactories_.find(name);
        if(it != middlewareFactories_.end())
            middlewareGlobalCallbacks_.push_back(std::move(it->second));
        else
            logger.Warn(
                "[HttpMiddleware]: Middleware '",
                name,
                "' was listed in config but has not been registered. This may be a typo or missing registration. Skipped"
            );
    }
}

void HttpMiddleware::DiscardFactoryMap()
{
    middlewareFactories_.clear();
    middlewareFactories_.rehash(0); // Force deallocation of internal buckets
}

// vvv Helper Functions vvv
bool HttpMiddleware::ExecuteHelper(HttpRequest& req, Response& res, const MiddlewareStack& stack)
{
    for(std::size_t i = 0; i < stack.size(); ++i)
    {
        MiddlewareAction action = stack[i](req, res);

        switch(action)
        {
            // Continue to next middleware
            case MiddlewareAction::CONTINUE:
                break;

            // Skip the next middleware
            case MiddlewareAction::SKIP_NEXT:
                ++i;
                break;

            // Stop middleware chain
            case MiddlewareAction::BREAK:
                return false;
        }
    }

    return true;
}

} // namespace WFX::Http