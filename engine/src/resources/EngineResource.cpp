#include <cstring>
#include "Engine.h"
#include "graphics/FontFormat.h"
#include "graphics/ModelFormat.h"
#include "graphics/Renderer.h"
#include "graphics/Types.h"
#include "graphics/tim2.h"
#include "platform/Platform.h"
#include "ui/EngineUi.h"

// Stable per-dependency reference: packs a slot index and a generation counter
// into 4 bytes (same width as the old int32_t). The generation must match the
// target slot's generation at unload time; a mismatch means the slot was reused
// for a different resource, so the decrement is skipped.
typedef struct
{
    int16_t index; // slot index, or -1 for "none"
    uint16_t generation; // slot generation when this dep was bound
} DepHandle;

// Internal resource entry — holds a Raylib resource handle + metadata
typedef struct
{
    ResourceType type;
    ResourceState state;
    uint32_t refCount;
    uint32_t lastUsedFrame;
    uint32_t textureBytes; // texture VRAM footprint (RES_TEXTURE only; 0 otherwise)
    char key[IO_FILE_MAX_PATH];
    bool pinned;
    // Generation counter — incremented every time this slot is cleared.
    // DepHandle.generation is compared against this value on unload to detect
    // stale references caused by slot reuse.
    uint16_t generation;
    DepHandle deps[RES_MAX_DEPENDENCIES];
    uint8_t depCount;

    // Engine-native resource storage (only one is active based on type).
    // Sound has no implementation on any platform; a load of that type is
    // logged and rejected.
    union
    {
        Texture2D texture;
        Model model;
        Font font;
        UiTheme theme;
    } handle;
} ResourceEntry;

static ResourceEntry s_Entries[RES_MAX_ENTRIES];
static uint32_t s_CurrentFrame = 0;
// Bytes occupied by every READY texture. The engine tracks this itself: a
// backend need not expose a query, and must not be left to resolve an overrun
// on its own terms.
static uint32_t s_TextureBytesUsed = 0;

// --- Internal helpers ---

static int32_t Internal_FindByKey(const char* key)
{
    for (int32_t i = 0; i < RES_MAX_ENTRIES; i++)
    {
        if (s_Entries[i].state != RES_STATE_EMPTY && strncmp(s_Entries[i].key, key, IO_FILE_MAX_PATH) == 0)
        {
            return i;
        }
    }
    return -1;
}

static int32_t Internal_FindFreeSlot()
{
    for (int32_t i = 0; i < RES_MAX_ENTRIES; i++)
    {
        if (s_Entries[i].state == RES_STATE_EMPTY)
        {
            return i;
        }
    }
    return -1;
}

static void Internal_UnloadEntry(int32_t index);

static bool Internal_ParseHeaderAndLoadDeps(const void* data, size_t size, AssetFileHeader* outHeader, int32_t entryIndex);

// Bytes that COULD be freed right now (non-pinned, reference-free, READY
// textures). Used only to make the over-budget error actionable - it evicts
// nothing.
static void Internal_CalcEvictableBytes(uint32_t* outBytes, int32_t* outCount)
{
    *outBytes = 0;
    *outCount = 0;
    for (int32_t i = 0; i < RES_MAX_ENTRIES; i++)
    {
        if (s_Entries[i].state != RES_STATE_READY)
            continue;
        if (s_Entries[i].type != RES_TEXTURE)
            continue;
        if (s_Entries[i].pinned)
            continue;
        if (s_Entries[i].refCount > 0)
            continue;
        *outBytes += s_Entries[i].textureBytes;
        (*outCount)++;
    }
}

// The table is never reclaimed behind the programmer (see the resource
// management philosophy in .github/copilot-instructions.md), so a full table is
// an error. Naming the entry that has gone unused longest is what makes it
// actionable: it is the one the caller most likely meant to release.
static int32_t Internal_StalestReleasable()
{
    int32_t bestIndex = -1;
    uint32_t bestFrame = UINT32_MAX;

    for (int32_t i = 0; i < RES_MAX_ENTRIES; i++)
    {
        if (s_Entries[i].state == RES_STATE_EMPTY || s_Entries[i].state == RES_STATE_LOADING)
            continue;
        if (s_Entries[i].pinned)
            continue;
        if (s_Entries[i].refCount > 0)
            continue;
        if (s_Entries[i].lastUsedFrame < bestFrame)
        {
            bestFrame = s_Entries[i].lastUsedFrame;
            bestIndex = i;
        }
    }

    return bestIndex;
}

