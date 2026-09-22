#include "scenes/MaterialLightingScene.h"

#include <cmath>

#include "EcsComponents.h" // generated (build dir): declares Ecs_SpawnDispatch
#include "GameAPI.h"

namespace
{
    // Point light (slot 0): orbits the focus area, topping up the level's own
    // static bake in real time -- moving it closer to a wall/mound sector
    // should visibly brighten that sector's already-baked lighting.
    const float POINT_ORBIT_RADIUS = 7.0f;
    const float POINT_ORBIT_HEIGHT = 4.5f;
    const float POINT_ORBIT_RATE = 0.35f;
    const int POINT_COLOR_R = 255;
    const int POINT_COLOR_G = 190;
    const int POINT_COLOR_B = 120;
    const float POINT_INTENSITY = 3.0f;
    const float POINT_RANGE = 20.0f;

    // Directional light (slot 1): the single real-time shadow caster. Slowly
    // sweeps azimuth like a moving sun so the primitives' shadow direction
    // visibly changes -- direction is the way the light TRAVELS (matches
    // graphics/Primitives.h's Light3D default of straight down), so a
    // downward-shining sun needs a negative Y component.
    const float DIR_ROTATE_RATE = 0.12f;
    const float DIR_ELEVATION_DEG = 55.0f;
    const int DIR_COLOR_R = 210;
    const int DIR_COLOR_G = 215;
    const int DIR_COLOR_B = 235;
    const float DIR_INTENSITY = 1.1f;

    const int AMBIENT_R = 26;
    const int AMBIENT_G = 27;
    const int AMBIENT_B = 34;

    // Dynamic primitives (immediate-mode; no material assignment is possible
    // on this path -- see the panel text and the header comment).
    const float PRIM_SIZE = 1.6f;
    const float PRIM_GROUND_Y_OFFSET = -0.7f;
} // namespace

MaterialLightingScene* MaterialLightingScene::m_instance = nullptr;

MaterialLightingScene* MaterialLightingScene::GetInstance()
{
    if (!m_instance)
        m_instance = new MaterialLightingScene();
    return m_instance;
}

const char* MaterialLightingScene::GetLevelName() const { return LEVEL_NAME; }

game::SpawnHandler MaterialLightingScene::GetSpawnHandler() const { return &Ecs_SpawnDispatch; }

void MaterialLightingScene::OnLevelStart() { m_time = 0.0f; }

void MaterialLightingScene::GetStreamingCenter(float* outX, float* outZ) const
{
    *outX = m_focusX;
    *outZ = m_focusZ;
}

void MaterialLightingScene::OnLevelUpdate(float dt)
{
    m_time += dt;

    MoveFocus(dt);
    OrbitCamera(dt);

    game::Clear(14, 15, 20);
    game::DrawGrid(GRID_SLICES, GRID_SPACING);

    UpdateDynamicLights(dt);
    DrawDynamicPrimitives(dt);

    game::Panel("MATERIALS + LIGHTING", 16, 16, 360, 176);
    game::Label("STONEWALL_01 / GROUNDDIRT_01: BAKED PBR MATERIALS");
    game::Label("(NORMAL + ROUGHNESS + AO, LEVEL-COMPILER BAKE)");
    game::Label("ORANGE LIGHT: DYNAMIC, TOPS UP THE BAKE LIVE");
    game::Label("COOL LIGHT: DIRECTIONAL, CASTS REAL-TIME SHADOWS");
    game::Label("CUBE/SPHERE/CYLINDER: DYNAMIC, FLAT-SHADED ONLY");
    game::Label("(NO PER-INSTANCE MATERIAL ON PRIMITIVES)");
    game::Label("MODELS CAN'T CARRY MATERIALS YET -- RUNTIME ENTITY");
    game::Label("SPAWN/MODEL SYSTEM IS UNIMPLEMENTED PROJECT-WIDE");
    game::Label("LEFT STICK: MOVE FOCUS  RIGHT STICK: ORBIT CAMERA");
    game::EndPanel();
}

