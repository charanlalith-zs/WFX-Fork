#ifndef WFX_UTILS_CONCURRENT_HASH_MAP_HPP
#define WFX_UTILS_CONCURRENT_HASH_MAP_HPP

#include "utils/hash_map/hash_shard.hpp"

namespace WFX::Utils {

template <typename K, typename V, std::size_t SHARD_COUNT = 64, std::size_t BUCKET_COUNT = 512>
class ConcurrentHashMap {
    using Shard = HashShard<K, V>;

    static_assert((SHARD_COUNT & (SHARD_COUNT - 1)) == 0, "SHARD_COUNT must be a power of 2");

public:
    explicit ConcurrentHashMap(std::size_t poolSize = 512 * 1024)
        : bufferPool_(poolSize, [](std::size_t cur){ return cur * 1.5; })
    {
        for(std::size_t i = 0; i < SHARD_COUNT; ++i) {
            auto shard = std::make_unique<Shard>(bufferPool_);
            shard->Init(BUCKET_COUNT);
            shards_[i] = std::move(shard);
        }
    }

    bool Insert(const K& key, const V& value)
    {
        auto& shard = GetShard(key);
        auto lock = shard.UniqueLock();
        return shard.Insert(key, value);
    }

    bool Get(const K& key, V& out_value) const
    {
        const auto& shard = GetShard(key);
        auto lock = shard.SharedLock();
        return shard.Get(key, out_value);
    }

    bool Erase(const K& key)
    {
        auto& shard = GetShard(key);
        auto lock = shard.UniqueLock();
        return shard.Erase(key);
    }

private:
    BufferPool bufferPool_;
    std::array<std::unique_ptr<Shard>, SHARD_COUNT> shards_;

private:
    inline std::size_t GetShardIndex(const K& key) const
    {
        std::size_t h = WFXHash(key);
        return h & (SHARD_COUNT - 1); // Requires SHARD_COUNT to be power of 2
    }

    inline Shard& GetShard(const K& key)
    {
        return *shards_[GetShardIndex(key)];
    }

    inline const Shard& GetShard(const K& key) const
    {
        return *shards_[GetShardIndex(key)];
    }
};

} // namespace WFX::Utils

#endif // WFX_UTILS_CONCURRENT_HASH_MAP_HPP