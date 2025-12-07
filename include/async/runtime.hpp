#ifndef WFX_INC_ASYNC_RUNTIME_HPP
#define WFX_INC_ASYNC_RUNTIME_HPP

#include "interface.hpp"
#include "core/core.hpp"
#include <unordered_map>
#include <tuple>

namespace Async {

// Everything will be decayed except std::ref, which stays as is
template<typename T>
struct StoredHelper {
    using type = std::decay_t<T>;
};

template<typename T>
struct StoredHelper<std::reference_wrapper<T>> {
    using type = std::reference_wrapper<T>;
};

template<typename T>
using StoredType = typename StoredHelper<T>::type;

// Wraps any callable + args into a CoroutineBase type
template<typename Fn, typename... Args>
class CallableCoroutineBase : public CoroutineBase {
public:
    CallableCoroutineBase(Fn&& fn, Args&&... args)
        : fn_(std::forward<Fn>(fn)),
          args_(std::forward<Args>(args)...)
    {}

    CallableCoroutineBase(const CallableCoroutineBase&)            = delete;
    CallableCoroutineBase& operator=(const CallableCoroutineBase&) = delete;

public: // Main Functions
    void Resume() noexcept override
    {
        SetYielded(false);
        std::apply(
            [this](auto&&... unpacked) {
                fn_(this, UnwrapRef(unpacked)...);
            },
            args_
        );
    }

    LocalVariable& PersistLocal(const char* name) noexcept override
    {
        return locals_[name];
    }

protected: // Helper Function
    template<typename ArgType>
    constexpr static decltype(auto) UnwrapRef(ArgType&& value) noexcept
    {
        if constexpr(IsReferenceWrapperV<ArgType>)
            return value.get();      // Unwrap
        else
            return value;            // Pass through unchanged
    }

protected: // Main storage
    // Main function pointer
    Fn fn_;

    // Base storage of all function arguments within a coroutine
    std::tuple<StoredType<Args>...> args_;

    // Base storage for all local variables within a coroutine
    std::unordered_map<const char*, LocalVariable> locals_;
};

// Return type specialization
template<typename Ret, typename Fn, typename... Args>
class CallableCoroutine : public CallableCoroutineBase<Fn, Args...> {
    using Base = CallableCoroutineBase<Fn, Args...>;

public:
    CallableCoroutine(Fn&& fn, Args&&... args)
        : Base(std::forward<Fn>(fn), std::forward<Args>(args)...)
    {}

    void     SetReturnPtr(void* ptr) noexcept override { ret_ = static_cast<Ret*>(ptr); }
    void*    GetReturnPtr()          noexcept override { return static_cast<void*>(ret_); }
    TypeInfo GetReturnType()   const noexcept override { return WFX::Utils::TypeID::GetID<Ret>(); }

private:
    Ret* ret_ = nullptr;
};

// Void return type specialization
template<typename Fn, typename... Args>
class CallableCoroutine<void, Fn, Args...> : public CallableCoroutineBase<Fn, Args...> {
    using Base = CallableCoroutineBase<Fn, Args...>;

public:
    CallableCoroutine(Fn&& fn, Args&&... args)
        : Base(std::forward<Fn>(fn), std::forward<Args>(args)...)
    {}

    void     SetReturnPtr(void*)   noexcept override { /* no-op */ }
    void*    GetReturnPtr()        noexcept override { return nullptr; }
    TypeInfo GetReturnType() const noexcept override { return WFX::Utils::TypeID::GetID<void>(); }
};

// Factory that constructs and registers async func
// IMPORTANT: THIS FUNCTION EXPECTS POINTER TO CONNECTION CONTEXT BE SET VIA 'SetGlobalPtrData' BEFORE BEING INVOKED
template<typename Ret, typename Fn, typename... Args>
inline AsyncPtr MakeAsync(Fn&& fn, Args&&... args)
{
    // Keep 'Args' as forwarding refs
    using CoroutineType = CallableCoroutine<Ret, std::decay_t<Fn>, Args&&...>;

    auto coro = std::make_unique<CoroutineType>(std::forward<Fn>(fn), std::forward<Args>(args)...);
    auto ptr = __WFXApi->GetAsyncAPIV1()->RegisterCallback(
                __WFXApi->GetHttpAPIV1()->GetGlobalPtrData(), std::move(coro)
            );

    return ptr;
}

// Wrapper around 'MakeAsync', if u want to make ur life easier in implementing free standing functions-
// -(You don't have to call 'MakeAsync' directly). Only caveat is, u cannot directly call that function-
// -instead u have to use the below function to call it for you
template<typename Ret, typename Fn, typename... Args>
inline AsyncPtr Call(Fn&& fn, Args&&... args)
{
    return MakeAsync<Ret>(std::forward<Fn>(fn), std::forward<Args>(args)...);
}

// Await returns whether to yield or not. True means we need to yield, False means no need to [yield / handle error]-
// -function was completed in sync
// IMPORTANT: THIS FUNCTION EXPECTS POINTER TO CONNECTION CONTEXT BE SET VIA 'SetGlobalPtrData' BEFORE BEING INVOKED
template<typename Ret>
inline bool Await(AsyncPtr self, AsyncPtr callResult, Ret* returnIfAny) noexcept
{
    /*
     * NOTE: Programmer mistakes are 'Fatal', others just propagate error as is
     */
    auto& logger = WFX::Utils::Logger::GetInstance();

    if(self->IsYielded())
        logger.Fatal(
            "Async::Await() called while coroutine was still yielded from previous await"
        );

    if(!callResult) {
        self->SetError(Error::INTERNAL_FAILURE);
        goto __CallComplete;
    }

    // Only enforce type safety if pointer is non-null
    if(returnIfAny != nullptr) {
        TypeInfo crType = callResult->GetReturnType();
        TypeInfo urType = WFX::Utils::TypeID::GetID<Ret>();

        if(crType != urType)
            logger.Fatal(
                "Async::Await() found mismatched return type. Coroutine expected: ", WFX::Utils::TypeID::GetName(crType),
                " but found: ", WFX::Utils::TypeID::GetName(urType)
            );
    }

    // Set the return ptr (if any) and resume the coroutine
    callResult->SetReturnPtr(static_cast<void*>(returnIfAny));
    callResult->Resume();

    if(!callResult->IsFinished()) {
        self->SetYielded(true);
        return true;
    }

    // If the async completed instantly but failed, propagate error
    if(callResult->HasError())
        self->SetError(callResult->GetError());

__CallComplete:
    __WFXApi->GetAsyncAPIV1()->PopCallback(__WFXApi->GetHttpAPIV1()->GetGlobalPtrData());
    return false;
}

// Runtime casting. Casts pointer to valid type if types match
template<typename T>
inline T* SafeCastReturnPtr(AsyncPtr callResult) noexcept
{
    if(!callResult || callResult->GetReturnType() != WFX::Utils::TypeID::GetID<T>())
        return nullptr;

    return static_cast<T*>(callResult->GetReturnPtr());
}

} // namespace Async

#endif // WFX_INC_ASYNC_RUNTIME_HPP