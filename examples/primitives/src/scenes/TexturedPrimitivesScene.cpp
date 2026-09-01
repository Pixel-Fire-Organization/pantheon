#include "scenes/TexturedPrimitivesScene.h"

#include <cmath>

#include "ActionIds.h"
#include "GameAPI.h"
#include "scenes/ColouredPrimitivesScene.h"
#include "scenes/DrawLoadScene.h"

namespace
{
    const float ORBIT_RADIUS = 14.0f;
    const float ORBIT_HEIGHT = 5.0f;
    const float ORBIT_RATE = 0.25f;
} // namespace

TexturedPrimitivesScene* TexturedPrimitivesScene::m_instance = nullptr;

TexturedPrimitivesScene* TexturedPrimitivesScene::GetInstance()
{
    if (!m_instance)
        m_instance = new TexturedPrimitivesScene();
    return m_instance;
}

const game::SceneResource* TexturedPrimitivesScene::GetResources(int* outCount) const
{
    static const game::SceneResource kResources[] = {
        {"TEXTURE", BOX_TEXTURE_PATH},
    };
    *outCount = 1;
    return kResources;
}

void TexturedPrimitivesScene::OnStart()
{
    m_time = 0.0f;
    m_texture = game::LoadResource("TEXTURE", game::MakePath(BOX_TEXTURE_PATH));
}

void TexturedPrimitivesScene::OnUpdate(float dt)
{
    if (game::ActionPressed(static_cast<int>(ActionId::CycleNext)))
    {
        game::SwitchScene(DrawLoadScene::GetInstance());
        return;
    }
    if (game::ActionPressed(static_cast<int>(ActionId::CyclePrev)))
    {
        game::SwitchScene(ColouredPrimitivesScene::GetInstance());
        return;
    }

    m_time += dt;

    game::Clear(20, 23, 30);
    game::SetCamera3D(ORBIT_RADIUS * sinf(m_time * ORBIT_RATE), ORBIT_HEIGHT, ORBIT_RADIUS * cosf(m_time * ORBIT_RATE), 0.0f, 1.0f, 0.0f, 45.0f);
    game::DrawGrid(20, 1.0f);

    const bool ready = m_texture >= 0 && game::IsResourceReady(m_texture);
    if (ready)
    {
        game::DrawCubeTextured(-3.0f, 1.0f, 0.0f, 2.0f, m_texture);
        game::DrawCubeTextured(0.0f, 1.0f, 0.0f, 2.0f, m_texture);
        game::DrawCubeTextured(3.0f, 1.0f, 0.0f, 2.0f, m_texture);
    }

    game::Panel("TEXTURED", 16, 16, 280, 90);
    game::Label(ready ? "TEXTURE READY" : (m_texture >= 0 ? "TEXTURE LOADING" : "TEXTURE MISSING"));
    game::Label("R1/L1: NEXT SCENE");
    game::EndPanel();
}
