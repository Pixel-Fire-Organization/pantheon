#include "../../include/ui/EngineUi.h"

#include <cstdio>
#include <cstring>

#include "../../include/core/EngineDebug.h"
#include "../../include/core/EngineInput.h"
#include "core/EngineCore.h"
#include "UiInternal.h"
#include "graphics/Renderer.h"
#include "graphics/StagedGeometry.h"
#include "platform/Platform.h"

namespace
{
    const float CURSOR_SPEED_SLOW = 0.35f;
    const float CURSOR_SPEED_FAST = 1.60f;
    const float CURSOR_RAMP_SECONDS = 0.45f;

    // Pixels per second a fully-deflected right stick scrolls the open region,
    // proportional to deflection below that. No ramp: unlike the cursor, there
    // is no fine-positioning case that needs distinguishing a tap from a hold.
    const float SCROLL_STICK_SPEED = 900.0f;
    const uint32_t FNV_OFFSET_BASIS = 2166136261u;
    const uint32_t FNV_PRIME = 16777619u;

    /// @param a Value at the start of the span.
    /// @param b Value at the end of the span.
    /// @param cut How far into the span the clip fell.
    /// @param span The span's full length in pixels.
    /// @return The value at the cut.
    uint16_t Interpolate(uint16_t a, uint16_t b, int cut, int span)
    {
        if (span <= 0)
            return a;
        const int32_t delta = static_cast<int32_t>(b) - static_cast<int32_t>(a);
        return static_cast<uint16_t>(static_cast<int32_t>(a) + (delta * cut) / span);
    }

    bool s_Active = false;
    UI s_Ui;

    // Content that must draw above the interface lives in its own buffer and is
    // appended at submission. One hand-off still, and a modal cannot starve the
    // screen underneath it.
    UiQuad s_Overlay[UI_MAX_OVERLAY_QUADS];
    uint32_t s_OverlayCount = 0;
    uint32_t s_OverlayDropped = 0;

    UiFrameState s_State;
    float s_StickHeld = 0.0f;

    // Ui_BeginBudget/Ui_EndBudget: at most one scope open at a time, so this
    // is file-local state rather than a UiFrameState field the way the
    // disabled-scope stack is -- nothing outside this file's own push choke
    // point ever needs to see it.
    bool s_BudgetOpen = false;
    int s_BudgetCap = 0;
    uint32_t s_BudgetStartCount = 0;
    uint32_t s_BudgetDropped = 0;
    uint32_t s_LastContainerQuads = 0;
    uint32_t s_RunsUsed = 0;

    void ReadScreenSize()
    {
        const Platform* platform = Engine_GetPlatform();
        s_State.screenW = platform ? static_cast<int>(platform->GetConstant(PlatformConstant::ScreenWidth)) : GFX_SCREEN_WIDTH;
        s_State.screenH = platform ? static_cast<int>(platform->GetConstant(PlatformConstant::ScreenHeight)) : GFX_SCREEN_HEIGHT;
    }

    float s_VerticalHeld = 0.0f;
    float s_HorizontalHeld = 0.0f;
    int s_VerticalFired = 0;
    int s_HorizontalFired = 0;

    /// A held direction repeats after a delay and then at an interval, both in
    /// seconds. Never in frames: two of this engine's platform variants differ
    /// only in refresh rate, and a frame-counted repeat would run measurably
    /// faster on one of them.
    /// @param positive The button meaning +1.
    /// @param negative The button meaning -1.
    /// @param held How long the current direction has been down; updated.
    /// @param fired How many repeats have been emitted; updated.
    /// @param dt Seconds since the previous frame.
    /// @return -1, 0 or +1 for this frame.
    int Repeat(GamepadButton positive, GamepadButton negative, float& held, int& fired, float dt)
    {
        const bool down = IsGamePadButtonPressed(0, positive);
        const bool up = IsGamePadButtonPressed(0, negative);
        if (down == up)
        {
            held = 0.0f;
            fired = 0;
            return 0;
        }

        const int direction = down ? 1 : -1;
        const UiStyle& style = Ui_GetStyle();
        const float previous = held;
        held += dt;

        if (previous == 0.0f)
        {
            fired = 1;
            return direction;
        }
        if (held < style.repeatDelaySeconds)
            return 0;

        const float since = held - style.repeatDelaySeconds;
        const int want = 1 + static_cast<int>(since / style.repeatIntervalSeconds) + 1;
        if (want > fired)
        {
            fired = want;
            return direction;
        }
        return 0;
    }

