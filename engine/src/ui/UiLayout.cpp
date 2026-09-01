#include "../../include/ui/EngineUi.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "../../include/core/EngineDebug.h"
#include "UiInternal.h"

namespace
{
    const char* const ELLIPSIS = "...";

    void SlotRect(UiPanelSlot slot, int* x, int* y, int* w, int* h)
    {
        const UiStyle& style = Ui_GetStyle();
        const int margin = style.screenMargin;
        const int gap = style.panelGap;
        const int fullW = Ui_ScreenWidth() - margin * 2;
        const int fullH = Ui_ScreenHeight() - margin * 2;
        const int halfW = (fullW - gap) / 2;
        const int halfH = (fullH - gap) / 2;
        const int rightX = margin + halfW + gap;
        const int lowerY = margin + halfH + gap;

        switch (slot)
        {
        case UiPanelSlot::Full:
            *x = margin;
            *y = margin;
            *w = fullW;
            *h = fullH;
            break;
        case UiPanelSlot::Left:
            *x = margin;
            *y = margin;
            *w = halfW;
            *h = fullH;
            break;
        case UiPanelSlot::Right:
            *x = rightX;
            *y = margin;
            *w = halfW;
            *h = fullH;
            break;
        case UiPanelSlot::Top:
            *x = margin;
            *y = margin;
            *w = fullW;
            *h = halfH;
            break;
        case UiPanelSlot::Bottom:
            *x = margin;
            *y = lowerY;
            *w = fullW;
            *h = halfH;
            break;
        case UiPanelSlot::TopLeft:
            *x = margin;
            *y = margin;
            *w = halfW;
            *h = halfH;
            break;
        case UiPanelSlot::TopRight:
            *x = rightX;
            *y = margin;
            *w = halfW;
            *h = halfH;
            break;
        case UiPanelSlot::BottomLeft:
            *x = margin;
            *y = lowerY;
            *w = halfW;
            *h = halfH;
            break;
        case UiPanelSlot::BottomRight:
            *x = rightX;
            *y = lowerY;
            *w = halfW;
            *h = halfH;
            break;
        case UiPanelSlot::Count:
            *x = margin;
            *y = margin;
            *w = fullW;
            *h = fullH;
            break;
        }
    }
} // namespace

bool Ui_BeginPanelSlot(const char* title, UiPanelSlot slot)
{
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    SlotRect(slot, &x, &y, &w, &h);
    return Ui_BeginPanel(title, x, y, w, h);
}

int Ui_ScreenWidth() { return UiInternal_State().screenW; }

int Ui_ScreenHeight() { return UiInternal_State().screenH; }

void Ui_PushId(const char* text) { UiInternal_PushId(text); }

void Ui_PushIdIndex(int index) { UiInternal_PushIdIndex(index); }

void Ui_PopId() { UiInternal_PopId(); }

void Ui_BeginDisabled(bool disabled)
{
    if (!UiInternal_CanDraw())
        return;

    UiFrameState& state = UiInternal_State();
    if (state.disabledStackDepth >= UI_MAX_CLIP_DEPTH)
    {
        // Past the array's capacity, so there is nowhere to remember whether
        // this specific level asked to disable. Ui_BeginDisabled returns
        // nothing a caller could react to (unlike the internal clip stack, whose
        // callers check its bool and do not open the container), so the matching
        // Ui_EndDisabled is coming regardless: count it conservatively as
        // disabling, and unwind it before touching the real stack, in the same
        // LIFO order it was pushed.
        ++state.disabledOverflowCount;
        ++state.disabledDepth;
        if (!state.disabledOverflowReported)
        {
            state.disabledOverflowReported = true;
            Engine_LogError("Ui: disabled scopes nested deeper than %u.", static_cast<unsigned>(UI_MAX_CLIP_DEPTH));
        }
        return;
    }

    state.disabledStack[state.disabledStackDepth++] = disabled ? 1 : 0;
    if (disabled)
        ++state.disabledDepth;
}

