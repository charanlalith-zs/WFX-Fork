#ifndef WFX_UTILS_HASH_SHARD_HPP
#define WFX_UTILS_HASH_SHARD_HPP

#include "utils/buffer_pool/buffer_pool.hpp"

#include <atomic>
#include <shared_mutex>
#include <functional>
#include <cstdint>
#include <cstring>
#include <array>
#include <cassert>
#include <vector>
#include <memory>
#include <immintrin.h>

namespace WFX::Utils {

template <typename T>
inline std::size_t WFXHash(const T& key)
{
    return std::hash<T>{}(key);
}

template <typename K, typename V>
class HashShard {
    static constexpr std::size_t MAX_PROBE_LIMIT     = 64;
    static constexpr float       KLOAD_FACTOR_GROW   = 0.7f;
    static constexpr float       KLOAD_FACTOR_SHRINK = 0.2f;

    struct Entry {
        K key;
        V value;
        uint8_t probe_length;
        bool occupied;
    };

    BufferPool&               pool_;
    Entry*                    entries_               = nullptr;
    std::size_t               capacity_              = 0;
    std::size_t               initialBucketCapacity_ = 0;
    std::atomic<std::size_t>  size_{0};
    mutable std::shared_mutex mutex_;

public:
    explicit HashShard(BufferPool& pool);
    ~HashShard();

    void Init(std::size_t cap);
    bool Insert(const K& key, const V& value);
    bool Get(const K& key, V& out_value) const;
    bool Erase(const K& key);

    std::unique_lock<std::shared_mutex> UniqueLock() const;
    std::shared_lock<std::shared_mutex> SharedLock() const;
    std::shared_mutex& Mutex() const;

private:
    inline bool KeysEqual(const K& a, const K& b) const;
    void Resize(std::size_t newCapacity = 0);
};

} // namespace WFX::Utils

#include "utils/hash_map/hash_shard.ipp"

#endif // WFX_UTILS_HASH_SHARD_HPP