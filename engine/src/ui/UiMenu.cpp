#include "../../include/ui/EngineUi.h"

#include "../../include/core/EngineDebug.h"
#include "UiInternal.h"

namespace
{
    // No content-based auto-sizing exists for an overlay container (see the
    // dropdown-height note below), so a dropdown is this wide regardless of
    // what its items say; a caller with wider items is expected to keep labels
    // short, the way a console menu's prompts already have to be.
    const int MENU_MIN_WIDTH = 140;

    bool s_MenuBarOpen = false;
    int s_MenuBarY = 0;
    int s_MenuBarW = 0;
    int s_MenuBarH = 0;
    int s_MenuBarPenX = 0;

    // Which menu, if any, is open -- persists frame to frame, unlike the
    // s_Menu* fields below, which describe only the one currently being drawn
    // between a matched Ui_BeginMenu/Ui_EndMenu pair.
    uint32_t s_OpenMenuId = 0;

    // The dropdown a Ui_SplitButton's chevron asked to open, consumed by the
    // very next Ui_BeginMenu call whose id matches -- the one bridge between
    // the two entry points into the same dropdown-drawing code.
    bool s_PendingAnchorValid = false;
    uint32_t s_PendingAnchorId = 0;
    int s_PendingAnchorX = 0;
    int s_PendingAnchorY = 0;
    int s_PendingAnchorW = 0;

    bool s_MenuOpen = false; // an Ui_BeginMenu...Ui_EndMenu body is currently being drawn
    uint32_t s_MenuId = 0;
    int s_MenuX = 0;
    int s_MenuY = 0;
    int s_MenuW = 0;
    int s_MenuPenY = 0;
    int s_MenuItemCount = 0;
    bool s_MenuActivateHighlighted = false; // Accept was pressed this frame, for the highlighted item to claim

    void CloseMenu()
    {
        s_OpenMenuId = 0;
        s_MenuOpen = false;
        UiInternal_State().menuCapturing = false;
    }

    /// Common tail of both Ui_BeginMenu entry points once an id has resolved to
    /// open, at the geometry the caller (a title cell or a split button's
    /// chevron) has already computed.
    void OpenDropdown(uint32_t id, int anchorX, int anchorY, int anchorW)
    {
        const UiStyle& style = Ui_GetStyle();
        UiFrameState& state = UiInternal_State();

        if (state.back)
        {
            CloseMenu();
            return;
        }

        // The dropdown's own height cannot be known before every item in it
        // has been drawn, and nothing in this contract reorders or defers a
        // draw call to fix that up afterward -- so, exactly like a scroll
        // region's thumb, it is sized from what was measured one frame ago.
        // A menu whose item count never changes, which is every menu in
        // practice, is never visibly wrong; one that does is wrong for
        // exactly one frame.
        UiState* remembered = UiInternal_StateFor(id);
        const int lastItemCount = (remembered->fraction >= 1.0f) ? static_cast<int>(remembered->fraction) : 1;
        const int itemHeight = Ui_TextHeight(style.textScale) + style.rowPadding * 2;
        const int bodyWidth = (anchorW > MENU_MIN_WIDTH) ? anchorW : MENU_MIN_WIDTH;
        const int bodyHeight = lastItemCount * itemHeight;

        if (state.navDelta != 0)
        {
            int next = remembered->whole + state.navDelta;
            while (next < 0)
                next += lastItemCount;
            while (next >= lastItemCount)
                next -= lastItemCount;
            remembered->whole = next;
        }
        s_MenuActivateHighlighted = state.accept;

        Ui_BeginOverlay();
        UiInternal_PushRect(anchorX, anchorY, bodyWidth, bodyHeight, Ui_GetColor(UiColor::PanelBackground));
        UiInternal_PushBorder(anchorX, anchorY, bodyWidth, bodyHeight, style.borderWidth, Ui_GetColor(UiColor::Border));

        s_MenuOpen = true;
        s_MenuId = id;
        s_MenuX = anchorX;
        s_MenuY = anchorY;
        s_MenuW = bodyWidth;
        s_MenuPenY = anchorY;
        s_MenuItemCount = 0;
        state.menuCapturing = true;
    }
} // namespace

