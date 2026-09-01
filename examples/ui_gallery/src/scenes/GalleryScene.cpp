#include "scenes/GalleryScene.h"

#include <cstdio>

#include "ActionIds.h"
#include "GameAPI.h"
#include "platform/Platform.h"
#include "scenes/BudgetScene.h"
#include "scenes/StyleScene.h"
#include "ui/EngineUi.h"

namespace
{
    const int PAGE_COUNT = 3;
    const int PLOT_SAMPLES = 48;
    const int BOX_HEIGHT = 90;
    const int LIST_BOX_HEIGHT = 70;

    bool s_Toggle = true;
    int s_Slider = 32;
    float s_Wave[PLOT_SAMPLES];
    float s_Phase = 0.0f;

    const char* const kChoices[] = {"ALPHA", "BETA", "GAMMA"};
    const int kChoiceCount = static_cast<int>(sizeof(kChoices) / sizeof(kChoices[0]));
    int s_ComboIndex = 0;
    int s_ListIndex = 0;
    bool s_DisabledDummy = false;
    const char* s_LastMenuPick = "NONE";

    char s_PlayerName[32] = "PLAYER ONE";
    bool s_ShowMessage = false;
    bool s_ShowConfirm = false;
    bool s_ShowTextDialog = false;
    char s_TextDialogBuffer[32] = "EDIT ME";
    const char* s_LastDialogResult = "NONE";

    struct SampleRow
    {
        const char* name;
        int value;
    };
    const SampleRow kSampleRows[] = {
        {"ATTACK", 12}, {"DEFENSE", 8}, {"SPEED", 21},
    };
    const int kSampleRowCount = static_cast<int>(sizeof(kSampleRows) / sizeof(kSampleRows[0]));

    const char* const kGlyphRows[] = {
        "ABCDEFGHIJKLM",
        "NOPQRSTUVWXYZ",
        "0123456789",
        "+-/*=<>()[]",
        ".,:;!?%_#@",
    };

    const int kGlyphRowCount = static_cast<int>(sizeof(kGlyphRows) / sizeof(kGlyphRows[0]));

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

    void DrawWidgets()
    {
        Ui_Header("WIDGETS");
        Ui_Label("LABEL");
        Ui_LabelColored("DIM LABEL", UiColor::TextDim);
        Ui_LabelColored("WARNING", UiColor::TextWarn);
        Ui_LabelValue("LABEL VALUE", "RIGHT");
        Ui_Separator();

        const int third = Ui_ContentWidth() / 3;
        const int barHeight = Ui_TextHeight(Ui_GetStyle().textScale) + Ui_GetStyle().rowPadding * 2;
        Ui_SameLine(third);
        Ui_Button("LEFT");
        Ui_SameLine(Ui_GetStyle().itemSpacing * 2 + Ui_GetStyle().borderWidth);
        Ui_SeparatorVertical(barHeight);
        Ui_SameLine(0);
        Ui_Button("RIGHT");

        Ui_Button("BUTTON");
        Ui_Selectable("SELECTABLE", false);
        Ui_Selectable("SELECTED", true);
        Ui_Checkbox("CHECKBOX", &s_Toggle);
        Ui_SliderInt("SLIDER", &s_Slider, 0, 100);
        Ui_Bar("BAR", s_Slider, 100);
        Ui_Plot("PLOT", s_Wave, PLOT_SAMPLES, -1.0f, 1.0f, 0.0f);
        Ui_BeginPointBox("POINT BOX", BOX_HEIGHT);
        Ui_Point(0.5f, 0.5f, UiColor::TextAccent);
        Ui_Point(0.1f, 0.9f, UiColor::BarFill);
        Ui_Point(0.9f, 0.1f, UiColor::TextWarn);
        Ui_EndPointBox();
    }

