#include <cmath>
#include <cstdio>
#include <cstring>

#include "Engine.h"
#include "level/EngineLevelSpan.h"
#include "level/EngineSector.h"

#include "core/EngineSubsystems.h"

// Nine residents (a 3x3 ring); each owns one ARENA_LEVEL_DATA slot. Slots hold
// the PSEC blob and the Mesh views point straight into that slot memory.
static const Level* s_Level = nullptr;
static SectorResident s_Residents[LEVEL_RESIDENT_SECTORS];
static SectorResident s_Lod1Residents[MEM_BLOCK_LEVEL_LOD1_SLOTS];
static bool s_Lod1Pending = false;
static float s_CenterX = 0.0f;
static float s_CenterZ = 0.0f;
static int s_CenterCellX = -0x7fff;
static int s_CenterCellZ = -0x7fff;
static bool s_Primed = false;

static int Internal_CellIndex(float world, float origin, float cellSize)
{
    const float cell = std::floor((world - origin) / cellSize);
    if (!(cell > -32768.0f))
        return -32768;
    if (!(cell < 32767.0f))
        return 32767;
    return static_cast<int>(cell);
}

static void Internal_CellOf(float worldX, float worldZ, int* outCx, int* outCz)
{
    const LevelInfoChunk* info = s_Level->info;
    *outCx = Internal_CellIndex(worldX, info->gridOriginX, info->cellSize);
    *outCz = Internal_CellIndex(worldZ, info->gridOriginZ, info->cellSize);
}

static bool Internal_InGrid(int cx, int cz)
{
    const LevelInfoChunk* info = s_Level->info;
    return cx >= 0 && cz >= 0 && cx < info->cellsX && cz < info->cellsZ;
}

// A PSEC blob addresses its geometry by absolute byte offset, and those views
// become the renderer's vertex source with no further checking. Confirm every
// entry's arrays sit inside the blob and start on the transfer alignment the
// format guarantees, before any of them is turned into a Mesh.
static bool Internal_SectorMeshesAreSane(const BakedMeshEntry* entries, uint32_t meshCount, size_t blobSize, const char* key)
{
    for (uint32_t m = 0; m < meshCount; ++m)
    {
        const BakedMeshEntry& e = entries[m];
        if (e.vertexCount == 0 || e.vertsOffset == 0)
        {
            Engine_LogError("Sector '%s': mesh %u has no vertices", key, m);
            return false;
        }

        const uint64_t count = e.vertexCount;
        const uint32_t offsets[4] = {e.vertsOffset, e.normsOffset, e.uvsOffset, e.colorsOffset};
        const uint64_t strides[4] = {4 * sizeof(float), 3 * sizeof(float), 2 * sizeof(float), 4 * sizeof(float)};
        for (int a = 0; a < 4; ++a)
        {
            if (offsets[a] == 0)
                continue;
            if (!Level_OffsetAligned(offsets[a], LEVEL_CHUNK_ALIGN) || !Level_SpanFits(offsets[a], count * strides[a], blobSize))
            {
                Engine_LogError("Sector '%s': mesh %u array %d at %u does not fit its %zu-byte blob", key, m, a, offsets[a], blobSize);
                return false;
            }
        }
    }
    return true;
}

typedef struct
{
    SectorResident* res;
    uint8_t         generation;
} SectorLoadContext;