bool Ui_BeginMenuBar()
{
    if (!UiInternal_CanDraw() || s_MenuBarOpen)
        return false;

    const UiStyle& style = Ui_GetStyle();
    UiFrameState& state = UiInternal_State();
    const int h = style.menuBarHeight;
    int x = 0;
    int y = 0;
    int w = 0;

    if (state.inPanel)
    {
        bool visible = false;
        if (!UiInternal_TakeRow(h, &x, &y, &w, &visible) || !visible)
            return false;
    }
    else
    {
        x = 0;
        y = 0;
        w = Ui_ScreenWidth();
    }

    s_MenuBarOpen = true;
    s_MenuBarY = y;
    s_MenuBarW = w;
    s_MenuBarH = h;
    s_MenuBarPenX = x;

    // Every title cell this bar draws is registered focusable without going
    // through UiInternal_TakeRow, so they all share whatever row key was
    // already current; stamping it here, once, makes that deliberate instead
    // of an accident of whatever widget happened to run before this one, which
    // is what lets Left and Right move between them as a single run.
    state.lastRowTop = y;

    UiInternal_PushRect(x, y, w, h, Ui_GetColor(UiColor::PanelBackground));
    UiInternal_PushId("menubar");
    return true;
}

void Ui_EndMenuBar()
{
    if (!UiInternal_CanDraw() || !s_MenuBarOpen)
        return;
    UiInternal_PopId();
    s_MenuBarOpen = false;
}

bool Ui_BeginMenu(const char* id)
{
    if (!UiInternal_CanDraw() || !id || s_MenuOpen)
        return false;

    const uint32_t hashed = UiInternal_Id(id);

    if (s_PendingAnchorValid && s_PendingAnchorId == hashed)
    {
        // A Ui_SplitButton draws every frame regardless of whether its menu is
        // open, refreshing this anchor each time; whether to actually open is
        // s_OpenMenuId, which only the chevron's own click in Ui_SplitButton
        // changes, not the mere presence of a fresh anchor.
        s_PendingAnchorValid = false;
        if (s_OpenMenuId != hashed)
            return false;
        OpenDropdown(hashed, s_PendingAnchorX, s_PendingAnchorY, s_PendingAnchorW);
        return s_MenuOpen && s_MenuId == hashed;
    }

    if (!s_MenuBarOpen)
        return false;

    const UiStyle& style = Ui_GetStyle();
    UiFrameState& state = UiInternal_State();

    const int width = Ui_TextWidth(style.textScale, id) + style.rowPadding * 4;
    const int x = s_MenuBarPenX;
    const int y = s_MenuBarY;
    s_MenuBarPenX += width;

    const bool focused = UiInternal_RegisterFocusable(hashed);
    const bool hovered = UiInternal_PointerOver(x, y, width, s_MenuBarH);
    if (hovered && state.pointerMoved)
    {
        state.focusId = hashed;
        state.focusIndex = static_cast<int>(state.focusableCount) - 1;
    }

    const bool wasOpen = (s_OpenMenuId == hashed);
    const bool clicked = (focused && state.accept) || (hovered && state.pointer.pressed);
    const bool isOpen = clicked ? !wasOpen : wasOpen;

    UiInternal_PushRect(x, y, width, s_MenuBarH, isOpen ? Ui_GetColor(UiColor::ItemActive) : (hovered ? Ui_GetColor(UiColor::ItemHovered) : Ui_GetColor(UiColor::PanelBackground)));
    UiFont_Draw(x + style.rowPadding * 2, y + (s_MenuBarH - Ui_TextHeight(style.textScale)) / 2, style.textScale, id, Ui_GetColor(UiColor::Text));

    if (!isOpen)
    {
        if (s_OpenMenuId == hashed)
            s_OpenMenuId = 0;
        return false;
    }

    s_OpenMenuId = hashed;
    OpenDropdown(hashed, x, y + s_MenuBarH, width);
    return s_MenuOpen && s_MenuId == hashed;
}

