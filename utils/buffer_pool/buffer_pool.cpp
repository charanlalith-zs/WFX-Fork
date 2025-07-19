#include "buffer_pool.hpp"

#include <cstdlib>
#include <algorithm>
#include <cstring>

#if defined(_WIN32)
    #include <malloc.h>
#endif

extern "C" {
    #include "third_party/tlsf/tlsf.h"
}

namespace WFX::Utils {

BufferPool::BufferPool(std::size_t initialSize, ResizeCallback resizeCb)
    : poolSize_(initialSize), resizeCallback_(resizeCb)
{
    void* memory = AlignedMalloc(poolSize_, tlsf_align_size());
    if(!memory)
        logger_.Fatal("[BufferPool]: Initial malloc failed");

    tlsfAllocator_ = tlsf_create_with_pool(memory, poolSize_);
    if(!tlsfAllocator_) {
        AlignedFree(memory);
        logger_.Fatal("[BufferPool]: Failed to initialize TLSF with pool");
    }

    // The first pool itself has no handle, so its nullptr
    pools_.emplace_back(memory, nullptr);
    logger_.Info("[BufferPool]: Created initial pool with size ", poolSize_, " bytes at ", reinterpret_cast<uintptr_t>(memory));
}

BufferPool::~BufferPool()
{
    logger_.Info("[BufferPool]: Freeing ", pools_.size(), pools_.size() > 1 ? " pools:" : " master pool:");
    std::lock_guard<std::recursive_mutex> lock(poolMutex_);

    // Free additional pools (1 to N)
    for(std::size_t i = 1; i < pools_.size(); ++i) {
        auto& pool = pools_[i];
        if(pool.handle) {
            tlsf_remove_pool(tlsfAllocator_, pool.handle);
            logger_.Info("[BufferPool]: Removed TLSF pool at ", reinterpret_cast<uintptr_t>(pool.memory));
        }

        AlignedFree(pool.memory);
        logger_.Info("[BufferPool]: Freed memory pool at ", reinterpret_cast<uintptr_t>(pool.memory));
    }

    // Destroy allocator BEFORE freeing initial pool's memory
    tlsf_destroy(tlsfAllocator_);
    logger_.Info("[BufferPool]: Destroyed TLSF allocator");

    if(!pools_.empty()) {
        void* memory = pools_[0].memory;
        AlignedFree(memory);
        logger_.Info("[BufferPool]: Freed initial memory pool at ", reinterpret_cast<uintptr_t>(memory));
    }

    pools_.clear();
}

void* BufferPool::AlignedMalloc(std::size_t size, std::size_t alignment)
{
#if defined(_WIN32)
    return _aligned_malloc(size, alignment);
#else
    void* ptr = nullptr;
    if(posix_memalign(&ptr, alignment, size) != 0) return nullptr;
    return ptr;
#endif
}

void  BufferPool::AlignedFree(void* ptr)
{
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

void* BufferPool::AddPool(std::size_t size)
{
    void* memory = AlignedMalloc(size, tlsf_align_size());
    if(!memory)
        logger_.Fatal("[BufferPool]: malloc failed for ", size, " bytes");

    void* poolHandle = tlsf_add_pool(tlsfAllocator_, memory, size);
    if(!poolHandle) {
        AlignedFree(memory);
        logger_.Fatal("[BufferPool]: Failed to add TLSF pool");
    }

    pools_.emplace_back(memory, poolHandle);
    logger_.Info("[BufferPool]: Added new pool of size ", size, " bytes at ", reinterpret_cast<uintptr_t>(memory));

    return poolHandle;
}

void* BufferPool::Lease(std::size_t size)
{
    std::lock_guard<std::recursive_mutex> lock(poolMutex_);

    void* ptr = tlsf_malloc(tlsfAllocator_, size);
    if(ptr)
    {
        logger_.Debug("[BufferPool]: Allocated ", size, " bytes at ", reinterpret_cast<uintptr_t>(ptr));
        return ptr;
    }

    std::size_t newSize = resizeCallback_ ? resizeCallback_(poolSize_) : poolSize_;
    newSize             = std::max(newSize, poolSize_);
    poolSize_           = newSize;

    AddPool(newSize);
    ptr = tlsf_malloc(tlsfAllocator_, size);
    if(!ptr)
        logger_.Fatal("[BufferPool]: Allocation failed after adding new pool");

    logger_.Debug("[BufferPool]: Allocated ", size, " bytes from new pool at ", reinterpret_cast<uintptr_t>(ptr));
    return ptr;
}

void BufferPool::Release(void* ptr)
{
    if(!ptr) return;

    std::lock_guard<std::recursive_mutex> lock(poolMutex_);
    tlsf_free(tlsfAllocator_, ptr);
    logger_.Debug("[BufferPool]: Freed memory at ", reinterpret_cast<uintptr_t>(ptr));
}

std::size_t BufferPool::PoolSize() const
{
    return poolSize_;
}

} // namespace WFX::Utils