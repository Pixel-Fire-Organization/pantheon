#include "../../include/ui/EngineUi.h"

#include <cstdio>

#include "../../include/core/EngineCore.h"
#include "../../include/core/EngineDebug.h"
#include "../../include/core/EngineInput.h"
#include "../../include/graphics/Renderer.h"
#include "UiInternal.h"

void UI::Reset()
{
    m_count = 0;
    m_dropped = 0;
}

bool UI::Add(const UiQuad& quad)
{
    if (m_count >= UI_MAX_QUADS)
    {
        ++m_dropped;
        return false;
    }
    m_quads[m_count++] = quad;
    return true;
}

namespace
{
    /// Draw the shared body of an activatable row and report what happened to it.
    /// @param label The row text, which also carries its identity.
    /// @param highlight Whether to draw it as the current choice.
    /// @param outX Receives the row's left edge.
    /// @param outY Receives the row's top edge.
    /// @param outW Receives the row's width.
    /// @return True on the frame the row is activated.
    bool ActivatableRow(const char* label, bool highlight, int* outX, int* outY, int* outW) { return UiInternal_ActivatableRow(UiInternal_Id(label), highlight, outX, outY, outW); }
} // namespace

bool Ui_BeginPanel(const char* title, int x, int y, int w, int h)
{
    if (!UiInternal_CanDraw())
        return false;

    UiFrameState& state = UiInternal_State();
    if (state.inPanel)
    {
        Engine_LogError("Ui: BeginPanel called inside an open panel");
        return false;
    }

    const bool backgroundImageActive = state.nextPanelBackgroundImageActive;
    const int32_t backgroundImageHandle = state.nextPanelBackgroundImageHandle;
    const UiRgba backgroundColor = state.nextPanelBackgroundColorActive ? state.nextPanelBackgroundColor : Ui_GetColor(UiColor::PanelBackground);
    const bool collapsible = state.nextPanelCollapsibleActive && state.nextPanelCollapsible && title && title[0];
    state.nextPanelBackgroundColorActive = false;
    state.nextPanelBackgroundImageActive = false;
    state.nextPanelCollapsibleActive = false;

    const UiStyle& style = Ui_GetStyle();
    state.inPanel = true;
    state.panelX = x;
    state.panelY = y;
    state.panelW = w;
    state.panelH = h;
    state.contentX = x + style.panelPadding;
    state.contentW = w - style.panelPadding * 2;
    state.cursorY = y + style.panelPadding;
    state.rowOpen = false;
    state.runActive = false;
    state.inlineActive = false;
    state.nextItemXActive = false;
    state.nextItemWidthActive = false;
    state.nextItemHeightActive = false;
    state.panelContentClipped = false;

    uint32_t backgroundTexture = 0;
    int backgroundTexW = 0;
    int backgroundTexH = 0;
    if (backgroundImageActive && UiInternal_ResolveTexture(backgroundImageHandle, &backgroundTexture, &backgroundTexW, &backgroundTexH) == UiTextureState::Ready)
        UiInternal_PushTexturedQuad(x, y, w, h, backgroundTexture, 0, 0, 65535, 65535, UiRgba{255, 255, 255, 255});
    else
        UiInternal_PushRect(x, y, w, h, backgroundColor);
    UiInternal_PushBorder(x, y, w, h, style.borderWidth, Ui_GetColor(UiColor::Border));
    UiInternal_PushClip(x + style.borderWidth, y + style.borderWidth, w - style.borderWidth * 2, h - style.borderWidth * 2);
    UiInternal_PushId(title && title[0] ? title : "panel");
    UiInternal_BeginFocusGroup(UiInternal_Id(title ? title : ""));

    UiState* collapseState = nullptr;
    bool collapsed = false;
    if (collapsible)
    {
        collapseState = UiInternal_StateFor(UiInternal_Id("__collapsed"));
        if (collapseState->whole == 0)
            collapseState->whole = 1;
        collapsed = collapseState->whole < 0;
    }

    if (title && title[0])
    {
        if (collapsible)
        {
            if (Ui_Selectable(title, !collapsed))
                collapseState->whole = collapsed ? 1 : -1;
        }
        else
        {
            const int height = Ui_TextHeight(style.textScale);
            UiFont_Draw(state.contentX, state.cursorY, style.textScale, title, Ui_GetColor(UiColor::Header));
            state.cursorY += height + style.itemSpacing;
        }
        UiInternal_PushRect(state.contentX, state.cursorY, state.contentW, style.borderWidth, Ui_GetColor(UiColor::Border));
        state.cursorY += style.borderWidth + style.itemSpacing;
    }

    if (collapsed && UiInternal_PushClip(state.contentX, state.cursorY, state.contentW, 0))
        state.panelContentClipped = true;

    return !collapsed;
}

