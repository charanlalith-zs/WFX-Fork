#include "ip_connection_limiter.hpp"

namespace WFX::Http {

IpConnectionLimiter& IpConnectionLimiter::GetInstance()
{
    static IpConnectionLimiter instance;
    return instance;
}

bool IpConnectionLimiter::Allow(const std::string& ip)
{
    return ipConnCount_.GetOrInsertWith(ip, [](int& count) -> bool {
        if(count >= MAX_CONNECTIONS_PER_IP)
            return false;

        ++count;
        return true;
    });
}

void IpConnectionLimiter::Release(const std::string& ip)
{
    if(ipConnCount_.GetOrInsertWith(ip, [](int& count) -> bool {
        return --count <= 0;
    }))
        ipConnCount_.Erase(ip);
}

} // namespace WFX::Http