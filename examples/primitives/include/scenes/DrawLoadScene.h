#pragma once

#include "scenes/Scene.h"

class DrawLoadScene final : public game::Scene
{
public:
    static DrawLoadScene* GetInstance();

    void OnStart() override;
    void OnUpdate(float dt) override;

private:
    int m_count = 64;
    float m_time = 0.0f;
    static DrawLoadScene* m_instance;
};