static void Internal_UnloadHandle(ResourceEntry* entry)
{
    if (entry->state != RES_STATE_READY)
        return;

    switch (entry->type)
    {
    case RES_TEXTURE:
        {
            Renderer* r = Engine_GetRenderer();
            if (r && entry->handle.texture.id != 0)
                r->ReleaseTexture(entry->handle.texture.id);
        }
        break;
    case RES_MODEL:
        Model_FreeBaked(&entry->handle.model);
        break;
    case RES_FONT:
        Font_FreeBaked(&entry->handle.font);
        break;
    case RES_THEME:
    case RES_SOUND:
        break; // nothing was allocated
    }
}

static void Internal_UnloadEntry(int32_t index)
{
    if (index < 0 || index >= RES_MAX_ENTRIES)
        return;

    ResourceEntry* entry = &s_Entries[index];
    if (entry->state == RES_STATE_EMPTY)
        return;

    // Decrement refCount on all dependencies.
    // Validate the generation before touching the slot — if the dep was already
    // unloaded and its slot reused for a different resource, the generation will
    // have advanced and we must NOT decrement the new resource's refCount.
    for (uint8_t d = 0; d < entry->depCount; d++)
    {
        int16_t depIdx = entry->deps[d].index;
        uint16_t depGen = entry->deps[d].generation;
        if (depIdx >= 0 && depIdx < RES_MAX_ENTRIES && s_Entries[depIdx].state != RES_STATE_EMPTY && s_Entries[depIdx].generation == depGen)
        {
            if (s_Entries[depIdx].refCount > 0)
            {
                s_Entries[depIdx].refCount--;
            }
        }
    }

    Internal_UnloadHandle(entry);

    // Release shadow GS page accounting for textures
    if (entry->type == RES_TEXTURE && entry->textureBytes > 0)
    {
        s_TextureBytesUsed = (s_TextureBytesUsed >= entry->textureBytes) ? s_TextureBytesUsed - entry->textureBytes : 0;
    }

    // Bump the generation BEFORE clearing the slot so any parent whose async
    // unload races with a new load into this slot will see the mismatch.
    uint16_t nextGeneration = static_cast<uint16_t>(entry->generation + 1u);

    // Clear the slot
    memset(entry, 0, sizeof(ResourceEntry));
    entry->state = RES_STATE_EMPTY;
    // Restore the incremented generation so future DepHandle bindings get the
    // new value and old stale bindings remain detectable.
    entry->generation = nextGeneration;
}

// Resolve a baked model's diffuse texture reference (an index into the owning
// asset's dependency list) to a resource handle. Passed to Model_LoadBaked; the
// returned handle is stored in the material and resolved to a live Texture2D at
// draw time (the texture dependency may still be streaming in).
static int32_t Internal_ResolveModelTexture(uint32_t diffuseTexRef, void* user)
{
    const ResourceEntry* entry = static_cast<const ResourceEntry*>(user);
    if (!entry || diffuseTexRef >= entry->depCount)
        return -1;
    return entry->deps[diffuseTexRef].index; // resource handle, or -1 if unbound
}

// Callback context for async IO loads. The generation is the slot's value when
// the read was queued: an unload between queueing and completion advances it,
// which is how a late callback recognises that its slot is no longer its own.
typedef struct
{
    int32_t entryIndex;
    ResourceType type;
    uint16_t generation;
} ResourceLoadContext;

