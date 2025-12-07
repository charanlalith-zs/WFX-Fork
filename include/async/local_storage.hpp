#ifndef WFX_INC_ASYNC_LOCAL_STORAGE_HPP
#define WFX_INC_ASYNC_LOCAL_STORAGE_HPP

#include "utils/logger/logger.hpp"
#include <memory>

namespace Async {

/*
 * This will used to store local variables which may need to persist across async calls
 * While i could use std::any, making life simpler, decided not to for this specific area
 * Simply cuz std::any can change types during runtime, i don't want to allow that
 * 
 * "Uhh but you're just outright wron-"
 * 
 * Sybau
 */

struct LocalVariableBase {
    virtual ~LocalVariableBase() = default;
};

template<typename T>
struct LocalVariableTmpl : LocalVariableBase {
    T value;

    template<typename... Args>
    LocalVariableTmpl(Args&&... args)
        : value(std::forward<Args>(args)...)
    {}
};

struct LocalVariable {
public: // Constructor & Destructor
    LocalVariable()
        : type(Type::EMPTY), heapObj(nullptr)
    {}
    ~LocalVariable()
    {
        if(type == Type::HEAP) {
            delete heapObj;
            heapObj = nullptr;
        }
        // Trivial types do not need destruction
        type = Type::EMPTY;
    }

public: // Main Functions
    template<typename T, typename... Args>
    T& InitOrGet(Args&&... args)
    {
        static_assert(!std::is_reference_v<T>,
            "LocalVariable cannot store references");

        // First-time init
        if(type == Type::EMPTY) {
            if constexpr(std::is_trivially_copyable_v<T> &&
                         sizeof(T) <= sizeof(void*)) {
                type = Type::TRIVIAL;
                new (&trivial) T(std::forward<Args>(args)...);
            }
            else {
                type = Type::HEAP;
                heapObj = new LocalVariableTmpl<T>(std::forward<Args>(args)...);
            }
        }

        // Always return reference
        if(type == Type::TRIVIAL)
            return *reinterpret_cast<T*>(&trivial);
        else
            return static_cast<LocalVariableTmpl<T>*>(heapObj)->value;
    }

private:
    enum class Type : std::uint8_t {
        EMPTY,
        TRIVIAL,
        HEAP
    };

    using HeapStorageType  = LocalVariableBase*;
    using StackStorageType = std::aligned_storage_t<sizeof(void*), alignof(void*)>;
    
    // Actual storage
    Type type;
    union {
        StackStorageType trivial;
        HeapStorageType  heapObj;
    };
};

static_assert(sizeof(LocalVariable) <= 16, "LocalVariable must be strictly less than or equal to 16 bytes");

} // namespace Async

#endif // WFX_INC_ASYNC_LOCAL_STORAGE_HPP