void Ui_EndDisabled()
{
    if (!UiInternal_CanDraw())
        return;

    UiFrameState& state = UiInternal_State();
    if (state.disabledOverflowCount > 0)
    {
        --state.disabledOverflowCount;
        --state.disabledDepth;
        return;
    }
    if (state.disabledStackDepth == 0)
        return;

    --state.disabledStackDepth;
    if (state.disabledStack[state.disabledStackDepth])
        --state.disabledDepth;
}

// ---------------------------------------------------------------------------
// The horizontal axis
// ---------------------------------------------------------------------------

void Ui_SameLine(int width)
{
    if (!UiInternal_CanDraw())
        return;

    UiFrameState& state = UiInternal_State();
    if (!state.inPanel)
        return;

    const UiStyle& style = Ui_GetStyle();

    if (state.runActive)
    {
        // Continuing a row that already has an inline-placed widget on it:
        // rewind past its tentative advance and place the pen after its edge.
        state.cursorY = state.lastRowTop;
        state.inlineX = state.lastRowRight + style.itemSpacing;
    }
    else
    {
        // Starting a run. Whatever was drawn last, if anything, took the
        // row's full width and has nothing to share it with, so the first
        // widget of the run starts fresh, at the panel's own left edge, where
        // the cursor already sits.
        state.lastRowTop = state.cursorY;
        state.lastRowHeight = 0;
        state.inlineX = state.contentX;
        state.rowOpen = true;
    }

    const int remaining = state.contentX + state.contentW - state.inlineX;
    state.inlineWidth = (width > 0) ? width : remaining;
    if (state.inlineWidth < 0)
        state.inlineWidth = 0;
    state.inlineActive = true;
    state.runActive = true;
}

void Ui_SetNextItemWidth(int width)
{
    if (!UiInternal_CanDraw())
        return;

    UiFrameState& state = UiInternal_State();
    if (!state.inPanel)
        return;

    state.nextItemWidth = width;
    state.nextItemWidthActive = true;
}

void Ui_SetNextItemHeight(int height)
{
    if (!UiInternal_CanDraw())
        return;

    UiFrameState& state = UiInternal_State();
    if (!state.inPanel)
        return;

    state.nextItemHeight = height;
    state.nextItemHeightActive = true;
}

// ---------------------------------------------------------------------------
// Scrolling
// ---------------------------------------------------------------------------

bool Ui_BeginScroll(const char* id, int height)
{
    if (!UiInternal_CanDraw())
        return false;

    UiFrameState& state = UiInternal_State();
    if (!state.inPanel)
        return false;
    if (state.inScroll)
    {
        if (!state.scrollNestingReported)
        {
            state.scrollNestingReported = true;
            Engine_LogError("Ui: a scrolling region ('%s') was opened inside another; nesting is not supported, the inner one is not opened.", id ? id : "");
        }
        return false;
    }

    int x = 0;
    int y = 0;
    int w = 0;
    bool visible = false;
    if (!UiInternal_TakeRow(height, &x, &y, &w, &visible))
        return false;

    const uint32_t regionId = UiInternal_Id(id);
    UiState* remembered = UiInternal_StateFor(regionId);

    state.inScroll = true;
    state.scrollId = regionId;
    state.scrollTop = y;
    state.scrollBottom = y + height;
    state.scrollY = remembered->whole;
    state.scrollContentStart = y;

    if (!UiInternal_PushClip(x, y, w, height))
    {
        state.inScroll = false;
        return false;
    }

    UiInternal_PushId(id);
    state.cursorY = y - state.scrollY;
    state.rowOpen = false;
    state.runActive = false;
    state.inlineActive = false;
    state.nextItemXActive = false;
    state.nextItemWidthActive = false;
    state.nextItemHeightActive = false;
    return true;
}

