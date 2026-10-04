#include <cstdlib>

#include "Macros.h"
#include "Platform.h"
#include "core/EngineDebug.h"
#include "level/EngineLevelFormat.h"

static_assert((MEM_BLOCK_LEVEL_DATA_SIZE / MEM_BLOCK_LEVEL_DATA_SLOTS) >= LEVEL_SECTOR_MAX_BYTES,
              "ARENA_LEVEL_DATA slot is smaller than LEVEL_SECTOR_MAX_BYTES - a compiled sector could not be loaded");

MacosPlatform::MacosMemory::MacosMemory(const MacosPlatform* owner) : m_owner(owner), m_arenaBlock(nullptr), m_poolBlock(nullptr), m_reservedBytes(0) {}

size_t MacosPlatform::MacosMemory::GetBudgetBytes() const { return static_cast<size_t>(MEM_LIMIT_TOTAL_BUDGET); }

bool MacosPlatform::MacosMemory::Reserve(EngineMemoryMap* outMap)
{
    if (!outMap)
        return false;

    const size_t arenaTotal = MEM_BLOCK_CONFIG_SIZE + MEM_BLOCK_LEVEL_DATA_SIZE + MEM_BLOCK_LEVEL_LOD1_SIZE + MEM_BLOCK_RENDERER_SIZE;
    const size_t required = arenaTotal + MEM_POOL_MAIN_SIZE;

    if (required > MEM_LIMIT_TOTAL_BUDGET)
    {
        Engine_LogError("%s: engine memory map is %zu KB, over the %d KB budget", m_owner->GetName(), required / 1024, MEM_LIMIT_TOTAL_BUDGET / 1024);
        return false;
    }

    m_arenaBlock = Alloc(arenaTotal, MEM_ARENA_SLOT_ALIGNMENT);
    m_poolBlock = Alloc(MEM_POOL_MAIN_SIZE, MEM_POOL_CHUNK_SIZE);

    if (!m_arenaBlock || !m_poolBlock)
    {
        Engine_LogError("%s: out of heap reserving %zu KB", m_owner->GetName(), required / 1024);
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
    outMap->arenas[2].size = MEM_BLOCK_LEVEL_LOD1_SIZE;
    outMap->arenas[2].slots = MEM_BLOCK_LEVEL_LOD1_SLOTS;
    outMap->arenas[3].size = MEM_BLOCK_RENDERER_SIZE;
    outMap->arenas[3].slots = MEM_BLOCK_RENDERER_SLOTS;
    outMap->arenaCount = 4;

    Engine_LogInfo("%s: reserved %zu KB arenas + %d KB pool", m_owner->GetName(), arenaTotal / 1024, MEM_POOL_MAIN_SIZE / 1024);
    return true;
}

void MacosPlatform::MacosMemory::Release()
{
    Free(m_arenaBlock);
    Free(m_poolBlock);
    m_arenaBlock = nullptr;
    m_poolBlock = nullptr;
    m_reservedBytes = 0;
}

void* MacosPlatform::MacosMemory::Alloc(size_t size, size_t alignment)
{
    if (alignment < sizeof(void*))
        alignment = sizeof(void*);

    void* block = nullptr;
    if (posix_memalign(&block, alignment, size) != 0)
        return nullptr;
    return block;
}

void MacosPlatform::MacosMemory::Free(void* ptr) { free(ptr); }

void MacosPlatform::MacosMemory::GetHeapStats(HeapStats* outStats) const
{
    if (!outStats)
        return;

    outStats->totalBytes = static_cast<size_t>(MEM_LIMIT_TOTAL_BUDGET);
    outStats->usedBytes = m_reservedBytes;
    outStats->freeBytes = static_cast<size_t>(MEM_LIMIT_TOTAL_BUDGET) - m_reservedBytes;
}

uint32_t MacosPlatform::GetTextureFootprintBytes(uint32_t width, uint32_t height, PixelFormat format, uint8_t mipCount) const
{
    UNUSED_VAR(format);

    uint32_t bytes = 0;
    const uint8_t levels = mipCount ? mipCount : 1u;
    for (uint8_t lvl = 0; lvl < levels; ++lvl)
    {
        const uint32_t w = (width >> lvl) ? (width >> lvl) : 1u;
        const uint32_t h = (height >> lvl) ? (height >> lvl) : 1u;
        bytes += w * h * 4u;
    }
    return bytes;
}
