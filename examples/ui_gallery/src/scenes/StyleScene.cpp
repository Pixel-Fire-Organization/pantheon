#include "scenes/StyleScene.h"

#include "ActionIds.h"
#include "GameAPI.h"
#include "scenes/BudgetScene.h"
#include "scenes/GalleryScene.h"
#include "ui/EngineUi.h"

namespace
{
    const int SWATCH_HEIGHT = 18;

    void ChannelSlider(const char* label, uint8_t* channel)
    {
        int value = static_cast<int>(*channel);
        if (Ui_SliderInt(label, &value, 0, 255))
            *channel = static_cast<uint8_t>(value);
    }
} // namespace

StyleScene* StyleScene::m_instance = nullptr;

StyleScene* StyleScene::GetInstance()
{
    if (!m_instance)
        m_instance = new StyleScene();
    return m_instance;
}

void StyleScene::OnUiStart() { m_role = 0; }

void StyleScene::OnDrawUi(float dt)
{
    (void)dt;

    if (game::ActionPressed(static_cast<int>(ActionId::CycleNext)))
    {
        game::SwitchScene(BudgetScene::GetInstance());
        return;
    }
    if (game::ActionPressed(static_cast<int>(ActionId::CyclePrev)))
    {
        game::SwitchScene(GalleryScene::GetInstance());
        return;
    }

    Ui_Rect(0, 0, game::ScreenWidth(), game::ScreenHeight(), UiColor::WindowBackground);

    Ui_BeginPanelSlot("ROLES", UiPanelSlot::Left);

    const int roles = static_cast<int>(UiColor::Count);
    if (Ui_BeginScroll("roles", Ui_ContentHeight()))
    {
        for (int i = 0; i < roles; ++i)
        {
            const UiColor role = static_cast<UiColor>(i);
            if (Ui_Selectable(Ui_ColorName(role), i == m_role))
                m_role = i;
        }
        Ui_EndScroll();
    }
    Ui_EndPanel();

    Ui_BeginPanelSlot("EDIT", UiPanelSlot::Right);
    if (m_role < 0 || m_role >= roles)
        m_role = 0;

    const UiColor role = static_cast<UiColor>(m_role);
    Ui_LabelValue("ROLE", Ui_ColorName(role));

    UiStyle style = Ui_GetStyle();
    UiRgba* colour = &style.colors[m_role];
    ChannelSlider("RED", &colour->r);
    ChannelSlider("GREEN", &colour->g);
    ChannelSlider("BLUE", &colour->b);
    Ui_SetStyle(style);

    Ui_RectRgba(Ui_ContentX(), Ui_CursorY(), Ui_ContentWidth(), SWATCH_HEIGHT * 2, style.colors[m_role]);
    Ui_Spacing(SWATCH_HEIGHT * 2 + 6);

    if (Ui_Button("RESET THEME"))
        Ui_SetStyle(Ui_DefaultStyle());

    Ui_Separator();
    Ui_Label("R1/L1 OR ARROWS: CYCLE SCENE");
    Ui_EndPanel();
}