static void Internal_OnAsyncLoadComplete(const void* data, size_t size, void* userData)
{
    ResourceLoadContext* ctx = static_cast<ResourceLoadContext*>(userData);
    if (!ctx)
        return;

    int32_t idx = ctx->entryIndex;
    ResourceEntry* entry = &s_Entries[idx];

    if (entry->state != RES_STATE_LOADING || entry->generation != ctx->generation)
    {
        Engine_LogInfo("Resource: discarding a completed read for slot %d; it was unloaded while in flight", idx);
        Engine_PoolFreeMain(ctx);
        return;
    }

    if (!data || size == 0)
    {
        Engine_LogError("Resource async load failed for slot %d (%s)", idx, entry->key);
        // Unload the entry to clear metadata, decrement any partial dep refs, and
        // bump generation so stale DepHandles won't point to a future occupant.
        Internal_UnloadEntry(idx);
        Engine_PoolFreeMain(ctx);
        return;
    }

    // Parse the .ps2a header and load any declared dependencies
    AssetFileHeader header;
    if (!Internal_ParseHeaderAndLoadDeps(data, size, &header, idx))
    {
        // Internal_ParseHeaderAndLoadDeps only returns false before the dep-loading
        // loop, so no refCounts can have been bumped yet. Internal_UnloadEntry is
        // still used for a consistent cleanup path.
        Internal_UnloadEntry(idx);
        Engine_PoolFreeMain(ctx);
        return;
    }

    const unsigned char* payload = static_cast<const unsigned char*>(data) + sizeof(AssetFileHeader);
    int32_t payloadSize = static_cast<int32_t>(size - sizeof(AssetFileHeader));

    if (payloadSize <= 0 || static_cast<uint32_t>(payloadSize) < header.dataSize)
    {
        Engine_LogError("Resource payload mismatch for slot %d (%s)", idx, entry->key);
        // Deps were already loaded and their refCounts bumped; roll back via
        // Internal_UnloadEntry so they are properly decremented.
        Internal_UnloadEntry(idx);
        Engine_PoolFreeMain(ctx);
        return;
    }

    // Use the type declared in the .ps2a header as the authoritative decode type.
    // ctx->type is the caller-supplied hint; the header's type is what the asset
    // packer stamped and should always be preferred. Update entry->type so that
    // later unload / get calls use the correct Raylib handle union member.

    // Validate header.type is within the supported ResourceType enum range.
    // A corrupt asset or out-of-date packer can produce invalid type values.
    if (header.type > RES_THEME) // RES_TEXTURE=0, RES_MODEL=1, RES_SOUND=2, RES_FONT=3, RES_THEME=4
    {
        Engine_LogError("Resource: invalid type %u in .ps2a header for slot %d (%s)", header.type, idx, entry->key);
        Internal_UnloadEntry(idx);
        Engine_PoolFreeMain(ctx);
        return;
    }

    entry->type = static_cast<ResourceType>(header.type);

    switch (static_cast<ResourceType>(header.type))
    {
    case RES_TEXTURE:
        {
            // Textures are baked to TIM2 (GS-native). Parse the header, budget-check
            // GS VRAM, then hand the pixels to the active renderer for upload.
            Tim2Image img;
            if (!Tim2_Parse(payload, static_cast<size_t>(payloadSize), &img))
            {
                Engine_LogError("Resource: TIM2 parse failed for slot %d (%s)", idx, entry->key);
                Internal_UnloadEntry(idx);
                Engine_PoolFreeMain(ctx);
                return;
            }

            // How much texture memory this costs is the platform's business:
            // the PS2 rounds every mip level up to whole GS pages (whose size
            // depends on the pixel format), a desktop GPU does not.
            Platform* platform = Engine_GetPlatform();
            const uint32_t footprint = platform->GetTextureFootprintBytes(static_cast<uint32_t>(img.width), static_cast<uint32_t>(img.height), img.format, img.mipCount);
            // The ceiling is the backend's: two backends on one platform can be
            // left with different amounts after their own frame and depth buffers.
            Renderer* renderer = Engine_GetRenderer();
            const uint32_t budgetBytes = renderer ? renderer->GetTextureBudgetBytes() : platform->GetConstant(PlatformConstant::TextureBudgetBytes);
            const uint32_t maxTextureBytes = platform->GetConstant(PlatformConstant::MaxTextureBytes);
            const uint32_t maxWidth = platform->GetConstant(PlatformConstant::MaxTextureWidth);
            const uint32_t maxHeight = platform->GetConstant(PlatformConstant::MaxTextureHeight);

            if (static_cast<uint32_t>(img.width) > maxWidth || static_cast<uint32_t>(img.height) > maxHeight || footprint > maxTextureBytes)
            {
                Engine_LogError("Resource: texture rejected — %dx%d (%u KB) exceeds budget "
                                "(max %u KB, dimension cap %ux%u) in slot %d '%s'",
                                img.width, img.height, footprint / 1024u, maxTextureBytes / 1024u, maxWidth, maxHeight, idx, entry->key);
                Internal_UnloadEntry(idx);
                Engine_PoolFreeMain(ctx);
                return;
            }

            if (s_TextureBytesUsed + footprint > budgetBytes)
            {
                uint32_t evictableBytes = 0;
                int32_t evictableCount = 0;
                Internal_CalcEvictableBytes(&evictableBytes, &evictableCount);
                Engine_LogError("Resource: texture memory full — cannot load %dx%d (%u KB). Usage: %u/%u KB. "
                                "Call Engine_Resource_Unload() to free up to %u KB across %d texture(s), then retry.",
                                img.width, img.height, footprint / 1024u, s_TextureBytesUsed / 1024u, budgetBytes / 1024u, evictableBytes / 1024u, evictableCount);
                Internal_UnloadEntry(idx);
                Engine_PoolFreeMain(ctx);
                return;
            }

            TextureUpload upload{};
            for (int lvl = 0; lvl < TEX_MAX_MIP_LEVELS; ++lvl)
                upload.levelPtr[lvl] = img.levelPtr[lvl];
            upload.mipCount = img.mipCount;
            upload.width = img.width;
            upload.height = img.height;
            upload.format = img.format;
            upload.filter = img.filter;
            upload.clut = img.clut;

            const uint32_t texId = renderer ? renderer->UploadTexture(upload) : 0u;
            if (texId == 0)
            {
                // footprint was not yet committed to the shadow counter, so no rollback needed.
                Engine_LogError("Resource: GPU texture upload failed for slot %d (%s)", idx, entry->key);
                Internal_UnloadEntry(idx);
                Engine_PoolFreeMain(ctx);
                return;
            }

            entry->handle.texture.id = texId;
            entry->handle.texture.width = img.width;
            entry->handle.texture.height = img.height;
            entry->handle.texture.format = static_cast<int>(img.format);
            entry->textureBytes = footprint;
            s_TextureBytesUsed += footprint;
            entry->state = RES_STATE_READY;
            Engine_LogInfo("Texture loaded (%dx%d). Remaining: %u KB", img.width, img.height, (budgetBytes - s_TextureBytesUsed) / 1024u);
        }
        break;
    case RES_MODEL:
        {
            // Models are baked to separated, unindexed vertex arrays. Their texture
            // dependencies were queued by Internal_ParseHeaderAndLoadDeps above;
            // materials store the dep resource handles and are resolved to live
            // textures at draw time.
            if (!Model_LoadBaked(payload, static_cast<size_t>(payloadSize), &entry->handle.model, Internal_ResolveModelTexture, entry))
            {
                Engine_LogError("Resource: baked model load failed for slot %d (%s)", idx, entry->key);
                Internal_UnloadEntry(idx);
                Engine_PoolFreeMain(ctx);
                return;
            }
            entry->state = RES_STATE_READY;
        }
        break;
    case RES_FONT:
        {
            // Metrics only: the atlas is an ordinary texture named as this
            // asset's first dependency, already queued above, so it is budgeted
            // and uploaded by the texture path rather than a second one.
            const int32_t atlas = (entry->depCount > 0) ? entry->deps[0].index : -1;
            if (atlas < 0)
            {
                Engine_LogError("Resource: font slot %d (%s) names no atlas dependency", idx, entry->key);
                Internal_UnloadEntry(idx);
                Engine_PoolFreeMain(ctx);
                return;
            }
            if (!Font_LoadBaked(payload, static_cast<size_t>(payloadSize), atlas, &entry->handle.font))
            {
                Engine_LogError("Resource: cooked font load failed for slot %d (%s)", idx, entry->key);
                Internal_UnloadEntry(idx);
                Engine_PoolFreeMain(ctx);
                return;
            }
            entry->state = RES_STATE_READY;
        }
        break;
    case RES_THEME:
        {
            // A theme is copied into live engine state rather than parsed, so
            // the decode is where identity, version, size and integrity are
            // checked; nothing downstream would catch a bad one.
            if (!Ui_ThemeDecode(payload, static_cast<size_t>(payloadSize), &entry->handle.theme))
            {
                Engine_LogError("Resource: cooked theme load failed for slot %d (%s)", idx, entry->key);
                Internal_UnloadEntry(idx);
                Engine_PoolFreeMain(ctx);
                return;
            }
            entry->state = RES_STATE_READY;
        }
        break;
    case RES_SOUND:
        Engine_LogError("Resource: sound is not implemented on any platform; refusing slot %d (%s)", idx, entry->key);
        Internal_UnloadEntry(idx);
        Engine_PoolFreeMain(ctx);
        return;
    default:
        Engine_LogError("Resource: unknown type %u for slot %d (%s)", header.type, idx, entry->key);
        Internal_UnloadEntry(idx);
        Engine_PoolFreeMain(ctx);
        return;
    }

    Engine_PoolFreeMain(ctx);
}


