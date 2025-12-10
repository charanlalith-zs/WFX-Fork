#ifndef WFX_HTTP_ROUTE_COMMON_HPP
#define WFX_HTTP_ROUTE_COMMON_HPP

#include "async/interface.hpp"
#include "utils/uuid/uuid.hpp"
#include "shared/utils/compiler_macro.hpp"
#include "utils/backport/move_only_function.hpp"

#include <string_view>
#include <cstdint>
#include <variant>
#include <vector>

// Forward declare for 'HttpCallbackType' and other stuff using this file
namespace WFX::Http {
    struct HttpRequest;
    struct HttpResponse;
} // namespace WFX::Http

// Defined in user side of code (include/http/response.hpp)
class Response;

// Defined in user side of code (include/http/stream_response.hpp)
class StreamResponse;

// Bunch of stuff which will be used in routes and outside of routing as well
using DynamicSegment         = std::variant<std::uint64_t, std::int64_t, std::string_view, WFX::Utils::UUID>;
using StaticOrDynamicSegment = std::variant<std::string_view, DynamicSegment>;
using PathSegments           = std::vector<DynamicSegment>;

// vvv Outbound Streaming vvv
enum class StreamAction {
    CONTINUE,
    STOP_AND_ALIVE_CONN,
    STOP_AND_CLOSE_CONN
};

struct StreamResult {
    std::size_t  writtenBytes;
    StreamAction action;
};

struct StreamBuffer {
    char*       buffer;
    std::size_t size;
};

using StreamGenerator = WFX::Utils::MoveOnlyFunction<StreamResult(StreamBuffer)>;

// vvv Middleware (Streaming & Sync) vvv
/*
 * NOTE: I expect that middleware will not be more than uint16_t max value (aka 65535)
 *       Cuz it makes no sense for someone to have that many middlewares realistically
 */
enum class MiddlewareAction : std::uint8_t {
    CONTINUE,  // Continue to next middleware
    BREAK,     // Break out of middleware chain
    SKIP_NEXT  // Skip the next middleware in chain if any
};

// So for async routes we need this to determine whether we are executing global-
// -mw or per route middleware
enum class MiddlewareLevel : std::uint8_t {
    GLOBAL,
    PER_ROUTE
};

enum class MiddlewareType : std::uint8_t {
    LINEAR       = 1 << 0,  // For normal use case
    STREAM_CHUNK = 1 << 1,  // Streaming inbound chunks
    STREAM_END   = 1 << 2   // Streaming inbound last chunk
};

struct MiddlewareBuffer {
    const char* buffer = nullptr;
    std::size_t size   = 0;
};

struct MiddlewareMeta {
    MiddlewareType   type;
    MiddlewareBuffer buffer;
};

using SyncMiddlewareType  = MiddlewareAction (*)(WFX::Http::HttpRequest&, Response&, MiddlewareMeta);
using AsyncMiddlewareType = WFX::Utils::MoveOnlyFunction<AsyncPtr(WFX::Http::HttpRequest&, Response&, MiddlewareMeta)>;
using HttpMiddlewareType  = std::variant<std::monostate, SyncMiddlewareType, AsyncMiddlewareType>;

struct MiddlewareEntry {
public: // Helpers
    constexpr static std::uint16_t END = UINT16_MAX;

public: // Main
    HttpMiddlewareType mw;

    // User can specify what type of middlewares can this function handle
    // By default it only handles linear middleware
    std::uint8_t handled = static_cast<std::uint8_t>(MiddlewareType::LINEAR);

    // Values set by middleware handler, used for fast traversal for a specific type of group
    // uint16_t max value is considered to be invalid value (it can be used to signify end)
    std::uint16_t nextSm  = END;    // Index of next linear capable middleware
    std::uint16_t nextCbm = END;    // Index of next stream capable middleware
    std::uint16_t nextCem = END;    // Index of next stream end capable middleware
};

inline constexpr std::uint16_t MiddlewareEntry::* MiddlewareEntryNext(MiddlewareType t) noexcept
{
    switch(t) {
        case MiddlewareType::LINEAR:       return &MiddlewareEntry::nextSm;
        case MiddlewareType::STREAM_CHUNK: return &MiddlewareEntry::nextCbm;
        case MiddlewareType::STREAM_END:   return &MiddlewareEntry::nextCem;
    }
    WFX_UNREACHABLE;
}

using MiddlewareStack = std::vector<MiddlewareEntry>;

// vvv User Callbacks vvv
using AsyncCallbackType = WFX::Utils::MoveOnlyFunction<AsyncPtr(WFX::Http::HttpRequest&, Response&)>;  
using SyncCallbackType  = void (*)(WFX::Http::HttpRequest&, Response&);
using HttpCallbackType  = std::variant<std::monostate, SyncCallbackType, AsyncCallbackType>;

#endif // WFX_HTTP_ROUTE_COMMON_HPP