static void Internal_OnSectorLoaded(const void* data, size_t size, void* userData)
{
    SectorLoadContext* ctx = static_cast<SectorLoadContext*>(userData);
    SectorResident* res = ctx->res;
    const uint8_t  gen  = ctx->generation;
    Engine_PoolFreeMain(ctx);

    if (res->generation != gen)
        return;

    if (!data)
    {
        Engine_LogError("Sector: read failed");
        res->state = SECTOR_EMPTY;
        return;
    }

        ArenaType arenaType = res->isLod1 ? ARENA_LEVEL_LOD1 : ARENA_LEVEL_DATA;
    const size_t cap = Engine_GetSlotCapacity(arenaType, res->arenaSlot);
    if (size > cap)
    {
        Engine_LogError("Sector is %zu bytes, exceeds slot capacity %zu", size, cap);
        res->state = SECTOR_EMPTY;
        return;
    }

        uint8_t* dst = static_cast<uint8_t*>(Engine_GetSlot(arenaType, res->arenaSlot));
    std::memcpy(dst, data, size);

    const size_t blobSize = size;
    if (blobSize < sizeof(SectorHeader))
    {
        Engine_LogError("Sector is %zu bytes, shorter than a PSEC header", blobSize);
        res->state = SECTOR_EMPTY;
        return;
    }

    const SectorHeader* hdr = reinterpret_cast<const SectorHeader*>(dst);
    if (hdr->magic != LEVEL_SECTOR_MAGIC || hdr->version != LEVEL_SECTOR_VERSION)
    {
        Engine_LogError("Sector: bad PSEC header");
        res->state = SECTOR_EMPTY;
        return;
    }

    const uint32_t meshCount = hdr->meshCount;
    if (meshCount > LEVEL_MAX_MESHES_PER_SECTOR)
    {
        Engine_LogError("Sector declares %u meshes, the resident ring holds %d", meshCount, LEVEL_MAX_MESHES_PER_SECTOR);
        res->state = SECTOR_EMPTY;
        return;
    }

    const BakedMeshEntry* entries = reinterpret_cast<const BakedMeshEntry*>(dst + sizeof(SectorHeader));
    if (!Level_SpanFits(sizeof(SectorHeader), static_cast<uint64_t>(meshCount) * sizeof(BakedMeshEntry), blobSize) || !Internal_SectorMeshesAreSane(entries, meshCount, blobSize, "ASYNC_SECTOR"))
    {
        res->state = SECTOR_EMPTY;
        return;
    }

    for (uint32_t m = 0; m < meshCount; ++m)
    {
        const BakedMeshEntry& e = entries[m];
        Mesh& mesh = res->meshes[m];
        std::memset(&mesh, 0, sizeof(Mesh));
        mesh.vertexCount = static_cast<int>(e.vertexCount);
        mesh.vertices = reinterpret_cast<float*>(dst + e.vertsOffset);
        mesh.normals = e.normsOffset ? reinterpret_cast<float*>(dst + e.normsOffset) : nullptr;
        mesh.texcoords = e.uvsOffset ? reinterpret_cast<float*>(dst + e.uvsOffset) : nullptr;
        mesh.colors = e.colorsOffset ? reinterpret_cast<float*>(dst + e.colorsOffset) : nullptr;
        mesh.indices = nullptr;
        mesh.boundsCenter = Vector3{e.boundsCenter[0], e.boundsCenter[1], e.boundsCenter[2]};
        mesh.boundsRadius = e.boundsRadius;
        mesh.topology = static_cast<unsigned char>(e.topology);
        mesh.vertexComponents = 4; // PSEC positions are vec4

        const uint32_t matIdx = e.materialIndex;
        res->meshMaterial[m] = -1;
        if (matIdx < s_Level->info->materialCount && s_Level->materials)
        {
            const char* assetKey = s_Level->materials[matIdx].assetKey;
            if (assetKey[0] != '\0')
            {
                const int32_t handle = Engine_Resource_LoadAuto(assetKey);
                if (handle >= 0)
                {
                    Engine_Resource_Pin(handle);
                    res->meshMaterial[m] = handle;
                }
            }
        }
    }
    res->meshCount = meshCount;
    res->bounds.min = Vector3{hdr->aabbMin[0], hdr->aabbMin[1], hdr->aabbMin[2]};
    res->bounds.max = Vector3{hdr->aabbMax[0], hdr->aabbMax[1], hdr->aabbMax[2]};
    res->state = SECTOR_READY;

    Engine_LogInfo("Sector loaded: %s cell (%d, %d) at physical address %p (slot %d, %zu bytes)", 
                   res->isLod1 ? "LOD1" : "LOD0", res->cellX, res->cellZ, dst, res->arenaSlot, size);
}

static void Internal_LoadSector(int cx, int cz, SectorResident* res)
{
    const LevelInfoChunk* info = s_Level->info;
    res->cellX = static_cast<int16_t>(cx);
    res->cellZ = static_cast<int16_t>(cz);
    res->meshCount = 0;

    const LevelGridCell* cell = &s_Level->grid[cz * info->cellsX + cx];
    if (cell->sectorBytes == 0)
    {
        // Empty cell: resident but nothing to draw (avoids retrying every frame).
        res->bounds.min = Vector3{0, 0, 0};
        res->bounds.max = Vector3{0, 0, 0};
        res->state = SECTOR_READY;
        return;
    }

    char key[IO_FILE_MAX_PATH];
    if (res->isLod1)
        std::snprintf(key, sizeof(key), "%s/L%03d_%03d.SEC", s_Level->name, cx, cz);
    else
        std::snprintf(key, sizeof(key), "%s/S%03d_%03d.SEC", s_Level->name, cx, cz);

    SectorLoadContext* ctx = static_cast<SectorLoadContext*>(Engine_PoolAllocMain());
    if (!ctx)
    {
        Engine_LogError("Sector: OOM allocating load context for '%s'", key);
        res->state = SECTOR_EMPTY;
        return;
    }
    ctx->res        = res;
    ctx->generation = res->generation;

    res->state = SECTOR_LOADING;
    if (!Engine_IO_ReadAsync(key, Internal_OnSectorLoaded, ctx))
    {
        Engine_LogError("Sector: async read failed for '%s'", key);
        Engine_PoolFreeMain(ctx);
        res->state = SECTOR_EMPTY;
    }
}