    void ClampPointer()
    {
        const float maxX = static_cast<float>(s_State.screenW - 1);
        const float maxY = static_cast<float>(s_State.screenH - 1);
        if (s_State.pointer.x < 0.0f)
            s_State.pointer.x = 0.0f;
        if (s_State.pointer.y < 0.0f)
            s_State.pointer.y = 0.0f;
        if (s_State.pointer.x > maxX)
            s_State.pointer.x = maxX;
        if (s_State.pointer.y > maxY)
            s_State.pointer.y = maxY;
    }

    void UpdateStickPointer(float dt)
    {
        const Vector2 stick = GetGamePadAxis(0, GamepadStick::Left);
        const float magnitude = (stick.x * stick.x) + (stick.y * stick.y);
        if (magnitude <= 0.0f)
        {
            s_StickHeld = 0.0f;
            s_State.pointer.stickSpeed = 0.0f;
            return;
        }

        s_StickHeld += dt;
        float ramp = s_StickHeld / CURSOR_RAMP_SECONDS;
        if (ramp > 1.0f)
            ramp = 1.0f;

        const float screens = CURSOR_SPEED_SLOW + (CURSOR_SPEED_FAST - CURSOR_SPEED_SLOW) * ramp;
        const float speed = screens * static_cast<float>(s_State.screenW);

        s_State.pointer.x += stick.x * speed * dt;
        s_State.pointer.y += stick.y * speed * dt;
        s_State.pointer.stickSpeed = speed;
        s_State.pointer.source = UiPointerSource::Stick;
        s_State.pointer.visible = true;
        s_State.pointerMoved = true;
    }

    void UpdateMousePointer()
    {
        const Platform* platform = Engine_GetPlatform();
        if (!platform || !platform->HasCapability(PlatformCapability::Mouse))
            return;

        const Vector2 delta = GetMouseDelta();
        const bool clicked = WasMouseButtonPressed(MouseButton::Left);
        if (delta.x == 0.0f && delta.y == 0.0f && !clicked)
            return;

        const Vector2 position = GetMousePosition();
        s_State.pointer.x = position.x;
        s_State.pointer.y = position.y;
        s_State.pointer.source = UiPointerSource::Mouse;
        s_State.pointer.visible = true;
        s_State.pointerMoved = true;
    }

    void UpdateTouchPointer()
    {
        Platform* platform = Engine_GetPlatform();
        if (!platform || !platform->HasCapability(PlatformCapability::Touch))
            return;
        if (platform->Touch_GetContactCount(TouchSurface::Front) == 0)
            return;

        TouchContact contact;
        if (!platform->Touch_GetContact(TouchSurface::Front, 0, &contact))
            return;

        s_State.pointer.x = contact.position.x * static_cast<float>(s_State.screenW);
        s_State.pointer.y = contact.position.y * static_cast<float>(s_State.screenH);
        s_State.pointer.source = UiPointerSource::Touch;
        s_State.pointer.visible = true;
        s_State.pointerMoved = true;
    }

    bool PointerHeld()
    {
        if (s_State.pointer.source == UiPointerSource::Touch)
        {
            Platform* platform = Engine_GetPlatform();
            return platform && platform->Touch_GetContactCount(TouchSurface::Front) > 0;
        }
        if (s_State.pointer.source == UiPointerSource::Mouse)
            return IsMouseButtonDown(MouseButton::Left);
        return false;
    }

    bool PointerPressed()
    {
        if (s_State.pointer.source == UiPointerSource::Touch)
            return s_State.pointerMoved && PointerHeld() && !s_State.pointer.down;
        if (s_State.pointer.source == UiPointerSource::Mouse)
            return WasMouseButtonPressed(MouseButton::Left);
        return false;
    }

    void DrawCursor()
    {
        if (!s_State.pointer.visible)
            return;

        const UiStyle& style = Ui_GetStyle();
        const int size = style.cursorSize;
        const int x = static_cast<int>(s_State.pointer.x);
        const int y = static_cast<int>(s_State.pointer.y);
        const UiRgba outline = Ui_GetColor(UiColor::CursorOutline);
        const UiRgba fill = Ui_GetColor(UiColor::Cursor);

        UiInternal_PushRect(x - 1, y - size - 1, 3, size * 2 + 3, outline);
        UiInternal_PushRect(x - size - 1, y - 1, size * 2 + 3, 3, outline);
        UiInternal_PushRect(x, y - size, 1, size * 2 + 1, fill);
        UiInternal_PushRect(x - size, y, size * 2 + 1, 1, fill);
    }
} // namespace

UiFrameState& UiInternal_State() { return s_State; }

bool UiInternal_CanDraw() { return s_Active && s_State.inFrame; }

bool UiInternal_Disabled() { return s_State.disabledDepth > 0; }

UiColor UiInternal_TextRole(UiColor normal) { return (s_State.disabledDepth > 0) ? UiColor::TextDisabled : normal; }

