#ifndef WFX_HTTP_MIDDLEWARE_HPP
#define WFX_HTTP_MIDDLEWARE_HPP

#include "http/common/http_route_common.hpp"
#include <unordered_map>

namespace WFX::Http {

// Forward declare TrieNode and ConnectionContext
// Each defined inside of routing/route_segment.hpp and connection/http_connection.hpp
struct TrieNode;
struct ConnectionContext;

using MiddlewareName        = std::string_view;
using MiddlewareConfigOrder = const std::vector<std::string>&;
using MiddlewareFactory     = std::unordered_map<MiddlewareName, MiddlewareEntry>;
using MiddlewarePerRoute    = std::unordered_map<const TrieNode*, MiddlewareStack>;

// 1st parameter is whether we successfully executed all middleware or no
// 2nd parameter is for async functionality
using MiddlewareResult         = std::pair<bool, AsyncPtr>;
using MiddlewareFunctionResult = std::pair<MiddlewareAction, AsyncPtr>;

class HttpMiddleware {
public:
    HttpMiddleware()  = default;
    ~HttpMiddleware() = default;

public:
    void RegisterMiddleware(MiddlewareName name, MiddlewareEntry mw);
    void RegisterPerRouteMiddleware(const TrieNode* node, MiddlewareStack mwStack);

    MiddlewareResult ExecuteMiddleware(const TrieNode* node, HttpRequest& req, Response& res,
                            ConnectionContext* ctx, MiddlewareBuffer optBuf = {});

    // Using std::string because TOML loader returns vector<string>
    void LoadMiddlewareFromConfig(MiddlewareConfigOrder order);

    void DiscardFactoryMap();

private:
    HttpMiddleware(const HttpMiddleware&)            = delete;
    HttpMiddleware& operator=(const HttpMiddleware&) = delete;

private: // Helper functions
    MiddlewareResult ExecuteHelper(HttpRequest& req, Response& res, MiddlewareStack& stack,
                        ConnectionContext* ctx, MiddlewareBuffer optBuf);
    MiddlewareFunctionResult ExecuteFunction(ConnectionContext* ctx, MiddlewareEntry& entry, HttpRequest& req,
                        Response& res, MiddlewareMeta meta);

    void FixInternalLinks(MiddlewareStack& stack);

private:
    // Temporary construct
    MiddlewareFactory  middlewareFactories_;

    // Main stuff
    MiddlewareStack    middlewareGlobalCallbacks_;
    MiddlewarePerRoute middlewarePerRouteCallbacks_;
};

} // namespace WFX::Http

#endif // WFX_HTTP_MIDDLEWARE_HPP