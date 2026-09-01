#include <cmath>
#include <cstdio>
#include <cstring>

#include "Engine.h"
#include "GameAPI.h"
#include "level/EngineLevelSpan.h"
#include "level/EngineSector.h"

static Level* s_CurrentLevel = nullptr;

const Level* Engine_Level_Current() { return s_CurrentLevel; }

void Engine_Level_ForgetCurrent()
{
    if (s_CurrentLevel)
        Engine_Level_Unload(s_CurrentLevel, false);
}

// The ENTS chunk is a header, a record array, a property array and a string
// table, all addressed by offsets the file chooses. Check every one of them
// against the chunk before the spawn walk reads any of them, and terminate the
// string table so a key or classname cannot run past it.
static bool Internal_EntitiesAreSane(const char* name, uint8_t* ents, size_t entsSize)
{
    const size_t headerBytes = 12;
    if (entsSize < headerBytes)
    {
        Engine_LogError("Level '%s': ENTS chunk is %zu bytes, shorter than its header", name, entsSize);
        return false;
    }

    uint32_t count, stringsOffset, stringsSize;
    std::memcpy(&count, ents + 0, 4);
    std::memcpy(&stringsOffset, ents + 4, 4);
    std::memcpy(&stringsSize, ents + 8, 4);

    const uint64_t recordBytes = static_cast<uint64_t>(count) * sizeof(LevelEntityRecord);
    if (!Level_SpanFits(headerBytes, recordBytes, entsSize))
    {
        Engine_LogError("Level '%s': ENTS declares %u entities, past the end of its chunk", name, count);
        return false;
    }

    const uint64_t propsOffset = headerBytes + recordBytes;
    if (stringsOffset < propsOffset || !Level_SpanFits(stringsOffset, stringsSize, entsSize))
    {
        Engine_LogError("Level '%s': ENTS string table at %u does not fit its %zu-byte chunk", name, stringsOffset, entsSize);
        return false;
    }

    const uint64_t propsBytes = stringsOffset - propsOffset;
    if (propsBytes % sizeof(LevelEntityProp) != 0)
    {
        Engine_LogError("Level '%s': ENTS property array is %llu bytes, not a whole number of properties", name, static_cast<unsigned long long>(propsBytes));
        return false;
    }
    const uint64_t propTotal = propsBytes / sizeof(LevelEntityProp);

    if (count > 0 && stringsSize == 0)
    {
        Engine_LogError("Level '%s': ENTS declares %u entities and no string table", name, count);
        return false;
    }
    if (stringsSize > 0)
        ents[stringsOffset + stringsSize - 1] = '\0';

    const LevelEntityRecord* records = reinterpret_cast<const LevelEntityRecord*>(ents + headerBytes);
    const LevelEntityProp* props = reinterpret_cast<const LevelEntityProp*>(ents + propsOffset);
    for (uint32_t r = 0; r < count; ++r)
    {
        const LevelEntityRecord& rec = records[r];
        if (rec.classnameOffset >= stringsSize)
        {
            Engine_LogError("Level '%s': entity %u names a classname outside the ENTS string table", name, r);
            return false;
        }
        const uint64_t last = static_cast<uint64_t>(rec.propFirst) + rec.propCount;
        if (last > propTotal)
        {
            Engine_LogError("Level '%s': entity %u claims properties %u..%llu of %llu", name, r, rec.propFirst, static_cast<unsigned long long>(last), static_cast<unsigned long long>(propTotal));
            return false;
        }
        for (uint16_t p = 0; p < rec.propCount; ++p)
        {
            const LevelEntityProp& lp = props[rec.propFirst + p];
            if (lp.keyOffset >= stringsSize || lp.valueOffset >= stringsSize)
            {
                Engine_LogError("Level '%s': entity %u property %u points outside the ENTS string table", name, r, p);
                return false;
            }
        }
    }
    return true;
}