void Ui_EndPanel()
{
    if (!UiInternal_CanDraw() || !UiInternal_State().inPanel)
        return;
    UiFrameState& state = UiInternal_State();
    if (state.panelContentClipped)
    {
        UiInternal_PopClip();
        state.panelContentClipped = false;
    }
    UiInternal_EndFocusGroup();
    UiInternal_PopId();
    UiInternal_PopClip();
    state.inPanel = false;
}

void Ui_SetNextPanelBackgroundColor(UiRgba color)
{
    if (!UiInternal_CanDraw())
        return;
    UiFrameState& state = UiInternal_State();
    state.nextPanelBackgroundColor = color;
    state.nextPanelBackgroundColorActive = true;
}

void Ui_SetNextPanelBackgroundImage(int32_t handle)
{
    if (!UiInternal_CanDraw())
        return;
    UiFrameState& state = UiInternal_State();
    state.nextPanelBackgroundImageHandle = handle;
    state.nextPanelBackgroundImageActive = true;
}

void Ui_SetNextPanelCollapsible(bool collapsible)
{
    if (!UiInternal_CanDraw())
        return;
    UiFrameState& state = UiInternal_State();
    state.nextPanelCollapsible = collapsible;
    state.nextPanelCollapsibleActive = true;
}

void Ui_Spacing(int pixels)
{
    if (!UiInternal_CanDraw())
        return;
    UiFrameState& state = UiInternal_State();
    state.cursorY += pixels;
    state.rowOpen = false;
    state.runActive = false;
    state.inlineActive = false;
    state.nextItemXActive = false;
    state.nextItemWidthActive = false;
    state.nextItemHeightActive = false;
}

void Ui_Separator()
{
    const UiStyle& style = Ui_GetStyle();
    int x = 0;
    int y = 0;
    int w = 0;
    bool visible = false;
    if (!UiInternal_CanDraw() || !UiInternal_TakeRow(style.borderWidth, &x, &y, &w, &visible) || !visible)
        return;
    UiInternal_PushRect(x, y, w, style.borderWidth, Ui_GetColor(UiColor::Border));
}

void Ui_SeparatorVertical(int height)
{
    const UiStyle& style = Ui_GetStyle();
    int x = 0;
    int y = 0;
    int w = 0;
    bool visible = false;
    if (!UiInternal_CanDraw() || !UiInternal_TakeRow(height, &x, &y, &w, &visible) || !visible)
        return;
    (void)w;
    UiInternal_PushRect(x, y, style.borderWidth, height, Ui_GetColor(UiColor::Border));
}

void Ui_Header(const char* text)
{
    const UiStyle& style = Ui_GetStyle();
    const int height = Ui_TextHeight(style.textScale);
    int x = 0;
    int y = 0;
    int w = 0;
    bool visible = false;
    if (!UiInternal_CanDraw() || !UiInternal_TakeRow(height + style.borderWidth + style.rowPadding, &x, &y, &w, &visible) || !visible)
        return;

    UiFont_Draw(x, y, style.textScale, text, Ui_GetColor(UiColor::Header));
    UiInternal_PushRect(x, y + height + style.rowPadding - style.borderWidth, w, style.borderWidth, Ui_GetColor(UiColor::Border));
}

int Ui_ContentX() { return UiInternal_CanDraw() ? UiInternal_State().contentX : 0; }

int Ui_ContentWidth() { return UiInternal_CanDraw() ? UiInternal_State().contentW : 0; }

int Ui_CursorX()
{
    if (!UiInternal_CanDraw())
        return 0;
    const UiFrameState& state = UiInternal_State();
    if (state.nextItemXActive)
        return state.nextItemX;
    if (state.inlineActive)
        return state.inlineX;
    return state.contentX;
}

int Ui_CursorY() { return UiInternal_CanDraw() ? UiInternal_State().cursorY : 0; }

