#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <malloc.h>

#include "Macros.h"
#include "Platform.h"
#include "core/EngineDebug.h"
#include "level/EngineLevelFormat.h"

extern "C" {
#include <pspkernel.h>
#include <pspsysmem.h>
}

static_assert((MEM_BLOCK_LEVEL_DATA_SIZE / MEM_BLOCK_LEVEL_DATA_SLOTS) >= LEVEL_SECTOR_MAX_BYTES,
              "ARENA_LEVEL_DATA slot is smaller than LEVEL_SECTOR_MAX_BYTES - a compiled sector could not be loaded");

static_assert(MEM_BLOCK_CONFIG_SIZE % MEM_SYSTEM_BLOCK_GRANULARITY == 0, "config arena is not a whole number of system blocks");
static_assert(MEM_BLOCK_LEVEL_DATA_SIZE % MEM_SYSTEM_BLOCK_GRANULARITY == 0, "level-data arena is not a whole number of system blocks");
static_assert(MEM_BLOCK_RENDERER_SIZE % MEM_SYSTEM_BLOCK_GRANULARITY == 0, "renderer arena is not a whole number of system blocks");
static_assert(MEM_POOL_MAIN_SIZE % MEM_SYSTEM_BLOCK_GRANULARITY == 0, "main pool is not a whole number of system blocks");

namespace
{
    // The partition allocator makes no alignment promise, so a block is taken
    // oversized by one alignment and its base rounded up inside.
    SceUID ReserveBlock(const char* name, size_t size, size_t alignment, void** outBase)
    {
        const SceUID uid = sceKernelAllocPartitionMemory(PSP_MEMORY_PARTITION_USER, name, PSP_SMEM_Low, static_cast<SceSize>(size + alignment), nullptr);
        if (uid < 0)
            return uid;

        void* raw = sceKernelGetBlockHeadAddr(uid);
        if (!raw)
        {
            sceKernelFreePartitionMemory(uid);
            return -1;
        }

        const uintptr_t aligned = (reinterpret_cast<uintptr_t>(raw) + (alignment - 1u)) & ~static_cast<uintptr_t>(alignment - 1u);
        *outBase = reinterpret_cast<void*>(aligned);
        return uid;
    }
} // namespace

PspPlatform::PspMemory::PspMemory(const PspPlatform* owner) : m_owner(owner), m_arenaBlockId(-1), m_poolBlockId(-1), m_arenaBlock(nullptr), m_poolBlock(nullptr), m_reservedBytes(0) {}

size_t PspPlatform::PspMemory::GetBudgetBytes() const { return static_cast<size_t>(MEM_LIMIT_TOTAL_BUDGET); }

bool PspPlatform::PspMemory::Reserve(EngineMemoryMap* outMap)
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

    const uint32_t freeBefore = static_cast<uint32_t>(sceKernelTotalFreeMemSize());
    const uint32_t largestBefore = static_cast<uint32_t>(sceKernelMaxFreeMemSize());

    m_arenaBlockId = ReserveBlock("engine_arenas", arenaTotal, MEM_ARENA_SLOT_ALIGNMENT, &m_arenaBlock);
    m_poolBlockId = ReserveBlock("engine_pool", MEM_POOL_MAIN_SIZE, MEM_ARENA_SLOT_ALIGNMENT, &m_poolBlock);

    if (m_arenaBlockId < 0 || m_poolBlockId < 0)
    {
        Engine_LogError("%s: could not reserve %u KB of main memory (%u KB free, largest block %u KB; arenas %d, pool %d)", m_owner->GetName(), static_cast<unsigned>(required / 1024),
                        static_cast<unsigned>(freeBefore / 1024), static_cast<unsigned>(largestBefore / 1024), static_cast<int>(m_arenaBlockId), static_cast<int>(m_poolBlockId));
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

    Engine_LogInfo("%s: reserved %u KB arenas + %u KB pool, %u KB partition free after", m_owner->GetName(), static_cast<unsigned>(arenaTotal / 1024), static_cast<unsigned>(MEM_POOL_MAIN_SIZE / 1024),
                   static_cast<unsigned>(sceKernelTotalFreeMemSize() / 1024));
    return true;
}

void PspPlatform::PspMemory::Release()
{
    if (m_arenaBlockId >= 0)
        sceKernelFreePartitionMemory(m_arenaBlockId);
    if (m_poolBlockId >= 0)
        sceKernelFreePartitionMemory(m_poolBlockId);

    m_arenaBlockId = -1;
    m_poolBlockId = -1;
    m_arenaBlock = nullptr;
    m_poolBlock = nullptr;
    m_reservedBytes = 0;
}

void* PspPlatform::PspMemory::Alloc(size_t size, size_t alignment) { return memalign(alignment, size); }

void PspPlatform::PspMemory::Free(void* ptr)
{
    if (ptr)
        free(ptr);
}

void PspPlatform::PspMemory::GetHeapStats(HeapStats* outStats) const
{
    if (!outStats)
        return;

    outStats->totalBytes = static_cast<size_t>(MEM_LIMIT_TOTAL_BUDGET);
    outStats->usedBytes = m_reservedBytes;
    outStats->freeBytes = static_cast<size_t>(sceKernelMaxFreeMemSize());
}

uint32_t PspPlatform::GetTextureFootprintBytes(uint32_t width, uint32_t height, PixelFormat format, uint8_t mipCount) const
{
    uint32_t bytesPerPixel = 4u;
    switch (format)
    {
    case PixelFormat::RGBA16:
        bytesPerPixel = 2u;
        break;
    case PixelFormat::PAL8:
        bytesPerPixel = 1u;
        break;
    default:
        break;
    }

    uint32_t bytes = 0;
    const uint8_t levels = mipCount ? mipCount : 1u;
    for (uint8_t lvl = 0; lvl < levels; ++lvl)
    {
        const uint32_t w = (width >> lvl) ? (width >> lvl) : 1u;
        const uint32_t h = (height >> lvl) ? (height >> lvl) : 1u;
        bytes += w * h * bytesPerPixel;
    }

    if (format == PixelFormat::PAL8)
        bytes += 256u * 4u;

    return bytes;
}
