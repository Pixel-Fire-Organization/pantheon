#pragma once

#include <cstdint>

#include "PlatformConstants.h"

// FORMAT CONSTANTS - shared with tools/pack_archive.py, identical everywhere.

#define ARCH_FILE_MAGIC 0x52325350u // "PS2R" little-endian
#define ARCH_FILE_VERSION 1u

// Payloads are aligned to a DVD sector so a read never straddles an extra sector
// and every seek target lands on a sector boundary (drive locality).
#define ARCH_SECTOR_ALIGN 2048u

#define ARCH_FILE_EXT ".PS2R"

// Boot archive base name, mounted at Engine_Init against the active device token.
#define ARCH_BOOT_ARCHIVE_NAME "RASSETS.PS2R"

typedef struct
{
    uint32_t magic; // ARCH_FILE_MAGIC
    uint16_t version; // ARCH_FILE_VERSION
    uint16_t flags; // reserved, 0
    uint32_t entryCount;
    uint32_t stringsOffset; // byte offset of the string table
    uint32_t stringsSize; // string table size in bytes
    uint32_t dataOffset; // first payload byte (multiple of ARCH_SECTOR_ALIGN)
    uint32_t reserved[2];
} ArchiveFileHeader; // 32 bytes

typedef struct
{
    uint32_t nameHash; // FNV-1a 32 of the canonical key
    uint32_t nameOffset; // offset into the string table; hash hits confirmed by strcmp
    uint32_t offset; // absolute byte offset of the payload (ARCH_SECTOR_ALIGN-aligned)
    uint32_t size; // payload size in bytes
} ArchiveTocEntry; // 16 bytes

// Where a resolved asset lives: a mounted slot + its byte span in that archive.
typedef struct
{
    int32_t archive; // mounted slot index, or -1 = not found
    uint32_t offset;
    uint32_t size;
} ArchiveLocator;

// --- Runtime API ---

bool Engine_Archive_Init();

// Mount a .PS2R by disc path (build it with Engine_BuildPath). Reads the header,
// TOC, and string table into heap memory and keeps the FILE* open for streaming.
// Every entry's name offset is checked against the string table and the table is
// terminated at mount time, so a key read afterwards cannot leave the block.
// Blocking — call from a load screen, not mid-frame. Returns a mount handle
// (>= 0) or -1 on failure (missing file / bad magic / bad TOC / no free slot).
int32_t Engine_Archive_Mount(const char* discPath);

// Unmount: close the FILE* and free the TOC. Takes the IO file-access
// semaphore for the whole teardown, so a worker resolving or reading through
// this slot never observes it half-dropped.
void Engine_Archive_Unmount(int32_t handle);

// Resolve an asset path (any form) to a span in a mounted archive. Returns true
// and fills outLoc on a hit. Later mount slots are searched first, so a second
// mounted container shadows a colliding key in the first. Takes the IO
// file-access semaphore, so it is safe alongside a concurrent mount or unmount.
bool Engine_Archive_Find(const char* assetPath, ArchiveLocator* outLoc);

// Read `bytes` from a located span at span-relative `spanOffset` into dst.
// Bounds-checked against the span. Takes the IO file-access semaphore, so it is
// safe to call from the main thread while the IO worker may also touch a file.
bool Engine_Archive_ReadSync(const ArchiveLocator* loc, uint32_t spanOffset, void* dst, uint32_t bytes);

// --- Introspection ---
// Read-only, for diagnostics. Nothing here mounts, reads or changes state.

// What one mounted slot holds.
typedef struct
{
    const char* path; // the path it was mounted from; valid while mounted
    uint32_t entryCount;
    uint64_t payloadBytes; // sum of every entry's payload size
} ArchiveMountInfo;

// Describe one mount slot. Returns false when the slot is free or invalid,
// which is how a caller walks all ARCH_MAX_MOUNTED slots.
bool Engine_Archive_GetMount(int32_t slot, ArchiveMountInfo* outInfo);

// Read one TOC entry and its canonical key. `outName` may be null. Returns
// false when the slot is free or the index is past its entry count.
bool Engine_Archive_GetEntry(int32_t slot, uint32_t index, ArchiveTocEntry* outEntry, char* outName, uint32_t nameSize);

void Engine_Archive_Shutdown();