bool Engine_Sector_Begin(const Level* level)
{
    // Not an error: a level without streamed geometry is a supported
    // configuration, so report success and stay empty.
    if (!Engine_Subsystem_IsEnabled(EngineSubsystem::Sector))
        return true;

    if (!level || !level->info)
        return false;
    s_Level = level;
    s_Primed = false;
    s_Lod1Pending = false;
    s_CenterCellX = -0x7fff;
    s_CenterCellZ = -0x7fff;
    for (int i = 0; i < LEVEL_RESIDENT_SECTORS; ++i)
    {
        s_Residents[i].cellX = -1;
        s_Residents[i].cellZ = -1;
        s_Residents[i].arenaSlot = static_cast<uint8_t>(LEVEL_SECTOR_SLOT_BASE + i);
        s_Residents[i].state = SECTOR_EMPTY;
        s_Residents[i].generation = 0;
        s_Residents[i].meshCount = 0;
        s_Residents[i].isLod1 = false;
    }
    for (int i = 0; i < MEM_BLOCK_LEVEL_LOD1_SLOTS; ++i)
    {
        s_Lod1Residents[i].cellX = -1;
        s_Lod1Residents[i].cellZ = -1;
        s_Lod1Residents[i].arenaSlot = static_cast<uint8_t>(i);
        s_Lod1Residents[i].state = SECTOR_EMPTY;
        s_Lod1Residents[i].generation = 0;
        s_Lod1Residents[i].meshCount = 0;
        s_Lod1Residents[i].isLod1 = true;
    }
    return true;
}

void Engine_Sector_End()
{
    for (int i = 0; i < LEVEL_RESIDENT_SECTORS; ++i)
    {
        if (s_Residents[i].state != SECTOR_EMPTY)
        {
            for (uint32_t m = 0; m < s_Residents[i].meshCount; ++m)
            {
                if (s_Residents[i].meshMaterial[m] >= 0)
                {
                    Engine_Resource_Unpin(s_Residents[i].meshMaterial[m]);
                    Engine_Resource_Unload(s_Residents[i].meshMaterial[m]);
                    s_Residents[i].meshMaterial[m] = -1;
                }
            }
        }
        s_Residents[i].state = SECTOR_EMPTY;
        s_Residents[i].meshCount = 0;
        s_Residents[i].cellX = -1;
        s_Residents[i].cellZ = -1;
    }
    s_Level = nullptr;
    s_Primed = false;
    s_Lod1Pending = false;
}

static bool Internal_IsResident(int cx, int cz)
{
    for (int i = 0; i < LEVEL_RESIDENT_SECTORS; ++i)
    {
        if (s_Residents[i].state != SECTOR_EMPTY && s_Residents[i].cellX == cx && s_Residents[i].cellZ == cz)
            return true;
    }
    return false;
}

