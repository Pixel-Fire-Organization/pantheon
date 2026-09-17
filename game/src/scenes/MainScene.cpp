#include <cmath>

#include "../../include/scenes/MainScene.h"
#include "EcsComponents.h"
#include "GameAPI.h"

MainScene* MainScene::m_instance = nullptr;

const MainScene* MainScene::GetInstance()
{
    if (!m_instance)
    {
        m_instance = new MainScene();
    }

    return m_instance;
}

const char* MainScene::GetLevelName() const { return LEVEL_NAME; }

game::SpawnHandler MainScene::GetSpawnHandler() const { return &Ecs_SpawnDispatch; }

const game::SceneResource* MainScene::GetResources(int* outCount) const
{
    static const game::SceneResource kResources[] = {
        {"TEXTURE", BOX_TEXTURE_PATH},
    };
    *outCount = 1;
    return kResources;
}

void MainScene::OnLevelStart()
{
    m_playerX = 0.0f;
    m_playerY = 0.0f;
    m_playerZ = 0.0f;
    m_yaw = 0.0f;
    m_pitch = 0.4f;
    m_texture = game::LoadResource("TEXTURE", game::MakePath(BOX_TEXTURE_PATH));
}

void MainScene::GetStreamingCenter(float* outX, float* outZ) const
{
    *outX = m_playerX;
    *outZ = m_playerZ;
}

void MainScene::OnLevelUpdate(float dt)
{
    MovePlayer(dt);
    MoveCamera(dt);

    game::Clear(20, 20, 26);
    game::DrawGrid(GRID_SLICES, GRID_SPACING);

    if (m_texture >= 0 && game::IsResourceReady(m_texture))
        game::DrawCubeTextured(m_playerX, m_playerY, m_playerZ, PLAYER_SIZE, m_texture);
    else
        game::DrawCube(m_playerX, m_playerY, m_playerZ, PLAYER_SIZE, 220, 60, 60);
}

void MainScene::MovePlayer(float dt)
{
    float moveX = 0.0f;
    float moveZ = 0.0f;
    game::GetJoyAxis(0, "left", &moveX, &moveZ);
    m_playerX += moveX * MOVE_SPEED * dt;
    m_playerZ += moveZ * MOVE_SPEED * dt;

    if (game::IsPadPressed(0, "l1"))
        m_playerY += LIFT_SPEED * dt;
    if (game::IsPadPressed(0, "l2"))
        m_playerY -= LIFT_SPEED * dt;
    if (game::IsPadPressed(0, "tri"))
        game::ReloadScene();
}

void MainScene::MoveCamera(float dt)
{
    float turnX = 0.0f;
    float turnY = 0.0f;
    game::GetJoyAxis(0, "right", &turnX, &turnY);
    m_yaw += turnX * CAMERA_SPEED * dt;
    m_pitch += turnY * CAMERA_SPEED * dt;
    if (m_pitch > PITCH_MAX)
        m_pitch = PITCH_MAX;
    if (m_pitch < PITCH_MIN)
        m_pitch = PITCH_MIN;

    const float flat = cosf(m_pitch);
    game::SetCamera3D(m_playerX + CAMERA_DISTANCE * flat * sinf(m_yaw), m_playerY + CAMERA_DISTANCE * sinf(m_pitch), m_playerZ + CAMERA_DISTANCE * flat * cosf(m_yaw), m_playerX, m_playerY, m_playerZ,
                      45.0f);
}
