#include <cstdio>

#include "core/EngineCore.h"
#include "core/EngineIO.h"
#include "core/EngineSubsystems.h"
#include "debug/TestbedScene.h"
#include "platform/Platform.h"
#include "resources/EngineResource.h"
#include "ui/EngineUi.h"

namespace
{
    const int PANEL_MARGIN = 16;
    const int COLUMN_GAP = 8;
    const char* const DEFAULT_PATH = "RASSETS\\BOX.PS2A";

    const int TYPE_AUTO = 0;
    const int TYPE_COUNT = 6;
    const char* const TYPE_NAMES[TYPE_COUNT] = {"AUTO", "TEXTURE", "MODEL", "SOUND", "FONT", "THEME"};

    char s_PathBuffer[UI_TEXT_INPUT_MAX] = "";
    int s_TypeIndex = TYPE_AUTO;
    int32_t s_SelectedHandle = -1;
    bool s_LoadFailed = false;

    const char* TypeName(ResourceType type)
    {
        switch (type)
        {
        case RES_TEXTURE:
            return "TEX";
        case RES_MODEL:
            return "MDL";
        case RES_SOUND:
            return "SND";
        case RES_FONT:
            return "FNT";
        case RES_THEME:
            return "THM";
        }
        return "?";
    }

    const char* StateName(ResourceState state)
    {
        switch (state)
        {
        case RES_STATE_EMPTY:
            return "EMPTY";
        case RES_STATE_LOADING:
            return "LOADING";
        case RES_STATE_READY:
            return "READY";
        }
        return "?";
    }

    void DoLoad()
    {
        s_LoadFailed = false;

        char fullPath[IO_FILE_MAX_PATH];
        int32_t handle = -1;
        if (Engine_BuildPath(Engine_GetResourceLocationToken(), s_PathBuffer, fullPath, sizeof(fullPath)))
        {
            handle = (s_TypeIndex == TYPE_AUTO) ? Engine_Resource_LoadAuto(fullPath) : Engine_Resource_Load(static_cast<ResourceType>(s_TypeIndex - 1), fullPath);
        }

        if (handle >= 0)
            s_SelectedHandle = handle;
        else
            s_LoadFailed = true;
    }

    void DrawLoadAndInspect(int x, int y, int w, int h)
    {
        Ui_BeginPanel("LOAD", x, y, w, h);
        Ui_TextInput("PATH", s_PathBuffer, sizeof(s_PathBuffer));
        Ui_Combo("TYPE", &s_TypeIndex, TYPE_NAMES, TYPE_COUNT);
        if (Ui_Button("LOAD"))
            DoLoad();
        if (s_LoadFailed)
            Ui_LabelColored("LOAD FAILED", UiColor::TextWarn);

        Ui_Separator();
        Ui_Header("INSPECT");

        ResourceInfo info;
        if (s_SelectedHandle >= 0 && Engine_Resource_GetInfo(s_SelectedHandle, &info))
        {
            Ui_LabelPath(info.key, UiColor::TextAccent);

            char text[48];
            snprintf(text, sizeof(text), "H%d", static_cast<int>(s_SelectedHandle));
            Ui_LabelValue("HANDLE", text);
            Ui_LabelValue("TYPE", TypeName(info.type));
            Ui_LabelValue("STATE", StateName(info.state));
            snprintf(text, sizeof(text), "%u", static_cast<unsigned>(info.refCount));
            Ui_LabelValue("REFS", text);
            snprintf(text, sizeof(text), "%u", static_cast<unsigned>(info.depCount));
            Ui_LabelValue("DEPS", text);
            if (info.type == RES_TEXTURE)
            {
                snprintf(text, sizeof(text), "%dX%d %uKB", static_cast<int>(info.width), static_cast<int>(info.height), static_cast<unsigned>(info.textureBytes / 1024u));
                Ui_LabelValue("TEXTURE", text);
            }

            if (Ui_Button(info.pinned ? "UNPIN" : "PIN"))
            {
                if (info.pinned)
                    Engine_Resource_Unpin(s_SelectedHandle);
                else
                    Engine_Resource_Pin(s_SelectedHandle);
            }
            if (Ui_Button("UNLOAD"))
            {
                Engine_Resource_Unload(s_SelectedHandle);
                s_SelectedHandle = -1;
            }
        }
        else
        {
            Ui_LabelColored("NOTHING SELECTED", UiColor::TextDim);
            Ui_Label("PICK A ROW IN THE LIVE");
            Ui_Label("TABLE, OR LOAD SOMETHING.");
        }

        Ui_EndPanel();
    }

