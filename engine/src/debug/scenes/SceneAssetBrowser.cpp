#include <cstdio>
#include <cstring>

#include "core/EngineCore.h"
#include "core/EngineSubsystems.h"
#include "debug/TestbedScene.h"
#include "graphics/FontFormat.h"
#include "graphics/ModelFormat.h"
#include "graphics/Types.h"
#include "level/EngineLevelFormat.h"
#include "platform/Platform.h"
#include "resources/EngineArchive.h"
#include "resources/EngineResource.h"
#include "ui/EngineUi.h"

namespace
{
    const int PANEL_MARGIN = 16;
    const int COLUMN_GAP = 8;
    const float PREVIEW_SIZE = 3.0f;
    const float PREVIEW_SPIN = 0.5f;
    const int IMAGE_PREVIEW_HEIGHT = 96;

    // Caps on what one selection caches for listing. Counts and totals a
    // summary reports come from the format's own header fields and stay exact;
    // these bound only how many individual rows can be listed afterwards, and a
    // list that reaches one says how many it is not showing.
    const uint32_t BROWSE_MAX_CHUNKS = 8;
    const uint32_t BROWSE_MAX_MESHES = 16;
    const uint32_t BROWSE_MAX_MATERIALS = 16;
    const uint32_t BROWSE_MAX_ENTITIES = 24;
    const uint32_t BROWSE_ENTITY_NAME_MAX = 32;
    const uint32_t BROWSE_GRID_BLOCK = 64;
    const uint32_t BROWSE_MAX_GRID_CELLS = 16384;
    const uint32_t BROWSE_HEX_BYTES = 32;
    const uint32_t BROWSE_HEX_PER_ROW = 8;

    /// What a selected archive entry turned out to be. An archive holds more
    /// than cooked assets -- a compiled level core and its streamed sectors are
    /// ordinary entries too -- so the browser recognises each by its own magic
    /// rather than reporting everything that is not a PS2A as a raw blob.
    enum class EntryKind : uint8_t
    {
        Unknown = 0, // no recognised magic
        Asset, // PS2A cooked asset container
        LevelCore, // PS2L compiled level core
        Sector, // PSEC streamed sector payload
    };

    /// A baked model's tables, read without loading the model.
    struct ModelSummary
    {
        bool valid;
        BakedModelHeader header;
        BakedMeshEntry meshes[BROWSE_MAX_MESHES];
        uint32_t meshesShown;
        BakedMaterialEntry materials[BROWSE_MAX_MATERIALS];
        uint32_t materialsShown;
    };

    /// A compiled level core: its chunk table and whatever each chunk holds.
    struct LevelSummary
    {
        bool valid;
        LevelFileHeaderV2 header;
        LevelChunkEntry chunks[BROWSE_MAX_CHUNKS];
        uint32_t chunksShown;

        bool haveInfo;
        LevelInfoChunk info;

        uint32_t cellsScanned;
        uint32_t cellsOccupied;
        uint32_t sectorBytesTotal;
        uint32_t sectorBytesLargest;
        bool gridTruncated;

        LevelMaterialEntry materials[LEVEL_MAX_MATERIALS];
        uint32_t materialsShown;

        bool haveEntities;
        uint32_t entityCount;
        uint32_t entityStringsSize;
        char entityNames[BROWSE_MAX_ENTITIES][BROWSE_ENTITY_NAME_MAX];
        float entityOrigins[BROWSE_MAX_ENTITIES][3];
        uint16_t entityProps[BROWSE_MAX_ENTITIES];
        uint32_t entitiesShown;

        bool haveFarfield;
        FarfieldHeader farfield;
    };

    /// One streamed sector payload: its header and mesh table.
    struct SectorSummary
    {
        bool valid;
        SectorHeader header;
        BakedMeshEntry meshes[LEVEL_MAX_MESHES_PER_SECTOR];
        uint32_t meshesShown;
        uint32_t totalVertices;
    };

    int32_t s_Mount = -1;
    uint32_t s_Entry = 0;
    bool s_HaveEntry = false;
    char s_EntryName[IO_FILE_MAX_PATH] = {0};
    ArchiveTocEntry s_EntryToc;

    EntryKind s_Kind = EntryKind::Unknown;
    int32_t s_ResolvedMount = -1;
    AssetFileHeader s_Header;
    ModelSummary s_Model;
    LevelSummary s_Level;
    SectorSummary s_Sector;
    uint8_t s_Hex[BROWSE_HEX_BYTES];
    uint32_t s_HexBytes = 0;

    int32_t s_Preview = -1;
    float s_Spin = 0.0f;

    const char* TypeName(uint32_t type)
    {
        switch (type)
        {
        case RES_TEXTURE:
            return "TEXTURE";
        case RES_MODEL:
            return "MODEL";
        case RES_SOUND:
            return "SOUND";
        case RES_FONT:
            return "FONT";
        case RES_THEME:
            return "THEME";
        default:
            break;
        }
        return "?";
    }

    const char* PixelFormatName(int format)
    {
        switch (static_cast<PixelFormat>(format))
        {
        case PixelFormat::RGBA32:
            return "RGBA32";
        case PixelFormat::RGBA16:
            return "RGBA16";
        case PixelFormat::PAL8:
            return "PAL8";
        }
        return "?";
    }

    const char* TopologyName(uint32_t topology) { return (topology == BAKED_TOPOLOGY_STRIP) ? "STRIP" : "LIST"; }

    /// Render a chunk type's four-character code as the characters it spells.
    /// @param fourcc The code, little-endian as it sits on disc.
    /// @param out Receives five bytes: the four characters and a terminator.
    void FourCcText(uint32_t fourcc, char* out)
    {
        for (int i = 0; i < 4; ++i)
        {
            const char c = static_cast<char>((fourcc >> (i * 8)) & 0xFFu);
            out[i] = (c >= 32 && c < 127) ? c : '?';
        }
        out[4] = '\0';
    }

    void ForgetEntry()
    {
        s_HaveEntry = false;
        s_Kind = EntryKind::Unknown;
        s_ResolvedMount = -1;
        s_EntryName[0] = '\0';
        s_HexBytes = 0;
        memset(&s_Header, 0, sizeof(s_Header));
        memset(&s_Model, 0, sizeof(s_Model));
        memset(&s_Level, 0, sizeof(s_Level));
        memset(&s_Sector, 0, sizeof(s_Sector));
        if (s_Preview >= 0)
        {
            Engine_Resource_Unload(s_Preview);
            s_Preview = -1;
        }
    }

