#include "scenes/DrawLoadScene.h"

#include <cmath>
#include <cstdio>

#include "ActionIds.h"
#include "GameAPI.h"
#include "scenes/ColouredPrimitivesScene.h"
#include "scenes/TexturedPrimitivesScene.h"

namespace
{
    const int MAX_COUNT = 512;
    const int STEP = 8;
    const float ORBIT_RADIUS = 16.0f;
    const float ORBIT_HEIGHT = 8.0f;
    const float ORBIT_RATE = 0.18f;
    const float RING_SPREAD = 8.0f;
    const float MARKER_SIZE = 0.35f;
} // namespace

DrawLoadScene* DrawLoadScene::m_instance = nullptr;

DrawLoadScene* DrawLoadScene::GetInstance()
{
    if (!m_instance)
        m_instance = new DrawLoadScene();
    return m_instance;
}

void DrawLoadScene::OnStart()
{
    m_count = 64;
    m_time = 0.0f;
}

void DrawLoadScene::OnUpdate(float dt)
{
    if (game::ActionPressed(static_cast<int>(ActionId::CycleNext)))
    {
        game::SwitchScene(ColouredPrimitivesScene::GetInstance());
        return;
    }
    if (game::ActionPressed(static_cast<int>(ActionId::CyclePrev)))
    {
        game::SwitchScene(TexturedPrimitivesScene::GetInstance());
        return;
    }

    if (game::WasPadPressed(0, "dpad_right") || game::WasKeyPressed("up"))
        m_count = m_count + STEP < MAX_COUNT ? m_count + STEP : MAX_COUNT;
    if (game::WasPadPressed(0, "dpad_left") || game::WasKeyPressed("down"))
        m_count = m_count - STEP > 1 ? m_count - STEP : 1;

    m_time += dt;

    game::Clear(15, 18, 24);
    game::SetCamera3D(ORBIT_RADIUS * sinf(m_time * ORBIT_RATE), ORBIT_HEIGHT, ORBIT_RADIUS * cosf(m_time * ORBIT_RATE), 0.0f, 0.0f, 0.0f, 55.0f);

    for (int i = 0; i < m_count; ++i)
    {
        const float turn = static_cast<float>(i) * 0.618f;
        const float radius = RING_SPREAD * (0.2f + 0.8f * (static_cast<float>(i % 64) / 64.0f));
        const float x = radius * sinf(turn * 6.283f);
        const float z = radius * cosf(turn * 6.283f);
        const float y = sinf(m_time + static_cast<float>(i) * 0.05f) * 2.0f;
        const float shade = static_cast<float>(i % 32) / 32.0f;
        const int r = static_cast<int>(80.0f + shade * 100.0f);
        const int g = static_cast<int>(200.0f - shade * 80.0f);
        game::DrawCube(x, y, z, MARKER_SIZE, r, g, 230);
    }

    char text[32];
    snprintf(text, sizeof(text), "PRIMITIVES %d", m_count);

    game::Panel("DRAW LOAD", 16, 16, 280, 110);
    game::Label(text);
    game::Label("DPAD/UP/DOWN: COUNT");
    game::Label("R1/L1: NEXT SCENE");
    game::EndPanel();
}
