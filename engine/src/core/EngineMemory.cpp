#include <cstring>
#include "Engine.h"

// Round ptr up to the next multiple of alignment.
// PRECONDITION: alignment must be a power of two (or 0 to skip alignment).
// Callers are responsible for validating this before calling.
static inline uintptr_t AlignForward(uintptr_t ptr, size_t alignment)
{
    if (alignment == 0)
        return ptr;
    uintptr_t a = alignment;
    // Safe only for power-of-two alignment: (ptr & (a-1)) gives the remainder.
    uintptr_t modulo = ptr & (a - 1u);
    if (modulo != 0)
    {
        ptr += a - modulo;
    }
    return ptr;
}

// Internal specialized arenas (hidden from header)
// GFX resources are managed by Raylib — only engine-internal arenas remain.
static MemoryArena g_ConfigArena;
static MemoryArena g_LevelDataArena;
static MemoryArena g_RendererArena;

// Global pool (Encapsulated)
static MemoryPool g_MainPool;

void Engine_PoolInitMain(void* buffer, size_t capacity, size_t chunk_size) { Engine_PoolInit(&g_MainPool, buffer, capacity, chunk_size); }

void* Engine_PoolAllocMain() { return Engine_PoolAlloc(&g_MainPool); }

void Engine_PoolFreeMain(void* ptr) { Engine_PoolFree(&g_MainPool, ptr); }

void* Engine_PoolGetBufferMain() { return g_MainPool.buffer; }

// Max slots supported per arena type for metadata arrays
#define MAX_ARENA_SLOTS MEM_ARENA_MAX_SLOTS
static ResourceSlot s_Slots[ARENA_COUNT][MAX_ARENA_SLOTS];
static uint32_t s_SlotCounts[ARENA_COUNT];

static void Internal_InitSlots(ArenaType type, MemoryArena* arena, uint32_t count)
{
    if (count == 0)
        return;
    if (count > MAX_ARENA_SLOTS)
        count = MAX_ARENA_SLOTS;

    s_SlotCounts[type] = count;

    // Calculate size per slot, ensuring each slot start is 16KB aligned
    size_t alignment = MEM_ARENA_SLOT_ALIGNMENT;
    size_t total_capacity = arena->capacity;

    // We need to account for potential padding at the start of each slot
    // To be safe, we calculate a "safe" slot size
    size_t slot_capacity = (total_capacity / count);
    // Align slot capacity down to 16KB to ensure every start is aligned if we
    // start aligned
    slot_capacity = (slot_capacity / alignment) * alignment;

    uint8_t* ptr = arena->buffer;
    for (uint32_t i = 0; i < count; i++)
    {
        // Align the start of this specific slot
        uintptr_t aligned_start = AlignForward(reinterpret_cast<uintptr_t>(ptr), alignment);

        s_Slots[type][i].ptr = reinterpret_cast<void*>(aligned_start);
        s_Slots[type][i].capacity = slot_capacity;
        s_Slots[type][i].usedSize = 0;
        s_Slots[type][i].locked = false;

        ptr = (uint8_t*)(aligned_start + slot_capacity);
    }
}

void Engine_ArenasInitSegmented(void* base_ptr)
{
    uint8_t* ptr = static_cast<uint8_t*>(base_ptr);

    Engine_ArenaInit(&g_ConfigArena, ptr, MEM_BLOCK_CONFIG_SIZE);
    Internal_InitSlots(ARENA_CONFIG, &g_ConfigArena, MEM_BLOCK_CONFIG_SLOTS);
    ptr += MEM_BLOCK_CONFIG_SIZE;

    Engine_ArenaInit(&g_LevelDataArena, ptr, MEM_BLOCK_LEVEL_DATA_SIZE);
    Internal_InitSlots(ARENA_LEVEL_DATA, &g_LevelDataArena, MEM_BLOCK_LEVEL_DATA_SLOTS);
    ptr += MEM_BLOCK_LEVEL_DATA_SIZE;

    Engine_ArenaInit(&g_RendererArena, ptr, MEM_BLOCK_RENDERER_SIZE);
    Internal_InitSlots(ARENA_RENDERER, &g_RendererArena, MEM_BLOCK_RENDERER_SLOTS);
}

void* Engine_GetSlot(ArenaType type, uint32_t slotIndex)
{
    if (type >= ARENA_COUNT || slotIndex >= s_SlotCounts[type])
        return nullptr;
    return s_Slots[type][slotIndex].ptr;
}

