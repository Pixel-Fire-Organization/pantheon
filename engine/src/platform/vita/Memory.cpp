#include <cstdlib>
#include <cstring>
#include <malloc.h>

#include "Macros.h"
#include "Platform.h"
#include "core/EngineDebug.h"
#include "level/EngineLevelFormat.h"

extern "C" {
#include <psp2/kernel/sysmem.h>
}

static_assert((MEM_BLOCK_LEVEL_DATA_SIZE / MEM_BLOCK_LEVEL_DATA_SLOTS) >= LEVEL_SECTOR_MAX_BYTES,
              "ARENA_LEVEL_DATA slot is smaller than LEVEL_SECTOR_MAX_BYTES - a compiled sector could not be loaded");

static_assert(MEM_BLOCK_CONFIG_SIZE % MEM_SYSTEM_BLOCK_GRANULARITY == 0, "config arena is not a whole number of system blocks");
static_assert(MEM_BLOCK_LEVEL_DATA_SIZE % MEM_SYSTEM_BLOCK_GRANULARITY == 0, "level-data arena is not a whole number of system blocks");
static_assert(MEM_BLOCK_RENDERER_SIZE % MEM_SYSTEM_BLOCK_GRANULARITY == 0, "renderer arena is not a whole number of system blocks");
static_assert(MEM_POOL_MAIN_SIZE % MEM_SYSTEM_BLOCK_GRANULARITY == 0, "main pool is not a whole number of system blocks");

namespace
{
    int32_t ReserveBlock(const char* name, size_t size, size_t alignment, void** outBase)
    {
        SceKernelAllocMemBlockOpt opt;
        memset(&opt, 0, sizeof(opt));
        opt.size = sizeof(opt);
        opt.attr = SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_HAS_ALIGNMENT;
        opt.alignment = static_cast<SceSize>(alignment);

        const SceUID uid = sceKernelAllocMemBlock(name, SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, static_cast<SceSize>(size), &opt);
        if (uid < 0)
            return uid;

        void* base = nullptr;
        if (sceKernelGetMemBlockBase(uid, &base) < 0 || !base)
        {
            sceKernelFreeMemBlock(uid);
            return -1;
        }

        *outBase = base;
        return uid;
    }
} // namespace

VitaPlatform::VitaMemory::VitaMemory(const VitaPlatform* owner) : m_owner(owner), m_arenaBlockId(-1), m_poolBlockId(-1), m_arenaBlock(nullptr), m_poolBlock(nullptr), m_reservedBytes(0) {}

size_t VitaPlatform::VitaMemory::GetBudgetBytes() const { return static_cast<size_t>(MEM_LIMIT_TOTAL_BUDGET); }

bool VitaPlatform::VitaMemory::Reserve(EngineMemoryMap* outMap)
{
    if (!outMap)
        return false;

    const size_t arenaTotal = MEM_BLOCK_CONFIG_SIZE + MEM_BLOCK_LEVEL_DATA_SIZE + MEM_BLOCK_RENDERER_SIZE;
    const size_t required = arenaTotal + MEM_POOL_MAIN_SIZE;

    if (required > MEM_LIMIT_TOTAL_BUDGET)
    {
        Engine_LogError("%s: engine memory map is %zu KB, over the %d KB budget", m_owner->GetName(), required / 1024, MEM_LIMIT_TOTAL_BUDGET / 1024);
        return false;
    }

    m_arenaBlockId = ReserveBlock("engine_arenas", arenaTotal, MEM_ARENA_SLOT_ALIGNMENT, &m_arenaBlock);
    m_poolBlockId = ReserveBlock("engine_pool", MEM_POOL_MAIN_SIZE, MEM_ARENA_SLOT_ALIGNMENT, &m_poolBlock);

    if (m_arenaBlockId < 0 || m_poolBlockId < 0)
    {
        Engine_LogError("%s: could not reserve %zu KB of main memory (arenas %d, pool %d)", m_owner->GetName(), required / 1024, static_cast<int>(m_arenaBlockId), static_cast<int>(m_poolBlockId));
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

    Engine_LogInfo("%s: reserved %zu KB arenas + %d KB pool", m_owner->GetName(), arenaTotal / 1024, MEM_POOL_MAIN_SIZE / 1024);
    return true;
}

void VitaPlatform::VitaMemory::Release()
{
    if (m_arenaBlockId >= 0)
        sceKernelFreeMemBlock(m_arenaBlockId);
    if (m_poolBlockId >= 0)
        sceKernelFreeMemBlock(m_poolBlockId);

    m_arenaBlockId = -1;
    m_poolBlockId = -1;
    m_arenaBlock = nullptr;
    m_poolBlock = nullptr;
    m_reservedBytes = 0;
}

void* VitaPlatform::VitaMemory::Alloc(size_t size, size_t alignment) { return memalign(alignment, size); }

void VitaPlatform::VitaMemory::Free(void* ptr)
{
    if (ptr)
        free(ptr);
}

void VitaPlatform::VitaMemory::GetHeapStats(HeapStats* outStats) const
{
    if (!outStats)
        return;

    outStats->totalBytes = static_cast<size_t>(MEM_LIMIT_TOTAL_BUDGET);
    outStats->usedBytes = m_reservedBytes;
    outStats->freeBytes = static_cast<size_t>(MEM_LIMIT_TOTAL_BUDGET) - m_reservedBytes;
}

uint32_t VitaPlatform::GetTextureFootprintBytes(uint32_t width, uint32_t height, PixelFormat format, uint8_t mipCount) const
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