bool UiInternal_PushClip(int x, int y, int w, int h)
{
    if (s_State.clipDepth >= UI_MAX_CLIP_DEPTH)
    {
        if (!s_State.clipOverflowReported)
        {
            s_State.clipOverflowReported = true;
            Engine_LogError("Ui: containers nested deeper than %u; the innermost is not opened.", static_cast<unsigned>(UI_MAX_CLIP_DEPTH));
        }
        return false;
    }

    const UiClipRect& outer = UiInternal_CurrentClip();
    int x0 = x;
    int y0 = y;
    int x1 = x + w;
    int y1 = y + h;
    if (x0 < outer.x)
        x0 = outer.x;
    if (y0 < outer.y)
        y0 = outer.y;
    if (x1 > outer.x + outer.w)
        x1 = outer.x + outer.w;
    if (y1 > outer.y + outer.h)
        y1 = outer.y + outer.h;
    if (x1 < x0)
        x1 = x0;
    if (y1 < y0)
        y1 = y0;

    UiClipRect& top = s_State.clipStack[s_State.clipDepth++];
    top.x = static_cast<int16_t>(x0);
    top.y = static_cast<int16_t>(y0);
    top.w = static_cast<int16_t>(x1 - x0);
    top.h = static_cast<int16_t>(y1 - y0);

    if (s_State.clipDepth > s_State.clipHighWater)
        s_State.clipHighWater = s_State.clipDepth;
    return true;
}

void UiInternal_PopClip()
{
    if (s_State.clipDepth > 1)
        --s_State.clipDepth;
}

const UiClipRect& UiInternal_CurrentClip() { return s_State.clipStack[s_State.clipDepth ? s_State.clipDepth - 1 : 0]; }

bool UiInternal_ClipVisible(int x, int y, int w, int h)
{
    const UiClipRect& clip = UiInternal_CurrentClip();
    return (x < clip.x + clip.w) && (x + w > clip.x) && (y < clip.y + clip.h) && (y + h > clip.y);
}

void UiInternal_PushTexturedQuad(int x, int y, int w, int h, uint32_t texture, uint16_t u0, uint16_t v0, uint16_t u1, uint16_t v1, UiRgba color)
{
    if (!UiInternal_CanDraw() || w <= 0 || h <= 0)
        return;

    const UiClipRect& clip = UiInternal_CurrentClip();
    const int cx0 = clip.x;
    const int cy0 = clip.y;
    const int cx1 = clip.x + clip.w;
    const int cy1 = clip.y + clip.h;
    if (x >= cx1 || x + w <= cx0 || y >= cy1 || y + h <= cy0)
        return;

    int x0 = x;
    int y0 = y;
    int x1 = x + w;
    int y1 = y + h;
    uint16_t su0 = u0;
    uint16_t sv0 = v0;
    uint16_t su1 = u1;
    uint16_t sv1 = v1;

    if (x0 < cx0)
    {
        if (texture)
            su0 = Interpolate(u0, u1, cx0 - x, w);
        x0 = cx0;
    }
    if (x1 > cx1)
    {
        if (texture)
            su1 = Interpolate(u0, u1, cx1 - x, w);
        x1 = cx1;
    }
    if (y0 < cy0)
    {
        if (texture)
            sv0 = Interpolate(v0, v1, cy0 - y, h);
        y0 = cy0;
    }
    if (y1 > cy1)
    {
        if (texture)
            sv1 = Interpolate(v0, v1, cy1 - y, h);
        y1 = cy1;
    }

    UiQuad quad;
    quad.texture = texture;
    quad.x = static_cast<int16_t>(x0);
    quad.y = static_cast<int16_t>(y0);
    quad.w = static_cast<int16_t>(x1 - x0);
    quad.h = static_cast<int16_t>(y1 - y0);
    quad.u0 = su0;
    quad.v0 = sv0;
    quad.u1 = su1;
    quad.v1 = sv1;
    quad.r = color.r;
    quad.g = color.g;
    quad.b = color.b;
    quad.a = color.a;

    if (s_State.inOverlay)
    {
        UiInternal_PushOverlayQuad(quad);
        return;
    }

    // A budgeted container never reaches into the overlay's own, separately
    // protected allowance -- only the base layer is capped here.
    if (s_BudgetOpen && (s_Ui.Count() - s_BudgetStartCount) >= static_cast<uint32_t>(s_BudgetCap))
    {
        ++s_BudgetDropped;
        return;
    }
    s_Ui.Add(quad);
}

void UiInternal_PushOverlayQuad(const UiQuad& quad)
{
    if (s_OverlayCount >= UI_MAX_OVERLAY_QUADS)
    {
        ++s_OverlayDropped;
        return;
    }
    s_Overlay[s_OverlayCount++] = quad;
}