size_t Engine_GetSlotCapacity(ArenaType type, uint32_t slotIndex)
{
    if (type >= ARENA_COUNT || slotIndex >= s_SlotCounts[type])
        return 0;
    return s_Slots[type][slotIndex].capacity;
}

bool Engine_LoadToSlot(ArenaType type, uint32_t slotIndex, const void* data, size_t size)
{
    if (type >= ARENA_COUNT || slotIndex >= s_SlotCounts[type])
        return false;

    ResourceSlot* slot = &s_Slots[type][slotIndex];
    if (slot->locked)
    {
        Engine_LogError("Arena: slot %u of segment %d is locked; write of %zu bytes refused", slotIndex, static_cast<int>(type), size);
        return false;
    }
    if (size > slot->capacity)
    {
        Engine_LogError("Arena: %zu bytes exceeds slot %u capacity %zu in segment %d; refused", size, slotIndex, slot->capacity, static_cast<int>(type));
        return false;
    }

    if (data && size > 0)
    {
        memcpy(slot->ptr, data, size);
    }
    slot->usedSize = size;
    return true;
}

void Engine_LockSlot(ArenaType type, uint32_t slotIndex)
{
    if (type >= ARENA_COUNT || slotIndex >= s_SlotCounts[type])
        return;
    s_Slots[type][slotIndex].locked = true;
}

void Engine_UnlockSlot(ArenaType type, uint32_t slotIndex)
{
    if (type >= ARENA_COUNT || slotIndex >= s_SlotCounts[type])
        return;
    s_Slots[type][slotIndex].locked = false;
}

void Engine_ClearSlot(ArenaType type, uint32_t slotIndex)
{
    if (type >= ARENA_COUNT || slotIndex >= s_SlotCounts[type])
        return;
    ResourceSlot* slot = &s_Slots[type][slotIndex];
    if (slot->locked)
        return;

    memset(slot->ptr, 0, slot->capacity);
    slot->usedSize = 0;
}

void* Engine_AddToArena(ArenaType type, size_t size, size_t alignment)
{
    switch (type)
    {
    case ARENA_CONFIG:
        return Engine_ArenaAlloc(&g_ConfigArena, size, alignment);
    case ARENA_LEVEL_DATA:
        return Engine_ArenaAlloc(&g_LevelDataArena, size, alignment);
    case ARENA_RENDERER:
        return Engine_ArenaAlloc(&g_RendererArena, size, alignment);
    default:
        return nullptr;
    }
}

void Engine_ArenaInit(MemoryArena* arena, void* backing_buffer, size_t capacity)
{
    arena->buffer = static_cast<uint8_t*>(backing_buffer);
    arena->capacity = capacity;
    arena->offset = 0;
}

void* Engine_ArenaAlloc(MemoryArena* arena, size_t size, size_t alignment)
{
    // AlignForward uses a bitmask trick that is only correct for power-of-two
    // alignments. Catch bad values here so a misaligned alloc never silently
    // corrupts memory. alignment == 0 means "no alignment", which is also
    // accepted.
    if (alignment != 0 && !IS_POWER_OF_TWO(alignment))
    {
        Engine_LogError("Engine_ArenaAlloc: alignment %zu is not a power of two", alignment);
        return nullptr;
    }

    uintptr_t current_ptr = reinterpret_cast<uintptr_t>(arena->buffer) + arena->offset;
    uintptr_t aligned_ptr = AlignForward(current_ptr, alignment);
    size_t shift = aligned_ptr - reinterpret_cast<uintptr_t>(arena->buffer);

    if (shift + size > arena->capacity)
    {
        return nullptr; // Out of memory
    }

    arena->offset = shift + size;
    return reinterpret_cast<void*>(aligned_ptr);
}

void Engine_ArenaReset(MemoryArena* arena) { arena->offset = 0; }

void Engine_ResetArena(ArenaType type)
{
    MemoryArena* arena = nullptr;
    switch (type)
    {
    case ARENA_CONFIG:
        arena = &g_ConfigArena;
        break;
    case ARENA_LEVEL_DATA:
        arena = &g_LevelDataArena;
        break;
    case ARENA_RENDERER:
        arena = &g_RendererArena;
        break;
    default:
        return;
    }

    Engine_ArenaReset(arena);
    for (uint32_t i = 0; i < s_SlotCounts[type]; ++i)
    {
        s_Slots[type][i].usedSize = 0;
        s_Slots[type][i].locked = false;
    }
}

void Engine_ArenaClear(MemoryArena* arena)
{
    if (arena->buffer)
    {
        memset(arena->buffer, 0, arena->capacity);
    }
    arena->offset = 0;
}

