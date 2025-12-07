#ifndef WFX_UTILS_MATH_HPP
#define WFX_UTILS_MATH_HPP

#include <cstdint>
#include <cstddef>

namespace WFX::Utils {

namespace Math {
    std::size_t RoundUpToPowerOfTwo(std::size_t x);
    bool        IsPowerOfTwo(std::size_t x);

    int Log2(std::size_t x);
    int Log2RoundUp(std::size_t x);
} // namespace Math

} // namespace WFX::Utils


#endif // WFX_UTILS_MATH_HPP