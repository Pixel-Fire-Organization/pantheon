#pragma once

#include "scenes/UiScene.h"

class StyleScene final : public game::UiScene
{
public:
    static StyleScene* GetInstance();

protected:
    void OnUiStart() override;
    void OnDrawUi(float dt) override;

private:
    int m_role = 0;
    static StyleScene* m_instance;
};