void Engine_GetArenaStats(ArenaType type, size_t* outCapacity, size_t* outUsed)
{
    if (type >= ARENA_COUNT)
        return;

    MemoryArena* a = nullptr;
    switch (type)
    {
    case ARENA_CONFIG:
        a = &g_ConfigArena;
        break;
    case ARENA_LEVEL_DATA:
        a = &g_LevelDataArena;
        break;
    case ARENA_RENDERER:
        a = &g_RendererArena;
        break;
    default:
        break;
    }

    if (a)
    {
        if (outCapacity)
            *outCapacity = a->capacity;

        // Accumulate used size from all slots in this arena
        size_t totalUsed = 0;
        uint32_t count = s_SlotCounts[type];
        for (uint32_t i = 0; i < count; ++i)
        {
            totalUsed += s_Slots[type][i].usedSize;
        }
        if (outUsed)
            *outUsed = totalUsed;
    }
}

void Engine_PoolInit(MemoryPool* pool, void* backing_buffer, size_t capacity, size_t chunk_size)
{
    pool->buffer = (uint8_t*)backing_buffer;
    pool->capacity = capacity;
    if (chunk_size < sizeof(PoolFreeNode))
    {
        chunk_size = sizeof(PoolFreeNode);
    }
    pool->chunk_size = chunk_size;
    Engine_PoolReset(pool);
}

void Engine_PoolReset(MemoryPool* pool)
{
    if (pool->capacity < pool->chunk_size || !pool->buffer)
    {
        pool->head = nullptr;
        return;
    }

    size_t num_chunks = pool->capacity / pool->chunk_size;
    pool->head = reinterpret_cast<PoolFreeNode*>(pool->buffer);
    PoolFreeNode* curr = pool->head;

    for (size_t i = 1; i < num_chunks; ++i)
    {
        PoolFreeNode* next_node = reinterpret_cast<PoolFreeNode*>(pool->buffer + i * pool->chunk_size);
        curr->next = next_node;
        curr = next_node;
    }
    curr->next = nullptr;
}

void* Engine_PoolAlloc(MemoryPool* pool)
{
    if (pool->head == nullptr)
    {
        Engine_LogError("Pool: exhausted (%zu byte chunks); allocation refused", pool->chunk_size);
        return nullptr;
    }
    PoolFreeNode* node = pool->head;
    pool->head = pool->head->next;

    // Clear chunk memory for determinism (excluding what was used by the node
    // itself, actually we clear all of it)
    memset((void*)node, 0, pool->chunk_size);
    return (void*)node;
}

void Engine_PoolFree(MemoryPool* pool, void* ptr)
{
    if (ptr == nullptr)
        return;

    // In a robust pool allocator, you'd verify ptr is within bounds and aligned.
    // For speed we assume it is.
    PoolFreeNode* node = static_cast<PoolFreeNode*>(ptr);
    node->next = pool->head;
    pool->head = node;
}

void Engine_GetPoolStatsMain(size_t* outCapacity, size_t* outUsed)
{
    if (outCapacity)
        *outCapacity = g_MainPool.capacity;
    if (outUsed)
    {
        size_t totalChunks = g_MainPool.capacity / g_MainPool.chunk_size;
        size_t freeChunks = 0;
        PoolFreeNode* curr = g_MainPool.head;
        while (curr)
        {
            freeChunks++;
            curr = curr->next;
        }
        *outUsed = (totalChunks - freeChunks) * g_MainPool.chunk_size;
    }
}

#include "platform/Platform.h"

// The single pair through which shared engine code reaches platform memory.
// Routing both halves through here is what keeps an allocation and its release
// on the same allocator.
void* Engine_PlatformAlloc(size_t size, size_t alignment)
{
    Platform* platform = Engine_GetPlatform();
    return platform ? platform->GetMemory().Alloc(size, alignment) : nullptr;
}

void Engine_PlatformFree(void* ptr)
{
    if (!ptr)
        return;
    Platform* platform = Engine_GetPlatform();
    if (platform)
        platform->GetMemory().Free(ptr);
}

void Engine_GetHeapStats(size_t* outTotal, size_t* outUsed, size_t* outFree)
{
    // Heap accounting is platform-specific: newlib/glibc expose mallinfo(),
    // MinGW does not, and a platform may track its own reservations instead.
    HeapStats stats{};
    Platform* platform = Engine_GetPlatform();
    if (platform)
        platform->GetMemory().GetHeapStats(&stats);

    if (outTotal)
        *outTotal = stats.totalBytes;
    if (outUsed)
        *outUsed = stats.usedBytes;
    if (outFree)
        *outFree = stats.freeBytes;
}
