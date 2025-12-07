#include "http_middleware.hpp"
#include "http/connection/http_connection.hpp"
#include "shared/apis/http_api.hpp"
#include "utils/logger/logger.hpp"

#include <unordered_set>

namespace WFX::Http {

// vvv Main Functions vvv
void HttpMiddleware::RegisterMiddleware(MiddlewareName name, MiddlewareEntry mw)
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
    else
        FixInternalLinks(it->second);
}

MiddlewareResult HttpMiddleware::ExecuteMiddleware(
    const TrieNode* node, HttpRequest& req, Response& res,
    MiddlewareContext& ctx, MiddlewareBuffer optBuf
) {
    if(ctx.level == MiddlewareLevel::GLOBAL) {
        // Initially execute the global middleware stack
        auto [success, ptr] = ExecuteHelper(req, res, middlewareGlobalCallbacks_, ctx, optBuf);
        if(!success)
            return {false, ptr};

        // Reset the context to prepare for per route if it exists
        ctx.index = 0;
        ctx.level = MiddlewareLevel::PER_ROUTE;
    }

    // We assume that no node means no per-route middleware
    if(!node)
        return {true, nullptr};

    auto elem = middlewarePerRouteCallbacks_.find(node);
    
    // Node exists but no middleware exist, return true
    if(elem == middlewarePerRouteCallbacks_.end())
        return {true, nullptr};

    // Per route middleware exists, execute it
    return ExecuteHelper(req, res, elem->second, ctx, optBuf);
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

    FixInternalLinks(middlewareGlobalCallbacks_);
}

void HttpMiddleware::DiscardFactoryMap()
{
    middlewareFactories_.clear();
    middlewareFactories_.rehash(0); // Force deallocation of internal buckets
}

// vvv Helper Functions vvv
MiddlewareResult HttpMiddleware::ExecuteHelper(
    HttpRequest& req, Response& res, MiddlewareStack& stack,
    MiddlewareContext& ctx, MiddlewareBuffer optBuf
) {
    std::size_t size = stack.size();
    if(size == 0)
        return {true, nullptr};

    std::uint16_t head = MiddlewareEntry::END;

    // Select the correct 'next' pointer for this middleware type
    auto next = MiddlewareEntryNext(ctx.type);

    // Check if we already executed this beforehand, we just need to continue from where we left off
    if(ctx.index > 0) {
        head = ctx.index;
        goto __ContinueMiddleware;
    }

    // Fresh start, find first usable middleware
    head = (stack[0].handled & static_cast<std::uint8_t>(ctx.type)) ? 0 : (stack[0].*next);

    // No middleware for this type
    if(head == MiddlewareEntry::END)
        return {true, nullptr};

__ContinueMiddleware:
    // Walk the linked list via nextSm / nextCbm / nextCem
    std::uint16_t i = head;

    while(i != MiddlewareEntry::END) {
        MiddlewareEntry& entry = stack[i];
        MiddlewareMeta   meta  = {ctx.type, optBuf};

        // Execute
        auto [action, asyncPtr] = ExecuteFunction(ctx, entry, req, res, meta);

        // Async function, so we need to store the next valid middleware index because this async function-
        // -will run in scheduler seperate from this middleware chain, after it completes we need to invoke-
        // -the next valid scheduler
        if(asyncPtr) {
            ctx.index = entry.*next;
            return {false, asyncPtr};
        }

        // Interpret the result
        switch(action) {
            case MiddlewareAction::CONTINUE:
                // Move to next element of this type
                i = entry.*next;
                break;

            case MiddlewareAction::SKIP_NEXT:
                // Skip one element in this chain
                if(entry.*next != MiddlewareEntry::END)
                    i = stack[entry.*next].*next;
                else
                    i = MiddlewareEntry::END;
                break;

            case MiddlewareAction::BREAK:
                return {false, nullptr};
        }
    }

    return {true, nullptr};
}

MiddlewareFunctionResult HttpMiddleware::ExecuteFunction(
    MiddlewareContext& mctx, MiddlewareEntry& entry,
    HttpRequest& req, Response& res, MiddlewareMeta meta
) {
    auto& logger = WFX::Utils::Logger::GetInstance();

    // Sanity check, this shouldn't happen if user properly set handled types
    if(std::holds_alternative<std::monostate>(entry.mw)) {
        logger.Warn(
            "[HttpMiddleware]: Found empty handler while executing middleware for type: ", (int)meta.type,
            " Perhaps you forgot to set middleware handling type?"
        );
        return {MiddlewareAction::CONTINUE, nullptr};
    }

    // Check if its a sync function, it directly returns value
    if(auto* sync = std::get_if<SyncMiddlewareType>(&entry.mw))
        return {(*sync)(req, res, meta), nullptr};

    // For async function, the return value is stored in AsyncPtr '__Action' value
    // Yeah ik, weird
    auto* httpApi = WFX::Shared::GetHttpAPIV1();
    auto* cctx    = mctx.cctx;
    auto& async   = std::get<AsyncMiddlewareType>(entry.mw);

    // Set context (type erased) at http api side before calling async callback
    httpApi->SetGlobalPtrData(static_cast<void*>(cctx));

    auto ptr = async(req, res, meta);
    if(!ptr)
        logger.Fatal(
            "[HttpMiddleware]: Null coroutine detected in executed async middleware. Type: ", (int)meta.type
        );

    // Temporary context in case async was completed in sync
    // If it didnt, then we will clear the context and set it at scheduler side
    MiddlewareAction action;

    ptr->SetReturnPtr(static_cast<void*>(&action));
    ptr->Resume();

    // Reset to remove dangling references
    httpApi->SetGlobalPtrData(nullptr);

    // Check if we are done with async, if not return ptr
    if(!ptr->IsFinished()) {
        ptr->SetReturnPtr(nullptr);
        return {{}, ptr};
    }

    // We were able to finish async in sync, coroutine stack should only have 1 element, itself
    // If not, big no no
    if(cctx->coroStack.size() > 1)
        logger.Fatal(
            "[HttpMiddleware]: Coroutine stack imbalance detected after async middleware execution."
            " Type: ", (int)meta.type
        );

    // Clear out the coroutine stack for future middlewares
    cctx->coroStack.clear();

    return {static_cast<MiddlewareAction>(action), nullptr};
}

void HttpMiddleware::FixInternalLinks(MiddlewareStack& stack)
{
    constexpr std::uint16_t END = MiddlewareEntry::END;
    std::uint16_t size = stack.size();

    // Sanity checks
    if(size == 0)
        return;

    std::uint16_t lastSm  = END;
    std::uint16_t lastCbm = END;
    std::uint16_t lastCem = END;

    for(std::uint16_t i = 0; i < size; ++i) {
        auto h = stack[i].handled;

        // LINEAR
        if(h & static_cast<std::uint8_t>(MiddlewareType::LINEAR)) {
            if(lastSm != END)
                stack[lastSm].nextSm = i;
            lastSm = i;
        }

        // STREAM_CHUNK
        if(h & static_cast<std::uint8_t>(MiddlewareType::STREAM_CHUNK)) {
            if(lastCbm != END)
                stack[lastCbm].nextCbm = i;
            lastCbm = i;
        }

        // STREAM_END
        if(h & static_cast<std::uint8_t>(MiddlewareType::STREAM_END)) {
            if(lastCem != END)
                stack[lastCem].nextCem = i;
            lastCem = i;
        }
    }
}

} // namespace WFX::Http