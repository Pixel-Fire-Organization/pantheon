#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Engine.h"
#include "level/EngineLevelSpan.h"
#include "level/EngineSector.h"

#include "core/EngineSubsystems.h"

typedef struct
{
    int16_t x;
    int16_t z;
} CellRef;

static const int RingRadius = LEVEL_RING_RADIUS_CELLS;
static const int RingCells = (2 * RingRadius + 1) * (2 * RingRadius + 1);

static_assert(LEVEL_RESIDENT_SECTORS >= RingCells, "the resident array must hold the whole ring");

static const Level* s_Level = nullptr;
static SectorResident s_Residents[LEVEL_RESIDENT_SECTORS];
static SectorResident s_Lod1Residents[MEM_BLOCK_LEVEL_LOD1_SLOTS];
static CellRef s_Lod1Desired[MEM_BLOCK_LEVEL_LOD1_SLOTS];
static int s_Lod1DesiredCount = 0;
static int s_Lod1ReportedDemand = -1;
static bool s_Lod1Pending = false;
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

static int Internal_RingDistance(int cx, int cz)
{
    const int dx = std::abs(cx - s_CenterCellX);
    const int dz = std::abs(cz - s_CenterCellZ);
    return dx > dz ? dx : dz;
}

static bool Internal_InRing(int cx, int cz) { return s_Primed && Internal_RingDistance(cx, cz) <= RingRadius; }

static void Internal_NearestGridCell(int cx, int cz, int* outCx, int* outCz)
{
    const LevelInfoChunk* info = s_Level->info;
    *outCx = cx < 0 ? 0 : (cx >= info->cellsX ? info->cellsX - 1 : cx);
    *outCz = cz < 0 ? 0 : (cz >= info->cellsZ ? info->cellsZ - 1 : cz);
}

static SectorResident* Internal_FindResident(SectorResident* array, int count, int cx, int cz)
{
    for (int i = 0; i < count; ++i)
    {
        if (array[i].state != SECTOR_EMPTY && array[i].cellX == cx && array[i].cellZ == cz)
            return &array[i];
    }
    return nullptr;
}

static SectorResident* Internal_FindLod0(int cx, int cz) { return Internal_FindResident(s_Residents, LEVEL_RESIDENT_SECTORS, cx, cz); }

static SectorResident* Internal_FindLod1(int cx, int cz) { return Internal_FindResident(s_Lod1Residents, MEM_BLOCK_LEVEL_LOD1_SLOTS, cx, cz); }

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

static void Internal_ReleaseResident(SectorResident* res)
{
    ++res->generation;
    for (uint32_t m = 0; m < res->meshCount; ++m)
    {
        if (res->meshMaterial[m] >= 0)
        {
            Engine_Resource_Unpin(res->meshMaterial[m]);
            Engine_Resource_Unload(res->meshMaterial[m]);
            res->meshMaterial[m] = -1;
        }
    }
    res->state = SECTOR_EMPTY;
    res->meshCount = 0;
    res->cellX = -1;
    res->cellZ = -1;
}

static void Internal_EvictResident(SectorResident* res)
{
    Engine_LogInfo("Sector unloaded: %s cell (%d, %d) from slot %d", res->isLod1 ? "LOD1" : "LOD0", res->cellX, res->cellZ, res->arenaSlot);
    Internal_ReleaseResident(res);
}

static void Internal_ReleaseAll()
{
    for (int i = 0; i < LEVEL_RESIDENT_SECTORS; ++i)
    {
        if (s_Residents[i].state != SECTOR_EMPTY)
            Internal_ReleaseResident(&s_Residents[i]);
    }
    for (int i = 0; i < MEM_BLOCK_LEVEL_LOD1_SLOTS; ++i)
    {
        if (s_Lod1Residents[i].state != SECTOR_EMPTY)
            Internal_ReleaseResident(&s_Lod1Residents[i]);
    }
}

typedef struct
{
    SectorResident* res;
    uint8_t generation;
} SectorLoadContext;

