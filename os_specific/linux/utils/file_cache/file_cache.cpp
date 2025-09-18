#include "file_cache.hpp"

#include <cassert>
#include <sys/stat.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <unistd.h>

namespace WFX::OSSpecific {

FileCache::FileCache(std::size_t capacity)
    : minFreq_(0)
{
    struct rlimit rl;
    std::size_t safe = capacity;
    
    // Leave room for sockets + other fds
    if(getrlimit(RLIMIT_NOFILE, &rl) == 0)
        safe = rl.rlim_cur / 2;
    
    capacity_ = std::min(capacity, safe);
}

FileCache::~FileCache()
{
    for(auto &pair : entries_)
        close(pair.second.fd);
}

std::pair<int, off_t> FileCache::GetFileDesc(const std::string &path)
{
    auto it = entries_.find(path);
    if(it != entries_.end()) {
        Touch(it->first);
        return {it->second.fd, it->second.fileSize};
    }

    // Not cached, open the file
    int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if(fd < 0)
        return {-1, 0};

    // Get file size once
    struct stat st;
    if(fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        return {-1, 0};
    }

    Insert(path, fd, st.st_size);
    return {fd, st.st_size};
}

// vvv Helper Functions vvv
void FileCache::Touch(const std::string &key)
{
    auto &entry = entries_[key];
    int oldFreq = entry.freq;
    int newFreq = oldFreq + 1;
    
    entry.freq = newFreq;

    // Remove from old bucket
    freqBuckets_[oldFreq].erase(entry.bucketIter);
    if(freqBuckets_[oldFreq].empty()) {
        freqBuckets_.erase(oldFreq);
        if(minFreq_ == oldFreq)
            minFreq_ = newFreq;
    }

    // Insert into new bucket at front (most recently used among same freq)
    freqBuckets_[newFreq].push_front(key);
    entry.bucketIter = freqBuckets_[newFreq].begin();
}

void FileCache::Insert(const std::string &key, int fd, off_t size)
{
    if(entries_.size() >= capacity_)
        Evict();

    // Insert with freq = 1
    freqBuckets_[1].push_front(key);
    entries_[key] = {fd, 1, size, freqBuckets_[1].begin()};
    minFreq_ = 1;
}

void FileCache::Evict()
{
    assert(!freqBuckets_.empty());

    // Evict from minFreq bucket, oldest entry (back of list)
    auto &bucket = freqBuckets_[minFreq_];
    std::string keyToEvict = bucket.back();
    bucket.pop_back();

    int fd = entries_[keyToEvict].fd;
    close(fd); // Close FD

    entries_.erase(keyToEvict);

    // minFreq_ will be reset on next insertion
    if(bucket.empty())
        freqBuckets_.erase(minFreq_);
}

} // namespace WFX::OSSpecific