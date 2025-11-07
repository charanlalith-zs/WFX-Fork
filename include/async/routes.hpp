#ifndef WFX_INC_ASYNC_ROUTES_HPP
#define WFX_INC_ASYNC_ROUTES_HPP

/*
 * Contains the sugar syntax for async functions
 * While i could've defined all these inside of http/routes.hpp, decided it was cleaner to define-
 * -them here. They will get included anyways later on
 */

#include "interface.hpp"
#include "core/core.hpp"
#include <tuple>

namespace Async {

// Wraps any callable + args into a CoroutineBase
template<typename Fn, typename... Args>
class CallableCoroutine final : public CoroutineBase {
public:
    Fn fn_;
    std::tuple<Args...> args_; // Store references as is

    CallableCoroutine(Fn&& fn, Args&&... args)
        : fn_(std::forward<Fn>(fn)),
          args_(std::forward<Args>(args)...)
    {}

    CallableCoroutine(const CallableCoroutine&)            = delete;
    CallableCoroutine& operator=(const CallableCoroutine&) = delete;

    void Resume() noexcept override
    {
        std::apply(
            [&](auto&&... unpacked) {
                fn_(this, std::forward<decltype(unpacked)>(unpacked)...);
            },
            args_
        );
    }
};

// Factory that constructs, registers and resumes async func
// IMPORTANT: THIS FUNCTION EXPECTS POINTER TO CONNECTION CONTEXT BE SET VIA 'SetGlobalPtrData' BEFORE BEING INVOKED
template<typename Fn, typename... Args>
inline AsyncPtr MakeAsync(Fn&& fn, Args&&... args)
{
    // Keep 'Args' as forwarding refs
    using CoroutineType = CallableCoroutine<std::decay_t<Fn>, Args&&...>;

    auto coro = std::make_unique<CoroutineType>(std::forward<Fn>(fn), std::forward<Args>(args)...);
    auto ptr = __WFXApi->GetAsyncAPIV1()->RegisterCallback(
                __WFXApi->GetHttpAPIV1()->GetGlobalPtrData(), std::move(coro)
            );
    
    if(ptr)
        ptr->Resume();

    return ptr;
}

// Wrapper around 'MakeAsync', for semantic purpose
template<typename Fn, typename... Args>
inline AsyncPtr Call(Fn&& fn, Args&&... args)
{
    return MakeAsync(std::forward<Fn>(fn), std::forward<Args>(args)...);
}

// Await returns whether to yield or not. True means we need to yield, False me no need to yield-
// -function was completed in sync. It also increments 'self' state by 1 on every call
// IMPORTANT: THIS FUNCTION EXPECTS POINTER TO CONNECTION CONTEXT BE SET VIA 'SetGlobalPtrData' BEFORE BEING INVOKED
template<typename T>
inline bool Await(AsyncPtr self, T&& asyncCall) noexcept
{
    static_assert(Async::IsCoroutinePtrV<T>, "Await() requires callable to return 'AsyncPtr'. AsyncPtr is just Async::CoroutineRawPtr");

    self->IncState();

    AsyncPtr result = asyncCall;

    if(result && !result->IsFinished())
        return true;

    __WFXApi->GetAsyncAPIV1()->PopCallback(__WFXApi->GetHttpAPIV1()->GetGlobalPtrData());
    return false;
}

} // namespace Async

#endif // WFX_INC_ASYNC_ROUTES_HPP