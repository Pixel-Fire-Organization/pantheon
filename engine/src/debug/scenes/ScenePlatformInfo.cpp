#include <cstdio>

#include "core/EngineCore.h"
#include "ui/EngineUi.h"
#include "debug/TestbedScene.h"
#include "graphics/Renderer.h"
#include "platform/Platform.h"

namespace
{
    const int PANEL_MARGIN = 16;
    const int PAGE_COUNT = 2;

    const char* RendererName(RendererType type)
    {
        switch (type)
        {
        case RendererType::Null:
            return "NULL";
        case RendererType::Ps2Gl:
            return "PS2GL";
        case RendererType::GifTag:
            return "GIFTAG";
        case RendererType::OpenGl:
            return "OPENGL";
        case RendererType::WebGpu:
            return "WEBGPU";
        case RendererType::Gxm:
            return "GXM";
        case RendererType::VitaGl:
            return "VITAGL";
        case RendererType::Gu:
            return "GU";
        case RendererType::PspGl:
            return "PSPGL";
        case RendererType::Deko3d:
            return "DEKO3D";
        }
        return "?";
    }

    void Row(const char* label, uint32_t value)
    {
        char text[32];
        snprintf(text, sizeof(text), "%u", static_cast<unsigned>(value));
        Ui_LabelValue(label, text);
    }

    void CapabilityRow(const Platform& platform, const char* label, PlatformCapability key)
    {
        Ui_LabelValue(label, platform.HasCapability(key) ? "YES" : "NO");
    }

    const char* ButtonIconFamilyName(UiButtonIconFamily family)
    {
        switch (family)
        {
        case UiButtonIconFamily::PlayStation:
            return "PLAYSTATION";
        case UiButtonIconFamily::Xbox:
            return "XBOX";
        case UiButtonIconFamily::Keyboard:
            return "KEYBOARD";
        case UiButtonIconFamily::Nintendo:
            return "NINTENDO";
        case UiButtonIconFamily::Count:
            break;
        }
        return "?";
    }

    void DrawIdentity(const Platform& platform, int screenW, int screenH)
    {
        const Renderer* renderer = Engine_GetRenderer();

        Ui_LabelValue("NAME", platform.GetName());
        Ui_LabelValue("RENDERER", renderer ? RendererName(renderer->GetRendererType()) : "NONE");

        char text[32];
        snprintf(text, sizeof(text), "%dX%d", screenW, screenH);
        Ui_LabelValue("FRAMEBUFFER", text);

        snprintf(text, sizeof(text), "%u:%u", static_cast<unsigned>(platform.GetConstant(PlatformConstant::DisplayAspectX)),
                 static_cast<unsigned>(platform.GetConstant(PlatformConstant::DisplayAspectY)));
        Ui_LabelValue("ASPECT", text);

        Row("FRAME US", platform.GetConstant(PlatformConstant::TargetFrameMicros));
        Row("PAD PORTS", platform.GetConstant(PlatformConstant::MaxGamepadPorts));
        Ui_LabelValue("BUTTON ICONS", ButtonIconFamilyName(static_cast<UiButtonIconFamily>(platform.GetConstant(PlatformConstant::ButtonIconFamily))));

        Ui_Separator();
        Ui_Header("CAPABILITIES");
        CapabilityRow(platform, "GAMEPAD", PlatformCapability::Gamepad);
        CapabilityRow(platform, "KEYBOARD", PlatformCapability::Keyboard);
        CapabilityRow(platform, "MOUSE", PlatformCapability::Mouse);
        CapabilityRow(platform, "TOUCH", PlatformCapability::Touch);
        CapabilityRow(platform, "TRIGGERS", PlatformCapability::AnalogTriggers);
        CapabilityRow(platform, "RESIZE", PlatformCapability::ResizableWindow);
        CapabilityRow(platform, "ASYNC IO", PlatformCapability::AsyncIo);
        CapabilityRow(platform, "WRITE", PlatformCapability::FileWrite);
    }

    void DrawBudgets(const Platform& platform)
    {
        Row("TOTAL KB", platform.GetConstant(PlatformConstant::MemoryTotalBudget) / 1024u);
        Row("CONFIG KB", platform.GetConstant(PlatformConstant::MemoryArenaConfigSize) / 1024u);
        Row("LEVEL KB", platform.GetConstant(PlatformConstant::MemoryArenaLevelDataSize) / 1024u);
        Row("RENDER KB", platform.GetConstant(PlatformConstant::MemoryArenaRendererSize) / 1024u);
        Row("POOL KB", platform.GetConstant(PlatformConstant::MemoryPoolMainSize) / 1024u);
        Row("ALIGN KB", platform.GetConstant(PlatformConstant::MemoryArenaSlotAlignment) / 1024u);

        Ui_Separator();
        Ui_Header("GRAPHICS");
        Row("TEX KB", platform.GetConstant(PlatformConstant::TextureBudgetBytes) / 1024u);
        Row("TEX MAX KB", platform.GetConstant(PlatformConstant::MaxTextureBytes) / 1024u);
        Row("TEX MAX W", platform.GetConstant(PlatformConstant::MaxTextureWidth));
        Row("TEX MAX H", platform.GetConstant(PlatformConstant::MaxTextureHeight));
        Row("DRAW LIST", GFX_MAX_DRAW_LIST_LENGTH);
        Row("UI BUDGET", Ui_QuadBudget());
        Row("UI USED", Ui_QuadsUsed());
    }
} // namespace

void Scene_PlatformInfo_Init() {}

void Scene_PlatformInfo_Update(float dt)
{
    (void)dt;

    const Platform* platform = Engine_GetPlatform();
    if (!platform)
        return;

    const int screenW = static_cast<int>(platform->GetConstant(PlatformConstant::ScreenWidth));
    const int screenH = static_cast<int>(platform->GetConstant(PlatformConstant::ScreenHeight));

    Ui_Rect(0, 0, screenW, screenH, UiColor::WindowBackground);

    static const char* const PAGES[PAGE_COUNT] = {"PLATFORM", "BUDGETS"};

    Ui_BeginPanelSlot("PLATFORM INFO", UiPanelSlot::Full);
    const int page = Testbed_Tabs("pages", PAGES, PAGE_COUNT);
    if (page == 0)
        DrawIdentity(*platform, screenW, screenH);
    else
        DrawBudgets(*platform);
    Ui_EndPanel();
}
