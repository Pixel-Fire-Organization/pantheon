#include "scenes/EngineLoadingScreen.h"

#include "LoadingScreenAssets.h"
#include "core/EngineCore.h"
#include "core/EngineIO.h"
#include "core/EngineSubsystems.h"
#include "resources/EngineResource.h"
#include "ui/EngineUi.h"

namespace
{
    const int kImageSlots = LoadingScreenGenerated::kImageCount > 0 ? LoadingScreenGenerated::kImageCount : 1;

    int32_t s_ImageHandles[kImageSlots];
    float s_ElapsedSeconds = 0.0f;
} // namespace

void Engine_LoadingScreen_Begin()
{
    s_ElapsedSeconds = 0.0f;
    for (int i = 0; i < LoadingScreenGenerated::kImageCount; ++i)
    {
        char resolved[IO_FILE_MAX_PATH];
        Engine_BuildPath(nullptr, LoadingScreenGenerated::kImages[i], resolved, sizeof(resolved));
        s_ImageHandles[i] = Engine_Resource_Load(RES_TEXTURE, resolved);
    }
}

void Engine_LoadingScreen_Draw(float progress, float dt)
{
    if (LoadingScreenGenerated::kImageCount <= 0)
        return;
    if (!Engine_Subsystem_IsEnabled(EngineSubsystem::Ui))
        return;

    s_ElapsedSeconds += dt;
    const float cycle = LoadingScreenGenerated::kCycleSeconds > 0.0f ? LoadingScreenGenerated::kCycleSeconds : 1.0f;
    int index = static_cast<int>(s_ElapsedSeconds / cycle) % LoadingScreenGenerated::kImageCount;
    if (index < 0)
        index = 0;

    const int screenW = Ui_ScreenWidth();
    const int screenH = Ui_ScreenHeight();

    Ui_Rect(0, 0, screenW, screenH, UiColor::WindowBackground);
    Ui_ImageAt(s_ImageHandles[index], 0, 0, screenW, screenH, 0, 0, 65535, 65535, UiColor::Text);

    const int barWidth = screenW / 3;
    const int barX = (screenW - barWidth) / 2;
    const int barY = screenH - screenH / 8;
    Ui_BeginPanel("", barX, barY, barWidth, 40);
    Ui_Bar("LOADING", static_cast<int>(progress * 100.0f), 100);
    Ui_EndPanel();
}
