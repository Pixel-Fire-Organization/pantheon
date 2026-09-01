#pragma once

#include <cstddef>
#include <cstdint>

#include "platform/PlatformTypes.h"

class MemoryContract
{
public:
    virtual ~MemoryContract() = default;

    MemoryContract(const MemoryContract&) = delete;
    MemoryContract(MemoryContract&&) = delete;
    MemoryContract& operator=(const MemoryContract&) = delete;
    MemoryContract& operator=(MemoryContract&&) = delete;

    // Reserve the arenas and the main pool in one go. The platform enforces its
    // own ceiling here and fails rather than over-committing.
    virtual bool Reserve(EngineMemoryMap* outMap) = 0;
    virtual void Release() = 0;

    // Aligned allocation. Memory from Alloc MUST be returned to Free: on some
    // platforms aligned allocations come from a different heap than the C
    // allocator, and releasing one with free() corrupts it. Prefer the owning
    // types in EngineMemory.h, which make the pairing impossible to get wrong.
    virtual void* Alloc(size_t size, size_t alignment) = 0;
    virtual void Free(void* ptr) = 0;

    virtual void GetHeapStats(HeapStats* outStats) const = 0;

    // The ceiling this platform will not allocate past.
    virtual size_t GetBudgetBytes() const = 0;

protected:
    MemoryContract() = default;
};
