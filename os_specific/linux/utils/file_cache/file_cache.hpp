#ifndef WFX_LINUX_FILE_CACHE_HPP
#define WFX_LINUX_FILE_CACHE_HPP

#include <list>
#include <string>
#include <unordered_map>

namespace WFX::OSSpecific {

struct CacheEntry {
    int   fd;                                    // Actual file descriptor
    int   freq;                                  // Access frequency
    off_t fileSize;                              // File size in bytes
    std::list<std::string>::iterator bucketIter; // Position in the frequency bucket list
};

class FileCache {
public:
    FileCache(std::size_t capacity);
    ~FileCache();

public:
    // Get a file descriptor for a file, or open it if not cached
    std::pair<int, off_t> GetFileDesc(const std::string& path);

private: // Helper Functions
    void Touch(const std::string& key);
    void Insert(const std::string& key, int fd, off_t size);
    void Evict();

private:
    std::size_t capacity_;
    int         minFreq_;

    // Key -> CacheEntry
    std::unordered_map<std::string, CacheEntry> entries_;

    // Frequency -> list of keys (for LFU eviction)
    std::unordered_map<int, std::list<std::string>> freqBuckets_;
};

} // namespace WFX::OSSpecific

#endif // WFX_LINUX_FILE_CACHE_HPP