/// Whether a FARF chunk's header, cluster array and frame array agree with each
/// other, with the chunk and with the material table. Checked where the view is
/// published, ahead of any consumer.
/// @param name The level, for the report.
/// @param farf The chunk payload.
/// @param farfSize Its size in bytes.
/// @param materialCount Entries in the level's material table.
/// @return Whether every atlas, cluster and frame reference is in range.
static bool Internal_FarfieldIsSane(const char* name, const uint8_t* farf, size_t farfSize, uint16_t materialCount)
{
    if (farfSize < sizeof(FarfieldHeader))
    {
        Engine_LogError("Level '%s': FARF chunk is %zu bytes, shorter than its header", name, farfSize);
        return false;
    }

    const FarfieldHeader* hdr = reinterpret_cast<const FarfieldHeader*>(farf);
    if (hdr->atlasCount > LEVEL_FARFIELD_MAX_ATLASES)
    {
        Engine_LogError("Level '%s': FARF names %u atlases, the header holds %d", name, hdr->atlasCount, LEVEL_FARFIELD_MAX_ATLASES);
        return false;
    }
    for (uint32_t a = 0; a < hdr->atlasCount; ++a)
    {
        if (hdr->atlasMaterial[a] >= materialCount)
        {
            Engine_LogError("Level '%s': FARF atlas %u names material %u of %u", name, a, hdr->atlasMaterial[a], materialCount);
            return false;
        }
    }
    if (hdr->azimuthCount == 0)
    {
        Engine_LogError("Level '%s': FARF declares no azimuth views per cluster", name);
        return false;
    }

    const uint64_t clusterBytes = static_cast<uint64_t>(hdr->clusterCount) * sizeof(FarfieldCluster);
    if (!Level_SpanFits(sizeof(FarfieldHeader), clusterBytes, farfSize))
    {
        Engine_LogError("Level '%s': FARF declares %u clusters, past the end of its chunk", name, hdr->clusterCount);
        return false;
    }
    const uint64_t frameBytes = farfSize - sizeof(FarfieldHeader) - clusterBytes;
    if (frameBytes % sizeof(FarfieldFrame) != 0)
    {
        Engine_LogError("Level '%s': FARF frame array is %llu bytes, not a whole number of frames", name, static_cast<unsigned long long>(frameBytes));
        return false;
    }
    const uint64_t frameTotal = frameBytes / sizeof(FarfieldFrame);

    const FarfieldCluster* clusters = reinterpret_cast<const FarfieldCluster*>(farf + sizeof(FarfieldHeader));
    for (uint32_t c = 0; c < hdr->clusterCount; ++c)
    {
        if (clusters[c].atlasIndex >= hdr->atlasCount)
        {
            Engine_LogError("Level '%s': FARF cluster %u samples atlas %u of %u", name, c, clusters[c].atlasIndex, hdr->atlasCount);
            return false;
        }
        const uint64_t last = static_cast<uint64_t>(clusters[c].firstFrame) + hdr->azimuthCount;
        if (last > frameTotal)
        {
            Engine_LogError("Level '%s': FARF cluster %u claims frames %u..%llu of %llu", name, c, clusters[c].firstFrame, static_cast<unsigned long long>(last),
                            static_cast<unsigned long long>(frameTotal));
            return false;
        }
    }
    return true;
}