void Engine_Sector_Update(float worldX, float worldZ)
{
    if (!s_Level || !s_Level->info)
        return;
    s_CenterX = worldX;
    s_CenterZ = worldZ;

    const float cellSize = s_Level->info->cellSize;
    
    bool cellChanged = false;
    int cx = s_CenterCellX;
    int cz = s_CenterCellZ;

    if (s_Primed)
    {
        const float ccx = s_Level->info->gridOriginX + (s_CenterCellX + 0.5f) * cellSize;
        const float ccz = s_Level->info->gridOriginZ + (s_CenterCellZ + 0.5f) * cellSize;
        const float threshold = (0.5f + LEVEL_SECTOR_HYSTERESIS) * cellSize;
        if (std::fabs(worldX - ccx) > threshold || std::fabs(worldZ - ccz) > threshold)
        {
            Internal_CellOf(worldX, worldZ, &cx, &cz);
            s_CenterCellX = cx;
            s_CenterCellZ = cz;
            cellChanged = true;
        }
    }
    else
    {
        Internal_CellOf(worldX, worldZ, &cx, &cz);
        s_CenterCellX = cx;
        s_CenterCellZ = cz;
        s_Primed = true;
        cellChanged = true;
    }

    if (cellChanged)
    {
        // Evict residents that fall outside the new 3x3 ring.
        for (int i = 0; i < LEVEL_RESIDENT_SECTORS; ++i)
        {
            SectorResident* r = &s_Residents[i];
            if (r->state == SECTOR_EMPTY)
                continue;
            if (std::abs(r->cellX - cx) > 1 || std::abs(r->cellZ - cz) > 1)
            {
                Engine_LogInfo("Sector unloaded: %s cell (%d, %d) from slot %d", r->isLod1 ? "LOD1" : "LOD0", r->cellX, r->cellZ, r->arenaSlot);
                ++r->generation;
                for (uint32_t m = 0; m < r->meshCount; ++m)
                {
                    if (r->meshMaterial[m] >= 0)
                    {
                        Engine_Resource_Unpin(r->meshMaterial[m]);
                        Engine_Resource_Unload(r->meshMaterial[m]);
                        r->meshMaterial[m] = -1;
                    }
                }
                r->state = SECTOR_EMPTY;
                r->meshCount = 0;
                r->cellX = -1;
                r->cellZ = -1;
            }
        }
    }

    // Load ring cells that are not yet resident (nearest-first: centre before edge).
    for (int ring = 0; ring <= 1; ++ring)
    {
        for (int dz = -1; dz <= 1; ++dz)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                if ((std::abs(dx) == 1 || std::abs(dz) == 1) != (ring == 1))
                    continue; // ring 0 = centre, ring 1 = the 8 surrounding cells
                const int tcx = cx + dx;
                const int tcz = cz + dz;
                if (!Internal_InGrid(tcx, tcz) || Internal_IsResident(tcx, tcz))
                    continue;
                for (int i = 0; i < LEVEL_RESIDENT_SECTORS; ++i)
                {
                    if (s_Residents[i].state == SECTOR_EMPTY)
                    {
                        Internal_LoadSector(tcx, tcz, &s_Residents[i]);
                        break;
                    }
                }
            }
        }
    }

    // --- Stream LOD1 based on VISI chunk ---
    if (s_Level->visiChunk)
    {
        if (cx >= 0 && cx < s_Level->info->cellsX && cz >= 0 && cz < s_Level->info->cellsZ)
        {
            const uint32_t cellIdx = static_cast<uint32_t>(cz) * s_Level->info->cellsX + static_cast<uint32_t>(cx);
            const uint32_t* offsets = reinterpret_cast<const uint32_t*>(s_Level->visiChunk + sizeof(VisiHeader));
            const VisiCell* list = reinterpret_cast<const VisiCell*>(s_Level->visiChunk + offsets[cellIdx]);
            const uint32_t numVis = list->numVisible;
            const uint16_t* visCells = reinterpret_cast<const uint16_t*>(list + 1);

            if (cellChanged)
                s_Lod1Pending = true;

            if (cellChanged)
            {
                // Mark all LOD1 residents as stale if they are no longer visible
                for (int i = 0; i < MEM_BLOCK_LEVEL_LOD1_SLOTS; ++i)
                {
                    if (s_Lod1Residents[i].state == SECTOR_EMPTY) continue;
                    bool isVisible = false;
                    for (uint32_t v = 0; v < numVis; ++v)
                    {
                        if (s_Lod1Residents[i].cellX == visCells[v*2] && s_Lod1Residents[i].cellZ == visCells[v*2+1])
                        {
                            isVisible = true;
                            break;
                        }
                    }
                    


                    // Evict if it's no longer visible
                    if (!isVisible)
                    {
                        Engine_LogInfo("Sector unloaded: %s cell (%d, %d) from slot %d", s_Lod1Residents[i].isLod1 ? "LOD1" : "LOD0", s_Lod1Residents[i].cellX, s_Lod1Residents[i].cellZ, s_Lod1Residents[i].arenaSlot);
                        ++s_Lod1Residents[i].generation;
                        for (uint32_t m = 0; m < s_Lod1Residents[i].meshCount; ++m)
                        {
                            if (s_Lod1Residents[i].meshMaterial[m] >= 0)
                            {
                                Engine_Resource_Unpin(s_Lod1Residents[i].meshMaterial[m]);
                                Engine_Resource_Unload(s_Lod1Residents[i].meshMaterial[m]);
                                s_Lod1Residents[i].meshMaterial[m] = -1;
                            }
                        }
                        s_Lod1Residents[i].state = SECTOR_EMPTY;
                        s_Lod1Residents[i].meshCount = 0;
                        s_Lod1Residents[i].cellX = -1;
                        s_Lod1Residents[i].cellZ = -1;
                    }
                }
            }
            
            int lod1Loads = 0;
            for (uint32_t v = 0; s_Lod1Pending && v < numVis; ++v)
            {
                int tcx = visCells[v*2];
                int tcz = visCells[v*2+1];
                
                // Skip if it's in the 3x3 LOD0 ring! We don't need LOD1 if LOD0 is loading/loaded.
                if (std::abs(tcx - cx) <= 1 && std::abs(tcz - cz) <= 1)
                    continue;
                    
                bool alreadyResident = false;
                for (int i = 0; i < MEM_BLOCK_LEVEL_LOD1_SLOTS; ++i)
                {
                    if (s_Lod1Residents[i].state != SECTOR_EMPTY && s_Lod1Residents[i].cellX == tcx && s_Lod1Residents[i].cellZ == tcz)
                    {
                        alreadyResident = true;
                        break;
                    }
                }
                if (alreadyResident) continue;
                // Find empty slot
                for (int i = 0; i < MEM_BLOCK_LEVEL_LOD1_SLOTS; ++i)
                {
                    if (s_Lod1Residents[i].state == SECTOR_EMPTY)
                    {
                        Internal_LoadSector(tcx, tcz, &s_Lod1Residents[i]);
                        ++lod1Loads;
                        break;
                    }
                }
                
                if (lod1Loads >= LEVEL_LOD1_LOADS_PER_FRAME)
                    break;
            }
            if (lod1Loads < LEVEL_LOD1_LOADS_PER_FRAME)
                s_Lod1Pending = false;
        }
    }
}

