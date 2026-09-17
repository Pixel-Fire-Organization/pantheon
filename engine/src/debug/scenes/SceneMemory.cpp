#include <cstdio>

#include "PlatformConstants.h"
#include "core/EngineMemory.h"
#include "debug/TestbedScene.h"
#include "platform/Platform.h"
#include "resources/EngineResource.h"
#include "ui/EngineUi.h"

namespace
{
    const int PANEL_MARGIN = 16;
    const int COLUMN_GAP = 8;
    const int MAX_POOL_CHUNKS = 8;

    const int TARGET_CONFIG = 0;
    const int TARGET_LEVEL_DATA = 1;
    const int TARGET_POOL = 2;
    const int TARGET_COUNT = 3;
    const char* const TARGET_NAMES[TARGET_COUNT] = {"CONFIG", "LEVEL DATA", "MAIN POOL"};

    int s_Target = TARGET_CONFIG;
    uint32_t s_NextSlot[2] = {0, 0}; // indexed by TARGET_CONFIG / TARGET_LEVEL_DATA

    void* s_PoolChunks[MAX_POOL_CHUNKS];
    int s_PoolOutstanding = 0;

    char s_LastAction[20] = "";
    size_t s_LastBefore = 0;
    size_t s_LastAfter = 0;
    bool s_HaveLastAction = false;
    bool s_LastActionRefused = false;

    int Kilobytes(size_t bytes) { return static_cast<int>(bytes / 1024u); }

    void ArenaBar(const char* label, ArenaType type)
    {
        size_t capacity = 0;
        size_t used = 0;
        Engine_GetArenaStats(type, &capacity, &used);
        Ui_Bar(label, Kilobytes(used), Kilobytes(capacity));
    }

    ArenaType TargetArenaType() { return (s_Target == TARGET_CONFIG) ? ARENA_CONFIG : ARENA_LEVEL_DATA; }

    // The engine exposes no "how many slots" getter of its own: a segment's
    // slot count is a platform constant baked into EngineMemory.cpp's private
    // table. Engine_GetSlotCapacity already answers zero once an index runs
    // past the real slots, so that is the probe.
    uint32_t ArenaSlotCount(ArenaType type)
    {
        uint32_t count = 0;
        while (count < MEM_ARENA_MAX_SLOTS && Engine_GetSlotCapacity(type, count) > 0)
            ++count;
        return count;
    }

    size_t TargetUsedBytes()
    {
        if (s_Target == TARGET_POOL)
        {
            size_t capacity = 0;
            size_t used = 0;
            Engine_GetPoolStatsMain(&capacity, &used);
            return used;
        }
        size_t capacity = 0;
        size_t used = 0;
        Engine_GetArenaStats(TargetArenaType(), &capacity, &used);
        return used;
    }

    void RecordAction(const char* label, size_t before, size_t after, bool refused)
    {
        snprintf(s_LastAction, sizeof(s_LastAction), "%s", label);
        s_LastBefore = before;
        s_LastAfter = after;
        s_LastActionRefused = refused;
        s_HaveLastAction = true;
    }

    // Arenas free per slot, never per allocation, so the arena tool writes a
    // reserved-but-empty slot (the same nullptr-data idiom the PS2 renderer
    // backends use to size their own geometry slot) rather than the unused
    // Engine_AddToArena bump path, whose bytes Engine_GetArenaStats cannot see.
    void ForceAllocArena()
    {
        const ArenaType type = TargetArenaType();
        const int targetIdx = (s_Target == TARGET_CONFIG) ? 0 : 1;
        const uint32_t slots = ArenaSlotCount(type);
        if (slots == 0)
        {
            RecordAction("FORCE ALLOC", TargetUsedBytes(), TargetUsedBytes(), true);
            return;
        }

        const uint32_t slot = s_NextSlot[targetIdx] % slots;
        const size_t capacity = Engine_GetSlotCapacity(type, slot);
        const size_t size = (capacity >= 4) ? capacity / 4 : capacity;

        const size_t before = TargetUsedBytes();
        const bool ok = Engine_LoadToSlot(type, slot, nullptr, size);
        if (ok)
            s_NextSlot[targetIdx] = slot + 1;
        RecordAction("FORCE ALLOC", before, TargetUsedBytes(), !ok);
    }

    void ForceAllocPool()
    {
        const size_t before = TargetUsedBytes();
        bool refused = true;
        if (s_PoolOutstanding < MAX_POOL_CHUNKS)
        {
            void* chunk = Engine_PoolAllocMain();
            if (chunk)
            {
                s_PoolChunks[s_PoolOutstanding++] = chunk;
                refused = false;
            }
        }
        RecordAction("FORCE ALLOC", before, TargetUsedBytes(), refused);
    }

    void ForceFreePool()
    {
        if (s_PoolOutstanding == 0)
            return;
        const size_t before = TargetUsedBytes();
        --s_PoolOutstanding;
        Engine_PoolFreeMain(s_PoolChunks[s_PoolOutstanding]);
        s_PoolChunks[s_PoolOutstanding] = nullptr;
        RecordAction("FORCE FREE", before, TargetUsedBytes(), false);
    }

