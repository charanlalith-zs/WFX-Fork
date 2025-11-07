#ifndef WFX_INC_ASYNC_INTERFACE_HPP
#define WFX_INC_ASYNC_INTERFACE_HPP

/*
 * Interface for async emulation in C++17
 */

#include <cstdint>
#include <memory>
#include <type_traits>

namespace Async {

struct CoroutineBase {
    virtual ~CoroutineBase() = default;

    void          IncState()                       { __State_++; }
    void          SetState(std::uint32_t newState) { __State_ = newState; }
    std::uint32_t GetState()                       { return __State_; }
    void          Finish()                         { __Done_ = true; }
    bool          IsFinished()                     { return __Done_; }

    virtual void Resume() noexcept = 0;

private: // Internals
    std::uint32_t __State_ = 0;
    bool          __Done_  = false;
};

// Ease of use :)
using CoroutinePtr    = std::unique_ptr<CoroutineBase>;
using CoroutineRawPtr = CoroutineBase*;

// vvv Custom Traits for C++17 vvv
template<typename T>
struct RemoveConstValRef {
    using type = std::remove_cv_t<std::remove_reference_t<T>>;
};

template<typename T>
using RemoveConstValRefT = typename RemoveConstValRef<T>::type;

template<typename T>
constexpr bool IsCoroutinePtrV = std::is_same_v<RemoveConstValRefT<T>, CoroutineRawPtr>;

} // namespace Async

// Considering this shits used quite often, better just alias it atp
using AsyncPtr = Async::CoroutineRawPtr;

#endif // WFX_INC_ASYNC_INTERFACE_HPP