    // --- Parsing ------------------------------------------------------------
    // Everything below runs once, when an entry is selected, and writes into
    // the summaries above. Nothing here runs per frame: these are archive
    // reads, and a browser that repeated them every frame would be reading the
    // disc every frame on the platform where that costs the most.

    void ParseModel(const ArchiveLocator& locator, uint32_t payloadOffset)
    {
        if (!Engine_Archive_ReadSync(&locator, payloadOffset, &s_Model.header, sizeof(s_Model.header)))
            return;
        if (s_Model.header.magic != BAKED_MODEL_MAGIC)
            return;

        const bool isV2 = (s_Model.header.version == BAKED_MODEL_VERSION);
        if (!isV2 && s_Model.header.version != BAKED_MODEL_VERSION_LEGACY)
            return;

        const uint32_t entrySize = isV2 ? static_cast<uint32_t>(sizeof(BakedMeshEntry)) : static_cast<uint32_t>(sizeof(BakedMeshEntryV1));
        const uint32_t meshTable = payloadOffset + static_cast<uint32_t>(sizeof(BakedModelHeader));
        const uint32_t materialTable = meshTable + s_Model.header.meshCount * entrySize;

        for (uint32_t i = 0; i < s_Model.header.meshCount && i < BROWSE_MAX_MESHES; ++i)
        {
            BakedMeshEntry entry;
            memset(&entry, 0, sizeof(entry));
            if (isV2)
            {
                if (!Engine_Archive_ReadSync(&locator, meshTable + i * entrySize, &entry, sizeof(entry)))
                    break;
            }
            else
            {
                BakedMeshEntryV1 legacy;
                if (!Engine_Archive_ReadSync(&locator, meshTable + i * entrySize, &legacy, sizeof(legacy)))
                    break;
                entry.vertexCount = legacy.vertexCount;
                entry.materialIndex = legacy.materialIndex;
                entry.vertsOffset = legacy.vertsOffset;
                entry.normsOffset = legacy.normsOffset;
                entry.uvsOffset = legacy.uvsOffset;
                entry.topology = BAKED_TOPOLOGY_LIST;
            }
            s_Model.meshes[s_Model.meshesShown++] = entry;
        }

        for (uint32_t i = 0; i < s_Model.header.materialCount && i < BROWSE_MAX_MATERIALS; ++i)
        {
            BakedMaterialEntry entry;
            if (!Engine_Archive_ReadSync(&locator, materialTable + i * static_cast<uint32_t>(sizeof(entry)), &entry, sizeof(entry)))
                break;
            s_Model.materials[s_Model.materialsShown++] = entry;
        }

        s_Model.valid = true;
    }

    void ParseLevelGrid(const ArchiveLocator& locator, const LevelChunkEntry& chunk)
    {
        if (!s_Level.haveInfo)
            return;

        const uint32_t cells = static_cast<uint32_t>(s_Level.info.cellsX) * static_cast<uint32_t>(s_Level.info.cellsZ);
        uint32_t remaining = cells;
        if (remaining > BROWSE_MAX_GRID_CELLS)
        {
            remaining = BROWSE_MAX_GRID_CELLS;
            s_Level.gridTruncated = true;
        }

        LevelGridCell block[BROWSE_GRID_BLOCK];
        uint32_t index = 0;
        while (index < remaining)
        {
            uint32_t count = remaining - index;
            if (count > BROWSE_GRID_BLOCK)
                count = BROWSE_GRID_BLOCK;

            const uint32_t offset = chunk.offset + index * static_cast<uint32_t>(sizeof(LevelGridCell));
            if (!Engine_Archive_ReadSync(&locator, offset, block, count * static_cast<uint32_t>(sizeof(LevelGridCell))))
                break;

            for (uint32_t i = 0; i < count; ++i)
            {
                ++s_Level.cellsScanned;
                if (block[i].sectorBytes == 0)
                    continue;
                ++s_Level.cellsOccupied;
                s_Level.sectorBytesTotal += block[i].sectorBytes;
                if (block[i].sectorBytes > s_Level.sectorBytesLargest)
                    s_Level.sectorBytesLargest = block[i].sectorBytes;
            }
            index += count;
        }
    }

    void ParseLevelEntities(const ArchiveLocator& locator, const LevelChunkEntry& chunk)
    {
        uint32_t table[3];
        if (!Engine_Archive_ReadSync(&locator, chunk.offset, table, sizeof(table)))
            return;

        s_Level.haveEntities = true;
        s_Level.entityCount = table[0];
        s_Level.entityStringsSize = table[2];

        const uint32_t stringsOffset = chunk.offset + table[1];
        const uint32_t records = chunk.offset + static_cast<uint32_t>(sizeof(table));

        for (uint32_t i = 0; i < s_Level.entityCount && i < BROWSE_MAX_ENTITIES; ++i)
        {
            LevelEntityRecord record;
            if (!Engine_Archive_ReadSync(&locator, records + i * static_cast<uint32_t>(sizeof(record)), &record, sizeof(record)))
                break;

            char* name = s_Level.entityNames[s_Level.entitiesShown];
            memset(name, 0, BROWSE_ENTITY_NAME_MAX);
            if (!Engine_Archive_ReadSync(&locator, stringsOffset + record.classnameOffset, name, BROWSE_ENTITY_NAME_MAX - 1))
            {
                // A classname running past the end of the span is the only way
                // this read fails, so the record is still worth listing.
                snprintf(name, BROWSE_ENTITY_NAME_MAX, "?");
            }
            name[BROWSE_ENTITY_NAME_MAX - 1] = '\0';

            s_Level.entityOrigins[s_Level.entitiesShown][0] = record.origin[0];
            s_Level.entityOrigins[s_Level.entitiesShown][1] = record.origin[1];
            s_Level.entityOrigins[s_Level.entitiesShown][2] = record.origin[2];
            s_Level.entityProps[s_Level.entitiesShown] = record.propCount;
            ++s_Level.entitiesShown;
        }
    }

