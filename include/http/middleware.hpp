#ifndef WFX_INC_HTTP_MIDDLEWARE_MACROS_HPP
#define WFX_INC_HTTP_MIDDLEWARE_MACROS_HPP

#include "aliases.hpp"
#include "response.hpp"
#include "helper.hpp"
#include "core/core.hpp"
#include "shared/utils/deferred_init_vector.hpp"

#define WFX_MW_CLASS(id)    WFX_CONCAT(WFXMiddleware_, id)
#define WFX_MW_INSTANCE(id) WFX_CONCAT(WFXMiddlewareInst_, id)

// Generate once
#define WFX_INTERNAL_MW_REGISTER_IMPL(name, handle, callback, uniq)    \
    static struct WFX_MW_CLASS(uniq) {                                 \
        WFX_MW_CLASS(uniq)() {                                         \
            WFX::Shared::__WFXDeferredMiddleware().emplace_back([] {   \
                __WFXApi->GetHttpAPIV1()->RegisterMiddleware(          \
                    name, MakeMiddlewareEntry(callback, handle)        \
                );                                                     \
            });                                                        \
        }                                                              \
    } WFX_MW_INSTANCE(uniq);

#define WFX_INTERNAL_MW_REGISTER(name, callback)                       \
    WFX_INTERNAL_MW_REGISTER_IMPL(                                     \
        name, static_cast<std::uint8_t>(MiddlewareType::LINEAR),       \
        callback, __COUNTER__                                          \
    )

#define WFX_INTERNAL_MW_REGISTER_EX(name, handle, callback)            \
    WFX_INTERNAL_MW_REGISTER_IMPL(name, handle, callback, __COUNTER__)

// vvv User friendly Macros vvv
#define WFX_MIDDLEWARE(name, cb)             WFX_INTERNAL_MW_REGISTER(name, cb)
#define WFX_MIDDLEWARE_EX(name, handle, cb)  WFX_INTERNAL_MW_REGISTER_EX(name, handle, cb)

// vvv Helper Macros vvv
#define WFX_MW_LIST(...)      MakeMiddlewareFromFunctions(__VA_ARGS__)
#define WFX_MW_HANDLE(...)    MakeMiddlewareHandle(__VA_ARGS__)
#define WFX_MW_ENTRY(cb, ...) MiddlewareEntry{ .mw = cb, .handled = MakeMiddlewareHandle(__VA_ARGS__) }

#endif // WFX_INC_HTTP_MIDDLEWARE_MACROS_HPP