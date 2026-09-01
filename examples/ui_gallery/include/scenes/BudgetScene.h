#pragma once

#include "scenes/UiScene.h"

class BudgetScene final : public game::UiScene
{
public:
    static BudgetScene* GetInstance();

protected:
    void OnUiStart() override;
    void OnDrawUi(float dt) override;

private:
    static const int PLOT_SAMPLES = 64;

    float m_quads[PLOT_SAMPLES] = {};
    int m_filled = 0;
    bool m_showModal = false;
    int m_stepper = 8;
    float m_slider = 0.5f;
    int m_radio = 0;

    static BudgetScene* m_instance;
};
