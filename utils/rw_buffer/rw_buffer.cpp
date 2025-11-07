#include "rw_buffer.hpp"
#include "utils/logger/logger.hpp"

#include <cstring>

namespace WFX::Utils {
    
// vvv Constructor and Destructor vvv
RWBuffer::RWBuffer()
{
    auto& pool = BufferPool::GetInstance();
    if(!pool.IsInitialized())
        Logger::GetInstance().Fatal("[RWBuffer]: 'BufferPool' must be initialized for 'RWBuffer' to work");
}

RWBuffer::~RWBuffer()
{
    ResetBuffer();
}

// vvv Initializer / Destructor Functions vvv
bool RWBuffer::InitReadBuffer(std::uint32_t size)
{
    // Already initialized
    if(readBuffer_) return true;

    auto& pool = BufferPool::GetInstance();

    std::size_t allocSize = sizeof(ReadMetadata) + size;
    readBuffer_ = static_cast<char*>(pool.Lease(allocSize));
    if(!readBuffer_)
        return false;

    auto* readMeta = reinterpret_cast<ReadMetadata*>(readBuffer_);
    readMeta->bufferSize = size;
    readMeta->dataLength = 0;

    return true;
}

bool RWBuffer::InitWriteBuffer(std::uint32_t size)
{
    // Already initialized
    if(writeBuffer_)
        return true;

    auto& pool = BufferPool::GetInstance();

    std::size_t allocSize = sizeof(WriteMetadata) + size;
    writeBuffer_ = static_cast<char*>(pool.Lease(allocSize));
    if(!writeBuffer_)
        return false;

    auto* writeMeta = reinterpret_cast<WriteMetadata*>(writeBuffer_);
    writeMeta->bufferSize    = size;
    writeMeta->dataLength    = 0;
    writeMeta->writtenLength = 0;

    return true;
}

void RWBuffer::ResetBuffer()
{
    auto& pool = BufferPool::GetInstance();

    if(readBuffer_) {
        pool.Release(readBuffer_);
        readBuffer_ = nullptr;
    }

    if(writeBuffer_) {
        pool.Release(writeBuffer_);
        writeBuffer_ = nullptr;
    }
}

void RWBuffer::ClearBuffer()
{
    auto* readMeta  = GetReadMeta();
    auto* writeMeta = GetWriteMeta();

    if(readMeta)
        readMeta->dataLength = 0;

    if(writeMeta) {
        writeMeta->dataLength    = 0;
        writeMeta->writtenLength = 0;
    }
}

// vvv Getter Functions vvv
char* RWBuffer::GetWriteData() const noexcept
{
    return writeBuffer_ ? writeBuffer_ + sizeof(WriteMetadata) : nullptr;
}

char* RWBuffer::GetReadData() const noexcept
{
    return readBuffer_ ? readBuffer_ + sizeof(ReadMetadata) : nullptr;
}

WriteMetadata* RWBuffer::GetWriteMeta() const noexcept
{
    return reinterpret_cast<WriteMetadata*>(writeBuffer_);
}

ReadMetadata* RWBuffer::GetReadMeta() const noexcept
{
    return reinterpret_cast<ReadMetadata*>(readBuffer_);
}

bool RWBuffer::IsReadInitialized() const noexcept
{
    return (!!readBuffer_);
}

bool RWBuffer::IsWriteInitialized() const noexcept
{
    return (!!writeBuffer_);
}

// vvv Read Buffer Management vvv
bool RWBuffer::GrowReadBuffer(std::uint32_t defaultSize, std::uint32_t maxSize)
{
    if(!readBuffer_) return false;

    auto& pool = BufferPool::GetInstance();

    auto* readMeta = reinterpret_cast<ReadMetadata*>(readBuffer_);
    if(readMeta->dataLength >= readMeta->bufferSize) {
        if(readMeta->bufferSize >= maxSize)
            return false;

        std::uint32_t newSize = readMeta->bufferSize + defaultSize;
        if(newSize > maxSize)
            newSize = maxSize;

        std::uint32_t allocSize = sizeof(ReadMetadata) + newSize;

        // Reacquire = realloc
        char* newBuf = static_cast<char*>(pool.Reacquire(readBuffer_, allocSize));
        if(!newBuf)
            return false;

        readBuffer_ = newBuf;
        readMeta = reinterpret_cast<ReadMetadata*>(readBuffer_);
        readMeta->bufferSize = newSize;
    }

    return true;
}

ValidRegion RWBuffer::GetWritableReadRegion() const noexcept
{
    if(!readBuffer_) return {nullptr, 0};

    auto* readMeta = reinterpret_cast<ReadMetadata*>(readBuffer_);
    return {
        readBuffer_ + sizeof(ReadMetadata) + readMeta->dataLength,
        readMeta->bufferSize - readMeta->dataLength
    };
}

ValidRegion RWBuffer::GetWritableWriteRegion() const noexcept
{
    if(!writeBuffer_) return {nullptr, 0};

    auto* writeMeta = reinterpret_cast<WriteMetadata*>(writeBuffer_);
    return {
        writeBuffer_ + sizeof(WriteMetadata) + writeMeta->dataLength,
        writeMeta->bufferSize - writeMeta->dataLength
    };
}

void RWBuffer::AdvanceReadLength(std::uint32_t n) noexcept
{
    if(!readBuffer_) return;

    auto* meta = reinterpret_cast<ReadMetadata*>(readBuffer_);
    meta->dataLength = std::min(meta->dataLength + n, meta->bufferSize);
}

void RWBuffer::AdvanceWriteLength(std::uint32_t n) noexcept
{
    if(!writeBuffer_) return;

    auto* meta = reinterpret_cast<WriteMetadata*>(writeBuffer_);
    meta->writtenLength = std::min(meta->writtenLength + n, meta->dataLength);
}

// vvv Write Buffer Management vvv
bool RWBuffer::AppendData(const char* data, std::uint32_t size)
{
    if(!writeBuffer_ || !data || size == 0)
        return false;

    auto* meta = GetWriteMeta();
    std::uint32_t capacity = meta->bufferSize;
    std::uint32_t used     = meta->dataLength;

    // Write buffer is fixed, any attempts to overflow will fail
    if(size > capacity - used)
        return false;

    char* dest = writeBuffer_ + sizeof(WriteMetadata) + used;
    std::memcpy(dest, data, size);

    meta->dataLength += size;
    return true;
}

} // namespace WFX::Utils