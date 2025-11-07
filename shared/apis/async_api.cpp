#include "async_api.hpp"
#include "http/connection/http_connection.hpp"

namespace WFX::Shared {

using WFX::Http::ConnectionContext;

const ASYNC_API_TABLE* GetAsyncAPIV1()
{
    // 'ctx' is ConnectionContext just type erased so user doesn't DO anything
    static ASYNC_API_TABLE __GlobalAsyncAPIV1 = {
        [](void* ctx, Async::CoroutinePtr&& frame) -> AsyncPtr {  // RegisterAsyncCallback
            if(!ctx || !frame)
                return nullptr;

            auto cctx = static_cast<ConnectionContext*>(ctx);
            return (cctx->coroStack.emplace_back(std::move(frame))).get();
        },
        [](void* ctx) { // PopAsyncCallback
            if(!ctx)
                return;

            auto cctx = static_cast<ConnectionContext*>(ctx);
            cctx->coroStack.pop_back();
        },
        [](void* ctx) { // ResumeRecentCallback
            if(!ctx)
                return true;

            auto cctx = static_cast<ConnectionContext*>(ctx);
            if(!cctx->coroStack.empty()) {
                auto& coro = cctx->coroStack.back();
                coro->Resume();
                return coro->IsFinished();
            }

            return true;
        },

        // Version
        AsyncAPIVersion::V1
    };

    return &__GlobalAsyncAPIV1;
}

} // namespace WFX::Shared