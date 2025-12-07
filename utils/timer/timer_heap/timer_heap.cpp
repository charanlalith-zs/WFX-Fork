#include "timer_heap.hpp"

namespace WFX::Utils {

// vvv Constructor vvv
TimerHeap::TimerHeap(BufferPool& pool)
    : idMap_(pool)
{
    idMap_.Init(512);
}

// vvv Main Functions vvv
bool TimerHeap::Insert(std::uint64_t data, std::uint64_t delay, std::uint64_t delta) noexcept
{
    // Does data already exist? If so gg, we don't really want duplicate entries
    std::size_t* existing = idMap_.Get(data);
    if(existing)
        return false;

    // Bucket coalesce
    delay = RoundToBucket(delay, delta);

    std::size_t idx = heap_.size();
    heap_.emplace_back(TimerNode{data, delay, idx});

    // Insert into map, rollback if fails
    if(!idMap_.Insert(data, idx)) {
        heap_.pop_back();
        return false;
    }

    FixHeap(idx);
    return true;
}

bool TimerHeap::Remove(std::uint64_t data) noexcept
{
    std::size_t* heapIdx = idMap_.Get(data);
    // Already removed, treat as success
    if(!heapIdx)
        return true;

    std::size_t idx = *heapIdx;

    if(!idMap_.Erase(data))
        return false;

    std::size_t lastIdx = heap_.size() - 1;
    if(idx != lastIdx) {
        TimerNode backupTarget = heap_[idx];
        TimerNode backupLast   = heap_[lastIdx];

        TimerNode& target = heap_[idx];
        TimerNode& last   = heap_[lastIdx];

        target         = last;
        target.heapIdx = idx;

        // Target = last already done
        if(auto* idxPtr = idMap_.Get(last.data))
            *idxPtr = idx;
        else {
            // If this ever triggers, map is corrupted
            heap_[idx]     = backupTarget;
            heap_[lastIdx] = backupLast;
            idMap_.Insert(backupTarget.data, idx);
            return false;
        }

        FixHeap(idx);
    }

    heap_.pop_back();
    return true;
}

bool TimerHeap::PopExpired(std::uint64_t now, std::uint64_t& outData) noexcept
{
    if(heap_.empty())
        return false;

    TimerNode &min = heap_[0];

    if(min.delay > now)
        return false;

    outData = min.data;
    Remove(outData);
    return true;
}

TimerNode* TimerHeap::GetMin() noexcept
{
    return heap_.empty() ? nullptr : &heap_.front();
}

std::size_t TimerHeap::Size() const noexcept
{
    return heap_.size();
}

// vvv Helper Functions vvv
void TimerHeap::FixHeap(std::size_t idx) noexcept
{
    // Try sift-up
    while(idx > 0) {
        std::size_t parent = (idx - 1) / 2;
        if(heap_[idx].delay >= heap_[parent].delay)
            break;
        SwapNodes(heap_[idx], heap_[parent]);
        idx = parent;
    }

    // Then sift-down
    std::size_t n = heap_.size();
    while(true) {
        std::size_t smallest = idx;
        std::size_t l = 2 * idx + 1;
        std::size_t r = 2 * idx + 2;

        if(l < n && heap_[l].delay < heap_[smallest].delay)
            smallest = l;
        if(r < n && heap_[r].delay < heap_[smallest].delay)
            smallest = r;
        if(smallest == idx)
            break;

        SwapNodes(heap_[idx], heap_[smallest]);
        idx = smallest;
    }
}

void TimerHeap::SwapNodes(TimerNode& lhs, TimerNode& rhs) noexcept
{
    std::swap(lhs, rhs);

    lhs.heapIdx = &lhs - &heap_[0];
    rhs.heapIdx = &rhs - &heap_[0];

    if(auto* idxPtr = idMap_.Get(lhs.data))
        *idxPtr = lhs.heapIdx;

    if(auto* idxPtr = idMap_.Get(rhs.data))
        *idxPtr = rhs.heapIdx;
}

std::uint64_t TimerHeap::RoundToBucket(std::uint64_t expire, std::uint64_t delta) noexcept
{
    if(!delta)
        return expire;

    std::uint64_t half = delta >> 1;
    return (expire + half) / delta * delta;
}

} // namespace WFX::Utils