void MaterialLightingScene::MoveFocus(float dt)
{
    float moveX = 0.0f;
    float moveZ = 0.0f;
    game::GetJoyAxis(0, "left", &moveX, &moveZ);
    m_focusX += moveX * FOCUS_MOVE_SPEED * dt;
    m_focusZ += moveZ * FOCUS_MOVE_SPEED * dt;

    if (game::IsPadPressed(0, "l1"))
        m_focusY += FOCUS_LIFT_SPEED * dt;
    if (game::IsPadPressed(0, "l2"))
        m_focusY -= FOCUS_LIFT_SPEED * dt;
}

void MaterialLightingScene::OrbitCamera(float dt)
{
    float turnX = 0.0f;
    float turnY = 0.0f;
    game::GetJoyAxis(0, "right", &turnX, &turnY);
    m_camYaw += turnX * CAMERA_LOOK_SPEED * dt;
    m_camPitch += turnY * CAMERA_LOOK_SPEED * dt;
    if (m_camPitch > PITCH_MAX)
        m_camPitch = PITCH_MAX;
    if (m_camPitch < PITCH_MIN)
        m_camPitch = PITCH_MIN;

    const float flat = cosf(m_camPitch);
    game::SetCamera3D(m_focusX + CAMERA_DISTANCE * flat * sinf(m_camYaw), m_focusY + CAMERA_DISTANCE * sinf(m_camPitch), m_focusZ + CAMERA_DISTANCE * flat * cosf(m_camYaw), m_focusX, m_focusY,
                      m_focusZ, 45.0f);
}

void MaterialLightingScene::UpdateDynamicLights(float dt)
{
    (void)dt;

    const float px = m_focusX + POINT_ORBIT_RADIUS * cosf(m_time * POINT_ORBIT_RATE);
    const float pz = m_focusZ + POINT_ORBIT_RADIUS * sinf(m_time * POINT_ORBIT_RATE);
    const float py = m_focusY + POINT_ORBIT_HEIGHT;
    game::SetLight(0, false, px, py, pz, POINT_COLOR_R, POINT_COLOR_G, POINT_COLOR_B, POINT_INTENSITY, POINT_RANGE);

    const float elevRad = DIR_ELEVATION_DEG * 3.14159265f / 180.0f;
    const float azimuth = m_time * DIR_ROTATE_RATE;
    const float horizontal = cosf(elevRad);
    const float dx = horizontal * cosf(azimuth);
    const float dy = -sinf(elevRad);
    const float dz = horizontal * sinf(azimuth);
    game::SetLight(1, true, dx, dy, dz, DIR_COLOR_R, DIR_COLOR_G, DIR_COLOR_B, DIR_INTENSITY, 0.0f);

    game::SetAmbientLight(AMBIENT_R, AMBIENT_G, AMBIENT_B);
    game::SetShadowCaster(1);
}

void MaterialLightingScene::DrawDynamicPrimitives(float dt)
{
    (void)dt;

    const float groundY = m_focusY + PRIM_GROUND_Y_OFFSET;

    const float cubeX = m_focusX - 2.6f + sinf(m_time * 0.7f) * 1.4f;
    game::DrawCube(cubeX, groundY, m_focusZ - 1.4f, PRIM_SIZE, 217, 64, 64);

    const float sphereY = groundY + fabsf(sinf(m_time * 1.1f)) * 1.2f;
    game::DrawSphere(m_focusX, sphereY, m_focusZ - 2.6f, PRIM_SIZE, 64, 191, 115);

    const float cylX = m_focusX + 2.6f + cosf(m_time * 0.5f) * 1.0f;
    const float cylZ = m_focusZ - 1.4f + sinf(m_time * 0.5f) * 1.0f;
    game::DrawCylinder(cylX, groundY, cylZ, PRIM_SIZE, 89, 140, 230);
}
