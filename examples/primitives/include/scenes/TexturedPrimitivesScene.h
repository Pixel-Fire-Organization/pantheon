#pragma once

#include "scenes/Scene.h"

class TexturedPrimitivesScene final : public game::Scene
{
public:
    static TexturedPrimitivesScene* GetInstance();

    void OnStart() override;
    void OnUpdate(float dt) override;
    const game::SceneResource* GetResources(int* outCount) const override;

private:
    const char* BOX_TEXTURE_PATH = "RASSETS\\BOX.PS2A";
    int m_texture = -1;
    float m_time = 0.0f;
    static TexturedPrimitivesScene* m_instance;
};
