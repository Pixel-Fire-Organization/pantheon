#include <cstdio>

#include "core/EngineCore.h"
#include "ui/EngineUi.h"
#include "debug/TestbedScene.h"
#include "graphics/Renderer.h"
#include "platform/Platform.h"

namespace
{
    const int PANEL_MARGIN = 16;
    const int PANEL_WIDTH = 240;
    const int LADDER_STEPS = 10;
    const float LADDER_FIRST = 2.0f;
    const float LADDER_RATIO = 2.0f;

    /// Grow the marker with distance so a step that vanishes is a depth problem
    /// rather than one that is merely small.
    /// @param distance How far down the ladder the marker sits.
    /// @return The cube's edge length.
    float MarkerSize(float distance) { return 0.05f * distance; }
} // namespace

void Scene_DepthRange_Init() {}

void Scene_DepthRange_Update(float dt)
{
    (void)dt;

    Renderer* renderer = Engine_GetRenderer();
    const Platform* platform = Engine_GetPlatform();
    if (!renderer || !platform)
        return;

    renderer->ClearFrame(Color3{0.05f, 0.06f, 0.09f});

    Camera3D camera;
    camera.position = Vector3{0.0f, 0.6f, 0.0f};
    camera.target = Vector3{0.0f, 0.0f, -1.0f};
    camera.up = Vector3{0.0f, 1.0f, 0.0f};
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;
    renderer->SetCamera3D(static_cast<CameraID>(0), camera);
    renderer->SetActiveCamera3D(static_cast<CameraID>(0));
    renderer->DrawGrid(64, 2.0f);

    float distance = LADDER_FIRST;
    for (int i = 0; i < LADDER_STEPS; ++i)
    {
        const float size = MarkerSize(distance);
        const float shade = 1.0f - (static_cast<float>(i) / static_cast<float>(LADDER_STEPS));
        renderer->AddPrimitiveToDrawList(Primitive3D::Cube, Vector3{0.0f, 0.0f, -distance}, Vector3{0.0f, 0.0f, 0.0f}, Vector3{size, size, size},
                                         Color3{shade, 0.4f + shade * 0.5f, 1.0f - shade});
        distance *= LADDER_RATIO;
    }

    renderer->AddPrimitiveToDrawList(Primitive3D::Sphere, Vector3{0.0f, 0.0f, -GFX_NEAR_PLANE * 4.0f}, Vector3{0.0f, 0.0f, 0.0f},
                                     Vector3{0.02f, 0.02f, 0.02f}, Color3{1.0f, 0.9f, 0.2f});

    const int screenH = static_cast<int>(platform->GetConstant(PlatformConstant::ScreenHeight));
    Ui_BeginPanel("DEPTH RANGE", PANEL_MARGIN, PANEL_MARGIN, PANEL_WIDTH, screenH / 2);

    char text[48];
    snprintf(text, sizeof(text), "%.2f", static_cast<double>(GFX_NEAR_PLANE));
    Ui_LabelValue("NEAR", text);
    snprintf(text, sizeof(text), "%.0f", static_cast<double>(GFX_FAR_PLANE));
    Ui_LabelValue("FAR", text);
    snprintf(text, sizeof(text), "%.0f", static_cast<double>(distance / LADDER_RATIO));
    Ui_LabelValue("LAST STEP", text);
    Ui_Separator();
    Ui_Label("EACH STEP DOUBLES");
    Ui_Label("MARKERS GROW WITH RANGE");
    Ui_Label("A GAP IS A DEPTH BUG");
    Ui_EndPanel();
}
