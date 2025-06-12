#ifndef WFX_HTTP_HEADERS_HPP
#define WFX_HTTP_HEADERS_HPP

#include <string>
#include <unordered_map>
#include <vector>

namespace WFX {

struct CaseInsensitiveHash {
    size_t operator()(const std::string& key) const;
};

struct CaseInsensitiveEqual {
    bool operator()(const std::string& lhs, const std::string& rhs) const;
};

// === HttpHeaders class === //
class HttpHeaders {
public:
    HttpHeaders() = default;

    void        SetHeader(const std::string& key, const std::string& value);
    bool        HasHeader(const std::string& key) const;
    std::string GetHeader(const std::string& key) const;
    void        RemoveHeader(const std::string& key);
    void        Clear();
    
    std::vector<std::pair<std::string, std::string>> GetAllHeaders() const;

private:
    std::unordered_map<std::string, std::string, CaseInsensitiveHash, CaseInsensitiveEqual> headers_;
};

} // namespace WFX

#endif