const SectorResident* Engine_Sector_GetResidents(uint32_t* outCount)
{
    if (outCount)
        *outCount = LEVEL_RESIDENT_SECTORS;
    return s_Residents;
}

const SectorResident* Engine_Sector_GetLod1Residents(uint32_t* outCount)
{
    if (outCount)
        *outCount = MEM_BLOCK_LEVEL_LOD1_SLOTS;
    return s_Lod1Residents;
}

bool Engine_Sector_IsResidentReady(const SectorResident* res)
{
    if (!res || res->state != SECTOR_READY)
        return false;
    for (uint32_t m = 0; m < res->meshCount; ++m)
    {
        const int32_t material = res->meshMaterial[m];
        if (material < 0)
            continue;
        if (!Engine_Resource_Get(material))
            return false;
        const int32_t albedo = Engine_Resource_GetMaterialTexture(material, MATERIAL_PBR_TEX_ALBEDO);
        if (albedo >= 0 && !Engine_Resource_Get(albedo))
            return false;
    }
    return true;
}

float Engine_Sector_Lod1Opacity(const SectorResident* res, float cameraX, float cameraZ, bool fade)
{
    if (!res || !res->isLod1 || !s_Level)
        return 1.0f;
    if (!Engine_Sector_IsLod0Ready(res->cellX, res->cellZ))
        return 1.0f;
    if (!fade)
        return 0.0f;

    const float cellSize = s_Level->info->cellSize;
    const float fadeStart = cellSize * LEVEL_LOD1_FADE_START_CELLS;
    const float fadeEnd = fadeStart + cellSize * LEVEL_LOD1_FADE_WIDTH_CELLS;
    const float dx = cameraX - res->bounds.min.x;
    const float dz = cameraZ - res->bounds.min.z;
    const float dist = std::sqrt(dx * dx + dz * dz);
    if (dist <= fadeStart)
        return 0.0f;
    if (dist >= fadeEnd)
        return 1.0f;
    return (dist - fadeStart) / (fadeEnd - fadeStart);
}

bool Engine_Sector_IsLod0Ready(int16_t cellX, int16_t cellZ)
{
    for (int i = 0; i < LEVEL_RESIDENT_SECTORS; ++i)
    {
        if (s_Residents[i].state == SECTOR_READY && s_Residents[i].cellX == cellX && s_Residents[i].cellZ == cellZ)
            return Engine_Sector_IsResidentReady(&s_Residents[i]);
    }
    return false;
}
