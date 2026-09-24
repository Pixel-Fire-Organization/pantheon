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

#include "core/EngineCore.h"
#include "level/EngineSector.h"

static void DrawGroundGridBox(const Vector3& min, const Vector3& max, Color3 c)
{
    Renderer* renderer = Engine_GetRenderer();
    if (!renderer) return;

    float thickness = 0.4f; // slightly thicker so it's visible on the ground
    float y = 0.5f; // hover slightly above 0 to prevent z-fighting with the ground

    // 4 edges to form a flat square on the ground
    Vector3 cX = {(min.x+max.x)*0.5f, y, min.z};
    Vector3 sX = {max.x-min.x, thickness, thickness};
    renderer->AddPrimitiveToDrawList(Primitive3D::Cube, cX, Vector3{0,0,0}, sX, c);
    
    cX.z = max.z; 
    renderer->AddPrimitiveToDrawList(Primitive3D::Cube, cX, Vector3{0,0,0}, sX, c);

    Vector3 cZ = {min.x, y, (min.z+max.z)*0.5f};
    Vector3 sZ = {thickness, thickness, max.z-min.z};
    renderer->AddPrimitiveToDrawList(Primitive3D::Cube, cZ, Vector3{0,0,0}, sZ, c);
    
    cZ.x = max.x; 
    renderer->AddPrimitiveToDrawList(Primitive3D::Cube, cZ, Vector3{0,0,0}, sZ, c);
}

void MainScene::OnLevelUpdate(float dt)
{
    MovePlayer(dt);
    MoveCamera(dt);

    game::Clear(20, 20, 26);
    game::DrawGrid(GRID_SLICES, GRID_SPACING);

    // Draw sector boundaries on the ground
    uint32_t count = 0;
    const SectorResident* res = Engine_Sector_GetResidents(&count);
    for (uint32_t i = 0; i < count; ++i)
    {
        if (res[i].state == SECTOR_READY)
        {
            DrawGroundGridBox(res[i].bounds.min, res[i].bounds.max, Color3{0.f, 1.f, 0.f}); // Green for LOD0
        }
    }
    const SectorResident* lod1 = Engine_Sector_GetLod1Residents(&count);
    for (uint32_t i = 0; i < count; ++i)
    {
        if (lod1[i].state == SECTOR_READY)
        {
            DrawGroundGridBox(lod1[i].bounds.min, lod1[i].bounds.max, Color3{0.f, 0.5f, 1.f}); // Light Blue for LOD1
        }
    }

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
