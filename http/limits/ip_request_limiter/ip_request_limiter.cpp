#include "ip_request_limiter.hpp"

namespace WFX::Http {

IpRequestLimiter& IpRequestLimiter::GetInstance()
{
    static IpRequestLimiter instance;
    return instance;
}

bool IpRequestLimiter::Allow(const std::string& ip)
{
    using namespace std::chrono;
    auto now = steady_clock::now();

    return buckets_.GetOrInsertWith(ip, [&](TokenBucket& bucket) {
        int refill = static_cast<int>(
            duration_cast<milliseconds>(now - bucket.lastRefill).count() * REFILL_RATE / 1000
        );

        if(refill > 0) {
            bucket.tokens = std::min(MAX_TOKENS, bucket.tokens + refill);
            bucket.lastRefill = now;
        }

        if(bucket.tokens > 0) {
            --bucket.tokens;
            return true;
        }

        return false;
    });
}

} // namespace WFX::Http