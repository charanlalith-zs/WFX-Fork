// ip_connection_limiter.hpp
#pragma once

#include "utils/hash_map/concurrent_hash_map.hpp"

#include <string>
#include <chrono>
#include <mutex>

namespace WFX::Http {

using namespace WFX::Utils;

class IpConnectionLimiter {
public:
    static IpConnectionLimiter& GetInstance();

    bool Allow(const std::string& ip);
    void Release(const std::string& ip);

private:
    IpConnectionLimiter() = default;
    ~IpConnectionLimiter() = default;

    IpConnectionLimiter(const IpConnectionLimiter&) = delete;
    IpConnectionLimiter& operator=(const IpConnectionLimiter&) = delete;
    IpConnectionLimiter(IpConnectionLimiter&&) = delete;
    IpConnectionLimiter& operator=(IpConnectionLimiter&&) = delete;

private:
    static constexpr int MAX_CONNECTIONS_PER_IP = 10;
    ConcurrentHashMap<std::string, int, 32, 64> ipConnCount_;
};

} // namespace WFX::Http