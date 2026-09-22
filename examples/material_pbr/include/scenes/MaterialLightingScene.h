#pragma once

#include "scenes/LevelScene.h"

// Loads MAINASSETSTEST (assets/maps/mainAssetsTest.map), whose StoneWall_01
// and GroundDirt_01 brushes pick up real PBR materials (assets/materials/env/
// StoneWall_01, .../GroundDirt_01 -- normal + roughness + ambient-occlusion,
// metallicFactor 0.0) purely by mirroring their texture path, and whose two
// "light" entities are baked into the sectors' static vertex colour at
// compile time. On top of that baked base this scene drives a moving dynamic
// point light and a directional shadow-caster light (GameAPI's SetLight /
// SetAmbientLight / SetShadowCaster), and draws a handful of primitives near
// the lit area so their real-time shadow is visible on the ground.
//
// Dynamic material-carrying models are deliberately not part of this demo:
// the runtime entity/model-spawn system is unimplemented project-wide (every
// Game_Spawn_<classname> handler in this example's own EcsHooks.cpp is a
// stub, mirroring game/src/EcsHooks.cpp) -- see the on-screen panel and
// docs/EXAMPLES.md.
class MaterialLightingScene final : public game::LevelScene
{
public:
    static MaterialLightingScene* GetInstance();

protected:
    const char* GetLevelName() const override;
    game::SpawnHandler GetSpawnHandler() const override;
    void OnLevelStart() override;
    void GetStreamingCenter(float* outX, float* outZ) const override;
    void OnLevelUpdate(float dt) override;

private:
    MaterialLightingScene() = default;

    void MoveFocus(float dt);
    void OrbitCamera(float dt);
    void UpdateDynamicLights(float dt);
    void DrawDynamicPrimitives(float dt);

    static MaterialLightingScene* m_instance;

    float m_time = 0.0f;

    // World position the demo is framed around -- the midpoint between the
    // StoneWall_01 wall and the GroundDirt_01 mound, both painted with the
    // hand-authored materials above. The player can still nudge it and orbit
    // freely; it never has to be re-found after moving away.
    float m_focusX = 10.5f;
    float m_focusY = 1.5f;
    float m_focusZ = 10.5f;

    float m_camYaw = -0.78f;
    float m_camPitch = 0.30f;

    const char* const LEVEL_NAME = "MAINASSETSTEST";
    const float FOCUS_MOVE_SPEED = 5.0f;
    const float FOCUS_LIFT_SPEED = 3.0f;
    const float CAMERA_DISTANCE = 16.0f;
    const float CAMERA_LOOK_SPEED = 1.6f;
    const float PITCH_MIN = 0.08f;
    const float PITCH_MAX = 1.30f;
    const int GRID_SLICES = 100;
    const float GRID_SPACING = 1.0f;
};
