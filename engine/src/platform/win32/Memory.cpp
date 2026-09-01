#include <cstdlib>
#include <malloc.h>

#include "core/EngineDebug.h"
#include "level/EngineLevelFormat.h"
#include "Macros.h"
#include "Platform.h"

// A streamed sector payload must fit one ARENA_LEVEL_DATA slot. The level
// compiler enforces the same limit from the other side; this catches a platform
// whose arena layout would silently make compiled levels unloadable.
static_assert((MEM_BLOCK_LEVEL_DATA_SIZE / MEM_BLOCK_LEVEL_DATA_SLOTS) >= LEVEL_SECTOR_MAX_BYTES,
              "ARENA_LEVEL_DATA slot is smaller than LEVEL_SECTOR_MAX_BYTES - a compiled sector could not be loaded");

Win32Platform::Win32Memory::Win32Memory(const Win32Platform* owner) : m_owner(owner), m_arenaBlock(nullptr), m_poolBlock(nullptr), m_reservedBytes(0) {}

size_t Win32Platform::Win32Memory::GetBudgetBytes() const { return static_cast<size_t>(MEM_LIMIT_TOTAL_BUDGET); }

bool Win32Platform::Win32Memory::Reserve(EngineMemoryMap* outMap)
{
    if (!outMap)
        return false;

    const size_t arenaTotal = MEM_BLOCK_CONFIG_SIZE + MEM_BLOCK_LEVEL_DATA_SIZE + MEM_BLOCK_RENDERER_SIZE;
    const size_t required = arenaTotal + MEM_POOL_MAIN_SIZE;

    // Desktop has RAM to spare, but the budget is still enforced. The engine
    // contract is that an over-budget load fails loudly instead of being quietly
    // swapped out, and that only means something if a budget exists.
    if (required > MEM_LIMIT_TOTAL_BUDGET)
    {
        Engine_LogError("%s: engine memory map is %zu KB, over the %d KB budget", m_owner->GetName(), required / 1024, MEM_LIMIT_TOTAL_BUDGET / 1024);
        return false;
    }

    m_arenaBlock = Alloc(arenaTotal, MEM_ARENA_SLOT_ALIGNMENT);
    m_poolBlock = malloc(MEM_POOL_MAIN_SIZE);

    if (!m_arenaBlock || !m_poolBlock)
    {
        Engine_LogError("%s: out of memory reserving %zu KB", m_owner->GetName(), required / 1024);
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

    // Order matches ArenaType (ARENA_CONFIG, ARENA_LEVEL_DATA, ARENA_RENDERER).
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

void Win32Platform::Win32Memory::Release()
{
    Free(m_arenaBlock);
    free(m_poolBlock);
    m_arenaBlock = nullptr;
    m_poolBlock = nullptr;
    m_reservedBytes = 0;
}

void* Win32Platform::Win32Memory::Alloc(size_t size, size_t alignment)
{
    // MinGW routes to the MSVC runtime here, so the allocation MUST be released
    // with _aligned_free - free() on this pointer is undefined behaviour. That
    // asymmetry is exactly why aligned allocation sits behind the platform.
    return _aligned_malloc(size, alignment);
}

void Win32Platform::Win32Memory::Free(void* ptr)
{
    if (ptr)
        _aligned_free(ptr);
}

void Win32Platform::Win32Memory::GetHeapStats(HeapStats* outStats) const
{
    if (!outStats)
        return;

    // mallinfo() is glibc/newlib only and does not exist here - one of the
    // concrete reasons heap reporting is a platform responsibility rather than
    // something shared engine code does for itself.
    outStats->totalBytes = static_cast<size_t>(MEM_LIMIT_TOTAL_BUDGET);
    outStats->usedBytes = m_reservedBytes;
    outStats->freeBytes = static_cast<size_t>(MEM_LIMIT_TOTAL_BUDGET) - m_reservedBytes;
}

uint32_t Win32Platform::GetTextureFootprintBytes(uint32_t width, uint32_t height, PixelFormat format, uint8_t mipCount) const
{
    // A desktop GPU has no page-rounding rule the engine needs to model, and
    // every format is expanded to RGBA8 on upload (see the renderer contract), so
    // the footprint is simply the mip chain at 4 bytes per texel.
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