bool UiInternal_InOverlay() { return s_State.inOverlay; }

void UiInternal_PushRect(int x, int y, int w, int h, UiRgba color)
{
    // Deliberately untextured. Sampling the atlas's white texel here would pay
    // a texture fetch on every pixel of every panel, row, border and bar on a
    // fill-rate-bound GS, plus a third qword per quad for UV registers, to save
    // a TEX0 rebind at a run boundary. Vertex and colour only is cheaper on
    // both counts; GFX_MAX_2D_RUNS exists to absorb the resulting run count.
    UiInternal_PushTexturedQuad(x, y, w, h, 0, 0, 0, 0, 0, color);
}

void UiInternal_PushBorder(int x, int y, int w, int h, int thickness, UiRgba color)
{
    if (thickness <= 0)
        return;
    UiInternal_PushRect(x, y, w, thickness, color);
    UiInternal_PushRect(x, y + h - thickness, w, thickness, color);
    UiInternal_PushRect(x, y + thickness, thickness, h - thickness * 2, color);
    UiInternal_PushRect(x + w - thickness, y + thickness, thickness, h - thickness * 2, color);
}

uint32_t UiInternal_Id(const char* label)
{
    const uint32_t seed = s_State.idDepth ? s_State.idStack[s_State.idDepth - 1] : FNV_OFFSET_BASIS;
    return UiInternal_StateHash(seed, label);
}

void UiInternal_PushId(const char* text)
{
    if (s_State.idDepth >= UI_MAX_ID_DEPTH)
        return;
    s_State.idStack[s_State.idDepth] = UiInternal_Id(text);
    ++s_State.idDepth;
}

void UiInternal_PushIdIndex(int index)
{
    char text[16];
    snprintf(text, sizeof(text), "#%d", index);
    UiInternal_PushId(text);
}

void UiInternal_PopId()
{
    if (s_State.idDepth > 0)
        --s_State.idDepth;
}

bool UiInternal_RegisterFocusable(uint32_t id)
{
    for (uint16_t i = 0; i < s_State.focusableCount; ++i)
    {
        if (s_State.focusables[i] != id)
            continue;
        if (!s_State.collisionReported)
        {
            s_State.collisionReported = true;
            Engine_LogError("Ui: two widgets share the identity 0x%08X in one frame; they will act as one.", id);
        }
        return s_State.focusId == id;
    }

    if (s_State.focusableCount < UI_MAX_FOCUSABLES)
    {
        if (s_State.focusId == id)
            s_State.focusIndex = static_cast<int>(s_State.focusableCount);
        s_State.focusRows[s_State.focusableCount] = static_cast<int16_t>(s_State.lastRowTop);
        s_State.focusables[s_State.focusableCount++] = id;
        if (s_State.groupCount > 0)
            ++s_State.groups[s_State.groupCount - 1].count;
    }
    return s_State.focusId == id;
}

namespace
{
    int UiRowHeight()
    {
        const UiStyle& style = Ui_GetStyle();
        return Ui_TextHeight(style.textScale) + style.rowPadding * 2;
    }

    UiRgba UiRowBackground(bool focused, bool hovered, bool held)
    {
        if (held)
            return Ui_GetColor(UiColor::ItemActive);
        if (hovered)
            return Ui_GetColor(UiColor::ItemHovered);
        if (focused)
            return Ui_GetColor(UiColor::Focus);
        return Ui_GetColor(UiColor::ItemBackground);
    }
} // namespace

bool UiInternal_ActivatableRow(uint32_t id, bool highlight, int* outX, int* outY, int* outW)
{
    const UiStyle& style = Ui_GetStyle();
    const int height = UiRowHeight();
    int x = 0;
    int y = 0;
    int w = 0;
    bool visible = false;
    if (!UiInternal_CanDraw() || !UiInternal_TakeRow(height, &x, &y, &w, &visible) || !visible)
        return false;

    *outX = x;
    *outY = y;
    *outW = w;

    if (s_State.disabledDepth > 0)
    {
        // Not registered focusable, so it is unreachable by navigation; and any
        // focus this row held before it became disabled is dropped here rather
        // than surviving as a stale id nothing will ever re-register.
        if (s_State.focusId == id)
            s_State.focusId = 0;
        UiInternal_PushRect(x, y, w, height, Ui_GetColor(UiColor::ItemBackground));
        return false;
    }

    const bool focused = UiInternal_RegisterFocusable(id);
    const bool hovered = UiInternal_PointerOver(x, y, w, height);

    if (hovered)
    {
        s_State.hotId = id;
        if (s_State.pointerMoved)
        {
            s_State.focusId = id;
            s_State.focusIndex = static_cast<int>(s_State.focusableCount) - 1;
            s_State.navDelta = 0;
        }
    }

    if (focused)
    {
        s_State.focusRowTop = y;
        s_State.focusRowHeight = height;
        s_State.focusRowValid = true;
        s_State.focusRowScrollId = s_State.inScroll ? s_State.scrollId : 0;
    }

    const bool held = hovered && s_State.pointer.down;
    UiInternal_PushRect(x, y, w, height, UiRowBackground(focused || highlight, hovered, held));
    if (focused)
        UiInternal_PushBorder(x, y, w, height, style.borderWidth, Ui_GetColor(UiColor::Border));

    const bool byFocus = focused && s_State.accept;
    const bool byPointer = hovered && s_State.pointer.pressed;
    return byFocus || byPointer;
}