    void DrawMore()
    {
        if (Ui_BeginMenuBar())
        {
            if (Ui_BeginMenu("FILE"))
            {
                if (Ui_MenuItem("NEW", ""))
                    s_LastMenuPick = "NEW";
                if (Ui_MenuItem("OPEN", ""))
                    s_LastMenuPick = "OPEN";
                if (Ui_MenuItem("SAVE", "CTRL+S"))
                    s_LastMenuPick = "SAVE";
                Ui_EndMenu();
            }
            if (Ui_BeginMenu("VIEW"))
            {
                if (Ui_MenuItem("GRID", ""))
                    s_LastMenuPick = "GRID";
                if (Ui_MenuItem("LIST", ""))
                    s_LastMenuPick = "LIST";
                Ui_EndMenu();
            }
            Ui_EndMenuBar();
        }
        Ui_LabelValue("MENU PICK", s_LastMenuPick);

        Ui_Separator();
        Ui_Header("SPLIT BUTTON");
        if (Ui_SplitButton("EXPORT", UiIcon::ChevronDown))
            s_LastMenuPick = "EXPORT (PRIMARY)";
        if (Ui_BeginMenu("EXPORT"))
        {
            if (Ui_MenuItem("AS PNG", ""))
                s_LastMenuPick = "EXPORT AS PNG";
            if (Ui_MenuItem("AS JSON", ""))
                s_LastMenuPick = "EXPORT AS JSON";
            Ui_EndMenu();
        }

        Ui_Separator();
        Ui_Header("CHOICE AND LIST");
        Ui_Combo("COMBO", &s_ComboIndex, kChoices, kChoiceCount);
        Ui_ListBox("listbox", &s_ListIndex, kChoices, kChoiceCount, LIST_BOX_HEIGHT);

        Ui_Separator();
        Ui_Header("DISABLED SCOPE");
        Ui_Button("ENABLED BUTTON");
        Ui_BeginDisabled(true);
        Ui_Button("DISABLED BUTTON");
        Ui_Checkbox("DISABLED CHECKBOX", &s_DisabledDummy);
        Ui_EndDisabled();

        Ui_Separator();
        Ui_Header("DIALOGS AND TEXT ENTRY");
        if (Ui_TextInput("NAME", s_PlayerName, sizeof(s_PlayerName)))
            s_LastDialogResult = "NAME CHANGED";
        Ui_LabelValue("DIALOG RESULT", s_LastDialogResult);
        Ui_SameLine(Ui_ContentWidth() / 3);
        if (Ui_Button("MESSAGE"))
            s_ShowMessage = true;
        Ui_SameLine(Ui_ContentWidth() / 3);
        if (Ui_Button("CONFIRM"))
            s_ShowConfirm = true;
        Ui_SameLine(0);
        if (Ui_Button("TEXT DIALOG"))
            s_ShowTextDialog = true;

        Ui_Separator();
        Ui_Header("TABLE");
        const int widths[] = {0, 60};
        if (Ui_BeginTable("stats", widths, 2))
        {
            const char* const headers[] = {"STAT", "VALUE"};
            Ui_TableHeader(headers);
            for (int i = 0; i < kSampleRowCount; ++i)
            {
                Ui_PushIdIndex(i);
                Ui_TableRow(kSampleRows[i].name, false);
                Ui_PopId();

                Ui_TableCell(kSampleRows[i].name, UiAlign::Left, UiColor::Text);
                char value[16];
                snprintf(value, sizeof(value), "%d", kSampleRows[i].value);
                Ui_TableCell(value, UiAlign::Right, UiColor::TextAccent);
            }
            Ui_EndTable();
        }

        Ui_Separator();
        Ui_Header("ICONS");
        const int iconCell = Ui_TextHeight(Ui_GetStyle().textScale) + Ui_GetStyle().iconSpacing * 2;
        Ui_SameLine(iconCell);
        Ui_Icon(UiIcon::ButtonSouth, UiColor::TextAccent);
        Ui_SameLine(iconCell);
        Ui_Icon(Ui_ButtonIcon(GamepadButton::Circle), UiColor::TextWarn);
        Ui_SameLine(iconCell);
        Ui_Icon(UiIcon::DPad, UiColor::Text);
        Ui_SameLine(iconCell);
        Ui_Icon(UiIcon::Warning, UiColor::TextWarn);
        Ui_SameLine(0);
        Ui_Icon(UiIcon::Check, UiColor::BarFill);

        const UiHint hints[] = {
            {Ui_ButtonIcon(GamepadButton::Cross), "ACCEPT"},
            {Ui_ButtonIcon(GamepadButton::Circle), "BACK"},
        };
        Ui_HintBar(hints, static_cast<int>(sizeof(hints) / sizeof(hints[0])));
    }