// Read the compiled level core (.ps2l) into ARENA_LEVEL_DATA slot 0 and point the
// Level's chunk views at it. Returns false on any format/size error.
static bool Internal_ReadCore(Level* level)
{
    char coreKey[IO_FILE_MAX_PATH];
    std::snprintf(coreKey, sizeof(coreKey), "%s.PS2L", level->name);

    ArchiveLocator loc;
    if (!Engine_Archive_Find(coreKey, &loc))
    {
        Engine_LogError("Level '%s': core '%s' not found in archive", level->name, coreKey);
        return false;
    }

    const size_t cap = Engine_GetSlotCapacity(ARENA_LEVEL_DATA, 0);
    if (loc.size > cap)
    {
        Engine_LogError("Level '%s': core is %u bytes, exceeds core slot capacity %zu", level->name, loc.size, cap);
        return false;
    }

    uint8_t* core = static_cast<uint8_t*>(Engine_GetSlot(ARENA_LEVEL_DATA, 0));
    if (!core || !Engine_Archive_ReadSync(&loc, 0, core, loc.size))
    {
        Engine_LogError("Level '%s': failed to read core", level->name);
        return false;
    }

    const size_t blobSize = loc.size;
    if (blobSize < sizeof(LevelFileHeaderV2))
    {
        Engine_LogError("Level '%s': core is %zu bytes, shorter than a .ps2l header", level->name, blobSize);
        return false;
    }

    const LevelFileHeaderV2* hdr = reinterpret_cast<const LevelFileHeaderV2*>(core);
    if (hdr->magic != LEVEL_FILE_MAGIC || hdr->version != LEVEL_FILE_VERSION)
    {
        Engine_LogError("Level '%s': bad .ps2l magic/version", level->name);
        return false;
    }
    if (hdr->totalSize > blobSize)
    {
        Engine_LogError("Level '%s': core declares %u bytes, the archive entry holds %zu", level->name, hdr->totalSize, blobSize);
        return false;
    }

    const uint64_t tableBytes = static_cast<uint64_t>(hdr->chunkCount) * sizeof(LevelChunkEntry);
    if (!Level_SpanFits(sizeof(LevelFileHeaderV2), tableBytes, blobSize))
    {
        Engine_LogError("Level '%s': chunk table of %u entries does not fit the core", level->name, hdr->chunkCount);
        return false;
    }

    uint8_t* infoChunk = nullptr;
    uint8_t* materialsChunk = nullptr;
    uint8_t* entsChunk = nullptr;
    uint8_t* farfieldChunk = nullptr;
    uint32_t infoSize = 0;
    uint32_t materialsSize = 0;
    uint32_t gridSize = 0;
    uint32_t entsSize = 0;
    uint32_t farfieldSize = 0;

    const LevelChunkEntry* table = reinterpret_cast<const LevelChunkEntry*>(core + sizeof(LevelFileHeaderV2));
    for (uint32_t i = 0; i < hdr->chunkCount; ++i)
    {
        const uint32_t chunkOffset = table[i].offset;
        const uint32_t chunkSize = table[i].size;
        if (!Level_SpanFits(chunkOffset, chunkSize, blobSize))
        {
            Engine_LogError("Level '%s': chunk %u spans %u..%llu, past the %zu-byte core", level->name, i, chunkOffset, static_cast<unsigned long long>(static_cast<uint64_t>(chunkOffset) + chunkSize),
                            blobSize);
            return false;
        }
        if (!Level_OffsetAligned(chunkOffset, LEVEL_CHUNK_ALIGN))
        {
            Engine_LogError("Level '%s': chunk %u starts at %u, not a multiple of %u", level->name, i, chunkOffset, LEVEL_CHUNK_ALIGN);
            return false;
        }

        uint8_t* chunk = core + chunkOffset;
        switch (table[i].type)
        {
        case LEVEL_CHUNK_INFO:
            infoChunk = chunk;
            infoSize = chunkSize;
            break;
        case LEVEL_CHUNK_MATERIALS:
            materialsChunk = chunk;
            materialsSize = chunkSize;
            break;
        case LEVEL_CHUNK_GRID:
            level->grid = reinterpret_cast<const LevelGridCell*>(chunk);
            gridSize = chunkSize;
            break;
        case LEVEL_CHUNK_ENTITIES:
            entsChunk = chunk;
            entsSize = chunkSize;
            break;
        case LEVEL_CHUNK_FARFIELD:
            farfieldChunk = chunk;
            farfieldSize = chunkSize;
            break;
        case LEVEL_CHUNK_BSP:
            // Reserved for indoor BSP — forward-compat hook, nothing to do yet.
            break;
        default:
            Engine_LogInfo("Level '%s': skipping unknown chunk 0x%08X", level->name, table[i].type);
            break;
        }
    }

    if (!infoChunk || !level->grid)
    {
        Engine_LogError("Level '%s': missing INFO or SGRD chunk", level->name);
        return false;
    }
    if (infoSize < sizeof(LevelInfoChunk))
    {
        Engine_LogError("Level '%s': INFO chunk is %u bytes, shorter than the chunk layout", level->name, infoSize);
        return false;
    }

    infoChunk[sizeof(LevelInfoChunk::name) - 1] = '\0';
    const LevelInfoChunk* info = reinterpret_cast<const LevelInfoChunk*>(infoChunk);
    level->info = info;

    if (!(info->cellSize > 0.0f) || !std::isfinite(info->cellSize))
    {
        Engine_LogError("Level '%s': INFO cell size is %f; it divides every world position", level->name, static_cast<double>(info->cellSize));
        return false;
    }
    if (info->cellsX == 0 || info->cellsZ == 0)
    {
        Engine_LogError("Level '%s': INFO grid is %ux%u cells", level->name, info->cellsX, info->cellsZ);
        return false;
    }
    if (!std::isfinite(info->gridOriginX) || !std::isfinite(info->gridOriginZ))
    {
        Engine_LogError("Level '%s': INFO grid origin is not a finite position", level->name);
        return false;
    }

    const uint64_t gridBytes = static_cast<uint64_t>(info->cellsX) * info->cellsZ * sizeof(LevelGridCell);
    if (!Level_SpanFits(0, gridBytes, gridSize))
    {
        Engine_LogError("Level '%s': SGRD chunk is %u bytes, %ux%u cells need %llu", level->name, gridSize, info->cellsX, info->cellsZ, static_cast<unsigned long long>(gridBytes));
        return false;
    }

    if (info->materialCount > LEVEL_MAX_MATERIALS)
    {
        Engine_LogError("Level '%s': declares %u materials, the engine table holds %d", level->name, info->materialCount, LEVEL_MAX_MATERIALS);
        return false;
    }
    if (info->materialCount > 0)
    {
        const uint64_t materialBytes = static_cast<uint64_t>(info->materialCount) * sizeof(LevelMaterialEntry);
        if (!materialsChunk || !Level_SpanFits(0, materialBytes, materialsSize))
        {
            Engine_LogError("Level '%s': MATL chunk is %u bytes, %u materials need %llu", level->name, materialsSize, info->materialCount, static_cast<unsigned long long>(materialBytes));
            return false;
        }
        for (uint16_t i = 0; i < info->materialCount; ++i)
            materialsChunk[static_cast<size_t>(i) * sizeof(LevelMaterialEntry) + sizeof(LevelMaterialEntry::assetKey) - 1] = '\0';
        level->materials = reinterpret_cast<const LevelMaterialEntry*>(materialsChunk);
    }

    if (entsChunk)
    {
        if (!Internal_EntitiesAreSane(level->name, entsChunk, entsSize))
            return false;
        level->entsChunk = entsChunk;
    }

    if (farfieldChunk)
    {
        if (!Internal_FarfieldIsSane(level->name, farfieldChunk, farfieldSize, info->materialCount))
            return false;
        level->farfieldChunk = farfieldChunk;
    }

    return true;
}