// Read just the magic and type fields (first 8 bytes) of a .ps2a asset without
// loading its full payload. Resolves through a mounted archive first, falling
// back to a loose file on disc. Returns false if the asset can't be read or the
// magic is wrong.
static bool Internal_PeekAssetType(const char* path, ResourceType* outType)
{
    uint32_t peek[2]; // [0] = magic, [1] = type

    // ARCHIVE SEAM (header peek): prefer a mounted archive.
    ArchiveLocator loc;
    if (Engine_Archive_Find(path, &loc))
    {
        if (!Engine_Archive_ReadSync(&loc, 0, peek, sizeof(peek)))
            return false;
    }
    else
    {
        // Loose fallback — take the file-access semaphore so this raw read never
        // races the IO worker's file access.
        Platform* platform = Engine_GetPlatform();

        Engine_IO_AcquireFileAccess();
        FileHandle f = platform->FileOpen(path, FileMode::Read);
        size_t bytesRead = 0;
        if (f)
        {
            bytesRead = platform->FileRead(f, peek, sizeof(peek));
            platform->FileClose(f);
        }
        Engine_IO_ReleaseFileAccess();
        if (!f || bytesRead < sizeof(peek))
            return false;
    }

    if (peek[0] != RES_ASSET_MAGIC)
        return false;

    *outType = static_cast<ResourceType>(peek[1]);
    return true;
}