void Ui_SetCursorY(int y)
{
    if (!UiInternal_CanDraw())
        return;
    UiFrameState& state = UiInternal_State();
    state.cursorY = y;
    state.rowOpen = false;
    state.runActive = false;
    state.inlineActive = false;
    state.nextItemXActive = false;
    state.nextItemWidthActive = false;
    state.nextItemHeightActive = false;
}

void Ui_SetCursorX(int x)
{
    if (!UiInternal_CanDraw())
        return;
    UiFrameState& state = UiInternal_State();
    if (!state.inPanel)
        return;

    state.nextItemX = x;
    state.nextItemXActive = true;
}

void Ui_SetCursor(int x, int y)
{
    Ui_SetCursorY(y);
    Ui_SetCursorX(x);
}

int Ui_ContentHeight()
{
    if (!UiInternal_CanDraw())
        return 0;
    const UiFrameState& state = UiInternal_State();
    if (!state.inPanel)
        return 0;
    const int bottom = state.panelY + state.panelH - Ui_GetStyle().panelPadding;
    const int left = bottom - state.cursorY;
    return (left > 0) ? left : 0;
}

void Ui_LabelColored(const char* text, UiColor role)
{
    const UiStyle& style = Ui_GetStyle();
    int x = 0;
    int y = 0;
    int w = 0;
    bool visible = false;
    if (!UiInternal_CanDraw() || !UiInternal_TakeRow(Ui_TextHeight(style.textScale), &x, &y, &w, &visible) || !visible)
        return;
    UiFont_Draw(x, y, style.textScale, text, Ui_GetColor(role));
}

void Ui_Label(const char* text) { Ui_LabelColored(text, UiColor::Text); }

void Ui_LabelValue(const char* label, const char* value)
{
    const UiStyle& style = Ui_GetStyle();
    int x = 0;
    int y = 0;
    int w = 0;
    bool visible = false;
    if (!UiInternal_CanDraw() || !UiInternal_TakeRow(Ui_TextHeight(style.textScale), &x, &y, &w, &visible) || !visible)
        return;

    UiFont_Draw(x, y, style.textScale, label, Ui_GetColor(UiColor::TextDim));
    const int valueWidth = Ui_TextWidth(style.textScale, value);
    UiFont_Draw(x + w - valueWidth, y, style.textScale, value, Ui_GetColor(UiColor::Text));
}

bool Ui_Button(const char* label)
{
    const UiStyle& style = Ui_GetStyle();
    int x = 0;
    int y = 0;
    int w = 0;
    const bool activated = ActivatableRow(label, false, &x, &y, &w);
    if (w == 0)
        return false;

    const int textWidth = Ui_TextWidth(style.textScale, label);
    UiFont_Draw(x + (w - textWidth) / 2, y + style.rowPadding, style.textScale, label, Ui_GetColor(UiInternal_TextRole(UiColor::Text)));
    return activated;
}

bool Ui_Selectable(const char* label, bool selected)
{
    const UiStyle& style = Ui_GetStyle();
    int x = 0;
    int y = 0;
    int w = 0;
    const bool activated = ActivatableRow(label, selected, &x, &y, &w);
    if (w == 0)
        return false;

    const int caretWidth = Ui_TextWidth(style.textScale, "> ");
    const UiColor role = UiInternal_TextRole(selected ? UiColor::TextAccent : UiColor::Text);
    if (selected)
        UiFont_Draw(x + style.rowPadding, y + style.rowPadding, style.textScale, ">", Ui_GetColor(role));
    UiFont_Draw(x + style.rowPadding + caretWidth, y + style.rowPadding, style.textScale, label, Ui_GetColor(role));
    return activated;
}

bool Ui_SelectableValue(const char* label, const char* value, bool selected)
{
    const UiStyle& style = Ui_GetStyle();
    int x = 0;
    int y = 0;
    int w = 0;
    const bool activated = ActivatableRow(label, selected, &x, &y, &w);
    if (w == 0)
        return false;

    UiFont_Draw(x + style.rowPadding, y + style.rowPadding, style.textScale, label, Ui_GetColor(UiInternal_TextRole(selected ? UiColor::TextAccent : UiColor::Text)));

    const int valueWidth = Ui_TextWidth(style.textScale, value);
    UiFont_Draw(x + w - valueWidth - style.rowPadding, y + style.rowPadding, style.textScale, value, Ui_GetColor(UiInternal_TextRole(UiColor::TextDim)));
    return activated;
}

