#include <cstdint>
#include <cstdlib>
#include <malloc.h>

#include "Platform.h"
#include "core/EngineDebug.h"

#include <switch.h>

extern "C" {
extern char* fake_heap_start;
extern char* fake_heap_end;
}

namespace
{
    size_t HeapTotalBytes()
    {
        if (!fake_heap_start || !fake_heap_end || fake_heap_end < fake_heap_start)
            return 0;
        return static_cast<size_t>(fake_heap_end - fake_heap_start);
    }

    size_t HeapUsedBytes()
    {
        const struct mallinfo info = mallinfo();
        return static_cast<size_t>(info.uordblks);
    }
} // namespace

NxPlatform::NxMemory::NxMemory(const NxPlatform* owner) : m_owner(owner), m_arenaBlock(nullptr), m_poolBlock(nullptr), m_reservedBytes(0) {}

size_t NxPlatform::NxMemory::GetBudgetBytes() const { return static_cast<size_t>(MEM_LIMIT_TOTAL_BUDGET); }

bool NxPlatform::NxMemory::Reserve(EngineMemoryMap* outMap)
{
    if (!outMap)
        return false;

    const size_t arenaTotal = MEM_BLOCK_CONFIG_SIZE + MEM_BLOCK_LEVEL_DATA_SIZE + MEM_BLOCK_RENDERER_SIZE;
    const size_t required = arenaTotal + MEM_POOL_MAIN_SIZE;

    if (required > MEM_LIMIT_TOTAL_BUDGET)
    {
        Engine_LogError("%s: engine memory map is %u KB, over the %u KB budget", m_owner->GetName(), static_cast<unsigned>(required / 1024), static_cast<unsigned>(MEM_LIMIT_TOTAL_BUDGET / 1024));
        return false;
    }

    const size_t heapTotal = HeapTotalBytes();
    const size_t heapUsed = HeapUsedBytes();

    m_arenaBlock = memalign(MEM_ARENA_SLOT_ALIGNMENT, arenaTotal);
    m_poolBlock = memalign(MEM_ARENA_SLOT_ALIGNMENT, MEM_POOL_MAIN_SIZE);

    if (!m_arenaBlock || !m_poolBlock)
    {
        Engine_LogError("%s: could not reserve %u KB for the engine map (heap %u KB, %u KB in use)", m_owner->GetName(), static_cast<unsigned>(required / 1024),
                        static_cast<unsigned>(heapTotal / 1024), static_cast<unsigned>(heapUsed / 1024));
        Release();
        return false;
    }

    m_reservedBytes = required;

    outMap->arenaBlock = m_arenaBlock;
    outMap->poolBlock = m_poolBlock;
    outMap->arenaBlockSize = arenaTotal;
    outMap->poolSize = MEM_POOL_MAIN_SIZE;
    outMap->poolChunkSize = MEM_POOL_CHUNK_SIZE;
    outMap->slotAlignment = MEM_ARENA_SLOT_ALIGNMENT;

    outMap->arenas[0].size = MEM_BLOCK_CONFIG_SIZE;
    outMap->arenas[0].slots = MEM_BLOCK_CONFIG_SLOTS;
    outMap->arenas[1].size = MEM_BLOCK_LEVEL_DATA_SIZE;
    outMap->arenas[1].slots = MEM_BLOCK_LEVEL_DATA_SLOTS;
    outMap->arenas[2].size = MEM_BLOCK_RENDERER_SIZE;
    outMap->arenas[2].slots = MEM_BLOCK_RENDERER_SLOTS;
    outMap->arenaCount = 3;

    Engine_LogInfo("%s: reserved %u KB arenas + %u KB pool from a %u KB heap", m_owner->GetName(), static_cast<unsigned>(arenaTotal / 1024), static_cast<unsigned>(MEM_POOL_MAIN_SIZE / 1024),
                   static_cast<unsigned>(heapTotal / 1024));
    return true;
}

void NxPlatform::NxMemory::Release()
{
    if (m_arenaBlock)
        free(m_arenaBlock);
    if (m_poolBlock)
        free(m_poolBlock);

    m_arenaBlock = nullptr;
    m_poolBlock = nullptr;
    m_reservedBytes = 0;
}

void* NxPlatform::NxMemory::Alloc(size_t size, size_t alignment) { return memalign(alignment, size); }

void NxPlatform::NxMemory::Free(void* ptr)
{
    if (ptr)
        free(ptr);
}

void NxPlatform::NxMemory::GetHeapStats(HeapStats* outStats) const
{
    if (!outStats)
        return;

    const size_t heapTotal = HeapTotalBytes();
    const size_t heapUsed = HeapUsedBytes();
    outStats->totalBytes = static_cast<size_t>(MEM_LIMIT_TOTAL_BUDGET);
    outStats->usedBytes = m_reservedBytes;
    outStats->freeBytes = (heapTotal > heapUsed) ? (heapTotal - heapUsed) : 0u;
}

uint32_t NxPlatform::GetTextureFootprintBytes(uint32_t width, uint32_t height, PixelFormat format, uint8_t mipCount) const
{
    static_cast<void>(format);

    uint64_t bytes = 0;
    const uint8_t levels = mipCount ? mipCount : 1u;
    for (uint8_t lvl = 0; lvl < levels; ++lvl)
    {
        const uint64_t w = (width >> lvl) ? (width >> lvl) : 1u;
        const uint64_t h = (height >> lvl) ? (height >> lvl) : 1u;
        bytes += w * h * 4u;
    }

    const uint64_t page = MEM_GPU_PAGE_SIZE;
    bytes = ((bytes + page - 1u) / page) * page;
    return (bytes > UINT32_MAX) ? UINT32_MAX : static_cast<uint32_t>(bytes);
}