// Load + pin every material texture. Async: handles are stored now and the
// textures stream in over the next frames (resolved at draw time, like models).
static void Internal_PinMaterials(Level* level)
{
    const uint16_t count = level->info->materialCount;
    for (uint16_t i = 0; i < LEVEL_MAX_MATERIALS; ++i)
        level->materialTex[i] = -1;

    for (uint16_t i = 0; i < count && i < LEVEL_MAX_MATERIALS; ++i)
    {
        const char* key = level->materials[i].assetKey;
        if (key[0] == '\0')
            continue;
        int32_t handle = Engine_Resource_LoadAuto(key);
        if (handle >= 0)
        {
            Engine_Resource_Pin(handle);
            level->materialTex[i] = handle;
        }
        else
        {
            Engine_LogError("Level '%s': failed to load material '%s'", level->name, key);
        }
    }
}

// Walk the ENTS chunk and hand each record to the game's spawn dispatcher.
static void Internal_SpawnEntities(Level* level)
{
    const uint8_t* base = level->entsChunk;
    if (!base)
        return;

    uint32_t count, stringsOffset, stringsSize;
    std::memcpy(&count, base + 0, 4);
    std::memcpy(&stringsOffset, base + 4, 4);
    std::memcpy(&stringsSize, base + 8, 4);
    (void)stringsSize;

    const LevelEntityRecord* records = reinterpret_cast<const LevelEntityRecord*>(base + 12);
    const LevelEntityProp* props = reinterpret_cast<const LevelEntityProp*>(base + 12 + count * sizeof(LevelEntityRecord));
    const char* strings = reinterpret_cast<const char*>(base + stringsOffset);

    for (uint32_t r = 0; r < count; ++r)
    {
        const LevelEntityRecord& rec = records[r];

        game::EntityProp propBuf[LEVEL_MAX_ENTITY_PROPS];
        uint16_t pcount = rec.propCount;
        if (pcount > LEVEL_MAX_ENTITY_PROPS)
            pcount = LEVEL_MAX_ENTITY_PROPS;
        for (uint16_t p = 0; p < pcount; ++p)
        {
            const LevelEntityProp& lp = props[rec.propFirst + p];
            propBuf[p].key = strings + lp.keyOffset;
            propBuf[p].value = strings + lp.valueOffset;
        }

        game::EntitySpawn spawn;
        spawn.classname = strings + rec.classnameOffset;
        spawn.props = propBuf;
        spawn.propCount = pcount;
        spawn.x = rec.origin[0];
        spawn.y = rec.origin[1];
        spawn.z = rec.origin[2];
        Engine_Game_DispatchSpawn(spawn);
    }
    Engine_LogInfo("Level '%s': spawned %u entities", level->name, count);
}