// Parse a .ps2a header from raw file data and load dependencies.
// Returns true if the header is valid.
static bool Internal_ParseHeaderAndLoadDeps(const void* data, size_t size, AssetFileHeader* outHeader, int32_t entryIndex)
{
    if (size < sizeof(AssetFileHeader))
        return false;

    memcpy(outHeader, data, sizeof(AssetFileHeader));

    // Force null-termination of header strings to guard against malformed/corrupt
    // assets. Without this, missing terminators can cause LoadImageFromMemory and
    // dependency lookups to read past the header into the payload or other memory.
    outHeader->ext[sizeof(outHeader->ext) - 1] = '\0';
    for (uint8_t d = 0; d < RES_MAX_DEPENDENCIES; d++)
    {
        outHeader->deps[d][IO_FILE_MAX_PATH - 1] = '\0';
    }

    if (outHeader->magic != RES_ASSET_MAGIC)
    {
        Engine_LogError("Resource: invalid .ps2a magic for slot %d", entryIndex);
        return false;
    }

    // Load each dependency (incrementing refCount if already loaded)
    ResourceEntry* entry = &s_Entries[entryIndex];
    entry->depCount = outHeader->depCount;
    if (entry->depCount > RES_MAX_DEPENDENCIES)
        entry->depCount = RES_MAX_DEPENDENCIES;

    for (uint8_t d = 0; d < entry->depCount; d++)
    {
        int32_t depHandle = Internal_FindByKey(outHeader->deps[d]);
        if (depHandle >= 0)
        {
            // Already loaded — just bump refCount
            s_Entries[depHandle].refCount++;
            entry->deps[d].index = static_cast<int16_t>(depHandle);
            entry->deps[d].generation = s_Entries[depHandle].generation;
        }
        else
        {
            // Need to load the dependency first.
            // The dependency is its own .ps2a file with its own type field — peek
            // just its header to get the correct type rather than inheriting the
            // parent's type, which may be different.
            ResourceType depType;
            if (!Internal_PeekAssetType(outHeader->deps[d], &depType))
            {
                Engine_LogError("Resource: cannot determine type for dependency '%s'", outHeader->deps[d]);
                entry->deps[d].index = -1;
                entry->deps[d].generation = 0;
                continue;
            }
            int32_t newDep = Engine_Resource_Load(depType, outHeader->deps[d]);
            if (newDep >= 0)
            {
                s_Entries[newDep].refCount++;
                entry->deps[d].index = static_cast<int16_t>(newDep);
                entry->deps[d].generation = s_Entries[newDep].generation;
            }
            else
            {
                Engine_LogError("Resource: failed to load dependency '%s'", outHeader->deps[d]);
                entry->deps[d].index = -1;
                entry->deps[d].generation = 0;
            }
        }
    }

    return true;
}

