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
        const uint32_t offsets[3] = {e.vertsOffset, e.normsOffset, e.uvsOffset};
        const uint64_t strides[3] = {4 * sizeof(float), 3 * sizeof(float), 2 * sizeof(float)};
        for (int a = 0; a < 3; ++a)
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
    std::snprintf(key, sizeof(key), "%s/S%03d_%03d.SEC", s_Level->name, cx, cz);

    ArchiveLocator loc;
    if (!Engine_Archive_Find(key, &loc))
    {
        Engine_LogError("Sector: '%s' not found in archive", key);
        res->state = SECTOR_EMPTY;
        return;
    }

    const size_t cap = Engine_GetSlotCapacity(ARENA_LEVEL_DATA, res->arenaSlot);
    if (loc.size > cap)
    {
        Engine_LogError("Sector '%s' is %u bytes, exceeds slot capacity %zu", key, loc.size, cap);
        res->state = SECTOR_EMPTY;
        return;
    }

    uint8_t* dst = static_cast<uint8_t*>(Engine_GetSlot(ARENA_LEVEL_DATA, res->arenaSlot));
    if (!dst || !Engine_Archive_ReadSync(&loc, 0, dst, loc.size))
    {
        Engine_LogError("Sector: read failed for '%s'", key);
        res->state = SECTOR_EMPTY;
        return;
    }

    const size_t blobSize = loc.size;
    if (blobSize < sizeof(SectorHeader))
    {
        Engine_LogError("Sector '%s' is %zu bytes, shorter than a PSEC header", key, blobSize);
        res->state = SECTOR_EMPTY;
        return;
    }

    const SectorHeader* hdr = reinterpret_cast<const SectorHeader*>(dst);
    if (hdr->magic != LEVEL_SECTOR_MAGIC || hdr->version != LEVEL_SECTOR_VERSION)
    {
        Engine_LogError("Sector: bad PSEC header for '%s'", key);
        res->state = SECTOR_EMPTY;
        return;
    }

    const uint32_t meshCount = hdr->meshCount;
    if (meshCount > LEVEL_MAX_MESHES_PER_SECTOR)
    {
        Engine_LogError("Sector '%s' declares %u meshes, the resident ring holds %d", key, meshCount, LEVEL_MAX_MESHES_PER_SECTOR);
        res->state = SECTOR_EMPTY;
        return;
    }

    const BakedMeshEntry* entries = reinterpret_cast<const BakedMeshEntry*>(dst + sizeof(SectorHeader));
    if (!Level_SpanFits(sizeof(SectorHeader), static_cast<uint64_t>(meshCount) * sizeof(BakedMeshEntry), blobSize) || !Internal_SectorMeshesAreSane(entries, meshCount, blobSize, key))
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
        mesh.indices = nullptr;
        mesh.boundsCenter = Vector3{e.boundsCenter[0], e.boundsCenter[1], e.boundsCenter[2]};
        mesh.boundsRadius = e.boundsRadius;
        mesh.topology = static_cast<unsigned char>(e.topology);
        mesh.vertexComponents = 4; // PSEC positions are vec4

        const uint32_t matIdx = e.materialIndex;
        res->meshTexture[m] = (matIdx < LEVEL_MAX_MATERIALS) ? s_Level->materialTex[matIdx] : -1;
    }
    res->meshCount = meshCount;
    res->bounds.min = Vector3{hdr->aabbMin[0], hdr->aabbMin[1], hdr->aabbMin[2]};
    res->bounds.max = Vector3{hdr->aabbMax[0], hdr->aabbMax[1], hdr->aabbMax[2]};
    res->state = SECTOR_READY;
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
    s_CenterCellX = -0x7fff;
    s_CenterCellZ = -0x7fff;
    for (int i = 0; i < LEVEL_RESIDENT_SECTORS; ++i)
    {
        s_Residents[i].cellX = -1;
        s_Residents[i].cellZ = -1;
        s_Residents[i].arenaSlot = static_cast<uint8_t>(LEVEL_SECTOR_SLOT_BASE + i);
        s_Residents[i].state = SECTOR_EMPTY;
        s_Residents[i].meshCount = 0;
    }
    return true;
}

void Engine_Sector_End()
{
    for (int i = 0; i < LEVEL_RESIDENT_SECTORS; ++i)
    {
        s_Residents[i].state = SECTOR_EMPTY;
        s_Residents[i].meshCount = 0;
        s_Residents[i].cellX = -1;
        s_Residents[i].cellZ = -1;
    }
    s_Level = nullptr;
    s_Primed = false;
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
    // Hysteresis: keep the current centre cell until the camera moves more than
    // (0.5 + H) cells from that cell's centre in either axis — avoids thrash when
    // the camera hovers on a boundary.
    if (s_Primed)
    {
        const float ccx = s_Level->info->gridOriginX + (s_CenterCellX + 0.5f) * cellSize;
        const float ccz = s_Level->info->gridOriginZ + (s_CenterCellZ + 0.5f) * cellSize;
        const float threshold = (0.5f + LEVEL_SECTOR_HYSTERESIS) * cellSize;
        if (std::fabs(worldX - ccx) <= threshold && std::fabs(worldZ - ccz) <= threshold)
            return; // still within the current cell (+ hysteresis)
    }

    int cx, cz;
    Internal_CellOf(worldX, worldZ, &cx, &cz);
    s_CenterCellX = cx;
    s_CenterCellZ = cz;
    s_Primed = true;

    // Evict residents that fall outside the new 3x3 ring.
    for (int i = 0; i < LEVEL_RESIDENT_SECTORS; ++i)
    {
        SectorResident* r = &s_Residents[i];
        if (r->state == SECTOR_EMPTY)
            continue;
        if (std::abs(r->cellX - cx) > 1 || std::abs(r->cellZ - cz) > 1)
        {
            r->state = SECTOR_EMPTY;
            r->meshCount = 0;
            r->cellX = -1;
            r->cellZ = -1;
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
}

const SectorResident* Engine_Sector_GetResidents(uint32_t* outCount)
{
    if (outCount)
        *outCount = LEVEL_RESIDENT_SECTORS;
    return s_Residents;
}