static void Internal_OnSectorLoaded(const void* data, size_t size, void* userData)
{
    SectorLoadContext* ctx = static_cast<SectorLoadContext*>(userData);
    SectorResident* res = ctx->res;
    const uint8_t gen = ctx->generation;
    Engine_PoolFreeMain(ctx);

    if (!s_Level || res->generation != gen || res->state != SECTOR_LOADING)
        return;

    if (!data)
    {
        Engine_LogError("Sector: read failed for %s cell (%d, %d)", res->isLod1 ? "LOD1" : "LOD0", res->cellX, res->cellZ);
        res->state = SECTOR_EMPTY;
        return;
    }

    const ArenaType arenaType = res->isLod1 ? ARENA_LEVEL_LOD1 : ARENA_LEVEL_DATA;
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
        mesh.vertexComponents = 4;

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

    Engine_LogInfo("Sector loaded: %s cell (%d, %d) at physical address %p (slot %d, %zu bytes)", res->isLod1 ? "LOD1" : "LOD0", res->cellX, res->cellZ, dst, res->arenaSlot, size);
}

static bool Internal_LoadSector(int cx, int cz, SectorResident* res)
{
    const LevelInfoChunk* info = s_Level->info;
    res->cellX = static_cast<int16_t>(cx);
    res->cellZ = static_cast<int16_t>(cz);
    res->meshCount = 0;

    const LevelGridCell* cell = &s_Level->grid[cz * info->cellsX + cx];
    if (cell->sectorBytes == 0)
    {
        res->bounds.min = Vector3{0, 0, 0};
        res->bounds.max = Vector3{0, 0, 0};
        res->state = SECTOR_READY;
        return true;
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
        return false;
    }
    ctx->res = res;
    ctx->generation = res->generation;

    res->state = SECTOR_LOADING;
    if (!Engine_IO_ReadAsync(key, Internal_OnSectorLoaded, ctx))
    {
        Engine_LogError("Sector: async read failed for '%s'", key);
        Engine_PoolFreeMain(ctx);
        res->state = SECTOR_EMPTY;
        return false;
    }
    return true;
}

bool Engine_Sector_Begin(const Level* level)
{
    if (!Engine_Subsystem_IsEnabled(EngineSubsystem::Sector))
        return true;

    if (!level || !level->info)
        return false;

    Internal_ReleaseAll();

    s_Level = level;
    s_Primed = false;
    s_Lod1Pending = false;
    s_Lod1DesiredCount = 0;
    s_Lod1ReportedDemand = -1;
    s_CenterCellX = -0x7fff;
    s_CenterCellZ = -0x7fff;
    for (int i = 0; i < LEVEL_RESIDENT_SECTORS; ++i)
    {
        s_Residents[i].cellX = -1;
        s_Residents[i].cellZ = -1;
        s_Residents[i].arenaSlot = static_cast<uint8_t>(LEVEL_SECTOR_SLOT_BASE + i);
        s_Residents[i].state = SECTOR_EMPTY;
        s_Residents[i].meshCount = 0;
        s_Residents[i].isLod1 = false;
    }
    for (int i = 0; i < MEM_BLOCK_LEVEL_LOD1_SLOTS; ++i)
    {
        s_Lod1Residents[i].cellX = -1;
        s_Lod1Residents[i].cellZ = -1;
        s_Lod1Residents[i].arenaSlot = static_cast<uint8_t>(i);
        s_Lod1Residents[i].state = SECTOR_EMPTY;
        s_Lod1Residents[i].meshCount = 0;
        s_Lod1Residents[i].isLod1 = true;
    }
    return true;
}

void Engine_Sector_End()
{
    Internal_ReleaseAll();
    s_Level = nullptr;
    s_Primed = false;
    s_Lod1Pending = false;
    s_Lod1DesiredCount = 0;
}

static const VisiCell* Internal_VisiList(int cx, int cz, const uint16_t** outCells)
{
    const uint32_t cellIdx = static_cast<uint32_t>(cz) * s_Level->info->cellsX + static_cast<uint32_t>(cx);
    const uint32_t* offsets = reinterpret_cast<const uint32_t*>(s_Level->visiChunk + sizeof(VisiHeader));
    const VisiCell* list = reinterpret_cast<const VisiCell*>(s_Level->visiChunk + offsets[cellIdx]);
    *outCells = reinterpret_cast<const uint16_t*>(list + 1);
    return list;
}

static bool Internal_Lod1Wanted(int cx, int cz)
{
    for (int i = 0; i < s_Lod1DesiredCount; ++i)
    {
        if (s_Lod1Desired[i].x == cx && s_Lod1Desired[i].z == cz)
            return true;
    }
    return false;
}

