#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

typedef struct
{
    uint8_t* buffer;
    size_t capacity;
    size_t offset;
} MemoryArena;

typedef struct
{
    void* ptr;
    size_t capacity;
    size_t usedSize;
    bool locked;
} ResourceSlot;

// Arena Types for segmented memory allocation
// GFX resources (textures, meshes, audio) are managed by Raylib's allocator.
// These arenas are for engine-internal subsystems only.
typedef enum
{
    ARENA_CONFIG,
    ARENA_LEVEL_DATA,
    ARENA_RENDERER,
    ARENA_COUNT
} ArenaType;

void Engine_ArenaInit(MemoryArena* arena, void* backing_buffer, size_t capacity);

void* Engine_ArenaAlloc(MemoryArena* arena, size_t size, size_t alignment);

void* Engine_AddToArena(ArenaType type, size_t size, size_t alignment);

void Engine_ArenasInitSegmented(void* base_ptr);

// Slot API for O(1) asset replacement
void* Engine_GetSlot(ArenaType type, uint32_t slotIndex);

size_t Engine_GetSlotCapacity(ArenaType type, uint32_t slotIndex);

bool Engine_LoadToSlot(ArenaType type, uint32_t slotIndex, const void* data, size_t size);

void Engine_LockSlot(ArenaType type, uint32_t slotIndex);

void Engine_UnlockSlot(ArenaType type, uint32_t slotIndex);

void Engine_ClearSlot(ArenaType type, uint32_t slotIndex);

void Engine_ArenaReset(MemoryArena* arena);

// Return one arena segment to its post-startup state: the bump offset goes
// back to zero and every slot is emptied and unlocked. Slot pointers and
// capacities are unchanged, so anything holding a slot address keeps a valid
// one. See docs/subsystems/MEMORY.md.
void Engine_ResetArena(ArenaType type);

void Engine_ArenaClear(MemoryArena* arena);

void Engine_GetArenaStats(ArenaType type, size_t* outCapacity, size_t* outUsed);
void Engine_GetHeapStats(size_t* outTotal, size_t* outUsed, size_t* outFree);

// Pool Allocator (Fixed block size)
typedef struct PoolFreeNode
{
    struct PoolFreeNode* next;
} PoolFreeNode;

typedef struct
{
    uint8_t* buffer;
    size_t capacity;
    size_t chunk_size;
    PoolFreeNode* head;
} MemoryPool;

void Engine_PoolInitMain(void* buffer, size_t capacity, size_t chunk_size);

void* Engine_PoolAllocMain();

void Engine_PoolFreeMain(void* ptr);
void Engine_GetPoolStatsMain(size_t* outCapacity, size_t* outUsed);
void* Engine_PoolGetBufferMain();

void Engine_PoolInit(MemoryPool* pool, void* backing_buffer, size_t capacity, size_t chunk_size);

void* Engine_PoolAlloc(MemoryPool* pool);

void Engine_PoolFree(MemoryPool* pool, void* ptr);

void Engine_PoolReset(MemoryPool* pool);

void* Engine_PlatformAlloc(size_t size, size_t alignment);
void Engine_PlatformFree(void* ptr);

struct PlatformDeleter
{
    void operator()(void* ptr) const { Engine_PlatformFree(ptr); }
};

// Owning handle to a platform-allocated array of trivially-copyable elements.
// No constructors run: the memory is filled by reading into it, exactly as the
// raw allocation it replaces.
template <class T>
using PlatformArray = std::unique_ptr<T[], PlatformDeleter>;

/// Allocate `count` elements of T through the platform memory contract.
/// @param count Element count; a count whose byte size would wrap size_t yields
///        an empty array rather than a short allocation.
/// @param alignment Byte alignment of the returned block.
/// @return An owning array, empty on overflow or allocation failure.
template <class T>
PlatformArray<T> Engine_PlatformArray(size_t count, size_t alignment = 16)
{
    if (count > SIZE_MAX / sizeof(T))
        return PlatformArray<T>(nullptr);
    return PlatformArray<T>(static_cast<T*>(Engine_PlatformAlloc(count * sizeof(T), alignment)));
}