void UiInternal_BeginFocusGroup(uint32_t id)
{
    if (s_State.groupCount >= UI_MAX_FOCUS_GROUPS)
        return;
    UiFocusGroup& group = s_State.groups[s_State.groupCount++];
    group.id = id;
    group.first = s_State.focusableCount;
    group.count = 0;
}

void UiInternal_EndFocusGroup() {}

bool UiInternal_PointerOver(int x, int y, int w, int h)
{
    if (!s_State.pointer.visible)
        return false;
    const float px = s_State.pointer.x;
    const float py = s_State.pointer.y;
    return px >= static_cast<float>(x) && px < static_cast<float>(x + w) && py >= static_cast<float>(y) && py < static_cast<float>(y + h);
}

bool UiInternal_TakeRow(int height, int* outX, int* outY, int* outW, bool* outVisible)
{
    if (!s_State.inPanel)
        return false;

    if (s_State.nextItemHeightActive)
    {
        if (s_State.nextItemHeight > 0)
            height = s_State.nextItemHeight;
        s_State.nextItemHeightActive = false;
    }
    const bool widthOverride = s_State.nextItemWidthActive && s_State.nextItemWidth > 0;
    s_State.nextItemWidthActive = false;
    const bool xOverride = s_State.nextItemXActive;
    s_State.nextItemXActive = false;

    const UiStyle& style = Ui_GetStyle();

    if (s_State.inlineActive)
    {
        // Continuing a row Ui_SameLine placed the pen on: same top, the width
        // the caller asked for (or Ui_SetNextItemWidth's override, if one is
        // pending), and cursorY does not move again until either another
        // Ui_SameLine extends the row or a fresh row closes it.
        *outX = xOverride ? s_State.nextItemX : s_State.inlineX;
        *outY = s_State.lastRowTop;
        *outW = widthOverride ? s_State.nextItemWidth : s_State.inlineWidth;
        if (outVisible)
            *outVisible = UiInternal_ClipVisible(*outX, *outY, *outW, height);

        s_State.inlineActive = false;
        s_State.lastRowRight = *outX + *outW;
        if (height > s_State.lastRowHeight)
            s_State.lastRowHeight = height;
        return true;
    }

    // A row still open from a previous Ui_SameLine run has not yet paid for
    // its tallest item; close it before starting the fresh one. For a row
    // that never had a Ui_SameLine, this recomputes the position cursorY
    // already tentatively holds, so it costs nothing.
    if (s_State.rowOpen)
        s_State.cursorY = s_State.lastRowTop + s_State.lastRowHeight + style.itemSpacing;

    *outX = xOverride ? s_State.nextItemX : s_State.contentX;
    *outY = s_State.cursorY;
    *outW = widthOverride ? s_State.nextItemWidth : s_State.contentW;
    if (outVisible)
        *outVisible = UiInternal_ClipVisible(*outX, *outY, *outW, height);

    s_State.lastRowTop = s_State.cursorY;
    s_State.lastRowHeight = height;
    s_State.lastRowRight = *outX + *outW;
    s_State.rowOpen = true;
    s_State.runActive = false;

    s_State.cursorY += height + style.itemSpacing;
    return true;
}

bool Engine_Ui_Init()
{
    if (!Engine_GetRenderer())
    {
        Engine_LogError("Ui: no renderer, interface cannot start");
        return false;
    }

    memset(&s_State, 0, sizeof(s_State));
    s_State.focusIndex = -1;
    s_State.clipDepth = 1;
    s_Ui.Reset();
    s_StickHeld = 0.0f;
    s_Active = true;
    Engine_LogInfo("Ui initialized. Quad budget %u per frame.", static_cast<unsigned>(UI::Capacity()));
    return true;
}