void Ui_EndScroll()
{
    if (!UiInternal_CanDraw())
        return;

    UiFrameState& state = UiInternal_State();
    if (!state.inScroll)
        return;

    UiInternal_PopId();
    UiInternal_PopClip();

    const int contentHeight = state.cursorY + state.scrollY - state.scrollContentStart;
    const int viewHeight = state.scrollBottom - state.scrollTop;

    UiState* remembered = UiInternal_StateFor(state.scrollId);
    remembered->fraction = static_cast<float>(contentHeight);

    // Navigation scrolls rather than pages: a focused row below or above the
    // band pulls the band to it. Only a row registered inside this region may
    // do that: the frame's focus state is shared by every region in it, and a
    // row focused elsewhere on screen would otherwise drag an unrelated band
    // to a position computed from coordinates that mean nothing here.
    const bool ownsFocus = state.focusRowValid && state.focusRowScrollId == state.scrollId;

    if (ownsFocus && contentHeight > viewHeight)
    {
        const int rowTop = state.focusRowTop + state.scrollY - state.scrollTop;
        const int rowBottom = rowTop + state.focusRowHeight;
        if (rowTop < state.scrollY)
            remembered->whole = rowTop;
        else if (rowBottom > state.scrollY + viewHeight)
            remembered->whole = rowBottom - viewHeight;
    }

    // The right stick moves the band directly, on top of whatever focus-follow
    // above already did -- a separate axis from navigation, not a replacement
    // for it, so looking ahead with the stick never fights moving focus with
    // the pad. It reaches the region holding focus, and reaches every region
    // only when focusId is the frame's "nothing focused anywhere" sentinel
    // (zero) -- which is what keeps a lone region with nothing focusable in it
    // reachable. state.focusRowScrollId == 0 is NOT that signal: it is also
    // zero whenever focus sits on an ordinary non-scrolling widget elsewhere on
    // the same screen (a Selectable in a sibling panel, say), and using it here
    // made every open region on screen answer to the stick the moment focus
    // left them all rather than none of them.
    if (ownsFocus || state.focusId == 0)
        remembered->whole += static_cast<int>(state.scrollStickDelta);

    const int maximum = (contentHeight > viewHeight) ? (contentHeight - viewHeight) : 0;
    if (remembered->whole > maximum)
        remembered->whole = maximum;
    if (remembered->whole < 0)
        remembered->whole = 0;

    // The bar is drawn from last frame's content height, which is what the
    // retained state is for.
    if (contentHeight > viewHeight)
    {
        const UiStyle& style = Ui_GetStyle();
        const int barX = state.contentX + state.contentW - style.scrollBarWidth;
        const int trackH = viewHeight;
        int thumbH = (trackH * viewHeight) / contentHeight;
        if (thumbH < 8)
            thumbH = 8;
        const int travel = trackH - thumbH;
        const int thumbY = state.scrollTop + ((maximum > 0) ? (state.scrollY * travel) / maximum : 0);
        UiInternal_PushRect(barX, state.scrollTop, style.scrollBarWidth, trackH, Ui_GetColor(UiColor::BarTrack));
        UiInternal_PushRect(barX, thumbY, style.scrollBarWidth, thumbH, Ui_GetColor(UiColor::Border));
    }

    state.inScroll = false;
    state.cursorY = state.scrollBottom + Ui_GetStyle().itemSpacing;
    state.rowOpen = false;
    state.runActive = false;
    state.inlineActive = false;
    state.nextItemXActive = false;
    state.nextItemWidthActive = false;
    state.nextItemHeightActive = false;
}

bool Ui_ListBox(const char* id, int* index, const char* const* items, int count, int height)
{
    if (!UiInternal_CanDraw() || !index || !items || count <= 0)
        return false;
    if (!Ui_BeginScroll(id, height))
        return false;

    bool changed = false;
    for (int i = 0; i < count; ++i)
    {
        Ui_PushIdIndex(i);
        if (Ui_Selectable(items[i], i == *index))
        {
            *index = i;
            changed = true;
        }
        Ui_PopId();
    }

    Ui_EndScroll();
    return changed;
}

