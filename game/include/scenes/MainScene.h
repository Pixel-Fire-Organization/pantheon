#pragma once

#include "scenes/LevelScene.h"

class MainScene final : public game::LevelScene
{
public:
    static const MainScene* GetInstance();
    const game::SceneResource* GetResources(int* outCount) const override;

protected:
    const char* GetLevelName() const override;
    game::SpawnHandler GetSpawnHandler() const override;
    void OnLevelStart() override;
    void GetStreamingCenter(float* outX, float* outZ) const override;
    void OnLevelUpdate(float dt) override;

private:
    MainScene() = default;

    void MovePlayer(float dt);
    void MoveCamera(float dt);

    static MainScene* m_instance;

    float m_playerX = 0.0f;
    float m_playerY = 0.0f;
    float m_playerZ = 0.0f;
    float m_yaw = 0.0f;
    float m_pitch = 0.4f;
    int m_texture = -1;

    const char* const LEVEL_NAME = "MAINASSETSTEST";
    const char* const BOX_TEXTURE_PATH = "RASSETS\\BOX.PS2A";
    const float MOVE_SPEED = 6.0f;
    const float LIFT_SPEED = 4.0f;
    const float CAMERA_DISTANCE = 20.0f;
    const float CAMERA_SPEED = 1.8f;
    const float PITCH_MIN = 0.05f;
    const float PITCH_MAX = 1.45f;
    const float PLAYER_SIZE = 2.0f;
    const int GRID_SLICES = 100;
    const float GRID_SPACING = 1.0f;
};