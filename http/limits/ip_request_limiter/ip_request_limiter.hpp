#ifndef WFX_HTTP_IP_REQUEST_LIMITER_HPP
#define WFX_HTTP_IP_REQUEST_LIMITER_HPP

#include "utils/hash_map/concurrent_hash_map.hpp"

#include <string>
#include <mutex>

namespace WFX::Http {

// For 'ConcurrentHashMap'
using namespace WFX::Utils;

class IpRequestLimiter {
public:
    static IpRequestLimiter& GetInstance();

    bool Allow(const std::string& ip);

private:
    IpRequestLimiter() = default;
    ~IpRequestLimiter() = default;

    IpRequestLimiter(const IpRequestLimiter&) = delete;
    IpRequestLimiter& operator=(const IpRequestLimiter&) = delete;
    IpRequestLimiter(IpRequestLimiter&&) = delete;
    IpRequestLimiter& operator=(IpRequestLimiter&&) = delete;

private:
    struct TokenBucket {
        int tokens = MAX_TOKENS;
        std::chrono::steady_clock::time_point lastRefill = std::chrono::steady_clock::now();
    };

    static constexpr int MAX_TOKENS = 10;      // burst size
    static constexpr int REFILL_RATE = 5;      // tokens per second

    ConcurrentHashMap<std::string, TokenBucket, 64, 128> buckets_;
};

} // namespace WFX::Core

#endif // WFX_HTTP_IP_REQUEST_LIMITER_HPP