bool Ui_Checkbox(const char* label, bool* value)
{
    const UiStyle& style = Ui_GetStyle();
    int x = 0;
    int y = 0;
    int w = 0;
    const bool activated = ActivatableRow(label, false, &x, &y, &w);
    if (w == 0)
        return false;

    const int box = Ui_TextHeight(style.textScale);
    const int boxX = x + w - box - style.rowPadding;
    const int boxY = y + style.rowPadding;
    UiFont_Draw(x + style.rowPadding, boxY, style.textScale, label, Ui_GetColor(UiInternal_TextRole(UiColor::Text)));
    UiInternal_PushBorder(boxX, boxY, box, box, style.borderWidth, Ui_GetColor(UiColor::Border));

    const bool on = value && *value;
    if (on)
        UiInternal_PushRect(boxX + 2, boxY + 2, box - 4, box - 4, Ui_GetColor(UiColor::TextAccent));

    if (activated && value)
    {
        *value = !*value;
        return true;
    }
    return false;
}

bool Ui_SliderInt(const char* label, int* value, int minimum, int maximum)
{
    const UiStyle& style = Ui_GetStyle();
    int x = 0;
    int y = 0;
    int w = 0;
    const bool activated = ActivatableRow(label, false, &x, &y, &w);
    if (w == 0 || !value)
        return false;

    UiFrameState& state = UiInternal_State();
    const uint32_t id = UiInternal_Id(label);
    const bool focused = state.focusId == id;

    char readout[64];
    snprintf(readout, sizeof(readout), "%d", *value);
    UiFont_Draw(x + style.rowPadding, y + style.rowPadding, style.textScale, label, Ui_GetColor(UiInternal_TextRole(UiColor::Text)));
    const int readoutWidth = Ui_TextWidth(style.textScale, readout);
    UiFont_Draw(x + w - readoutWidth - style.rowPadding, y + style.rowPadding, style.textScale, readout, Ui_GetColor(UiInternal_TextRole(UiColor::TextAccent)));

    if (!focused)
        return false;

    int step = state.horizontalRepeat;
    if (step != 0)
        state.consumedHorizontal = true;

    const int range = maximum - minimum;
    const int grain = (range > 32) ? (range / 32) : 1;
    step *= grain;

    if (step == 0)
        return activated;

    int next = *value + step;
    if (next < minimum)
        next = minimum;
    if (next > maximum)
        next = maximum;
    if (next == *value)
        return false;

    *value = next;
    return true;
}


bool Ui_SliderFloat(const char* label, float* value, float minimum, float maximum, float step)
{
    const UiStyle& style = Ui_GetStyle();
    int x = 0;
    int y = 0;
    int w = 0;
    const bool activated = ActivatableRow(label, false, &x, &y, &w);
    if (w == 0 || !value)
        return false;

    UiFrameState& state = UiInternal_State();
    const bool focused = state.focusId == UiInternal_Id(label);

    char readout[32];
    snprintf(readout, sizeof(readout), "%.3f", static_cast<double>(*value));
    UiFont_Draw(x + style.rowPadding, y + style.rowPadding, style.textScale, label, Ui_GetColor(UiInternal_TextRole(UiColor::Text)));
    const int readoutWidth = Ui_TextWidth(style.textScale, readout);
    UiFont_Draw(x + w - readoutWidth - style.rowPadding, y + style.rowPadding, style.textScale, readout, Ui_GetColor(UiInternal_TextRole(UiColor::TextAccent)));

    if (!focused)
        return false;

    const int direction = state.horizontalRepeat;
    if (direction == 0)
        return activated;
    state.consumedHorizontal = true;

    float next = *value + static_cast<float>(direction) * step;
    if (next < minimum)
        next = minimum;
    if (next > maximum)
        next = maximum;
    if (next == *value)
        return false;

    *value = next;
    return true;
}