    void DrawGlyphs(int x, int y)
    {
        int cursorY = y;
        for (int scale = 1; scale <= 3; ++scale)
        {
            for (int row = 0; row < kGlyphRowCount; ++row)
            {
                Ui_Text(x, cursorY, scale, kGlyphRows[row], UiColor::Text);
                cursorY += Ui_TextHeight(scale) + 2;
            }
            cursorY += 6;
        }
    }
} // namespace

GalleryScene* GalleryScene::m_instance = nullptr;

GalleryScene* GalleryScene::GetInstance()
{
    if (!m_instance)
        m_instance = new GalleryScene();
    return m_instance;
}

void GalleryScene::OnUiStart()
{
    s_Phase = 0.0f;
    for (int i = 0; i < PLOT_SAMPLES; ++i)
        s_Wave[i] = 0.0f;
}

void GalleryScene::OnDrawUi(float dt)
{
    if (game::ActionPressed(static_cast<int>(ActionId::CycleNext)))
    {
        game::SwitchScene(StyleScene::GetInstance());
        return;
    }
    if (game::ActionPressed(static_cast<int>(ActionId::CyclePrev)))
    {
        game::SwitchScene(BudgetScene::GetInstance());
        return;
    }

    s_Phase += dt;
    for (int i = 0; i < PLOT_SAMPLES - 1; ++i)
        s_Wave[i] = s_Wave[i + 1];
    s_Wave[PLOT_SAMPLES - 1] = (s_Phase - static_cast<float>(static_cast<int>(s_Phase))) * 2.0f - 1.0f;

    Ui_Rect(0, 0, game::ScreenWidth(), game::ScreenHeight(), UiColor::WindowBackground);

    static const char* const PAGES[PAGE_COUNT] = {"WIDGETS", "MORE", "GLYPHS"};

    Ui_BeginPanelSlot("UI GALLERY", UiPanelSlot::Full);
    const int page = Tabs("pages", PAGES, PAGE_COUNT);

    if (page == 0)
    {
        DrawWidgets();

        char text[48];
        snprintf(text, sizeof(text), "%u OF %u", static_cast<unsigned>(Ui_QuadsUsed()), static_cast<unsigned>(Ui_QuadBudget()));
        Ui_LabelValue("QUADS", text);
        Ui_EndPanel();
        return;
    }

    if (page == 1)
    {
        DrawMore();
        Ui_EndPanel();

        if (s_ShowMessage)
        {
            const UiDialogResult result = Ui_MessageDialog("gallery-message", "MESSAGE", "A one-button acknowledgement, drawn by whichever mechanism this platform offers.");
            if (result != UiDialogResult::Pending)
            {
                s_ShowMessage = false;
                s_LastDialogResult = "MESSAGE DISMISSED";
            }
        }
        if (s_ShowConfirm)
        {
            const UiDialogResult result = Ui_ConfirmDialog("gallery-confirm", "CONFIRM", "Accept or cancel?");
            if (result == UiDialogResult::Accepted)
            {
                s_ShowConfirm = false;
                s_LastDialogResult = "CONFIRMED";
            }
            else if (result == UiDialogResult::Cancelled)
            {
                s_ShowConfirm = false;
                s_LastDialogResult = "CONFIRM CANCELLED";
            }
        }
        if (s_ShowTextDialog)
        {
            const UiDialogResult result = Ui_TextDialog("gallery-text", "EDIT VALUE", s_TextDialogBuffer, sizeof(s_TextDialogBuffer));
            if (result == UiDialogResult::Accepted)
            {
                s_ShowTextDialog = false;
                s_LastDialogResult = "TEXT DIALOG ACCEPTED";
            }
            else if (result == UiDialogResult::Cancelled)
            {
                s_ShowTextDialog = false;
                s_LastDialogResult = "TEXT DIALOG CANCELLED";
            }
        }
        return;
    }

    Ui_Label("SCALES 1 2 3");
    const int glyphX = Ui_ContentX();
    const int glyphY = Ui_CursorY();
    Ui_EndPanel();
    DrawGlyphs(glyphX, glyphY);
}
