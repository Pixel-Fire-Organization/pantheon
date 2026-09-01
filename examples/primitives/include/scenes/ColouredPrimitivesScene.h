#pragma once

#include "scenes/Scene.h"

class ColouredPrimitivesScene final : public game::Scene
{
public:
    static ColouredPrimitivesScene* GetInstance();

    void OnStart() override;
    void OnUpdate(float dt) override;

private:
    float m_time = 0.0f;
    static ColouredPrimitivesScene* m_instance;
};