bool Ui_Stepper(const char* label, int* value, int minimum, int maximum, int step)
{
    const UiStyle& style = Ui_GetStyle();
    int x = 0;
    int y = 0;
    int w = 0;
    const bool activated = ActivatableRow(label, false, &x, &y, &w);
    if (w == 0 || !value)
        return false;

    UiFrameState& state = UiInternal_State();
    const bool focused = state.focusId == UiInternal_Id(label);

    char readout[32];
    snprintf(readout, sizeof(readout), "< %d >", *value);
    UiFont_Draw(x + style.rowPadding, y + style.rowPadding, style.textScale, label, Ui_GetColor(UiInternal_TextRole(UiColor::Text)));
    const int readoutWidth = Ui_TextWidth(style.textScale, readout);
    UiFont_Draw(x + w - readoutWidth - style.rowPadding, y + style.rowPadding, style.textScale, readout, Ui_GetColor(UiInternal_TextRole(focused ? UiColor::TextAccent : UiColor::TextDim)));

    if (!focused)
        return false;

    const int direction = state.horizontalRepeat;
    if (direction == 0)
        return activated;
    state.consumedHorizontal = true;

    int next = *value + direction * step;
    if (next < minimum)
        next = minimum;
    if (next > maximum)
        next = maximum;
    if (next == *value)
        return false;

    *value = next;
    return true;
}

bool Ui_Combo(const char* label, int* index, const char* const* items, int count)
{
    const UiStyle& style = Ui_GetStyle();
    int x = 0;
    int y = 0;
    int w = 0;
    const bool activated = ActivatableRow(label, false, &x, &y, &w);
    if (w == 0 || !index || !items || count <= 0)
        return false;

    if (*index < 0 || *index >= count)
        *index = 0;

    UiFrameState& state = UiInternal_State();
    const bool focused = state.focusId == UiInternal_Id(label);

    char readout[UI_TEXT_MAX];
    snprintf(readout, sizeof(readout), "< %s >", items[*index]);
    UiFont_Draw(x + style.rowPadding, y + style.rowPadding, style.textScale, label, Ui_GetColor(UiInternal_TextRole(UiColor::Text)));
    const int readoutWidth = Ui_TextWidth(style.textScale, readout);
    UiFont_Draw(x + w - readoutWidth - style.rowPadding, y + style.rowPadding, style.textScale, readout, Ui_GetColor(UiInternal_TextRole(focused ? UiColor::TextAccent : UiColor::TextDim)));

    if (!focused)
        return false;

    const int direction = state.horizontalRepeat;
    if (direction == 0)
        return activated;
    state.consumedHorizontal = true;

    int next = (*index + direction) % count;
    if (next < 0)
        next += count;
    if (next == *index)
        return false;

    *index = next;
    return true;
}

bool Ui_Radio(const char* label, int* value, int option)
{
    const UiStyle& style = Ui_GetStyle();
    int x = 0;
    int y = 0;
    int w = 0;
    const bool selected = value && (*value == option);
    const bool activated = ActivatableRow(label, selected, &x, &y, &w);
    if (w == 0 || !value)
        return false;

    const int box = Ui_TextHeight(style.textScale);
    const int boxX = x + style.rowPadding;
    const int boxY = y + style.rowPadding;
    UiInternal_PushBorder(boxX, boxY, box, box, style.borderWidth, Ui_GetColor(UiColor::Border));
    if (selected)
        UiInternal_PushRect(boxX + 3, boxY + 3, box - 6, box - 6, Ui_GetColor(UiColor::TextAccent));

    UiFont_Draw(boxX + box + style.rowPadding, boxY, style.textScale, label, Ui_GetColor(UiInternal_TextRole(selected ? UiColor::TextAccent : UiColor::Text)));

    if (activated && *value != option)
    {
        *value = option;
        return true;
    }
    return false;
}

void Ui_ColorSwatch(const char* label, UiRgba color)
{
    const UiStyle& style = Ui_GetStyle();
    const int height = Ui_TextHeight(style.textScale) + style.rowPadding * 2;
    int x = 0;
    int y = 0;
    int w = 0;
    bool visible = false;
    if (!UiInternal_CanDraw() || !UiInternal_TakeRow(height, &x, &y, &w, &visible) || !visible)
        return;

    UiFont_Draw(x, y + style.rowPadding, style.textScale, label, Ui_GetColor(UiColor::TextDim));

    const int box = height - style.rowPadding;
    const int boxX = x + w - box;
    UiInternal_PushRect(boxX, y, box, box, color);
    UiInternal_PushBorder(boxX, y, box, box, style.borderWidth, Ui_GetColor(UiColor::Border));
}