bool Ui_MenuItem(const char* label, const char* shortcut)
{
    if (!UiInternal_CanDraw() || !s_MenuOpen)
        return false;

    const UiStyle& style = Ui_GetStyle();
    const int height = Ui_TextHeight(style.textScale) + style.rowPadding * 2;
    const int x = s_MenuX;
    const int y = s_MenuPenY;
    const int w = s_MenuW;
    const int index = s_MenuItemCount++;
    s_MenuPenY += height;

    UiState* remembered = UiInternal_StateFor(s_MenuId);
    const bool highlighted = (remembered->whole == index);
    const bool hovered = UiInternal_PointerOver(x, y, w, height);

    if (highlighted || hovered)
        UiInternal_PushRect(x, y, w, height, Ui_GetColor(hovered ? UiColor::ItemHovered : UiColor::Focus));

    const int fit = Ui_TextFit(style.textScale, label, w - style.rowPadding * 2);
    char cut[UI_TEXT_MAX];
    const int length = (fit < UI_TEXT_MAX - 1) ? fit : (UI_TEXT_MAX - 1);
    for (int i = 0; i < length; ++i)
        cut[i] = label[i];
    cut[length] = '\0';
    UiFont_Draw(x + style.rowPadding, y + style.rowPadding, style.textScale, cut, Ui_GetColor(UiColor::Text));

    if (shortcut && shortcut[0])
    {
        const int shortcutWidth = Ui_TextWidth(style.textScale, shortcut);
        UiFont_Draw(x + w - shortcutWidth - style.rowPadding, y + style.rowPadding, style.textScale, shortcut, Ui_GetColor(UiColor::TextDim));
    }

    if (hovered && UiInternal_State().pointerMoved)
        remembered->whole = index;

    const bool byKey = highlighted && s_MenuActivateHighlighted;
    const bool byPointer = hovered && UiInternal_State().pointer.pressed;
    const bool activated = byKey || byPointer;
    if (activated)
        CloseMenu();
    return activated;
}

void Ui_EndMenu()
{
    if (!UiInternal_CanDraw() || !s_MenuOpen)
        return;

    UiState* remembered = UiInternal_StateFor(s_MenuId);
    remembered->fraction = static_cast<float>((s_MenuItemCount > 0) ? s_MenuItemCount : 1);
    if (s_MenuItemCount > 0 && remembered->whole >= s_MenuItemCount)
        remembered->whole = s_MenuItemCount - 1;

    Ui_EndOverlay();
    s_MenuOpen = false;
    // menuCapturing is left set by OpenDropdown for the rest of this frame
    // unless CloseMenu already cleared it (Back, or an item was selected).
}

bool Ui_SplitButton(const char* label, UiIcon icon)
{
    const UiStyle& style = Ui_GetStyle();
    const int chevronWidth = Ui_TextHeight(style.textScale) + style.rowPadding * 2;
    const int totalWidth = Ui_ContentWidth();
    const int primaryWidth = totalWidth - chevronWidth - style.itemSpacing;

    Ui_SameLine((primaryWidth > 0) ? primaryWidth : totalWidth);
    const bool primary = Ui_Button(label);

    if (primaryWidth <= 0)
        return primary;

    Ui_SameLine(0);
    const uint32_t chevronId = UiInternal_Id(label);
    int cx = 0;
    int cy = 0;
    int cw = 0;
    const bool chevronActivated = UiInternal_ActivatableRow(chevronId, s_OpenMenuId == chevronId, &cx, &cy, &cw);
    if (cw > 0)
        Ui_IconAt(cx + (cw - Ui_TextHeight(style.textScale)) / 2, cy + style.rowPadding, style.textScale, icon, UiInternal_TextRole(UiColor::Text));

    if (chevronActivated)
        s_OpenMenuId = (s_OpenMenuId == chevronId) ? 0 : chevronId;

    // Refreshed every frame this button draws, open or not: whether the menu
    // is actually open is s_OpenMenuId alone, which only the toggle above and
    // Ui_BeginMenu's own Back/selection handling ever change.
    const int rowHeight = Ui_TextHeight(style.textScale) + style.rowPadding * 2;
    s_PendingAnchorValid = true;
    s_PendingAnchorId = chevronId;
    s_PendingAnchorX = cx - primaryWidth - style.itemSpacing; // the row's own left edge, not the chevron's
    s_PendingAnchorY = cy + rowHeight;
    s_PendingAnchorW = totalWidth;

    return primary;
}