void Ui_ResetRuntimeState()
{
    if (!s_Active)
        return;

    UiInternal_FontForget();
    UiInternal_StateReset();
    s_OverlayCount = 0;
    s_OverlayDropped = 0;
    UiInternal_ToastsReset();
    UiInternal_DialogReset();
    s_BudgetOpen = false;
    s_BudgetDropped = 0;
    s_LastContainerQuads = 0;
    s_RunsUsed = 0;

    const int screenW = s_State.screenW;
    const int screenH = s_State.screenH;
    memset(&s_State, 0, sizeof(s_State));
    s_State.screenW = screenW;
    s_State.screenH = screenH;
    s_State.focusIndex = -1;
    s_State.clipDepth = 1;
    s_Ui.Reset();
    s_StickHeld = 0.0f;
    UiInternal_ThemeReset();
}

void Engine_Ui_Shutdown()
{
    UiInternal_FontShutdown();
    s_Active = false;
    s_Ui.Reset();
    memset(&s_State, 0, sizeof(s_State));
    s_State.focusIndex = -1;
    s_State.clipDepth = 1;
}

bool Engine_Ui_IsActive() { return s_Active; }

void Ui_BeginFrame()
{
    if (!s_Active)
        return;
    if (s_State.inFrame)
    {
        Engine_LogError("Ui: BeginFrame called with a frame already open");
        return;
    }

    UiInternal_UpdateFont();

    s_State.inFrame = true;
    s_State.inPanel = false;
    s_State.inBox = false;
    s_State.collisionReported = false;
    s_State.focusableCount = 0;
    s_State.focusIndex = -1;
    s_State.hotId = 0;
    s_State.navDelta = 0;
    s_State.pointerMoved = false;
    s_State.clipOverflowReported = false;
    s_State.disabledDepth = 0;
    s_State.disabledStackDepth = 0;
    s_State.disabledOverflowCount = 0;
    s_State.disabledOverflowReported = false;
    s_State.idDepth = 0;
    s_State.groupCount = 0;
    s_State.groupDelta = 0;
    s_State.consumedHorizontal = false;
    s_State.menuCapturing = false;
    s_State.textEditCapturing = false;
    s_State.inScroll = false;
    s_State.scrollNestingReported = false;
    s_State.inColumns = false;
    s_State.inOverlay = false;
    s_State.modalOpen = false;
    s_State.focusRowValid = false;
    s_State.focusRowScrollId = 0;
    if (s_BudgetOpen)
    {
        // A scope left open past its own frame is the same "container left
        // open" mistake a panel or a clip already discards the frame over --
        // here it simply closes rather than capping nothing all frame.
        s_BudgetOpen = false;
        s_BudgetDropped = 0;
    }
    s_State.rowOpen = false;
    s_State.runActive = false;
    s_State.inlineActive = false;
    s_State.nextItemXActive = false;
    s_State.nextItemWidthActive = false;
    s_State.nextItemHeightActive = false;
    s_OverlayCount = 0;
    s_Ui.Reset();
    UiInternal_StateBeginFrame();

    ReadScreenSize();

    s_State.clipDepth = 1;
    s_State.clipHighWater = 1;
    s_State.clipStack[0].x = 0;
    s_State.clipStack[0].y = 0;
    s_State.clipStack[0].w = static_cast<int16_t>(s_State.screenW);
    s_State.clipStack[0].h = static_cast<int16_t>(s_State.screenH);

    const float dt = Engine_GetDeltaTime();
    const int vertical = Repeat(GamepadButton::DPadDown, GamepadButton::DPadUp, s_VerticalHeld, s_VerticalFired, dt);
    if (vertical != 0)
    {
        s_State.navDelta = vertical;
        s_State.pointer.visible = false;
    }
    s_State.horizontalRepeat = Repeat(GamepadButton::DPadRight, GamepadButton::DPadLeft, s_HorizontalHeld, s_HorizontalFired, dt);
    s_State.groupDelta = s_State.horizontalRepeat;

    s_State.accept = WasGamePadButtonPressed(0, GamepadButton::Cross);
    s_State.back = WasGamePadButtonPressed(0, GamepadButton::Circle);

    s_State.tabDelta = 0;
    if (WasGamePadButtonPressed(0, GamepadButton::R1))
        s_State.tabDelta = 1;
    else if (WasGamePadButtonPressed(0, GamepadButton::L1))
        s_State.tabDelta = -1;

    const Vector2 scrollStick = GetGamePadAxis(0, GamepadStick::Right);
    s_State.scrollStickDelta = scrollStick.y * SCROLL_STICK_SPEED * dt;
    if (scrollStick.y != 0.0f)
        s_State.pointer.visible = false;

    UpdateStickPointer(Engine_GetDeltaTime());
    UpdateMousePointer();
    UpdateTouchPointer();
    ClampPointer();

    const bool pressed = PointerPressed();
    s_State.pointer.down = PointerHeld();
    s_State.pointer.pressed = pressed;
}

