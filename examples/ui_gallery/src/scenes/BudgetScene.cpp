#include "scenes/BudgetScene.h"

#include "ActionIds.h"
#include "GameAPI.h"
#include "scenes/GalleryScene.h"
#include "scenes/StyleScene.h"
#include "ui/EngineUi.h"

namespace
{
    // Deliberately tight: thirty rows always overruns a cap of twenty quads
    // regardless of which font is active, so this scene exercises the drop
    // path rather than merely declaring it exists.
    const int CAPPED_LIST_QUAD_CAP = 20;
    const int CAPPED_LIST_ROW_COUNT = 30;

    int Tabs(const char* id, const char* const* names, int count)
    {
        if (count <= 1 || !Ui_BeginTabBar(id))
            return 0;

        int active = 0;
        for (int i = 0; i < count; ++i)
        {
            if (Ui_Tab(names[i]))
                active = i;
        }
        Ui_EndTabBar();
        return active;
    }
} // namespace

BudgetScene* BudgetScene::m_instance = nullptr;

BudgetScene* BudgetScene::GetInstance()
{
    if (!m_instance)
        m_instance = new BudgetScene();
    return m_instance;
}

void BudgetScene::OnUiStart()
{
    m_filled = 0;
    m_showModal = false;
}

void BudgetScene::OnDrawUi(float dt)
{
    (void)dt;

    if (game::ActionPressed(static_cast<int>(ActionId::CycleNext)))
    {
        game::SwitchScene(GalleryScene::GetInstance());
        return;
    }
    if (game::ActionPressed(static_cast<int>(ActionId::CyclePrev)))
    {
        game::SwitchScene(StyleScene::GetInstance());
        return;
    }

    Ui_Rect(0, 0, game::ScreenWidth(), game::ScreenHeight(), UiColor::WindowBackground);

    const float used = static_cast<float>(Ui_QuadsUsed());
    if (m_filled < PLOT_SAMPLES)
    {
        m_quads[m_filled++] = used;
    }
    else
    {
        for (int i = 1; i < PLOT_SAMPLES; ++i)
            m_quads[i - 1] = m_quads[i];
        m_quads[PLOT_SAMPLES - 1] = used;
    }

    static const char* const PAGES[] = {"BUDGETS", "WIDGETS", "SCROLL", "THEMES"};

    Ui_BeginPanelSlot("UI BUDGET", UiPanelSlot::Full);
    const int page = Tabs("pages", PAGES, 4);
    switch (page)
    {
    case 0:
        Ui_Bar("QUADS", static_cast<int>(Ui_QuadsUsed()), static_cast<int>(Ui_QuadBudget()));
        Ui_Bar("OVERLAY", static_cast<int>(Ui_OverlayQuadsUsed()), static_cast<int>(Ui_OverlayQuadBudget()));
        Ui_Bar("FOCUSABLES", static_cast<int>(Ui_FocusablesUsed()), static_cast<int>(Ui_FocusableBudget()));
        Ui_Bar("STATES", static_cast<int>(Ui_StatesUsed()), static_cast<int>(Ui_StateBudget()));
        Ui_Bar("CLIP DEPTH", static_cast<int>(Ui_ClipDepthUsed()), static_cast<int>(Ui_ClipDepthBudget()));
        Ui_Bar("RUNS", static_cast<int>(Ui_RunsUsed()), static_cast<int>(Ui_RunBudget()));

        Ui_Separator();
        Ui_LabelValue("FONT", Ui_FontIsCooked() ? "COOKED" : "BUILT-IN");
        Ui_LabelValue("THEME", Ui_ThemeName());
        Ui_LabelValueFormat("TEXT HEIGHT", "%d PX", Ui_TextHeight(Ui_GetStyle().textScale));
        Ui_LabelValueFormat("SAMPLE COST", "%d QUADS", Ui_MeasureTextQuads(Ui_GetStyle().textScale, "EXAMPLE GALLERY"));

        Ui_Separator();
        Ui_Plot("QUADS PER FRAME", m_quads, m_filled, 0.0f, static_cast<float>(Ui_QuadBudget()), 0.0f);
        break;
    case 1:
        if (Ui_BeginTree("A TREE", true))
        {
            Ui_Label("NESTED ROW ONE");
            Ui_Label("NESTED ROW TWO");
            Ui_EndTree();
        }

        Ui_Stepper("STEPPER", &m_stepper, 0, 32, 1);
        Ui_SliderFloat("SLIDER", &m_slider, 0.0f, 1.0f, 0.05f);
        Ui_Radio("FIRST", &m_radio, 0);
        Ui_Radio("SECOND", &m_radio, 1);
        Ui_ColorSwatch("ACCENT", Ui_GetColor(UiColor::TextAccent));

        Ui_Separator();
        if (Ui_Button("TOAST"))
            Ui_Toast("A NOTIFICATION", 2.5f);
        if (Ui_Button("MODAL"))
            m_showModal = true;

        Ui_Separator();
        Ui_Header("A CONTAINER CAPPED WITH Ui_BeginBudget");
        Ui_BeginBudget(CAPPED_LIST_QUAD_CAP);
        for (int i = 0; i < CAPPED_LIST_ROW_COUNT; ++i)
            Ui_LabelValueFormat("ROW", "%d", i);
        Ui_EndBudget();
        Ui_LabelValueFormat("CONTAINER QUADS", "%u OF %d", static_cast<unsigned>(Ui_ContainerQuadsUsed()), CAPPED_LIST_QUAD_CAP);
        Ui_LabelValue("A FEW MORE QUADS WOULD FIT", Ui_WouldFit(4) ? "YES" : "NO");

        Ui_Separator();
        Ui_LabelWrapped("This scene is the observable proof of the interface budget: every bar above is a real ceiling, and a screen that overruns one is reported once per frame.", UiColor::TextDim);
        break;
    case 2:
        Ui_Label("SCROLLS RATHER THAN PAGES");
        if (Ui_BeginScroll("rows", Ui_ContentHeight()))
        {
            for (int i = 0; i < 60; ++i)
                Ui_LabelValueFormat("ROW", "%d", i);
            Ui_EndScroll();
        }
        break;
    default:
        Ui_Label("BUILT IN, NO FILESYSTEM NEEDED");
        for (uint8_t i = 0; i < static_cast<uint8_t>(UiBuiltinTheme::Count); ++i)
        {
            const UiBuiltinTheme theme = static_cast<UiBuiltinTheme>(i);
            if (Ui_Selectable(Ui_BuiltinThemeName(theme), false))
                Ui_SetBuiltinTheme(theme);
        }

        Ui_Separator();
        Ui_Label("COOKED, LOADED ON COMMAND");
        if (Ui_Button("LOAD THEME_SLATE"))
            Ui_LoadTheme("RASSETS\\THEME_SLATE.PS2A");
        if (Ui_Button("LOAD THEME_CONTRAST"))
            Ui_LoadTheme("RASSETS\\THEME_CONTRAST.PS2A");
        if (Ui_Button("LOAD A THEME THAT IS NOT THERE"))
            Ui_LoadTheme("RASSETS\\THEME_NOPE.PS2A");
        break;
    }
    Ui_EndPanel();

    if (m_showModal && Ui_BeginModal("MODAL", 320, 140))
    {
        Ui_LabelWrapped("A modal dims what is behind it, which is the first thing per-quad transparency bought.", UiColor::Text);
        Ui_Spacing(8);
        if (Ui_Button("CLOSE") || Ui_WasBackPressed())
            m_showModal = false;
        Ui_EndModal();
    }
}