    void ForceReset()
    {
        const size_t before = TargetUsedBytes();
        if (s_Target == TARGET_POOL)
        {
            const Platform* platform = Engine_GetPlatform();
            void* buffer = Engine_PoolGetBufferMain();
            if (platform && buffer)
            {
                Engine_PoolInitMain(buffer, platform->GetConstant(PlatformConstant::MemoryPoolMainSize), platform->GetConstant(PlatformConstant::MemoryPoolChunkSize));
            }
            s_PoolOutstanding = 0;
        }
        else
        {
            Engine_ResetArena(TargetArenaType());
            s_NextSlot[(s_Target == TARGET_CONFIG) ? 0 : 1] = 0;
        }
        RecordAction("FORCE RESET", before, TargetUsedBytes(), false);
    }

    void DrawTools()
    {
        Ui_Separator();
        Ui_Header("TOOLS");
        Ui_Combo("TARGET", &s_Target, TARGET_NAMES, TARGET_COUNT);

        const bool poolFull = s_Target == TARGET_POOL && s_PoolOutstanding >= MAX_POOL_CHUNKS;
        Ui_BeginDisabled(poolFull);
        if (Ui_Button("FORCE ALLOC"))
        {
            if (s_Target == TARGET_POOL)
                ForceAllocPool();
            else
                ForceAllocArena();
        }
        Ui_EndDisabled();

        Ui_BeginDisabled(!(s_Target == TARGET_POOL && s_PoolOutstanding > 0));
        if (Ui_Button("FORCE FREE"))
            ForceFreePool();
        Ui_EndDisabled();

        if (Ui_Button("FORCE RESET"))
            ForceReset();

        if (s_HaveLastAction)
        {
            char line[48];
            const long delta = static_cast<long>(s_LastAfter) - static_cast<long>(s_LastBefore);
            snprintf(line, sizeof(line), "%uB -> %uB (%+ldB)", static_cast<unsigned>(s_LastBefore), static_cast<unsigned>(s_LastAfter), delta);
            Ui_LabelValue(s_LastAction, line);
            if (s_LastActionRefused)
                Ui_LabelColored("REFUSED - TARGET FULL", UiColor::TextWarn);
        }

        Ui_Separator();
        Ui_Label("RENDERER ARENA HAS NO TOOL:");
        Ui_Label("A LIVE RENDERER OWNS IT AND");
        Ui_Label("IT IS NEVER EMPTIED.");
    }
} // namespace

void Scene_Memory_Init()
{
    s_Target = TARGET_CONFIG;
    s_NextSlot[0] = 0;
    s_NextSlot[1] = 0;
    s_PoolOutstanding = 0;
    s_HaveLastAction = false;
    s_LastActionRefused = false;
}

void Scene_Memory_Update(float dt)
{
    (void)dt;

    const Platform* platform = Engine_GetPlatform();
    if (!platform)
        return;

    const int screenW = static_cast<int>(platform->GetConstant(PlatformConstant::ScreenWidth));
    const int screenH = static_cast<int>(platform->GetConstant(PlatformConstant::ScreenHeight));
    Ui_Rect(0, 0, screenW, screenH, UiColor::WindowBackground);

    const int width = (screenW - PANEL_MARGIN * 2 - COLUMN_GAP) / 2;
    const int height = screenH - PANEL_MARGIN * 2;

    Ui_BeginPanel("ARENAS AND POOL", PANEL_MARGIN, PANEL_MARGIN, width, height);
    ArenaBar("CONFIG KB", ARENA_CONFIG);
    ArenaBar("LEVEL DATA KB", ARENA_LEVEL_DATA);
    ArenaBar("RENDERER KB", ARENA_RENDERER);

    size_t poolCapacity = 0;
    size_t poolUsed = 0;
    Engine_GetPoolStatsMain(&poolCapacity, &poolUsed);
    Ui_Bar("MAIN POOL KB", Kilobytes(poolUsed), Kilobytes(poolCapacity));

    DrawTools();
    Ui_EndPanel();

    Ui_BeginPanel("HEAP AND TEXTURES", PANEL_MARGIN + width + COLUMN_GAP, PANEL_MARGIN, width, height);

    size_t heapTotal = 0;
    size_t heapUsed = 0;
    size_t heapFree = 0;
    Engine_GetHeapStats(&heapTotal, &heapUsed, &heapFree);
    Ui_Bar("HEAP KB", Kilobytes(heapUsed), Kilobytes(heapTotal));

    char text[48];
    snprintf(text, sizeof(text), "%d KB", Kilobytes(heapFree));
    Ui_LabelValue("HEAP FREE", text);
    snprintf(text, sizeof(text), "%u KB", static_cast<unsigned>(platform->GetConstant(PlatformConstant::MemoryTotalBudget) / 1024u));
    Ui_LabelValue("BUDGET", text);

    Ui_Separator();
    Ui_Header("TEXTURES");
    Ui_Bar("TEXTURE KB", static_cast<int>(Engine_Resource_GetTextureBudgetUsed() / 1024u), static_cast<int>(Engine_Resource_GetTextureBudget() / 1024u));

    Ui_Separator();
    Ui_Header("INTERFACE");
    Ui_Bar("UI QUADS", static_cast<int>(Ui_QuadsUsed()), static_cast<int>(Ui_QuadBudget()));
    Ui_EndPanel();
}
