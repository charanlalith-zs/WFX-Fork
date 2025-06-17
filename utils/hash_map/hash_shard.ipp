#ifndef WFX_UTILS_HASH_SHARD_IPP
#define WFX_UTILS_HASH_SHARD_IPP

#include "utils/logger/logger.hpp"

namespace WFX::Utils {

template <typename K, typename V>
HashShard<K, V>::HashShard(BufferPool& pool) : pool_(pool) {}

template <typename K, typename V>
HashShard<K, V>::~HashShard()
{
    if(entries_)
        pool_.Release(entries_);
}

template <typename K, typename V>
void HashShard<K, V>::Init(std::size_t cap)
{
    initialBucketCapacity_ = cap;
    capacity_              = cap;
    entries_               = reinterpret_cast<Entry*>(pool_.Lease(cap * sizeof(Entry)));
    
    if(!entries_)
        Logger::GetInstance().Fatal("[HashShard]: Failed to get memory for entries");

    std::memset(entries_, 0, cap * sizeof(Entry));
}

template <typename K, typename V>
inline bool HashShard<K, V>::KeysEqual(const K& a, const K& b) const
{
    if constexpr(std::is_trivially_copyable_v<K> && (sizeof(K) == 8 || sizeof(K) == 4))
        return std::memcmp(&a, &b, sizeof(K)) == 0;
    else
        return a == b;
}

template <typename K, typename V>
void HashShard<K, V>::Resize(std::size_t newCapacity)
{
    if(newCapacity == 0) newCapacity = capacity_ * 2;
    if(newCapacity < initialBucketCapacity_) return;

    Entry* newEntries = reinterpret_cast<Entry*>(pool_.Lease(newCapacity * sizeof(Entry)));
    if(!newEntries)
        Logger::GetInstance().Fatal("[HashShard]: Failed to resize entries");
    
    std::memset(newEntries, 0, newCapacity * sizeof(Entry));

    for(std::size_t i = 0; i < capacity_; ++i) {
        Entry& currentEntry = entries_[i];
        if(!currentEntry.occupied) continue;

        std::size_t hash   = WFXHash(currentEntry.key);
        std::size_t idx    = hash % newCapacity;
        std::size_t probe  = 0;

        while(probe < MAX_PROBE_LIMIT) {
            std::size_t pos    = (idx + probe) % newCapacity;
            Entry&      target = newEntries[pos];
            _mm_prefetch(reinterpret_cast<const char*>(&newEntries[(pos + 1) % newCapacity]), _MM_HINT_T0);

            if(!target.occupied) {
                currentEntry.probe_length = static_cast<uint8_t>(probe);
                target = std::move(currentEntry);
                break;
            }

            if(target.probe_length < probe) {
                std::swap(target, currentEntry);
                currentEntry.probe_length = static_cast<uint8_t>(probe);
                probe = target.probe_length;
            }
            ++probe;
        }
    }

    pool_.Release(entries_);
    entries_  = newEntries;
    capacity_ = newCapacity;
}

template <typename K, typename V>
bool HashShard<K, V>::Insert(const K& key, const V& value)
{
    if((static_cast<float>(size_.load(std::memory_order_relaxed)) / capacity_) >= KLOAD_FACTOR_GROW)
        Resize();

    std::size_t hash  = WFXHash(key);
    std::size_t idx   = hash % capacity_;
    std::size_t probe = 0;
    Entry new_entry{key, value, 0, true};

    while(true) {
        std::size_t pos   = (idx + probe) % capacity_;
        Entry&      entry = entries_[pos];

        _mm_prefetch(reinterpret_cast<const char*>(&entries_[(pos + 1) % capacity_]), _MM_HINT_T0);

        if(!entry.occupied) {
            new_entry.probe_length = static_cast<uint8_t>(probe);
            entry = std::move(new_entry);
            size_.fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        if(KeysEqual(entry.key, key)) {
            entry.value = std::move(value);
            return true;
        }

        if(entry.probe_length < probe) {
            std::swap(entry, new_entry);
            new_entry.probe_length = static_cast<uint8_t>(probe);
            probe = entry.probe_length;
        }

        ++probe;
    }
}

template <typename K, typename V>
bool HashShard<K, V>::Get(const K& key, V& out_value) const
{
    std::size_t hash = WFXHash(key);
    std::size_t idx  = hash % capacity_;

    for(std::size_t i = 0; i < capacity_; ++i) {
        std::size_t  pos   = (idx + i) % capacity_;
        const Entry& entry = entries_[pos];

        if(!entry.occupied)
            return false;

        if(KeysEqual(entry.key, key)) {
            out_value = entry.value;
            return true;
        }

        if(entry.probe_length < i)
            return false;
    }

    return false;
}

template <typename K, typename V>
bool HashShard<K, V>::Erase(const K& key)
{
    std::size_t hash = WFXHash(key);
    std::size_t idx  = hash % capacity_;

    for(std::size_t i = 0; i < capacity_; ++i) {
        std::size_t pos   = (idx + i) % capacity_;
        Entry&      entry = entries_[pos];
        
        if(!entry.occupied && entry.probe_length == 0)
            return false;

        if(entry.occupied && KeysEqual(entry.key, key)) {
            std::size_t prev_size = size_.fetch_sub(1, std::memory_order_relaxed);
            std::size_t j         = pos;
            std::size_t next      = (j + 1) % capacity_;

            while(entries_[next].occupied && entries_[next].probe_length > 0) {
                entries_[j] = std::move(entries_[next]);

                entries_[next].occupied      = false;
                entries_[next].probe_length  = 0;
                
                entries_[j].probe_length--;
                j = next;
                next = (j + 1) % capacity_;
            }

            entries_[j].occupied = false;

            float currentLoad = static_cast<float>(prev_size - 1) / capacity_;
            if(currentLoad < KLOAD_FACTOR_SHRINK && capacity_ > initialBucketCapacity_)
                Resize(capacity_ / 2);

            return true;
        }

        if(entry.probe_length < i)
            return false;
    }
    return false;
}

template <typename K, typename V>
std::unique_lock<std::shared_mutex> HashShard<K, V>::UniqueLock() const
{
    return std::unique_lock<std::shared_mutex>(mutex_);
}

template <typename K, typename V>
std::shared_lock<std::shared_mutex> HashShard<K, V>::SharedLock() const
{
    return std::shared_lock<std::shared_mutex>(mutex_);
}

template <typename K, typename V>
std::shared_mutex& HashShard<K, V>::Mutex() const
{
    return mutex_;
}

} // namespace WFX::Utils

#endif // WFX_UTILS_HASH_SHARD_IPP