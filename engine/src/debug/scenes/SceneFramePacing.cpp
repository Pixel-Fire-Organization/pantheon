#include <cstdio>

#include "core/EngineCore.h"
#include "ui/EngineUi.h"
#include "debug/TestbedScene.h"
#include "platform/Platform.h"

namespace
{
    const int PANEL_MARGIN = 16;
    const int PANEL_WIDTH = 260;
    const int SAMPLES = 96;
    const int LANE_HEIGHT = 26;
    const int MARKER_SIZE = 18;
    const float PULSE_SECONDS = 1.0f;
    const float MOVER_SPEED = 0.25f;
    const float PLOT_CEILING_SCALE = 2.5f;

    float s_Frames[SAMPLES];
    float s_Mover = 0.0f;
    float s_Pulse = 0.0f;
    bool s_PulseOn = false;
    float s_Worst = 0.0f;
} // namespace

void Scene_FramePacing_Init()
{
    for (int i = 0; i < SAMPLES; ++i)
        s_Frames[i] = 0.0f;
    s_Mover = 0.0f;
    s_Pulse = 0.0f;
    s_PulseOn = false;
    s_Worst = 0.0f;
}

void Scene_FramePacing_Update(float dt)
{
    const Platform* platform = Engine_GetPlatform();
    if (!platform)
        return;

    const float milliseconds = dt * 1000.0f;
    for (int i = 0; i < SAMPLES - 1; ++i)
        s_Frames[i] = s_Frames[i + 1];
    s_Frames[SAMPLES - 1] = milliseconds;
    if (milliseconds > s_Worst)
        s_Worst = milliseconds;

    // Both of these are driven by elapsed time, not by frame count, so they run
    // at the same real speed whatever the refresh rate is. A difference between
    // two regions here is a timing bug, not a refresh-rate difference.
    s_Mover += dt * MOVER_SPEED;
    while (s_Mover > 1.0f)
        s_Mover -= 1.0f;

    s_Pulse += dt;
    if (s_Pulse >= PULSE_SECONDS)
    {
        s_Pulse -= PULSE_SECONDS;
        s_PulseOn = !s_PulseOn;
    }

    const int screenW = static_cast<int>(platform->GetConstant(PlatformConstant::ScreenWidth));
    const int screenH = static_cast<int>(platform->GetConstant(PlatformConstant::ScreenHeight));
    const float budgetMs = static_cast<float>(platform->GetConstant(PlatformConstant::TargetFrameMicros)) / 1000.0f;

    Ui_Rect(0, 0, screenW, screenH, UiColor::WindowBackground);

    const int laneY = screenH - PANEL_MARGIN - LANE_HEIGHT;
    Ui_Rect(PANEL_MARGIN, laneY, screenW - PANEL_MARGIN * 2, LANE_HEIGHT, UiColor::BarTrack);
    const int travel = screenW - PANEL_MARGIN * 2 - MARKER_SIZE;
    Ui_Rect(PANEL_MARGIN + static_cast<int>(s_Mover * static_cast<float>(travel)), laneY + (LANE_HEIGHT - MARKER_SIZE) / 2, MARKER_SIZE, MARKER_SIZE,
            UiColor::TextAccent);

    Ui_Rect(screenW - PANEL_MARGIN - MARKER_SIZE * 2, PANEL_MARGIN, MARKER_SIZE * 2, MARKER_SIZE * 2, s_PulseOn ? UiColor::BarFill : UiColor::BarTrack);

    Ui_BeginPanel("FRAME PACING", PANEL_MARGIN, PANEL_MARGIN, PANEL_WIDTH, screenH - PANEL_MARGIN * 2 - LANE_HEIGHT - PANEL_MARGIN);

    char text[48];
    snprintf(text, sizeof(text), "%.2f MS", static_cast<double>(budgetMs));
    Ui_LabelValue("TARGET", text);
    snprintf(text, sizeof(text), "%.2f MS", static_cast<double>(milliseconds));
    Ui_LabelValue("THIS FRAME", text);
    snprintf(text, sizeof(text), "%.2f MS", static_cast<double>(s_Worst));
    Ui_LabelValue("WORST", text);
    snprintf(text, sizeof(text), "%.1f", static_cast<double>(Engine_GetFPS()));
    Ui_LabelValue("FPS", text);

    if (Ui_Button("CLEAR WORST"))
        s_Worst = 0.0f;

    Ui_Plot("DELTA MS", s_Frames, SAMPLES, 0.0f, budgetMs * PLOT_CEILING_SCALE, budgetMs);
    Ui_Separator();
    Ui_Label("THE RED LINE IS BUDGET");
    Ui_Label("THE BAR CROSSES IN 4S");
    Ui_Label("THE SQUARE FLIPS EACH");
    Ui_Label("SECOND ON EVERY TARGET");
    Ui_EndPanel();
}