// --- Public API ---

bool Engine_Resource_Init()
{
    memset(s_Entries, 0, sizeof(s_Entries));
    for (int32_t i = 0; i < RES_MAX_ENTRIES; i++)
    {
        s_Entries[i].state = RES_STATE_EMPTY;
    }
    s_CurrentFrame = 0;
    s_TextureBytesUsed = 0;
    Engine_LogInfo("Resource Manager Initialized (%d slots)", RES_MAX_ENTRIES);
    return true;
}

void Engine_Resource_Shutdown() { Engine_Resource_UnloadAll(); }

int32_t Engine_Resource_Load(ResourceType type, const char* path)
{
    if (!path)
        return -1;

    // Canonicalise the path into the dedup/lookup key so the same asset requested
    // as a device path ("cdrom0:/RASSETS/BOX.PS2A;1") and as a baked dependency
    // string ("RASSETS/BOX.PS2A") map to one slot instead of two.
    char canonicalKey[IO_FILE_MAX_PATH];
    Engine_Path_Canonical(path, canonicalKey);

    // Check if already loaded or loading
    int32_t existing = Internal_FindByKey(canonicalKey);
    if (existing >= 0)
    {
        s_Entries[existing].lastUsedFrame = s_CurrentFrame;
        return existing;
    }

    // No implicit eviction: a full table is a content error the caller resolves
    // with Engine_Resource_Unload, never something the engine resolves by
    // dropping an asset another handle still names.
    int32_t slot = Internal_FindFreeSlot();
    if (slot < 0)
    {
        const int32_t stalest = Internal_StalestReleasable();
        if (stalest >= 0)
        {
            Engine_LogError("Resource: all %d slots are in use, cannot load '%s'. "
                            "Call Engine_Resource_Unload() first; slot %d ('%s') is unpinned, unreferenced and least recently used.",
                            RES_MAX_ENTRIES, path, stalest, s_Entries[stalest].key);
        }
        else
        {
            Engine_LogError("Resource: all %d slots are in use and every one is pinned, referenced or still loading; cannot load '%s'", RES_MAX_ENTRIES, path);
        }
        return -1;
    }

    // Initialize the entry
    ResourceEntry* entry = &s_Entries[slot];
    // The generation must survive the memset: it was already incremented by
    // Internal_UnloadEntry (unload path) or holds the boot-time 0 (fresh slot).
    // Saving it here and restoring it below keeps the counter monotonic.
    uint16_t savedGeneration = entry->generation;
    memset(entry, 0, sizeof(ResourceEntry));
    entry->generation = savedGeneration;
    entry->type = type;
    entry->state = RES_STATE_LOADING;
    entry->lastUsedFrame = s_CurrentFrame;
    entry->pinned = false;
    entry->refCount = 0;
    entry->depCount = 0;
    // canonicalKey is fully defined and null-terminated across all IO_FILE_MAX_PATH
    // bytes by Engine_Path_Canonical, so copy the whole buffer.
    memcpy(entry->key, canonicalKey, IO_FILE_MAX_PATH);

    for (uint8_t d = 0; d < RES_MAX_DEPENDENCIES; d++)
    {
        entry->deps[d].index = -1;
        entry->deps[d].generation = 0;
    }

    // All resource types stream through the async IO path. The header's declared
    // type drives decoding (TIM2 for textures, baked blob for models); the .ps2a
    // dependency list is loaded first so a model's textures are already queued.
    // Async path: allocate a context from the pool, then stream
    ResourceLoadContext* ctx = static_cast<ResourceLoadContext*>(Engine_PoolAllocMain());
    if (!ctx)
    {
        Engine_LogError("Resource: pool exhausted, cannot create load context");
        entry->state = RES_STATE_EMPTY;
        return -1;
    }

    ctx->entryIndex = slot;
    ctx->type = type;
    ctx->generation = entry->generation;


    if (!Engine_IO_ReadAsync(path, Internal_OnAsyncLoadComplete, ctx))
    {
        Engine_LogError("Resource: IO queue full for '%s'", path);
        Engine_PoolFreeMain(ctx);
        entry->state = RES_STATE_EMPTY;
        return -1;
    }

    return slot;
}

