#ifndef WFX_UTILS_BUFFER_POOL_HPP
#define WFX_UTILS_BUFFER_POOL_HPP

#include "utils/logger/logger.hpp"

#include <cstddef>
#include <mutex>
#include <vector>
#include <functional>

namespace WFX::Utils {

// Wrapper around TLSF by Matthew Conte
class BufferPool {
public:
    using ResizeCallback = std::function<std::size_t(std::size_t)>;

    BufferPool(std::size_t initialSize, ResizeCallback resizeCb = nullptr);
    ~BufferPool();

    void* Lease(std::size_t size);
    void  Release(void* ptr);

    std::size_t PoolSize() const;

private:
    void* AddPool(std::size_t size);

private:
    struct Pool {
        void* memory;
        void* handle;

        Pool(void* mem, void* poolHandle) : memory(mem), handle(poolHandle) {}
    };

    Logger& logger_ = Logger::GetInstance();

    std::vector<Pool>    pools_;
    void*                tlsfAllocator_;
    std::size_t          poolSize_;
    ResizeCallback       resizeCallback_;
    std::recursive_mutex poolMutex_;
};

} // namespace WFX::Utils

#endif // WFX_UTILS_BUFFER_POOL_HPP