    void ParseLevel(const ArchiveLocator& locator)
    {
        for (uint32_t i = 0; i < s_Level.header.chunkCount && i < BROWSE_MAX_CHUNKS; ++i)
        {
            LevelChunkEntry chunk;
            const uint32_t offset = static_cast<uint32_t>(sizeof(LevelFileHeaderV2)) + i * static_cast<uint32_t>(sizeof(chunk));
            if (!Engine_Archive_ReadSync(&locator, offset, &chunk, sizeof(chunk)))
                break;
            s_Level.chunks[s_Level.chunksShown++] = chunk;
        }

        // INFO first: the grid chunk cannot be walked without the cell counts
        // it carries, and the chunk table does not promise an order.
        for (uint32_t i = 0; i < s_Level.chunksShown; ++i)
        {
            if (s_Level.chunks[i].type != LEVEL_CHUNK_INFO)
                continue;
            if (Engine_Archive_ReadSync(&locator, s_Level.chunks[i].offset, &s_Level.info, sizeof(s_Level.info)))
                s_Level.haveInfo = true;
            break;
        }

        for (uint32_t i = 0; i < s_Level.chunksShown; ++i)
        {
            const LevelChunkEntry& chunk = s_Level.chunks[i];
            switch (chunk.type)
            {
            case LEVEL_CHUNK_MATERIALS:
                {
                    uint32_t count = s_Level.haveInfo ? s_Level.info.materialCount : 0;
                    if (count > LEVEL_MAX_MATERIALS)
                        count = LEVEL_MAX_MATERIALS;
                    for (uint32_t m = 0; m < count; ++m)
                    {
                        const uint32_t offset = chunk.offset + m * static_cast<uint32_t>(sizeof(LevelMaterialEntry));
                        if (!Engine_Archive_ReadSync(&locator, offset, &s_Level.materials[s_Level.materialsShown], sizeof(LevelMaterialEntry)))
                            break;
                        s_Level.materials[s_Level.materialsShown].assetKey[sizeof(s_Level.materials[0].assetKey) - 1] = '\0';
                        ++s_Level.materialsShown;
                    }
                    break;
                }
            case LEVEL_CHUNK_GRID:
                ParseLevelGrid(locator, chunk);
                break;
            case LEVEL_CHUNK_ENTITIES:
                ParseLevelEntities(locator, chunk);
                break;
            case LEVEL_CHUNK_FARFIELD:
                s_Level.haveFarfield = Engine_Archive_ReadSync(&locator, chunk.offset, &s_Level.farfield, sizeof(s_Level.farfield));
                break;
            default:
                break;
            }
        }

        s_Level.valid = true;
    }

    void ParseSector(const ArchiveLocator& locator)
    {
        uint32_t count = s_Sector.header.meshCount;
        if (count > LEVEL_MAX_MESHES_PER_SECTOR)
            count = LEVEL_MAX_MESHES_PER_SECTOR;

        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t offset = static_cast<uint32_t>(sizeof(SectorHeader)) + i * static_cast<uint32_t>(sizeof(BakedMeshEntry));
            if (!Engine_Archive_ReadSync(&locator, offset, &s_Sector.meshes[s_Sector.meshesShown], sizeof(BakedMeshEntry)))
                break;
            s_Sector.totalVertices += s_Sector.meshes[s_Sector.meshesShown].vertexCount;
            ++s_Sector.meshesShown;
        }

