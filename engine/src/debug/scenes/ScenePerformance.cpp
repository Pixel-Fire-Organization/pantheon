#include <cstdio>

#include "core/EngineCore.h"
#include "ui/EngineUi.h"
#include "debug/TestbedScene.h"
#include "graphics/Renderer.h"
#include "platform/Platform.h"

namespace
{
    const int PANEL_MARGIN = 16;
    const int COLUMN_GAP = 8;

    void Milliseconds(const char* label, float seconds)
    {
        char text[32];
        snprintf(text, sizeof(text), "%.2f MS", static_cast<double>(seconds * 1000.0f));
        Ui_LabelValue(label, text);
    }

    void Count(const char* label, uint32_t value)
    {
        char text[32];
        snprintf(text, sizeof(text), "%u", static_cast<unsigned>(value));
        Ui_LabelValue(label, text);
    }
} // namespace

void Scene_Performance_Init() {}

void Scene_Performance_Update(float dt)
{
    (void)dt;

    const Platform* platform = Engine_GetPlatform();
    const Renderer* renderer = Engine_GetRenderer();
    if (!platform)
        return;

    const int screenW = static_cast<int>(platform->GetConstant(PlatformConstant::ScreenWidth));
    const int screenH = static_cast<int>(platform->GetConstant(PlatformConstant::ScreenHeight));
    Ui_Rect(0, 0, screenW, screenH, UiColor::WindowBackground);

    const int width = (screenW - PANEL_MARGIN * 2 - COLUMN_GAP) / 2;
    const int height = screenH - PANEL_MARGIN * 2;
    const float budgetMs = static_cast<float>(platform->GetConstant(PlatformConstant::TargetFrameMicros)) / 1000.0f;

    Ui_BeginPanel("FRAME", PANEL_MARGIN, PANEL_MARGIN, width, height);

    char text[48];
    snprintf(text, sizeof(text), "%.1f", static_cast<double>(Engine_GetFPS()));
    Ui_LabelValue("FPS", text);
    snprintf(text, sizeof(text), "%.2f MS", static_cast<double>(budgetMs));
    Ui_LabelValue("BUDGET", text);

    const float logic = Engine_GetLogicTime();
    const float render = Engine_GetRenderTime();
    const float wait = Engine_GetWaitTime();
    Milliseconds("LOGIC", logic);
    Milliseconds("RENDER", render);
    Milliseconds("GPU WAIT", wait);
    Milliseconds("TOTAL", logic + render + wait);

    Ui_Bar("USED OF BUDGET", static_cast<int>((logic + render + wait) * 1000.0f * 100.0f), static_cast<int>(budgetMs * 100.0f));

    Ui_Separator();
    Count("FRAME", Engine_GetFrameCount());
    Ui_Label("FIGURES BELOW ARE FROM");
    Ui_Label("THE PREVIOUS FRAME");
    Ui_EndPanel();

    Ui_BeginPanel("RENDERER", PANEL_MARGIN + width + COLUMN_GAP, PANEL_MARGIN, width, height);
    if (!renderer)
    {
        Testbed_DrawUnavailable("RENDERER");
        Ui_EndPanel();
        return;
    }

    const DrawStats stats = renderer->GetLastStats();
    Count("PRIMITIVES", stats.primitiveCount);
    Count("MODELS", stats.modelCount);
    Count("ENTRIES CULLED", stats.entriesCulled);
    Count("TEXTURE BINDS", stats.texBinds);
    Count("TRIS SUBMITTED", stats.trisSubmitted);
    Count("TRIS CULLED", stats.trisCulled);
    Count("VERTS", stats.vertsTransformed);
    Ui_Bar("DRAW LIST", stats.primitiveCount, GFX_MAX_DRAW_LIST_LENGTH);

    if (stats.submitBufferCapacityBytes > 0)
        Ui_Bar("SUBMIT KB", static_cast<int>(stats.submitBufferUsedBytes / 1024u), static_cast<int>(stats.submitBufferCapacityBytes / 1024u));
    else
        Ui_LabelValue("SUBMIT BUFFER", "NOT REPORTED");

    if (stats.geometryBuildMs > 0.0f || stats.geometryUploadMs > 0.0f)
    {
        snprintf(text, sizeof(text), "%.2f MS", static_cast<double>(stats.geometryBuildMs));
        Ui_LabelValue("GEOM BUILD", text);
        snprintf(text, sizeof(text), "%.2f MS", static_cast<double>(stats.geometryUploadMs));
        Ui_LabelValue("GEOM UPLOAD", text);
    }
    else
    {
        Ui_LabelValue("GEOMETRY", "NOT MEASURED");
    }
    Ui_EndPanel();
}
