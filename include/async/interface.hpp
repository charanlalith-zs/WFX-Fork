#ifndef WFX_INC_ASYNC_INTERFACE_HPP
#define WFX_INC_ASYNC_INTERFACE_HPP

/*
 * Interface for async emulation in C++17
 */

#include "local_storage.hpp"
#include "utils/common/typeid.hpp"
#include <type_traits>

namespace Async {

enum class Error {
    NONE = 0,
    TIMER_FAILURE,
    IO_FAILURE,
    INTERNAL_FAILURE
};

struct CoroutineBase {
public:
    CoroutineBase()          = default;
    virtual ~CoroutineBase() = default;

public:
    void          IncState()                       { state_++; }
    void          SetState(std::uint32_t newState) { state_ = newState; }
    std::uint32_t GetState()                       { return state_; }

    void          SetYielded(bool yielded)         { yielded = yielded; }
    bool          IsYielded()                      { return yielded; }
    void          Finish()                         { done = 1; }
    bool          IsFinished()                     { return done; }

    void          SetError(Error e)                { error = static_cast<std::uint8_t>(e); }
    Error         GetError() const                 { return static_cast<Error>(error); }
    bool          HasError() const                 { return error != static_cast<std::uint8_t>(Error::NONE); }

public: // Contract
    // Main
    virtual void            Resume()                  noexcept = 0;

    // Storage
    virtual LocalVariable&  PersistLocal(const char*) noexcept = 0;

    // Return Values
    virtual void            SetReturnPtr(void*)       noexcept = 0;
    virtual void*           GetReturnPtr()            noexcept = 0;
    virtual TypeInfo        GetReturnType()     const noexcept = 0;

private: // Internals
    std::uint32_t state_ = 0;
    union {
        struct {
            std::uint8_t done    : 1;
            std::uint8_t yielded : 1; // For safeguarding against forgotten 'return' after 'Await'
            std::uint8_t error   : 3; // 'Error' enum
            std::uint8_t __Pad   : 3;
        };
        std::uint8_t _ = 0; // For zero initializing the bitfield struct
    };
};
static_assert(sizeof(CoroutineBase) <= 16, "CoroutineBase must strictly be less than or equal to 16 bytes");

// Ease of use :)
using CoroutinePtr    = std::unique_ptr<CoroutineBase>;
using CoroutineRawPtr = CoroutineBase*;

// vvv Custom Traits vvv
//  Coro
template<typename T>
struct RemoveConstValRef {
    using type = std::remove_cv_t<std::remove_reference_t<T>>;
};

template<typename T>
using RemoveConstValRefT = typename RemoveConstValRef<T>::type;

template<typename T>
constexpr bool IsCoroutinePtrV = std::is_same_v<RemoveConstValRefT<T>, CoroutineRawPtr>;

//  Ref
template<typename T>
struct IsReferenceWrapper : std::false_type {};

template<typename U>
struct IsReferenceWrapper<std::reference_wrapper<U>> : std::true_type {};

template<typename T>
constexpr bool IsReferenceWrapperV = IsReferenceWrapper<std::remove_reference_t<T>>::value;

} // namespace Async

// Considering this shits used quite often, better just alias it atp
using AsyncPtr = Async::CoroutineRawPtr;

#endif // WFX_INC_ASYNC_INTERFACE_HPP