// ---------------------------------------------------------------------------
// Columns
// ---------------------------------------------------------------------------

void Ui_BeginColumns(int count)
{
    if (!UiInternal_CanDraw())
        return;

    UiFrameState& state = UiInternal_State();
    if (!state.inPanel || state.inColumns || count <= 1)
        return;

    const UiStyle& style = Ui_GetStyle();
    state.inColumns = true;
    state.columnCount = count;
    state.columnIndex = 0;
    state.columnStartY = state.cursorY;
    state.columnMaxY = state.cursorY;
    state.columnX = state.contentX;
    state.columnWidth = state.contentW;

    const int gap = style.panelGap;
    const int each = (state.contentW - gap * (count - 1)) / count;
    state.contentW = each;
    UiInternal_PushIdIndex(0);
}

void Ui_NextColumn()
{
    if (!UiInternal_CanDraw())
        return;

    UiFrameState& state = UiInternal_State();
    if (!state.inColumns)
        return;

    if (state.cursorY > state.columnMaxY)
        state.columnMaxY = state.cursorY;

    UiInternal_PopId();
    ++state.columnIndex;
    UiInternal_PushIdIndex(state.columnIndex);

    const int gap = Ui_GetStyle().panelGap;
    state.contentX = state.columnX + state.columnIndex * (state.contentW + gap);
    state.cursorY = state.columnStartY;
    state.rowOpen = false;
    state.runActive = false;
    state.inlineActive = false;
    state.nextItemXActive = false;
    state.nextItemWidthActive = false;
    state.nextItemHeightActive = false;
}

void Ui_EndColumns()
{
    if (!UiInternal_CanDraw())
        return;

    UiFrameState& state = UiInternal_State();
    if (!state.inColumns)
        return;

    if (state.cursorY > state.columnMaxY)
        state.columnMaxY = state.cursorY;

    UiInternal_PopId();
    state.inColumns = false;
    state.contentX = state.columnX;
    state.contentW = state.columnWidth;
    state.cursorY = state.columnMaxY;
    state.rowOpen = false;
    state.runActive = false;
    state.inlineActive = false;
    state.nextItemXActive = false;
    state.nextItemWidthActive = false;
    state.nextItemHeightActive = false;
}

// ---------------------------------------------------------------------------
// Trees and tabs
// ---------------------------------------------------------------------------

bool Ui_BeginTree(const char* label, bool defaultOpen)
{
    if (!UiInternal_CanDraw())
        return false;

    const uint32_t id = UiInternal_Id(label);
    UiState* remembered = UiInternal_StateFor(id);
    if (remembered->touchedFrame == 0 || remembered->whole == 0)
    {
        if (remembered->whole == 0)
            remembered->whole = defaultOpen ? 1 : -1;
    }

    const bool open = remembered->whole > 0;

    // The caret rides in the row's value rather than in its label, because a
    // label that changes is a different widget and focus would jump off the
    // node at the moment it was toggled.
    UiInternal_PushId(label);
    const bool toggled = Ui_SelectableValue(label, open ? "-" : "+", open);
    UiInternal_PopId();
    if (toggled)
        remembered->whole = open ? -1 : 1;

    if (remembered->whole > 0)
    {
        UiInternal_PushId(label);
        UiFrameState& state = UiInternal_State();
        const int indent = Ui_GetStyle().panelPadding;
        state.contentX += indent;
        state.contentW -= indent;
        return true;
    }
    return false;
}

void Ui_EndTree()
{
    if (!UiInternal_CanDraw())
        return;
    UiFrameState& state = UiInternal_State();
    const int indent = Ui_GetStyle().panelPadding;
    state.contentX -= indent;
    state.contentW += indent;
    UiInternal_PopId();
}