void Ui_Bar(const char* label, int value, int maximum)
{
    const UiStyle& style = Ui_GetStyle();
    const int textHeight = Ui_TextHeight(style.textScale);
    const int height = textHeight + style.rowPadding + style.barHeight;
    int x = 0;
    int y = 0;
    int w = 0;
    bool visible = false;
    if (!UiInternal_CanDraw() || !UiInternal_TakeRow(height, &x, &y, &w, &visible) || !visible)
        return;

    char readout[64];
    snprintf(readout, sizeof(readout), "%d / %d", value, maximum);
    UiFont_Draw(x, y, style.textScale, label, Ui_GetColor(UiColor::TextDim));
    const int readoutWidth = Ui_TextWidth(style.textScale, readout);
    UiFont_Draw(x + w - readoutWidth, y, style.textScale, readout, Ui_GetColor(UiColor::Text));

    const int trackY = y + textHeight + style.rowPadding;
    UiInternal_PushRect(x, trackY, w, style.barHeight, Ui_GetColor(UiColor::BarTrack));

    if (maximum <= 0 || value <= 0)
        return;

    int filled = static_cast<int>((static_cast<long long>(value) * w) / maximum);
    if (filled > w)
        filled = w;
    const bool warn = (static_cast<long long>(value) * 4) >= (static_cast<long long>(maximum) * 3);
    UiInternal_PushRect(x, trackY, filled, style.barHeight, Ui_GetColor(warn ? UiColor::BarFillWarn : UiColor::BarFill));
}

void Ui_Plot(const char* label, const float* values, int count, float minimum, float maximum, float marker)
{
    const UiStyle& style = Ui_GetStyle();
    const int textHeight = Ui_TextHeight(style.textScale);
    const int plotHeight = textHeight * 4;
    const int height = textHeight + style.rowPadding + plotHeight;
    int x = 0;
    int y = 0;
    int w = 0;
    bool visible = false;
    if (!UiInternal_CanDraw() || !UiInternal_TakeRow(height, &x, &y, &w, &visible) || !visible)
        return;

    UiFont_Draw(x, y, style.textScale, label, Ui_GetColor(UiColor::TextDim));

    const int plotY = y + textHeight + style.rowPadding;
    UiInternal_PushRect(x, plotY, w, plotHeight, Ui_GetColor(UiColor::BarTrack));
    UiInternal_PushBorder(x, plotY, w, plotHeight, style.borderWidth, Ui_GetColor(UiColor::Border));

    const float span = maximum - minimum;
    if (span <= 0.0f || !values || count <= 0)
        return;

    if (marker >= minimum && marker <= maximum)
    {
        const int markerY = plotY + plotHeight - 1 - static_cast<int>(((marker - minimum) / span) * static_cast<float>(plotHeight - 1));
        UiInternal_PushRect(x, markerY, w, 1, Ui_GetColor(UiColor::TextWarn));
    }

    const int columns = (count < w) ? count : w;
    for (int i = 0; i < columns; ++i)
    {
        const int sample = (count * i) / columns;
        float normalised = (values[sample] - minimum) / span;
        if (normalised < 0.0f)
            normalised = 0.0f;
        if (normalised > 1.0f)
            normalised = 1.0f;

        const int barHeight = static_cast<int>(normalised * static_cast<float>(plotHeight - 2));
        const int columnW = (w / columns) > 0 ? (w / columns) : 1;
        UiInternal_PushRect(x + i * columnW, plotY + plotHeight - 1 - barHeight, columnW, 1 + (barHeight > 0 ? 1 : 0), Ui_GetColor(UiColor::BarFill));
    }
}

void Ui_BeginPointBox(const char* label, int height)
{
    const UiStyle& style = Ui_GetStyle();
    const int textHeight = Ui_TextHeight(style.textScale);
    int x = 0;
    int y = 0;
    int w = 0;
    bool visible = false;
    if (!UiInternal_CanDraw() || !UiInternal_TakeRow(textHeight + style.rowPadding + height, &x, &y, &w, &visible) || !visible)
        return;

    UiFrameState& state = UiInternal_State();
    UiFont_Draw(x, y, style.textScale, label, Ui_GetColor(UiColor::TextDim));

    state.inBox = true;
    state.boxX = x;
    state.boxY = y + textHeight + style.rowPadding;
    state.boxW = w;
    state.boxH = height;

    UiInternal_PushRect(state.boxX, state.boxY, w, height, Ui_GetColor(UiColor::BarTrack));
    UiInternal_PushBorder(state.boxX, state.boxY, w, height, style.borderWidth, Ui_GetColor(UiColor::Border));
}