static bool Internal_Lod1Covers(int cx, int cz)
{
    const SectorResident* lod1 = Internal_FindLod1(cx, cz);
    return lod1 && lod1->state == SECTOR_READY && Engine_Sector_IsResidentReady(lod1);
}

static bool Internal_Lod1Protected(const SectorResident* lod1) { return Internal_InRing(lod1->cellX, lod1->cellZ) && !Engine_Sector_IsLod0Ready(lod1->cellX, lod1->cellZ); }

static void Internal_ComputeLod1Desired(int cx, int cz)
{
    s_Lod1DesiredCount = 0;
    if (!s_Level->visiChunk)
        return;

    int listX = 0;
    int listZ = 0;
    Internal_NearestGridCell(cx, cz, &listX, &listZ);
    const uint16_t* cells = nullptr;
    const VisiCell* list = Internal_VisiList(listX, listZ, &cells);

    int eligible = 0;
    for (uint32_t v = 0; v < list->numVisible; ++v)
    {
        const int tcx = cells[v * 2];
        const int tcz = cells[v * 2 + 1];
        if (Internal_InRing(tcx, tcz))
            continue;
        ++eligible;
        if (s_Lod1DesiredCount < MEM_BLOCK_LEVEL_LOD1_SLOTS)
        {
            s_Lod1Desired[s_Lod1DesiredCount].x = static_cast<int16_t>(tcx);
            s_Lod1Desired[s_Lod1DesiredCount].z = static_cast<int16_t>(tcz);
            ++s_Lod1DesiredCount;
        }
    }

    if (eligible != s_Lod1ReportedDemand)
    {
        s_Lod1ReportedDemand = eligible;
        if (eligible > MEM_BLOCK_LEVEL_LOD1_SLOTS)
            Engine_LogInfo("Sector: %d LOD1 cells are in reach of cell (%d, %d) and the tier holds %d; the farthest %d stay unstreamed", eligible, cx, cz, MEM_BLOCK_LEVEL_LOD1_SLOTS,
                           eligible - MEM_BLOCK_LEVEL_LOD1_SLOTS);
    }
}

static void Internal_UpdateLod0Residency()
{
    for (int i = 0; i < LEVEL_RESIDENT_SECTORS; ++i)
    {
        SectorResident* r = &s_Residents[i];
        if (r->state == SECTOR_EMPTY || Internal_InRing(r->cellX, r->cellZ))
            continue;
        if (r->state == SECTOR_READY && Internal_Lod1Wanted(r->cellX, r->cellZ) && !Internal_Lod1Covers(r->cellX, r->cellZ))
            continue;
        Internal_EvictResident(r);
    }
}

static void Internal_ReleaseRetiredLod0()
{
    for (int i = 0; i < LEVEL_RESIDENT_SECTORS; ++i)
    {
        SectorResident* r = &s_Residents[i];
        if (r->state == SECTOR_EMPTY || Internal_InRing(r->cellX, r->cellZ))
            continue;
        if (!Internal_Lod1Wanted(r->cellX, r->cellZ) || Internal_Lod1Covers(r->cellX, r->cellZ))
            Internal_EvictResident(r);
    }
}

static SectorResident* Internal_AcquireRingSlot()
{
    for (int i = 0; i < LEVEL_RESIDENT_SECTORS; ++i)
    {
        if (s_Residents[i].state == SECTOR_EMPTY)
            return &s_Residents[i];
    }

    SectorResident* victim = nullptr;
    int victimDistance = -1;
    for (int i = 0; i < LEVEL_RESIDENT_SECTORS; ++i)
    {
        SectorResident* r = &s_Residents[i];
        if (Internal_InRing(r->cellX, r->cellZ))
            continue;
        const int distance = Internal_RingDistance(r->cellX, r->cellZ);
        if (distance > victimDistance)
        {
            victim = r;
            victimDistance = distance;
        }
    }
    if (victim)
        Internal_EvictResident(victim);
    return victim;
}