namespace
{
    uint32_t s_TabBarId = 0;
    int s_TabIndex = 0;
    int s_TabActive = 0;
    int s_TabX = 0;
    int s_TabY = 0;
    int s_TabHeight = 0;
    bool s_InTabBar = false;
} // namespace

bool Ui_BeginTabBar(const char* id)
{
    if (!UiInternal_CanDraw())
        return false;

    UiFrameState& state = UiInternal_State();
    if (!state.inPanel || s_InTabBar)
        return false;

    const UiStyle& style = Ui_GetStyle();
    const int height = Ui_TextHeight(style.textScale) + style.rowPadding * 2;
    int x = 0;
    int y = 0;
    int w = 0;
    bool visible = false;
    if (!UiInternal_TakeRow(height, &x, &y, &w, &visible))
        return false;

    s_TabBarId = UiInternal_Id(id);
    UiState* remembered = UiInternal_StateFor(s_TabBarId);

    // L1/R1 step the bar directly, against how many tabs it held last frame --
    // the same "sized from what was measured the frame before" answer a menu's
    // own item count already uses, since this frame's real count is not known
    // until every Ui_Tab call below has run.
    if (state.tabDelta != 0)
    {
        const int lastCount = (remembered->fraction >= 1.0f) ? static_cast<int>(remembered->fraction) : 1;
        int next = remembered->whole + state.tabDelta;
        while (next < 0)
            next += lastCount;
        while (next >= lastCount)
            next -= lastCount;
        remembered->whole = next;
    }

    s_TabActive = remembered->whole;
    s_TabIndex = 0;
    s_TabX = x;
    s_TabY = y;
    s_TabHeight = height;
    s_InTabBar = true;
    UiInternal_PushId(id);
    return true;
}

bool Ui_Tab(const char* label)
{
    if (!UiInternal_CanDraw() || !s_InTabBar)
        return false;

    const UiStyle& style = Ui_GetStyle();
    const int width = Ui_TextWidth(style.textScale, label) + style.rowPadding * 4;
    const int index = s_TabIndex++;
    const bool active = (index == s_TabActive);

    const uint32_t id = UiInternal_Id(label);
    const bool focused = UiInternal_RegisterFocusable(id);
    const bool hovered = UiInternal_PointerOver(s_TabX, s_TabY, width, s_TabHeight);

    UiFrameState& state = UiInternal_State();
    if (hovered && state.pointerMoved)
        state.focusId = id;

    UiInternal_PushRect(s_TabX, s_TabY, width, s_TabHeight, Ui_GetColor(active ? UiColor::ItemActive : UiColor::ItemBackground));
    if (focused)
        UiInternal_PushBorder(s_TabX, s_TabY, width, s_TabHeight, style.borderWidth, Ui_GetColor(UiColor::Border));
    UiFont_Draw(s_TabX + style.rowPadding * 2, s_TabY + style.rowPadding, style.textScale, label, Ui_GetColor(active ? UiColor::TextAccent : UiColor::Text));

    const bool picked = (focused && state.accept) || (hovered && state.pointer.pressed);
    if (picked)
        UiInternal_StateFor(s_TabBarId)->whole = index;

    s_TabX += width + style.itemSpacing;
    return active;
}

