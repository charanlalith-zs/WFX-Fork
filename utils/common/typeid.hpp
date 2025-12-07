#ifndef WFX_UTILS_TYPEID_HPP
#define WFX_UTILS_TYPEID_HPP

#include <cstdint>
#include <unordered_map>
#include <string>

using TypeInfo = std::uintptr_t;

namespace WFX::Utils {
namespace TypeID {

// vvv Main Function vvv
template<typename T>
inline std::uintptr_t GetID() noexcept
{
    static int dummy;
    return reinterpret_cast<std::uintptr_t>(&dummy);
}

// vvv Debug Purposes vvv
inline std::unordered_map<std::uintptr_t, std::string>& Map()
{
    static std::unordered_map<std::uintptr_t, std::string> map;
    return map;
}

template<typename T>
inline void RegisterTypeName()
{
    Map()[GetID<T>()] = typeid(T).name();
}

inline std::string GetName(std::uintptr_t id)
{
    auto it = Map().find(id);
    return (it != Map().end()) ? it->second : "<unknown>";
}

} // namespace TypeID
} // namespace WFX::Utils

#endif // WFX_UTILS_TYPEID_HPP