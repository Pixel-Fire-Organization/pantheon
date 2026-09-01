#include "scenes/ColouredPrimitivesScene.h"

#include <cmath>

#include "ActionIds.h"
#include "GameAPI.h"
#include "scenes/DrawLoadScene.h"
#include "scenes/TexturedPrimitivesScene.h"

namespace
{
    const float ORBIT_RADIUS = 14.0f;
    const float ORBIT_HEIGHT = 5.0f;
    const float ORBIT_RATE = 0.25f;
} // namespace

ColouredPrimitivesScene* ColouredPrimitivesScene::m_instance = nullptr;

ColouredPrimitivesScene* ColouredPrimitivesScene::GetInstance()
{
    if (!m_instance)
        m_instance = new ColouredPrimitivesScene();
    return m_instance;
}

void ColouredPrimitivesScene::OnStart() { m_time = 0.0f; }

void ColouredPrimitivesScene::OnUpdate(float dt)
{
    if (game::ActionPressed(static_cast<int>(ActionId::CycleNext)))
    {
        game::SwitchScene(TexturedPrimitivesScene::GetInstance());
        return;
    }
    if (game::ActionPressed(static_cast<int>(ActionId::CyclePrev)))
    {
        game::SwitchScene(DrawLoadScene::GetInstance());
        return;
    }

    m_time += dt;

    game::Clear(20, 23, 30);
    game::SetCamera3D(ORBIT_RADIUS * sinf(m_time * ORBIT_RATE), ORBIT_HEIGHT, ORBIT_RADIUS * cosf(m_time * ORBIT_RATE), 0.0f, 1.0f, 0.0f, 45.0f);
    game::DrawGrid(20, 1.0f);

    game::DrawCube(-3.0f, 1.0f, 0.0f, 2.0f, 217, 64, 64);
    game::DrawSphere(0.0f, 1.0f, 0.0f, 2.0f, 64, 191, 115);
    game::DrawCylinder(3.0f, 1.0f, 0.0f, 2.0f, 89, 140, 230);

    game::Panel("COLOURED", 16, 16, 280, 90);
    game::Label("CUBE, SPHERE, CYLINDER");
    game::Label("R1/L1: NEXT SCENE");
    game::EndPanel();
}
