#ifndef WFX_UTILS_STRING_HPP
#define WFX_UTILS_STRING_HPP

#include <cstdint>
#include <string_view>

namespace WFX::Utils {

namespace StringSanitizer {
    std::uint8_t ToLowerAscii(std::uint8_t c);

    // Constant time comparisions
    bool CTStringCompare(std::string_view lhs, std::string_view rhs);
    bool CTInsensitiveStringCompare(std::string_view lhs, std::string_view rhs);

    bool CaseInsensitiveCompare(std::string_view lhs, std::string_view rhs);

    /* NOTE: 'path' buffer must be a valid writable buffer */
    bool        NormalizeURIPathInplace(std::string_view& path);
    std::string NormalizePathToIdentifier(std::string_view path, std::string_view prefix);
} // namespace StringSanitizer

} // namespace WFX::Utils


#endif // WFX_UTILS_STRING_HPP