    void DrawLiveTable(int x, int y, int w, int h)
    {
        Ui_BeginPanel("LIVE TABLE", x, y, w, h);
        Ui_Bar("TEXTURE KB", static_cast<int>(Engine_Resource_GetTextureBudgetUsed() / 1024u), static_cast<int>(Engine_Resource_GetTextureBudget() / 1024u));

        char text[48];
        const Platform* platform = Engine_GetPlatform();
        if (platform)
        {
            snprintf(text, sizeof(text), "%u KB", static_cast<unsigned>(platform->GetConstant(PlatformConstant::MaxTextureBytes) / 1024u));
            Ui_LabelValue("MAX TEXTURE", text);
        }
        Ui_Separator();

        const uint32_t capacity = Engine_Resource_GetCapacity();
        uint32_t live = 0;

        // Leave room below the scroll region for the "SLOTS USED" summary row.
        const UiStyle& style = Ui_GetStyle();
        const int trailerHeight = Ui_TextHeight(style.textScale) + style.itemSpacing;
        const int scrollHeight = Ui_ContentHeight() - trailerHeight;

        if (scrollHeight > 0 && Ui_BeginScroll("live", scrollHeight))
        {
            const int tagWidth = Ui_TextWidth(style.textScale, "PIN TEX R99") + style.rowPadding * 2;
            const int widths[] = {0, tagWidth};
            if (Ui_BeginTable("table", widths, 2))
            {
                const char* const headers[] = {"KEY", "TAG"};
                Ui_TableHeader(headers);
                for (uint32_t i = 0; i < capacity; ++i)
                {
                    ResourceInfo info;
                    if (!Engine_Resource_GetInfo(static_cast<int32_t>(i), &info))
                        continue;
                    ++live;

                    char tag[24];
                    snprintf(tag, sizeof(tag), "%s%s R%u", info.pinned ? "PIN " : "", TypeName(info.type), static_cast<unsigned>(info.refCount));

                    Ui_PushIdIndex(static_cast<int>(i));
                    const bool activated = Ui_TableRow(info.key, static_cast<int32_t>(i) == s_SelectedHandle);
                    Ui_PopId();
                    if (activated)
                        s_SelectedHandle = static_cast<int32_t>(i);

                    Ui_TableCell(info.key, UiAlign::Left, UiColor::Text);
                    Ui_TableCell(tag, UiAlign::Right, UiColor::TextDim);
                }
                Ui_EndTable();
            }
            Ui_EndScroll();
        }

        if (live == 0)
            Ui_LabelColored("NOTHING RESIDENT", UiColor::TextDim);

        snprintf(text, sizeof(text), "%u / %u", static_cast<unsigned>(live), static_cast<unsigned>(capacity));
        Ui_LabelValue("SLOTS USED", text);
        Ui_EndPanel();
    }
} // namespace

void Scene_Resources_Init()
{
    snprintf(s_PathBuffer, sizeof(s_PathBuffer), "%s", DEFAULT_PATH);
    s_TypeIndex = TYPE_AUTO;
    s_SelectedHandle = -1;
    s_LoadFailed = false;
}

void Scene_Resources_Update(float dt)
{
    (void)dt;

    const Platform* platform = Engine_GetPlatform();
    if (!platform)
        return;

    const int screenW = static_cast<int>(platform->GetConstant(PlatformConstant::ScreenWidth));
    const int screenH = static_cast<int>(platform->GetConstant(PlatformConstant::ScreenHeight));
    Ui_Rect(0, 0, screenW, screenH, UiColor::WindowBackground);

    if (!Engine_Subsystem_IsEnabled(EngineSubsystem::Resource))
    {
        Ui_BeginPanel("RESOURCES", PANEL_MARGIN, PANEL_MARGIN, screenW - PANEL_MARGIN * 2, screenH - PANEL_MARGIN * 2);
        Testbed_DrawUnavailable("RESOURCE SUBSYSTEM");
        Ui_EndPanel();
        return;
    }

    const int width = (screenW - PANEL_MARGIN * 2 - COLUMN_GAP) / 2;
    const int height = screenH - PANEL_MARGIN * 2;

    DrawLoadAndInspect(PANEL_MARGIN, PANEL_MARGIN, width, height);
    DrawLiveTable(PANEL_MARGIN + width + COLUMN_GAP, PANEL_MARGIN, width, height);
}