static void Internal_FillRing(int cx, int cz)
{
    for (int ring = 0; ring <= RingRadius; ++ring)
    {
        for (int dz = -RingRadius; dz <= RingRadius; ++dz)
        {
            for (int dx = -RingRadius; dx <= RingRadius; ++dx)
            {
                const int distance = std::abs(dx) > std::abs(dz) ? std::abs(dx) : std::abs(dz);
                if (distance != ring)
                    continue;
                const int tcx = cx + dx;
                const int tcz = cz + dz;
                if (!Internal_InGrid(tcx, tcz) || Internal_FindLod0(tcx, tcz))
                    continue;
                SectorResident* slot = Internal_AcquireRingSlot();
                if (!slot)
                {
                    Engine_LogError("Sector: no free slot for cell (%d, %d)", tcx, tcz);
                    continue;
                }
                Internal_LoadSector(tcx, tcz, slot);
            }
        }
    }
}

static void Internal_StreamLod1(int cx, int cz, bool cellChanged)
{
    if (!s_Level->visiChunk)
        return;

    for (int i = 0; i < MEM_BLOCK_LEVEL_LOD1_SLOTS; ++i)
    {
        SectorResident* r = &s_Lod1Residents[i];
        if (r->state != SECTOR_EMPTY && Internal_InRing(r->cellX, r->cellZ) && Engine_Sector_IsLod0Ready(r->cellX, r->cellZ))
        {
            Internal_EvictResident(r);
            s_Lod1Pending = true;
        }
    }

    Internal_ReleaseRetiredLod0();

    if (cellChanged)
        s_Lod1Pending = true;
    if (!s_Lod1Pending)
        return;

    Internal_ComputeLod1Desired(cx, cz);

    for (int i = 0; i < MEM_BLOCK_LEVEL_LOD1_SLOTS; ++i)
    {
        SectorResident* r = &s_Lod1Residents[i];
        if (r->state == SECTOR_EMPTY || Internal_Lod1Wanted(r->cellX, r->cellZ) || Internal_Lod1Protected(r))
            continue;
        Internal_EvictResident(r);
    }

    int issued = 0;
    for (int d = 0; d < s_Lod1DesiredCount && issued < LEVEL_LOD1_LOADS_PER_FRAME; ++d)
    {
        const int tcx = s_Lod1Desired[d].x;
        const int tcz = s_Lod1Desired[d].z;
        if (Internal_FindLod1(tcx, tcz))
            continue;

        SectorResident* slot = nullptr;
        for (int i = 0; i < MEM_BLOCK_LEVEL_LOD1_SLOTS && !slot; ++i)
        {
            if (s_Lod1Residents[i].state == SECTOR_EMPTY)
                slot = &s_Lod1Residents[i];
        }
        if (!slot || !Internal_LoadSector(tcx, tcz, slot))
        {
            s_Lod1Pending = false;
            return;
        }
        ++issued;
    }
    s_Lod1Pending = issued >= LEVEL_LOD1_LOADS_PER_FRAME;
}

void Engine_Sector_Update(float worldX, float worldZ)
{
    if (!s_Level || !s_Level->info)
        return;

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
        Internal_ComputeLod1Desired(cx, cz);
        Internal_UpdateLod0Residency();
    }

    Internal_FillRing(cx, cz);
    Internal_StreamLod1(cx, cz, cellChanged);
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

bool Engine_Sector_GetCentreCell(int* outCellX, int* outCellZ)
{
    if (!s_Level || !s_Primed)
        return false;
    *outCellX = s_CenterCellX;
    *outCellZ = s_CenterCellZ;
    return true;
}

uint8_t Engine_Sector_GetCellResidency(int cellX, int cellZ)
{
    if (!s_Level || !s_Level->info || !Internal_InGrid(cellX, cellZ))
        return 0;

    int mask = 0;
    if (s_Level->grid[cellZ * s_Level->info->cellsX + cellX].sectorBytes != 0)
        mask |= SECTOR_CELL_GEOMETRY;

    const SectorResident* lod0 = Internal_FindLod0(cellX, cellZ);
    if (lod0)
        mask |= Engine_Sector_IsResidentReady(lod0) ? SECTOR_CELL_LOD0_READY : SECTOR_CELL_LOD0_LOADING;

    const SectorResident* lod1 = Internal_FindLod1(cellX, cellZ);
    if (lod1)
        mask |= Engine_Sector_IsResidentReady(lod1) ? SECTOR_CELL_LOD1_READY : SECTOR_CELL_LOD1_LOADING;

    return static_cast<uint8_t>(mask);
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
    const SectorResident* lod0 = Internal_FindLod0(cellX, cellZ);
    return lod0 && Engine_Sector_IsResidentReady(lod0);
}