        s_Sector.valid = true;
    }

    /// Recognise the selected entry and cache everything worth showing about
    /// it. An entry whose magic matches nothing is still described, down to the
    /// first bytes it actually holds.
    void InspectEntry()
    {
        ArchiveLocator locator;
        if (!Engine_Archive_Find(s_EntryName, &locator))
            return;

        s_ResolvedMount = locator.archive;
        s_HexBytes = (locator.size < BROWSE_HEX_BYTES) ? locator.size : BROWSE_HEX_BYTES;
        if (s_HexBytes > 0 && !Engine_Archive_ReadSync(&locator, 0, s_Hex, s_HexBytes))
            s_HexBytes = 0;

        uint32_t magic = 0;
        if (locator.size < sizeof(magic) || !Engine_Archive_ReadSync(&locator, 0, &magic, sizeof(magic)))
            return;

        if (magic == RES_ASSET_MAGIC)
        {
            if (locator.size < sizeof(AssetFileHeader) || !Engine_Archive_ReadSync(&locator, 0, &s_Header, sizeof(s_Header)))
                return;
            s_Kind = EntryKind::Asset;
            if (s_Header.type == RES_MODEL)
                ParseModel(locator, static_cast<uint32_t>(sizeof(AssetFileHeader)));
            return;
        }

        if (magic == LEVEL_FILE_MAGIC)
        {
            if (!Engine_Archive_ReadSync(&locator, 0, &s_Level.header, sizeof(s_Level.header)))
                return;
            s_Kind = EntryKind::LevelCore;
            ParseLevel(locator);
            return;
        }

        if (magic == LEVEL_SECTOR_MAGIC)
        {
            if (!Engine_Archive_ReadSync(&locator, 0, &s_Sector.header, sizeof(s_Sector.header)))
                return;
            s_Kind = EntryKind::Sector;
            ParseSector(locator);
        }
    }

    void SelectEntry(int32_t mount, uint32_t index)
    {
        ForgetEntry();
        if (!Engine_Archive_GetEntry(mount, index, &s_EntryToc, s_EntryName, sizeof(s_EntryName)))
            return;
        s_Mount = mount;
        s_Entry = index;
        s_HaveEntry = true;
        InspectEntry();
    }

    // --- Left column --------------------------------------------------------

    void DrawMounts(int x, int y, int w, int h)
    {
        Ui_BeginPanel("ARCHIVES", x, y, w, h);

        int mounted = 0;
        for (int32_t slot = 0; slot < ARCH_MAX_MOUNTED; ++slot)
        {
            ArchiveMountInfo info;
            if (!Engine_Archive_GetMount(slot, &info))
                continue;
            ++mounted;

            char row[64];
            snprintf(row, sizeof(row), "SLOT %d  %u ENTRIES", static_cast<int>(slot), static_cast<unsigned>(info.entryCount));
            if (Ui_Selectable(row, slot == s_Mount))
            {
                ForgetEntry();
                s_Mount = slot;
            }

            if (slot != s_Mount)
                continue;

            char value[48];
            snprintf(value, sizeof(value), "%llu KB", static_cast<unsigned long long>(info.payloadBytes / 1024u));
            Ui_LabelValue("PAYLOAD", value);
            Ui_LabelPath(info.path, UiColor::TextDim);
        }

        if (mounted == 0)
            Testbed_DrawUnavailable("NO ARCHIVE MOUNTED");

        Ui_EndPanel();
    }

    void DrawEntries(int x, int y, int w, int h)
    {
        Ui_BeginPanel("ENTRIES", x, y, w, h);

        ArchiveMountInfo info;
        if (s_Mount < 0 || !Engine_Archive_GetMount(s_Mount, &info))
        {
            Ui_LabelColored("SELECT AN ARCHIVE", UiColor::TextDim);
            Ui_EndPanel();
            return;
        }

        if (!Ui_BeginScroll("entries", Ui_ContentHeight()))
        {
            Ui_EndPanel();
            return;
        }

        const UiStyle& style = Ui_GetStyle();
        const int sizeColumn = Ui_TextWidth(style.textScale, "9999K") + style.rowPadding * 2;
        const int widths[] = {0, sizeColumn};
        if (Ui_BeginTable("list", widths, 2))
        {
            const char* const headers[] = {"NAME", "SIZE"};
            Ui_TableHeader(headers);
            for (uint32_t i = 0; i < info.entryCount; ++i)
            {
                ArchiveTocEntry toc;
                char name[IO_FILE_MAX_PATH];
                if (!Engine_Archive_GetEntry(s_Mount, i, &toc, name, sizeof(name)))
                    continue;

                Ui_PushIdIndex(static_cast<int>(i));
                const bool activated = Ui_TableRow(name, s_HaveEntry && i == s_Entry);
                Ui_PopId();
                if (activated)
                    SelectEntry(s_Mount, i);

                Ui_TableCell(name, UiAlign::Left, UiColor::Text);
                char size[16];
                snprintf(size, sizeof(size), "%uK", static_cast<unsigned>(toc.size / 1024u));
                Ui_TableCell(size, UiAlign::Right, UiColor::TextDim);
            }
            Ui_EndTable();
        }
        Ui_EndScroll();
        Ui_EndPanel();
    }

    // --- Preview ------------------------------------------------------------

    void LoadPreview()
    {
        char path[IO_FILE_MAX_PATH];
        if (Engine_BuildPath(Engine_GetResourceLocationToken(), s_EntryName, path, sizeof(path)))
            s_Preview = Engine_Resource_LoadAuto(path);
    }

    void UnloadPreview()
    {
        Engine_Resource_Unload(s_Preview);
        s_Preview = -1;
    }

    bool PreviewReady(ResourceInfo* outInfo) { return s_Preview >= 0 && Engine_Resource_GetInfo(s_Preview, outInfo) && outInfo->state == RES_STATE_READY; }

    /// The load/unload row every loadable type shares, so the difference
    /// between an unloaded, loading and ready asset reads the same everywhere.
    /// @return True while the preview is loaded and ready to be described.
    bool DrawPreviewControls(ResourceInfo* outInfo)
    {
        if (s_Preview < 0)
        {
            if (Ui_Button("LOAD"))
                LoadPreview();
            return false;
        }

        const bool ready = PreviewReady(outInfo);
        if (!ready)
            Ui_LabelColored("LOADING...", UiColor::TextDim);
        if (Ui_Button("UNLOAD"))
            UnloadPreview();
        return ready;
    }

    /// The live figures the resource table keeps for anything resident,
    /// whatever its type -- what a budget overrun or a stuck reference count
    /// actually shows up as.
    void DrawResidencyRows(const ResourceInfo& info)
    {
        char value[48];
        snprintf(value, sizeof(value), "H%d", static_cast<int>(s_Preview));
        Ui_LabelValue("HANDLE", value);
        snprintf(value, sizeof(value), "%u", static_cast<unsigned>(info.refCount));
        Ui_LabelValue("REFS", value);
        Ui_LabelValue("PINNED", info.pinned ? "YES" : "NO");
    }

    // --- Cooked asset bodies ------------------------------------------------

    void DrawTextureBody()
    {
        ResourceInfo info;
        const bool ready = DrawPreviewControls(&info);
        if (!ready)
            return;

        char value[48];
        DrawResidencyRows(info);
        snprintf(value, sizeof(value), "%dX%d", static_cast<int>(info.width), static_cast<int>(info.height));
        Ui_LabelValue("DIMENSIONS", value);
        snprintf(value, sizeof(value), "%u B", static_cast<unsigned>(info.textureBytes));
        Ui_LabelValue("VRAM COST", value);

        const Texture2D* texture = static_cast<const Texture2D*>(Engine_Resource_Get(s_Preview));
        if (texture)
        {
            Ui_LabelValue("FORMAT", PixelFormatName(texture->format));
            snprintf(value, sizeof(value), "%u", static_cast<unsigned>(texture->id));
            Ui_LabelValue("BACKEND ID", value);
        }

        Ui_Image(s_Preview, IMAGE_PREVIEW_HEIGHT);
    }

    void DrawModelTables()
    {
        if (!s_Model.valid)
        {
            Ui_LabelColored("MODEL TABLES UNREADABLE", UiColor::TextWarn);
            return;
        }

        char value[64];
        Ui_LabelValue("BAKED VERSION", (s_Model.header.version == BAKED_MODEL_VERSION) ? "2 (VEC4)" : "1 (VEC3)");
        snprintf(value, sizeof(value), "%u", static_cast<unsigned>(s_Model.header.meshCount));
        Ui_LabelValue("MESHES", value);
        snprintf(value, sizeof(value), "%u", static_cast<unsigned>(s_Model.header.materialCount));
        Ui_LabelValue("MATERIALS", value);

        if (s_Model.meshesShown > 0 && Ui_BeginTree("MESH TABLE", false))
        {
            for (uint32_t i = 0; i < s_Model.meshesShown; ++i)
            {
                const BakedMeshEntry& mesh = s_Model.meshes[i];
                char label[16];
                snprintf(label, sizeof(label), "MESH %u", static_cast<unsigned>(i));
                snprintf(value, sizeof(value), "%u V  M%u  %s", static_cast<unsigned>(mesh.vertexCount), static_cast<unsigned>(mesh.materialIndex), TopologyName(mesh.topology));
                Ui_LabelValue(label, value);

                snprintf(value, sizeof(value), "%s%s  R %.1f", mesh.normsOffset ? "N" : "-", mesh.uvsOffset ? "UV" : "-", mesh.boundsRadius);
                Ui_LabelValue("  ATTRS", value);
            }
            if (s_Model.header.meshCount > s_Model.meshesShown)
            {
                snprintf(value, sizeof(value), "%u MORE NOT LISTED", static_cast<unsigned>(s_Model.header.meshCount - s_Model.meshesShown));
                Ui_LabelColored(value, UiColor::TextDim);
            }
            Ui_EndTree();
        }

        if (s_Model.materialsShown > 0 && Ui_BeginTree("MATERIAL TABLE", false))
        {
            for (uint32_t i = 0; i < s_Model.materialsShown; ++i)
            {
                char label[16];
                snprintf(label, sizeof(label), "MAT %u", static_cast<unsigned>(i));
                const uint32_t ref = s_Model.materials[i].diffuseTexRef;
                if (ref == BAKED_MODEL_TEXREF_NONE)
                    Ui_LabelValue(label, "UNTEXTURED");
                else
                {
                    snprintf(value, sizeof(value), "DEP %u", static_cast<unsigned>(ref));
                    Ui_LabelValue(label, value);
                }
            }
            Ui_EndTree();
        }
    }

    void DrawModelBody()
    {
        DrawModelTables();
        Ui_Separator();

        ResourceInfo info;
        const bool ready = DrawPreviewControls(&info);
        if (!ready)
            return;

        const Model* model = static_cast<const Model*>(Engine_Resource_Get(s_Preview));
        if (!model)
            return;

        char value[64];
        DrawResidencyRows(info);

        int totalVertices = 0;
        for (int i = 0; i < model->meshCount; ++i)
            totalVertices += model->meshes[i].vertexCount;
        snprintf(value, sizeof(value), "%d", totalVertices);
        Ui_LabelValue("VERTICES", value);
        snprintf(value, sizeof(value), "%.1f", model->boundsRadius);
        Ui_LabelValue("BOUNDS RADIUS", value);
        snprintf(value, sizeof(value), "%.0f %.0f %.0f", model->boundsCenter.x, model->boundsCenter.y, model->boundsCenter.z);
        Ui_LabelValue("BOUNDS CENTRE", value);

        if (Ui_BeginTree("RESOLVED TEXTURES", false))
        {
            for (int i = 0; i < model->materialCount && i < static_cast<int>(BROWSE_MAX_MATERIALS); ++i)
            {
                char label[16];
                snprintf(label, sizeof(label), "MAT %d", i);

                const int32_t texId = model->materials[i].maps[MATERIAL_MAP_DIFFUSE].textureResourceId;
                if (texId < 0)
                {
                    Ui_LabelValue(label, "NONE");
                    continue;
                }
                ResourceInfo texInfo;
                if (Engine_Resource_GetInfo(texId, &texInfo) && texInfo.state == RES_STATE_READY)
                    snprintf(value, sizeof(value), "H%d %dX%d", static_cast<int>(texId), static_cast<int>(texInfo.width), static_cast<int>(texInfo.height));
                else
                    snprintf(value, sizeof(value), "H%d LOADING", static_cast<int>(texId));
                Ui_LabelValue(label, value);
            }
            Ui_EndTree();
        }

        Ui_Separator();
        Renderable3D what;
        what.modelId = s_Preview;
        what.rotation = Vector3{0.3f, s_Spin, 0.0f};
        what.scale = Vector3{PREVIEW_SIZE, PREVIEW_SIZE, PREVIEW_SIZE};
        Camera3D camera;
        camera.position = Vector3{0.0f, 2.0f, 9.0f};
        camera.target = Vector3{0.0f, 0.0f, 0.0f};
        camera.up = Vector3{0.0f, 1.0f, 0.0f};
        camera.fovy = 45.0f;
        camera.projection = CAMERA_PERSPECTIVE;
        Ui_Image3D(what, camera, IMAGE_PREVIEW_HEIGHT * 2, Color3{0.06f, 0.07f, 0.10f});
    }

    void DrawFontBody()
    {
        ResourceInfo info;
        const bool ready = DrawPreviewControls(&info);
        if (!ready)
            return;

        const Font* font = static_cast<const Font*>(Engine_Resource_Get(s_Preview));
        if (!font)
            return;

        char value[64];
        DrawResidencyRows(info);
        snprintf(value, sizeof(value), "%u", static_cast<unsigned>(font->glyphCount));
        Ui_LabelValue("GLYPHS", value);
        snprintf(value, sizeof(value), "%u..%u", static_cast<unsigned>(font->firstCode), static_cast<unsigned>(font->firstCode + font->glyphCount));
        Ui_LabelValue("CODEPOINTS", value);
        snprintf(value, sizeof(value), "%u", static_cast<unsigned>(font->missingIndex));
        Ui_LabelValue("MISSING INDEX", value);
        snprintf(value, sizeof(value), "%u", static_cast<unsigned>(font->cellCount));
        Ui_LabelValue("ICON CELLS", value);
        snprintf(value, sizeof(value), "%uX%u", static_cast<unsigned>(font->atlasWidth), static_cast<unsigned>(font->atlasHeight));
        Ui_LabelValue("ATLAS", value);
        snprintf(value, sizeof(value), "%u", static_cast<unsigned>(font->lineHeight));
        Ui_LabelValue("LINE HEIGHT", value);
        snprintf(value, sizeof(value), "%u", static_cast<unsigned>(font->baseline));
        Ui_LabelValue("BASELINE", value);
        snprintf(value, sizeof(value), "%u", static_cast<unsigned>(font->spaceAdvance));
        Ui_LabelValue("SPACE ADVANCE", value);
        Ui_LabelValue("MONOSPACED", (font->flags & FONT_FLAG_MONOSPACED) ? "YES" : "NO");

        if (font->glyphs && font->glyphCount > 0)
        {
            uint8_t minAdvance = 255;
            uint8_t maxAdvance = 0;
            uint8_t tallest = 0;
            for (uint16_t i = 0; i < font->glyphCount; ++i)
            {
                const FontGlyph& glyph = font->glyphs[i];
                if (glyph.advance < minAdvance)
                    minAdvance = glyph.advance;
                if (glyph.advance > maxAdvance)
                    maxAdvance = glyph.advance;
                if (glyph.h > tallest)
                    tallest = glyph.h;
            }
            snprintf(value, sizeof(value), "%u..%u", static_cast<unsigned>(minAdvance), static_cast<unsigned>(maxAdvance));
            Ui_LabelValue("ADVANCE", value);
            snprintf(value, sizeof(value), "%u", static_cast<unsigned>(tallest));
            Ui_LabelValue("TALLEST CELL", value);
        }

        if (font->atlasResourceId >= 0)
        {
            snprintf(value, sizeof(value), "H%d", static_cast<int>(font->atlasResourceId));
            Ui_LabelValue("ATLAS HANDLE", value);
            Ui_Image(font->atlasResourceId, IMAGE_PREVIEW_HEIGHT);
        }
    }

    void DrawThemeBody()
    {
        Ui_LabelColored("DECODED FOR INSPECTION ONLY;", UiColor::TextDim);
        Ui_LabelColored("THE LIVE THEME IS UNTOUCHED.", UiColor::TextDim);

        ResourceInfo info;
        const bool ready = DrawPreviewControls(&info);
        if (!ready)
            return;

        const UiTheme* decoded = static_cast<const UiTheme*>(Engine_Resource_Get(s_Preview));
        if (!decoded)
            return;
        const UiStyle* theme = &decoded->style;

        char value[48];
        DrawResidencyRows(info);

        if (Ui_BeginTree("COLOURS", true))
        {
            for (uint8_t role = 0; role < static_cast<uint8_t>(UiColor::Count); ++role)
                Ui_ColorSwatch(Ui_ColorName(static_cast<UiColor>(role)), theme->colors[role]);
            Ui_EndTree();
        }

        if (Ui_BeginTree("METRICS", true))
        {
            snprintf(value, sizeof(value), "%.2f S", static_cast<double>(theme->repeatDelaySeconds));
            Ui_LabelValue("REPEAT DELAY", value);
            snprintf(value, sizeof(value), "%.2f S", static_cast<double>(theme->repeatIntervalSeconds));
            Ui_LabelValue("REPEAT INTERVAL", value);
            snprintf(value, sizeof(value), "%d", theme->panelPadding);
            Ui_LabelValue("PANEL PADDING", value);
            snprintf(value, sizeof(value), "%d", theme->itemSpacing);
            Ui_LabelValue("ITEM SPACING", value);
            snprintf(value, sizeof(value), "%d", theme->borderWidth);
            Ui_LabelValue("BORDER WIDTH", value);
            snprintf(value, sizeof(value), "%d", theme->textScale);
            Ui_LabelValue("TEXT SCALE", value);
            snprintf(value, sizeof(value), "%d", theme->rowPadding);
            Ui_LabelValue("ROW PADDING", value);
            snprintf(value, sizeof(value), "%d", theme->barHeight);
            Ui_LabelValue("BAR HEIGHT", value);
            snprintf(value, sizeof(value), "%d", theme->cursorSize);
            Ui_LabelValue("CURSOR SIZE", value);
            snprintf(value, sizeof(value), "%d", theme->screenMargin);
            Ui_LabelValue("SCREEN MARGIN", value);
            snprintf(value, sizeof(value), "%d", theme->panelGap);
            Ui_LabelValue("PANEL GAP", value);
            snprintf(value, sizeof(value), "%d", theme->scrollBarWidth);
            Ui_LabelValue("SCROLLBAR WIDTH", value);
            snprintf(value, sizeof(value), "%d", theme->menuBarHeight);
            Ui_LabelValue("MENU BAR HEIGHT", value);
            snprintf(value, sizeof(value), "%d", theme->caretWidth);
            Ui_LabelValue("CARET WIDTH", value);
            snprintf(value, sizeof(value), "%d", theme->iconSpacing);
            Ui_LabelValue("ICON SPACING", value);
            Ui_EndTree();
        }
    }

    void DrawSoundBody()
    {
        Ui_LabelColored("NOT IMPLEMENTED ENGINE-WIDE", UiColor::TextWarn);
        Ui_Label("NOT ON ANY PLATFORM, NOT");
        Ui_Label("JUST THIS ONE. A LOAD WOULD");
        Ui_Label("FAIL IMMEDIATELY, SO NONE");
        Ui_Label("IS OFFERED.");
    }

    void DrawAssetDetail()
    {
        char value[64];

        if (Ui_BeginTree("ASSET HEADER", true))
        {
            Ui_LabelValue("TYPE", TypeName(s_Header.type));
            Ui_LabelValue("SOURCE EXT", s_Header.ext[0] ? s_Header.ext : "-");
            snprintf(value, sizeof(value), "%u B", static_cast<unsigned>(s_Header.dataSize));
            Ui_LabelValue("PAYLOAD", value);
            snprintf(value, sizeof(value), "%u B", static_cast<unsigned>(sizeof(AssetFileHeader)));
            Ui_LabelValue("HEADER", value);
            snprintf(value, sizeof(value), "%u / %u", static_cast<unsigned>(s_Header.depCount), static_cast<unsigned>(RES_MAX_DEPENDENCIES));
            Ui_LabelValue("DEPENDENCIES", value);
            Ui_EndTree();
        }

        if (s_Header.depCount > 0 && Ui_BeginTree("DEPENDENCIES", true))
        {
            for (uint8_t d = 0; d < s_Header.depCount && d < RES_MAX_DEPENDENCIES; ++d)
                Ui_LabelPath(s_Header.deps[d], UiColor::TextDim);
            Ui_EndTree();
        }

        Ui_Separator();
        Ui_Header(TypeName(s_Header.type));

        switch (static_cast<ResourceType>(s_Header.type))
        {
        case RES_TEXTURE:
            DrawTextureBody();
            break;
        case RES_MODEL:
            DrawModelBody();
            break;
        case RES_SOUND:
            DrawSoundBody();
            break;
        case RES_FONT:
            DrawFontBody();
            break;
        case RES_THEME:
            DrawThemeBody();
            break;
        }
    }

    // --- Level core and sector ----------------------------------------------

    void DrawLevelDetail()
    {
        char value[72];

        if (Ui_BeginTree("LEVEL FILE", true))
        {
            if (s_Level.haveInfo)
                Ui_LabelValue("NAME", s_Level.info.name[0] ? s_Level.info.name : "-");
            snprintf(value, sizeof(value), "%u", static_cast<unsigned>(s_Level.header.version));
            Ui_LabelValue("VERSION", value);
            snprintf(value, sizeof(value), "%u", static_cast<unsigned>(s_Level.header.chunkCount));
            Ui_LabelValue("CHUNKS", value);
            snprintf(value, sizeof(value), "%u B", static_cast<unsigned>(s_Level.header.totalSize));
            Ui_LabelValue("TOTAL SIZE", value);
            Ui_EndTree();
        }

        if (s_Level.haveInfo && Ui_BeginTree("GRID", true))
        {
            snprintf(value, sizeof(value), "%uX%u", static_cast<unsigned>(s_Level.info.cellsX), static_cast<unsigned>(s_Level.info.cellsZ));
            Ui_LabelValue("CELLS", value);
            snprintf(value, sizeof(value), "%.1f", static_cast<double>(s_Level.info.cellSize));
            Ui_LabelValue("CELL SIZE", value);
            snprintf(value, sizeof(value), "%.0f %.0f", static_cast<double>(s_Level.info.gridOriginX), static_cast<double>(s_Level.info.gridOriginZ));
            Ui_LabelValue("ORIGIN X Z", value);
            snprintf(value, sizeof(value), "%u / %u", static_cast<unsigned>(s_Level.cellsOccupied), static_cast<unsigned>(s_Level.cellsScanned));
            Ui_LabelValue("OCCUPIED", value);
            snprintf(value, sizeof(value), "%u KB", static_cast<unsigned>(s_Level.sectorBytesTotal / 1024u));
            Ui_LabelValue("SECTOR BYTES", value);
            snprintf(value, sizeof(value), "%u KB / %u KB", static_cast<unsigned>(s_Level.sectorBytesLargest / 1024u), static_cast<unsigned>(LEVEL_SECTOR_MAX_BYTES / 1024u));
            Ui_LabelValue("LARGEST SECTOR", value);
            if (s_Level.gridTruncated)
                Ui_LabelColored("GRID SCAN WAS CAPPED", UiColor::TextWarn);
            Ui_EndTree();
        }

        if (s_Level.materialsShown > 0 && Ui_BeginTree("MATERIALS", false))
        {
            for (uint32_t i = 0; i < s_Level.materialsShown; ++i)
                Ui_LabelPath(s_Level.materials[i].assetKey, UiColor::TextDim);
            Ui_EndTree();
        }

        if (s_Level.haveEntities && Ui_BeginTree("ENTITIES", false))
        {
            snprintf(value, sizeof(value), "%u", static_cast<unsigned>(s_Level.entityCount));
            Ui_LabelValue("COUNT", value);
            snprintf(value, sizeof(value), "%u B", static_cast<unsigned>(s_Level.entityStringsSize));
            Ui_LabelValue("STRING TABLE", value);

            for (uint32_t i = 0; i < s_Level.entitiesShown; ++i)
            {
                Ui_LabelColored(s_Level.entityNames[i], UiColor::TextAccent);
                snprintf(value, sizeof(value), "%.0f %.0f %.0f  %uP", static_cast<double>(s_Level.entityOrigins[i][0]), static_cast<double>(s_Level.entityOrigins[i][1]),
                         static_cast<double>(s_Level.entityOrigins[i][2]), static_cast<unsigned>(s_Level.entityProps[i]));
                Ui_LabelValue("  AT", value);
            }
            if (s_Level.entityCount > s_Level.entitiesShown)
            {
                snprintf(value, sizeof(value), "%u MORE NOT LISTED", static_cast<unsigned>(s_Level.entityCount - s_Level.entitiesShown));
                Ui_LabelColored(value, UiColor::TextDim);
            }
            Ui_EndTree();
        }

        if (s_Level.haveFarfield && Ui_BeginTree("FAR FIELD", false))
        {
            snprintf(value, sizeof(value), "%u", static_cast<unsigned>(s_Level.farfield.clusterCount));
            Ui_LabelValue("CLUSTERS", value);
            snprintf(value, sizeof(value), "%u", static_cast<unsigned>(s_Level.farfield.atlasCount));
            Ui_LabelValue("ATLASES", value);
            snprintf(value, sizeof(value), "%u", static_cast<unsigned>(s_Level.farfield.azimuthCount));
            Ui_LabelValue("AZIMUTHS", value);
            for (uint32_t i = 0; i < s_Level.farfield.atlasCount && i < 4; ++i)
            {
                char label[20];
                snprintf(label, sizeof(label), "ATLAS %u MATL", static_cast<unsigned>(i));
                snprintf(value, sizeof(value), "%u", static_cast<unsigned>(s_Level.farfield.atlasMaterial[i]));
                Ui_LabelValue(label, value);
            }
            Ui_EndTree();
        }

        if (s_Level.chunksShown > 0 && Ui_BeginTree("CHUNK TABLE", false))
        {
            for (uint32_t i = 0; i < s_Level.chunksShown; ++i)
            {
                char fourcc[5];
                FourCcText(s_Level.chunks[i].type, fourcc);
                snprintf(value, sizeof(value), "@%u  %u B", static_cast<unsigned>(s_Level.chunks[i].offset), static_cast<unsigned>(s_Level.chunks[i].size));
                Ui_LabelValue(fourcc, value);
            }
            if (s_Level.header.chunkCount > s_Level.chunksShown)
                Ui_LabelColored("MORE CHUNKS NOT LISTED", UiColor::TextDim);
            Ui_EndTree();
        }
    }

    void DrawSectorDetail()
    {
        char value[72];

        if (Ui_BeginTree("SECTOR", true))
        {
            snprintf(value, sizeof(value), "%u", static_cast<unsigned>(s_Sector.header.version));
            Ui_LabelValue("VERSION", value);
            snprintf(value, sizeof(value), "%u", static_cast<unsigned>(s_Sector.header.meshCount));
            Ui_LabelValue("MESHES", value);
            snprintf(value, sizeof(value), "%u", static_cast<unsigned>(s_Sector.totalVertices));
            Ui_LabelValue("VERTICES", value);
            snprintf(value, sizeof(value), "%u KB / %u KB", static_cast<unsigned>(s_EntryToc.size / 1024u), static_cast<unsigned>(LEVEL_SECTOR_MAX_BYTES / 1024u));
            Ui_LabelValue("SLOT COST", value);
            snprintf(value, sizeof(value), "%.0f %.0f %.0f", static_cast<double>(s_Sector.header.aabbMin[0]), static_cast<double>(s_Sector.header.aabbMin[1]),
                     static_cast<double>(s_Sector.header.aabbMin[2]));
            Ui_LabelValue("AABB MIN", value);
            snprintf(value, sizeof(value), "%.0f %.0f %.0f", static_cast<double>(s_Sector.header.aabbMax[0]), static_cast<double>(s_Sector.header.aabbMax[1]),
                     static_cast<double>(s_Sector.header.aabbMax[2]));
            Ui_LabelValue("AABB MAX", value);
            Ui_LabelValue("BVH", s_Sector.header.bvhOffset ? "PRESENT" : "NONE");
            Ui_EndTree();
        }

        if (s_Sector.meshesShown > 0 && Ui_BeginTree("MESHES", true))
        {
            for (uint32_t i = 0; i < s_Sector.meshesShown; ++i)
            {
                const BakedMeshEntry& mesh = s_Sector.meshes[i];
                char label[16];
                snprintf(label, sizeof(label), "MESH %u", static_cast<unsigned>(i));
                snprintf(value, sizeof(value), "%u V  MATL %u  %s", static_cast<unsigned>(mesh.vertexCount), static_cast<unsigned>(mesh.materialIndex), TopologyName(mesh.topology));
                Ui_LabelValue(label, value);
                snprintf(value, sizeof(value), "%s%s  R %.1f", mesh.normsOffset ? "N" : "-", mesh.uvsOffset ? "UV" : "-", mesh.boundsRadius);
                Ui_LabelValue("  ATTRS", value);
            }
            if (s_Sector.header.meshCount > s_Sector.meshesShown)
                Ui_LabelColored("MORE MESHES NOT LISTED", UiColor::TextDim);
            Ui_EndTree();
        }
    }

    void DrawUnknownDetail()
    {
        Ui_LabelColored("UNRECOGNISED CONTENT", UiColor::TextWarn);
        Ui_Label("NO PS2A, PS2L OR PSEC MAGIC.");

        if (s_HexBytes == 0 || !Ui_BeginTree("FIRST BYTES", true))
            return;

        for (uint32_t row = 0; row < s_HexBytes; row += BROWSE_HEX_PER_ROW)
        {
            char text[64];
            text[0] = '\0';
            size_t written = 0;
            for (uint32_t i = row; i < row + BROWSE_HEX_PER_ROW && i < s_HexBytes; ++i)
            {
                int n = snprintf(text + written, sizeof(text) - written, "%02X ", static_cast<unsigned>(s_Hex[i]));
                if (n < 0 || static_cast<size_t>(n) >= sizeof(text) - written)
                    break;
                written += static_cast<size_t>(n);
            }

            char label[16];
            snprintf(label, sizeof(label), "%04X", static_cast<unsigned>(row));
            Ui_LabelValue(label, text);
        }
        Ui_EndTree();
    }

    void DrawEntryDetail(int x, int y, int w, int h)
    {
        Ui_BeginPanel("ENTRY", x, y, w, h);

        if (!s_HaveEntry)
        {
            Ui_LabelColored("SELECT AN ENTRY", UiColor::TextDim);
            Ui_EndPanel();
            return;
        }

        Ui_LabelPath(s_EntryName, UiColor::TextAccent);
        Ui_Separator();

        if (Ui_BeginScroll("detail", Ui_ContentHeight()))
        {
            char value[48];
            if (Ui_BeginTree("ARCHIVE ENTRY", false))
            {
                snprintf(value, sizeof(value), "%u B", static_cast<unsigned>(s_EntryToc.size));
                Ui_LabelValue("BYTES", value);
                snprintf(value, sizeof(value), "%08X", static_cast<unsigned>(s_EntryToc.offset));
                Ui_LabelValue("OFFSET", value);
                snprintf(value, sizeof(value), "%08X", static_cast<unsigned>(s_EntryToc.nameHash));
                Ui_LabelValue("KEY HASH", value);
                snprintf(value, sizeof(value), "%d", static_cast<int>(s_Mount));
                Ui_LabelValue("MOUNT SLOT", value);

                // A later mount shadows a colliding key in an earlier one, so
                // the entry listed here and the bytes a load would actually
                // get can come from two different containers.
                if (s_ResolvedMount != s_Mount)
                {
                    snprintf(value, sizeof(value), "%d", static_cast<int>(s_ResolvedMount));
                    Ui_LabelValue("RESOLVES TO", value);
                    Ui_LabelColored("KEY SHADOWED", UiColor::TextWarn);
                }
                Ui_EndTree();
            }

            switch (s_Kind)
            {
            case EntryKind::Asset:
                DrawAssetDetail();
                break;
            case EntryKind::LevelCore:
                DrawLevelDetail();
                break;
            case EntryKind::Sector:
                DrawSectorDetail();
                break;
            case EntryKind::Unknown:
                DrawUnknownDetail();
                break;
            }
            Ui_EndScroll();
        }

        Ui_EndPanel();
    }

} // namespace

