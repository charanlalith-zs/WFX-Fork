#include "http_headers.hpp"

#include <algorithm>
#include <cctype>

namespace WFX {

// === Secure Case-Insensitive Hashing (FNV-1a 64-bit) === //
size_t CaseInsensitiveHash::operator()(const std::string& key) const {
    const size_t fnvOffsetBasis = 14695981039346656037ULL;
    const size_t fnvPrime = 1099511628211ULL;

    size_t hash = fnvOffsetBasis;
    for (char c : key) {
        hash ^= static_cast<unsigned char>(std::tolower(c));
        hash *= fnvPrime;
    }
    return hash;
}

bool CaseInsensitiveEqual::operator()(const std::string& lhs, const std::string& rhs) const {
    if (lhs.size() != rhs.size()) return false;

    unsigned char result = 0;
    for (size_t i = 0; i < lhs.size(); ++i) {
        result |= std::tolower(static_cast<unsigned char>(lhs[i])) ^
                  std::tolower(static_cast<unsigned char>(rhs[i]));
    }
    return result == 0;
}

// === HttpHeaders Methods === //
void HttpHeaders::SetHeader(const std::string& key, const std::string& value) {
    headers_[key] = value;
}

bool HttpHeaders::HasHeader(const std::string& key) const {
    return headers_.find(key) != headers_.end();
}

std::string HttpHeaders::GetHeader(const std::string& key) const {
    auto it = headers_.find(key);
    return (it != headers_.end()) ? it->second : "";
}

void HttpHeaders::RemoveHeader(const std::string& key) {
    headers_.erase(key);
}

std::vector<std::pair<std::string, std::string>> HttpHeaders::GetAllHeaders() const {
    std::vector<std::pair<std::string, std::string>> out;
    out.reserve(headers_.size());
    for (const auto& kv : headers_) {
        out.emplace_back(kv);
    }
    return out;
}

void HttpHeaders::Clear() {
    headers_.clear();
}

} // namespace WFX