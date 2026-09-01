#pragma once

#include "scenes/UiScene.h"

class GalleryScene final : public game::UiScene
{
public:
    static GalleryScene* GetInstance();

protected:
    void OnUiStart() override;
    void OnDrawUi(float dt) override;

private:
    static GalleryScene* m_instance;
};