void Scene_AssetBrowser_Init()
{
    s_Mount = 0;
    s_Entry = 0;
    s_Spin = 0.0f;
    s_Preview = -1;
    memset(&s_EntryToc, 0, sizeof(s_EntryToc));
    ForgetEntry();
}

void Scene_AssetBrowser_Shutdown() { ForgetEntry(); }

void Scene_AssetBrowser_Update(float dt)
{
    s_Spin += dt * PREVIEW_SPIN;

    const Platform* platform = Engine_GetPlatform();
    if (!platform)
        return;

    const int screenW = static_cast<int>(platform->GetConstant(PlatformConstant::ScreenWidth));
    const int screenH = static_cast<int>(platform->GetConstant(PlatformConstant::ScreenHeight));

    // A UI-only scene: no 3D backdrop is submitted here any more. A model
    // preview is drawn inline instead, into its own row via Ui_Image3D -- see
    // DrawModelBody -- which is what lets this scene stay pure interface with
    // nothing behind it, the same as every other Testbed scene except the ones
    // whose whole point is a 3D view.
    Ui_Rect(0, 0, screenW, screenH, UiColor::WindowBackground);

    if (!Engine_Subsystem_IsEnabled(EngineSubsystem::Archive))
    {
        Ui_BeginPanel("ASSET BROWSER", PANEL_MARGIN, PANEL_MARGIN, screenW - PANEL_MARGIN * 2, screenH - PANEL_MARGIN * 2);
        Testbed_DrawUnavailable("ARCHIVE SUBSYSTEM");
        Ui_Label("ASSETS WOULD RESOLVE AS");
        Ui_Label("LOOSE FILES INSTEAD.");
        Ui_EndPanel();
        return;
    }

    const int width = (screenW - PANEL_MARGIN * 2 - COLUMN_GAP) / 2;
    const int height = screenH - PANEL_MARGIN * 2;
    const int mountsHeight = height / 4;

    DrawMounts(PANEL_MARGIN, PANEL_MARGIN, width, mountsHeight);
    DrawEntries(PANEL_MARGIN, PANEL_MARGIN + mountsHeight + COLUMN_GAP, width, height - mountsHeight - COLUMN_GAP);
    DrawEntryDetail(PANEL_MARGIN + width + COLUMN_GAP, PANEL_MARGIN, width, height);
}