bool Engine_Level_Load(Level* level)
{
    if (!Engine_Subsystem_IsEnabled(EngineSubsystem::Level))
    {
        Engine_LogError("Level: the level subsystem was not requested; nothing can be loaded");
        return false;
    }

    if (!level)
        return false;

    level->info = nullptr;
    level->materials = nullptr;
    level->grid = nullptr;
    level->entsChunk = nullptr;
    level->farfieldChunk = nullptr;

    // The level's core, sectors and materials live in the master archive
    // alongside every other asset (see docs/subsystems/LEVEL.md) - Internal_
    // ReadCore and the sector loader (EngineSector.cpp) resolve their keys
    // through Engine_Archive_Find, which searches whatever is mounted.
    if (!Internal_ReadCore(level))
    {
        level->info = nullptr;
        level->materials = nullptr;
        level->grid = nullptr;
        level->entsChunk = nullptr;
        level->farfieldChunk = nullptr;
        return false;
    }

    Internal_PinMaterials(level);
    Internal_SpawnEntities(level);

    // Prime the resident sector ring around the grid centre.
    Engine_Sector_Begin(level);
    const float centerX = level->info->gridOriginX + level->info->cellsX * level->info->cellSize * 0.5f;
    const float centerZ = level->info->gridOriginZ + level->info->cellsZ * level->info->cellSize * 0.5f;
    Engine_Sector_Update(centerX, centerZ);

    s_CurrentLevel = level;
    Engine_LogInfo("Level '%s' loaded: %ux%u cells, %u materials", level->name, level->info->cellsX, level->info->cellsZ, level->info->materialCount);
    return true;
}

void Engine_Level_SetStreamingCenter(float worldX, float worldZ) { Engine_Sector_Update(worldX, worldZ); }

void Engine_Level_Unload(Level* level, bool keepPinned)
{
    Engine_Sector_End();

    if (level && !keepPinned)
    {
        const uint16_t count = level->info ? level->info->materialCount : 0;
        for (uint16_t i = 0; i < count && i < LEVEL_MAX_MATERIALS; ++i)
        {
            if (level->materialTex[i] >= 0)
            {
                Engine_Resource_Unpin(level->materialTex[i]);
                Engine_Resource_Unload(level->materialTex[i]);
                level->materialTex[i] = -1;
            }
        }
    }

    for (uint32_t i = 0; i < MEM_BLOCK_LEVEL_DATA_SLOTS; ++i)
        Engine_ClearSlot(ARENA_LEVEL_DATA, i);

    s_CurrentLevel = nullptr;
    Engine_LogInfo("Level unloaded (keepPinned=%s)", keepPinned ? "true" : "false");
}