void Ui_Point(float nx, float ny, UiColor role)
{
    if (!UiInternal_CanDraw())
        return;
    UiFrameState& state = UiInternal_State();
    if (!state.inBox)
        return;

    const int size = Ui_GetStyle().cursorSize / 2 + 1;
    const int px = state.boxX + static_cast<int>(nx * static_cast<float>(state.boxW - 1));
    const int py = state.boxY + static_cast<int>(ny * static_cast<float>(state.boxH - 1));
    UiInternal_PushRect(px - size, py - size, size * 2, size * 2, Ui_GetColor(role));
}

void Ui_EndPointBox()
{
    if (!UiInternal_CanDraw())
        return;
    UiInternal_State().inBox = false;
}

void Ui_Rect(int x, int y, int w, int h, UiColor role) { UiInternal_PushRect(x, y, w, h, Ui_GetColor(role)); }

void Ui_RectRgba(int x, int y, int w, int h, UiRgba color) { UiInternal_PushRect(x, y, w, h, color); }

void Ui_RectOutline(int x, int y, int w, int h, int thickness, UiColor role) { UiInternal_PushBorder(x, y, w, h, thickness, Ui_GetColor(role)); }

int Ui_Text(int x, int y, int scale, const char* text, UiColor role) { return UiFont_Draw(x, y, scale, text, Ui_GetColor(role)); }

namespace
{
    void ImagePlaceholder(int x, int y, int w, int h)
    {
        const UiStyle& style = Ui_GetStyle();
        UiInternal_PushRect(x, y, w, h, Ui_GetColor(UiColor::ItemBackground));
        UiInternal_PushBorder(x, y, w, h, style.borderWidth, Ui_GetColor(UiColor::Border));
    }
} // namespace

void Ui_ImageAt(int32_t handle, int x, int y, int w, int h, uint16_t u0, uint16_t v0, uint16_t u1, uint16_t v1, UiColor tint)
{
    uint32_t texture = 0;
    int texW = 0;
    int texH = 0;
    if (UiInternal_ResolveTexture(handle, &texture, &texW, &texH) != UiTextureState::Ready)
    {
        ImagePlaceholder(x, y, w, h);
        return;
    }
    UiInternal_PushTexturedQuad(x, y, w, h, texture, u0, v0, u1, v1, Ui_GetColor(tint));
}

void Ui_Image(int32_t handle, int height)
{
    int x = 0;
    int y = 0;
    int w = 0;
    bool visible = false;
    if (!UiInternal_CanDraw() || !UiInternal_TakeRow(height, &x, &y, &w, &visible) || !visible)
        return;

    uint32_t texture = 0;
    int texW = 0;
    int texH = 0;
    if (UiInternal_ResolveTexture(handle, &texture, &texW, &texH) != UiTextureState::Ready || texW <= 0 || texH <= 0)
    {
        ImagePlaceholder(x, y, w, height);
        return;
    }

    int drawW = (height * texW) / texH;
    if (drawW > w)
        drawW = w;
    if (drawW <= 0)
        drawW = w;
    UiInternal_PushTexturedQuad(x, y, drawW, height, texture, 0, 0, 65535, 65535, Ui_GetColor(UiColor::Text));
}

void Ui_Image3D(const Renderable3D& what, const Camera3D& camera, int height, const Color3& clearColor)
{
    int x = 0;
    int y = 0;
    int w = 0;
    bool visible = false;
    if (!UiInternal_CanDraw() || !UiInternal_TakeRow(height, &x, &y, &w, &visible) || !visible)
        return;

    Renderer* renderer = Engine_GetRenderer();
    const uint32_t texture = renderer ? renderer->RenderToImage3D(what, camera, height, height, clearColor) : 0;
    if (texture == 0)
    {
        ImagePlaceholder(x, y, w, height);
        return;
    }

    const int drawW = (height > w) ? w : height;
    UiInternal_PushTexturedQuad(x, y, drawW, height, texture, 0, 0, 65535, 65535, Ui_GetColor(UiColor::Text));
}