void Ui_EndFrame()
{
    if (!s_Active)
        return;
    if (!s_State.inFrame)
    {
        Engine_LogError("Ui: EndFrame called with no frame open");
        return;
    }
    if (s_State.inPanel || s_State.clipDepth != 1 || s_State.disabledStackDepth != 0 || s_State.disabledOverflowCount != 0)
    {
        Engine_LogError("Ui: a container was left open at end of frame; nothing submitted");
        s_State.inFrame = false;
        s_State.inPanel = false;
        s_State.clipDepth = 1;
        s_State.disabledDepth = 0;
        s_State.disabledStackDepth = 0;
        s_State.disabledOverflowCount = 0;
        s_Ui.Reset();
        return;
    }

    if (s_State.menuCapturing || s_State.textEditCapturing)
    {
        // An open menu moved its own highlighted item directly against
        // navDelta/accept/back as it drew, so focusId is left exactly as the
        // menu's own title cell set it -- resolving it again here would fight
        // that instead of leaving the open menu's title looking focused. A
        // Ui_TextInput row being edited needs the same thing: focusId already
        // names it, and nothing here should move it away mid-edit.
    }
    else if (s_State.focusableCount > 0)
    {
        const int count = static_cast<int>(s_State.focusableCount);
        int index = (s_State.focusIndex < 0) ? 0 : s_State.focusIndex;

        // Left and right have three claimants, in order: the focused widget
        // itself when it edits with them (a slider, a stepper, a tab strip);
        // failing that, the run it shares a row with, if it is not alone on
        // that row; failing that, movement between groups. Without the first
        // rule, grouping would silently steal a slider's own gesture; without
        // the second, two widgets placed side by side with Ui_SameLine would
        // be unreachable by the axis that separates them.
        if (s_State.groupDelta != 0 && !s_State.consumedHorizontal)
        {
            int first = 0;
            int span = count;
            for (uint8_t g = 0; g < s_State.groupCount; ++g)
            {
                if (index >= s_State.groups[g].first && index < s_State.groups[g].first + s_State.groups[g].count)
                {
                    first = s_State.groups[g].first;
                    span = s_State.groups[g].count;
                    break;
                }
            }

            const int16_t rowKey = s_State.focusRows[index];
            int runFirst = index;
            while (runFirst > first && s_State.focusRows[runFirst - 1] == rowKey)
                --runFirst;
            int runLast = index;
            while (runLast + 1 < first + span && s_State.focusRows[runLast + 1] == rowKey)
                ++runLast;

            if (runLast > runFirst)
            {
                int next = index + s_State.groupDelta;
                if (next < runFirst)
                    next = runFirst;
                if (next > runLast)
                    next = runLast;
                index = next;
                s_State.pointer.visible = false;
            }
            else if (s_State.groupCount > 1)
            {
                int current = 0;
                for (uint8_t g = 0; g < s_State.groupCount; ++g)
                {
                    if (index >= s_State.groups[g].first && index < s_State.groups[g].first + s_State.groups[g].count)
                    {
                        current = g;
                        break;
                    }
                }
                for (uint8_t step = 0; step < s_State.groupCount; ++step)
                {
                    current += s_State.groupDelta;
                    while (current < 0)
                        current += s_State.groupCount;
                    while (current >= s_State.groupCount)
                        current -= s_State.groupCount;
                    if (s_State.groups[current].count > 0)
                        break;
                }
                index = s_State.groups[current].first;
                s_State.pointer.visible = false;
            }
        }
        else if (s_State.navDelta != 0 && s_State.groupCount > 0)
        {
            // Cycling stays inside the active group; crossing between them is
            // the other axis. Without this, moving down the last row of one
            // panel walks into whichever panel happened to be built next.
            //
            // A run shares one row, so cycling steps over the whole run rather
            // than one focusable at a time within it: walk in the requested
            // direction until the row key changes, or the group has only one
            // row and there is nowhere to go.
            int first = 0;
            int span = count;
            for (uint8_t g = 0; g < s_State.groupCount; ++g)
            {
                if (s_State.groups[g].count > 0 && index >= s_State.groups[g].first && index < s_State.groups[g].first + s_State.groups[g].count)
                {
                    first = s_State.groups[g].first;
                    span = s_State.groups[g].count;
                    break;
                }
            }

            const int16_t rowKey = s_State.focusRows[index];
            int next = index;
            for (int step = 0; step < span; ++step)
            {
                int offset = (next - first) + s_State.navDelta;
                while (offset < 0)
                    offset += span;
                while (offset >= span)
                    offset -= span;
                next = first + offset;
                if (s_State.focusRows[next] != rowKey)
                    break;
            }
            index = next;
        }
        else
        {
            index += s_State.navDelta;
        }

        while (index < 0)
            index += count;
        while (index >= count)
            index -= count;
        s_State.focusId = s_State.focusables[index];
        s_State.focusIndex = index;
    }
    else
    {
        s_State.focusId = 0;
    }

    s_State.clipDepth = 1;
    UiInternal_DrawTextEditOverlay();
    UiInternal_DrawToasts(Engine_GetDeltaTime());

    // The cursor is drawn last within the overlay, so nothing can cover it.
    s_State.inOverlay = true;
    DrawCursor();
    s_State.inOverlay = false;

    if (s_Ui.Dropped() > 0)
    {
        Engine_LogError("Ui: dropped %u quad(s) this frame (budget %u); the screen needs paging.", static_cast<unsigned>(s_Ui.Dropped()), static_cast<unsigned>(UI::Capacity()));
    }

    // The overlay is concatenated rather than sorted: one hand-off, order
    // preserved, and bounded work whatever is on screen.
    for (uint32_t i = 0; i < s_OverlayCount; ++i)
        s_Ui.Add(s_Overlay[i]);

    if (s_OverlayDropped > 0)
    {
        Engine_LogError("Ui: dropped %u overlay quad(s) this frame (budget %u).", static_cast<unsigned>(s_OverlayDropped), static_cast<unsigned>(UI_MAX_OVERLAY_QUADS));
        s_OverlayDropped = 0;
    }

    // A run opens at the first quad and at every quad after whose texture
    // differs from the one before it -- the same coalescing rule
    // StagedGeometry::AddQuad2D uses, computed here directly against this
    // frame's own quad sequence so it reads the same on every backend,
    // including PS2's GIFTAG path, which does not itself count runs at all.
    s_RunsUsed = 0;
    {
        const UiQuad* quads = s_Ui.Quads();
        const uint32_t count = s_Ui.Count();
        if (count > 0)
        {
            s_RunsUsed = 1;
            for (uint32_t i = 1; i < count; ++i)
            {
                if (quads[i].texture != quads[i - 1].texture)
                    ++s_RunsUsed;
            }
        }
    }

    Renderer* renderer = Engine_GetRenderer();
    if (renderer)
    {
        const Vector2 offset = Vector2{0.0f, 0.0f};
        const Vector2 scale = Vector2{1.0f, 1.0f};
        renderer->AddUIToDrawList(s_Ui, offset, scale);
    }

    s_State.inFrame = false;
}

