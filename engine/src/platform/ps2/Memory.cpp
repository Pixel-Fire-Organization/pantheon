#include <cstdlib>
#include <malloc.h>

#include "Platform.h"
#include "core/EngineDebug.h"
#include "level/EngineLevelFormat.h"

// A streamed sector payload must fit one ARENA_LEVEL_DATA slot. This used to be
// a "keep in sync" comment in the constants header; now that the arena layout is
// platform-owned it is checked by the compiler.
static_assert((MEM_BLOCK_LEVEL_DATA_SIZE / MEM_BLOCK_LEVEL_DATA_SLOTS) >= LEVEL_SECTOR_MAX_BYTES,
              "ARENA_LEVEL_DATA slot is smaller than LEVEL_SECTOR_MAX_BYTES - a compiled sector could not be loaded");

Ps2Platform::Ps2Memory::Ps2Memory(const Ps2Platform* owner) : m_owner(owner), m_arenaBlock(nullptr), m_poolBlock(nullptr) {}

size_t Ps2Platform::Ps2Memory::GetBudgetBytes() const { return static_cast<size_t>(MEM_LIMIT_TOTAL_BUDGET); }

bool Ps2Platform::Ps2Memory::Reserve(EngineMemoryMap* outMap)
{
    if (!outMap)
        return false;

    const size_t arenaTotal = MEM_BLOCK_CONFIG_SIZE + MEM_BLOCK_LEVEL_DATA_SIZE + MEM_BLOCK_RENDERER_SIZE;
    const size_t required = arenaTotal + MEM_POOL_MAIN_SIZE;

    // The platform enforces its own ceiling. On a 32MB console, over-committing
    // here surfaces later as corruption in an unrelated subsystem, so refuse now.
    if (required > MEM_LIMIT_TOTAL_BUDGET)
    {
        Engine_LogError("%s: engine memory map is %zu KB, over the %d KB budget", m_owner->GetName(), required / 1024, MEM_LIMIT_TOTAL_BUDGET / 1024);
        return false;
    }

    // memalign, not malloc: malloc only guarantees 8-byte alignment, which would
    // force the first arena slot forward and could overrun the block.
    m_arenaBlock = memalign(MEM_ARENA_SLOT_ALIGNMENT, arenaTotal);
    m_poolBlock = malloc(MEM_POOL_MAIN_SIZE);

    if (!m_arenaBlock || !m_poolBlock)
    {
        Engine_LogError("%s: out of EE RAM reserving %zu KB", m_owner->GetName(), required / 1024);
        Release();
        return false;
    }

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

void Ps2Platform::Ps2Memory::Release()
{
    free(m_arenaBlock);
    free(m_poolBlock);
    m_arenaBlock = nullptr;
    m_poolBlock = nullptr;
}

void* Ps2Platform::Ps2Memory::Alloc(size_t size, size_t alignment) { return memalign(alignment, size); }

void Ps2Platform::Ps2Memory::Free(void* ptr) { free(ptr); }

void Ps2Platform::Ps2Memory::GetHeapStats(HeapStats* outStats) const
{
    if (!outStats)
        return;

    // mallinfo() is newlib/glibc only - one of the reasons heap reporting sits
    // behind the platform rather than in shared engine code.
    struct mallinfo info = mallinfo();
    outStats->totalBytes = static_cast<size_t>(MEM_LIMIT_TOTAL_BUDGET);
    outStats->usedBytes = static_cast<size_t>(info.uordblks);
    outStats->freeBytes = static_cast<size_t>(info.fordblks);
}

uint32_t Ps2Platform::GetTextureFootprintBytes(uint32_t width, uint32_t height, PixelFormat format, uint8_t mipCount) const
{
    // GS page dimensions depend on the pixel storage mode: 64x32 @ PSMCT32,
    // 64x64 @ PSMCT16, 128x64 @ PSMT8. Every mip level rounds up to whole pages,
    // and PAL8 costs one more page for its CLUT. That rounding is why the engine
    // cannot just multiply width by height by bpp.
    uint32_t pageW = GFX_GS_PAGE_WIDTH_PSM32;
    uint32_t pageH = GFX_GS_PAGE_HEIGHT_PSM32;
    switch (format)
    {
    case PixelFormat::RGBA16:
        pageW = 64u;
        pageH = 64u;
        break;
    case PixelFormat::PAL8:
        pageW = 128u;
        pageH = 64u;
        break;
    default:
        break;
    }

    uint32_t pages = 0;
    const uint8_t levels = mipCount ? mipCount : 1u;
    for (uint8_t lvl = 0; lvl < levels; ++lvl)
    {
        const uint32_t w = (width >> lvl) ? (width >> lvl) : 1u;
        const uint32_t h = (height >> lvl) ? (height >> lvl) : 1u;
        pages += ((w + pageW - 1u) / pageW) * ((h + pageH - 1u) / pageH);
    }
    if (format == PixelFormat::PAL8)
        pages += 1u; // CLUT

    // One GS page is 8 KB regardless of format.
    return pages * (GFX_GS_PAGE_WIDTH_PSM32 * GFX_GS_PAGE_HEIGHT_PSM32 * 4u);
}