int32_t Engine_Resource_LoadAuto(const char* path)
{
    if (!path)
        return -1;

    ResourceType type;
    if (!Internal_PeekAssetType(path, &type))
    {
        Engine_LogError("Resource: cannot determine type for '%s' — bad magic or unreadable", path);
        return -1;
    }

    return Engine_Resource_Load(type, path);
}

void* Engine_Resource_Get(int32_t handle)
{
    if (handle < 0 || handle >= RES_MAX_ENTRIES)
        return nullptr;

    ResourceEntry* entry = &s_Entries[handle];
    if (entry->state != RES_STATE_READY)
        return nullptr;

    entry->lastUsedFrame = s_CurrentFrame;

    switch (entry->type)
    {
    case RES_TEXTURE:
        return &entry->handle.texture;
    case RES_MODEL:
        return &entry->handle.model;
    case RES_FONT:
        return &entry->handle.font;
    case RES_THEME:
        return &entry->handle.theme;
    case RES_SOUND:
        return nullptr; // not implemented on any platform
    }
    return nullptr;
}

bool Engine_Resource_IsReady(int32_t handle)
{
    if (handle < 0 || handle >= RES_MAX_ENTRIES)
        return false;
    return s_Entries[handle].state == RES_STATE_READY;
}

void Engine_Resource_Pin(int32_t handle)
{
    if (handle < 0 || handle >= RES_MAX_ENTRIES)
        return;
    s_Entries[handle].pinned = true;
}

void Engine_Resource_Unpin(int32_t handle)
{
    if (handle < 0 || handle >= RES_MAX_ENTRIES)
        return;
    s_Entries[handle].pinned = false;
}

void Engine_Resource_Unload(int32_t handle)
{
    if (handle < 0 || handle >= RES_MAX_ENTRIES)
        return;
    Internal_UnloadEntry(handle);
}

void Engine_Resource_UnloadAll()
{
    s_TextureBytesUsed = 0;
    for (int32_t i = 0; i < RES_MAX_ENTRIES; i++)
    {
        if (s_Entries[i].state != RES_STATE_EMPTY)
        {
            Internal_UnloadHandle(&s_Entries[i]);
            const uint16_t nextGeneration = static_cast<uint16_t>(s_Entries[i].generation + 1u);
            memset(&s_Entries[i], 0, sizeof(ResourceEntry));
            s_Entries[i].state = RES_STATE_EMPTY;
            s_Entries[i].generation = nextGeneration;
        }
    }
}

void Engine_Resource_Update() { s_CurrentFrame++; }

bool Engine_Resource_GetInfo(int32_t handle, ResourceInfo* outInfo)
{
    if (!outInfo || handle < 0 || handle >= static_cast<int32_t>(RES_MAX_ENTRIES))
        return false;

    const ResourceEntry* e = &s_Entries[handle];
    if (e->state == RES_STATE_EMPTY)
        return false;

    outInfo->key = e->key;
    outInfo->type = e->type;
    outInfo->state = e->state;
    outInfo->refCount = e->refCount;
    outInfo->textureBytes = e->textureBytes;
    outInfo->width = (e->type == RES_TEXTURE) ? e->handle.texture.width : 0;
    outInfo->height = (e->type == RES_TEXTURE) ? e->handle.texture.height : 0;
    outInfo->depCount = e->depCount;
    outInfo->pinned = e->pinned;
    return true;
}

uint32_t Engine_Resource_GetCapacity() { return RES_MAX_ENTRIES; }

uint32_t Engine_Resource_GetTextureBudgetUsed() { return s_TextureBytesUsed; }
uint32_t Engine_Resource_GetTextureBudget()
{
    Platform* platform = Engine_GetPlatform();
    return platform ? platform->GetConstant(PlatformConstant::TextureBudgetBytes) : 0u;
}