bool Ui_WasBackPressed() { return s_Active && s_State.back; }

const UiPointer& Ui_GetPointer() { return s_State.pointer; }

uint32_t Ui_FocusablesUsed() { return s_State.focusableCount; }

uint32_t Ui_FocusableBudget() { return UI_MAX_FOCUSABLES; }

uint32_t Ui_OverlayQuadsUsed() { return s_OverlayCount; }

uint32_t Ui_OverlayQuadBudget() { return UI_MAX_OVERLAY_QUADS; }

uint32_t Ui_ClipDepthUsed() { return s_State.clipHighWater; }

uint32_t Ui_ClipDepthBudget() { return UI_MAX_CLIP_DEPTH; }

uint32_t Ui_QuadsUsed() { return s_Ui.Count(); }

uint32_t Ui_QuadBudget() { return UI::Capacity(); }

bool Ui_WouldFit(int quads)
{
    const uint32_t add = (quads > 0) ? static_cast<uint32_t>(quads) : 0;
    if (s_BudgetOpen)
        return (s_Ui.Count() - s_BudgetStartCount) + add <= static_cast<uint32_t>(s_BudgetCap);
    return s_Ui.Count() + add <= UI::Capacity();
}

void Ui_BeginBudget(int quads)
{
    if (!UiInternal_CanDraw() || s_BudgetOpen)
        return;
    s_BudgetOpen = true;
    s_BudgetCap = (quads > 0) ? quads : 0;
    s_BudgetStartCount = s_Ui.Count();
    s_BudgetDropped = 0;
}

void Ui_EndBudget()
{
    if (!UiInternal_CanDraw() || !s_BudgetOpen)
        return;
    s_LastContainerQuads = s_Ui.Count() - s_BudgetStartCount;
    if (s_BudgetDropped > 0)
    {
        Engine_LogError("Ui: a budgeted container dropped %u quad(s) (cap %d).", static_cast<unsigned>(s_BudgetDropped), s_BudgetCap);
        s_BudgetDropped = 0;
    }
    s_BudgetOpen = false;
}

uint32_t Ui_ContainerQuadsUsed() { return s_LastContainerQuads; }

uint32_t Ui_RunsUsed() { return s_RunsUsed; }

uint32_t Ui_RunBudget() { return GFX_MAX_2D_RUNS; }