void Ui_EndTabBar()
{
    if (!UiInternal_CanDraw() || !s_InTabBar)
        return;
    UiInternal_StateFor(s_TabBarId)->fraction = static_cast<float>((s_TabIndex > 0) ? s_TabIndex : 1);
    UiInternal_PopId();
    s_InTabBar = false;
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

void Ui_LabelWrapped(const char* text, UiColor role)
{
    if (!UiInternal_CanDraw() || !text || !text[0])
        return;

    const int scale = Ui_GetStyle().textScale;
    const int width = Ui_ContentWidth();
    const int length = static_cast<int>(strlen(text));

    int start = 0;
    while (start < length)
    {
        int take = Ui_TextFit(scale, text + start, width);
        if (take <= 0)
            take = 1;
        if (start + take < length)
        {
            int space = take;
            while (space > 0 && text[start + space] != ' ')
                --space;
            if (space > 0)
                take = space;
        }
        if (take > UI_TEXT_MAX - 1)
            take = UI_TEXT_MAX - 1;

        char line[UI_TEXT_MAX];
        memcpy(line, text + start, static_cast<size_t>(take));
        line[take] = '\0';
        Ui_LabelColored(line, role);

        start += take;
        while (start < length && text[start] == ' ')
            ++start;
    }
}

void Ui_LabelAligned(const char* text, UiAlign align, UiColor role)
{
    const UiStyle& style = Ui_GetStyle();
    int x = 0;
    int y = 0;
    int w = 0;
    bool visible = false;
    if (!UiInternal_CanDraw() || !UiInternal_TakeRow(Ui_TextHeight(style.textScale), &x, &y, &w, &visible) || !visible)
        return;
    Ui_TextAligned(x, y, w, style.textScale, text, align, role);
}

void Ui_LabelEllipsized(const char* text, UiColor role)
{
    if (!UiInternal_CanDraw() || !text)
        return;

    const int scale = Ui_GetStyle().textScale;
    const int width = Ui_ContentWidth();
    if (Ui_TextWidth(scale, text) <= width)
    {
        Ui_LabelColored(text, role);
        return;
    }

    const int room = width - Ui_TextWidth(scale, ELLIPSIS);
    int take = (room > 0) ? Ui_TextFit(scale, text, room) : 0;
    if (take > UI_TEXT_MAX - 4)
        take = UI_TEXT_MAX - 4;

    char line[UI_TEXT_MAX];
    memcpy(line, text, static_cast<size_t>(take));
    line[take] = '\0';
    strncat(line, ELLIPSIS, sizeof(line) - strlen(line) - 1);
    Ui_LabelColored(line, role);
}

void Ui_LabelPath(const char* path, UiColor role)
{
    if (!UiInternal_CanDraw() || !path)
        return;

    const int scale = Ui_GetStyle().textScale;
    const int width = Ui_ContentWidth();
    if (Ui_TextWidth(scale, path) <= width)
    {
        Ui_LabelColored(path, role);
        return;
    }

    const int room = width - Ui_TextWidth(scale, ELLIPSIS);
    const char* tail = (room > 0) ? Ui_TextFitTail(scale, path, room) : (path + strlen(path));

    char line[UI_TEXT_MAX];
    strncpy(line, ELLIPSIS, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';
    strncat(line, tail, sizeof(line) - strlen(line) - 1);
    Ui_LabelColored(line, role);
}

void Ui_LabelFormat(const char* format, ...)
{
    if (!UiInternal_CanDraw())
        return;
    char text[UI_TEXT_MAX];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    Ui_Label(text);
}

void Ui_LabelValueFormat(const char* label, const char* format, ...)
{
    if (!UiInternal_CanDraw())
        return;
    char value[UI_TEXT_MAX];
    va_list args;
    va_start(args, format);
    vsnprintf(value, sizeof(value), format, args);
    va_end(args);
    Ui_LabelValue(label, value);
}

int Ui_TextAligned(int x, int y, int w, int scale, const char* text, UiAlign align, UiColor role)
{
    const int width = Ui_TextWidth(scale, text);
    int drawX = x;
    switch (align)
    {
    case UiAlign::Left:
        break;
    case UiAlign::Center:
        drawX = x + (w - width) / 2;
        break;
    case UiAlign::Right:
        drawX = x + w - width;
        break;
    case UiAlign::Count:
        break;
    }
    return UiFont_Draw(drawX, y, scale, text, Ui_GetColor(role));
}
