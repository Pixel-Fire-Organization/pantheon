#pragma once

#include <cstddef>
#include "../../include/ui/EngineUi.h"

#include "graphics/FontFormat.h"

/// A clip rectangle in screen pixels. Quads are clipped against the top of the
/// stack before they are appended, so content outside it costs nothing.
struct UiClipRect
{
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
};

/// One widget's retained interaction state.
///
/// Never holds a value the interface displays -- only where a region is
/// scrolled to, whether a node is open, and how long a repeat has been held.
/// That distinction is what keeps the display unable to disagree with the thing
/// displayed.
struct UiState
{
    uint32_t id;
    uint32_t touchedFrame;
    int32_t whole;
    float fraction;
};

/// One navigation group: directional movement cycles within the active group
/// and moves between groups across it.
struct UiFocusGroup
{
    uint32_t id;
    uint16_t first;
    uint16_t count;
};

/// Per-frame interface state, shared between the core and the widgets.
struct UiFrameState
{
    int screenW;
    int screenH;

    int panelX;
    int panelY;
    int panelW;
    int panelH;
    int contentX;
    int contentW;
    int cursorY;

    // The row a widget last took, so Ui_SameLine can rewind onto it and a
    // fresh row knows how tall the one it is closing turned out to be. rowOpen
    // is what tells a fresh row apart from one continued by Ui_SameLine: every
    // direct write to cursorY outside UiInternal_TakeRow clears it. runActive
    // is narrower: whether the row already has an inline-placed widget on it,
    // which is what tells Ui_SameLine continuing a run apart from Ui_SameLine
    // starting one -- the widget drawn just before it may have taken the row's
    // full width and have nothing to share it with.
    int lastRowTop;
    int lastRowHeight;
    int lastRowRight;
    int inlineX;
    int inlineWidth;
    bool inlineActive;
    bool rowOpen;
    bool runActive;

    // A one-shot override for the next widget's left edge, width and/or row
    // height, set by Ui_SetCursorX/Ui_SetNextItemWidth/Ui_SetNextItemHeight
    // and consumed by the first UiInternal_TakeRow call after it,
    // independently of the inline fields above: it applies whether that row
    // is a fresh one or one Ui_SameLine continued.
    int nextItemX;
    bool nextItemXActive;
    int nextItemWidth;
    bool nextItemWidthActive;
    int nextItemHeight;
    bool nextItemHeightActive;

    // A one-shot override for the next Ui_BeginPanel/Ui_BeginPanelSlot/
    // Ui_BeginModal call's background and collapsibility, consumed the
    // moment that call opens.
    UiRgba nextPanelBackgroundColor;
    bool nextPanelBackgroundColorActive;
    int32_t nextPanelBackgroundImageHandle;
    bool nextPanelBackgroundImageActive;
    bool nextPanelCollapsible;
    bool nextPanelCollapsibleActive;
    // Whether Ui_BeginPanel pushed a second, zero-height clip layer to hide a
    // collapsed panel's content; Ui_EndPanel pops it only when this is set.
    bool panelContentClipped;

    int boxX;
    int boxY;
    int boxW;
    int boxH;

    uint32_t focusId;
    uint32_t activeId;
    uint32_t hotId;

    uint32_t focusables[UI_MAX_FOCUSABLES];
    // The row each focusable was taken on (UiFrameState::lastRowTop at
    // registration), so navigation can tell a multi-widget run apart from a
    // column of single-widget rows without a second registration call.
    int16_t focusRows[UI_MAX_FOCUSABLES];
    uint16_t focusableCount;
    int focusIndex;
    int navDelta;

    UiClipRect clipStack[UI_MAX_CLIP_DEPTH];
    uint8_t clipDepth;
    uint8_t clipHighWater;
    bool clipOverflowReported;

    // Whether an enclosing scope disabled the widgets inside it. A stack of
    // per-level markers rather than a plain toggle, so Ui_BeginDisabled(false)
    // inside an active disable cannot re-enable it: the marker for that level
    // is simply "contributed nothing", and popping it leaves the level below
    // in charge.
    int disabledDepth;
    uint8_t disabledStack[UI_MAX_CLIP_DEPTH];
    uint8_t disabledStackDepth;
    // Levels nested past the array's capacity: conservatively counted as
    // disabled (never as the level that re-enables), since Ui_BeginDisabled
    // returns nothing a caller could react to, unlike the internal clip stack.
    uint8_t disabledOverflowCount;
    bool disabledOverflowReported;

    uint32_t idStack[UI_MAX_ID_DEPTH];
    uint8_t idDepth;

    UiFocusGroup groups[UI_MAX_FOCUS_GROUPS];
    uint8_t groupCount;
    int8_t activeGroup;
    int groupDelta;
    bool consumedHorizontal;

    int navRepeat;
    int horizontalRepeat;

    // Which way a shoulder button asks the open tab bar to step this frame:
    // -1 for L1, +1 for R1, 0 when neither was pressed. An edge, not a held
    // repeat -- tab counts are small enough that discrete taps are what a
    // player expects, the same way they would from a settings menu.
    int tabDelta;

    // Pixels the right stick asks the open scroll region to move by this
    // frame, already scaled by deflection and dt. Independent of focus-follow
    // scrolling: this lets a player look ahead in a list without moving focus
    // off whatever is already selected.
    float scrollStickDelta;

    int scrollY;
    int scrollTop;
    int scrollBottom;
    uint32_t scrollId;
    int scrollContentStart;
    bool inScroll;
    bool scrollNestingReported;

    int columnCount;
    int columnIndex;
    int columnStartY;
    int columnMaxY;
    int columnX;
    int columnWidth;
    bool inColumns;

    bool inOverlay;
    bool modalOpen;
    uint32_t modalId;

    // An open menu is reading Up/Down/Accept/Back itself this frame, to move
    // its own highlighted item, rather than letting the ordinary focus
    // resolution in Ui_EndFrame touch focusId: groups are strictly sequential
    // and never reopen once left, which a menu's transient, conditionally-open
    // item list cannot be expressed as.
    bool menuCapturing;

    // A Ui_TextInput row is being edited this frame: focus must hold on that
    // row rather than let Up/Down move it elsewhere mid-edit, the same
    // suppression an open menu already needs and for the same reason.
    bool textEditCapturing;

    // Where the focused row sits this frame, so a scrolling region can pull it
    // into view without the row having to know it is inside one.
    int focusRowTop;
    int focusRowHeight;
    bool focusRowValid;

    // Which scrolling region that row was registered inside, or zero when it
    // was not inside one. Frame state is shared by every region in the frame,
    // so a region must be able to tell its own focused row from another's.
    uint32_t focusRowScrollId;

    UiPointer pointer;
    bool pointerMoved;
    bool accept;
    bool back;
    bool inFrame;
    bool inPanel;
    bool inBox;
    bool collisionReported;
};

/// @return The live frame state.
UiFrameState& UiInternal_State();

/// @return Whether the subsystem is up and a frame is open, so a widget may draw.
bool UiInternal_CanDraw();

/// @return Whether an enclosing Ui_BeginDisabled scope is active. An
///         activatable widget checks this to draw dimmed, skip registering
///         itself focusable, and return false unconditionally.
bool UiInternal_Disabled();

/// @param normal The role a widget would use if nothing disabled it.
/// @return UiColor::TextDisabled when a disabled scope is active, else `normal`.
UiColor UiInternal_TextRole(UiColor normal);

/// Append one solid screen-space rectangle to this frame's quads.
/// @param x Left edge in screen pixels.
/// @param y Top edge in screen pixels.
/// @param w Width in pixels; a non-positive value draws nothing.
/// @param h Height in pixels; a non-positive value draws nothing.
/// @param color The colour to fill with.
void UiInternal_PushRect(int x, int y, int w, int h, UiRgba color);

/// Append one textured screen-space quad, clipped, with its texture coordinates
/// adjusted to match the clipped rectangle.
/// @param x Left edge in screen pixels.
/// @param y Top edge in screen pixels.
/// @param w Width in pixels; a non-positive value draws nothing.
/// @param h Height in pixels; a non-positive value draws nothing.
/// @param texture Backend texture handle; zero draws a solid fill.
/// @param u0 Left texture coordinate, normalised over the full unsigned range.
/// @param v0 Top texture coordinate.
/// @param u1 Right texture coordinate.
/// @param v1 Bottom texture coordinate.
/// @param color The colour to modulate with.
void UiInternal_PushTexturedQuad(int x, int y, int w, int h, uint32_t texture, uint16_t u0, uint16_t v0, uint16_t u1, uint16_t v1, UiRgba color);

/// Narrow the clip rectangle to the intersection of the current one and this.
/// @param x Left edge in screen pixels.
/// @param y Top edge in screen pixels.
/// @param w Width in pixels.
/// @param h Height in pixels.
/// @return False when the stack is full, which is reported once per frame; the
///         clip is then left unchanged and must not be popped.
bool UiInternal_PushClip(int x, int y, int w, int h);

void UiInternal_PopClip();

/// @return The rectangle content is currently clipped against.
const UiClipRect& UiInternal_CurrentClip();

/// @param x Left edge in screen pixels.
/// @param y Top edge in screen pixels.
/// @param w Width in pixels.
/// @param h Height in pixels.
/// @return Whether any part of the rectangle survives the current clip.
bool UiInternal_ClipVisible(int x, int y, int w, int h);

/// A one-pixel-thick frame around a rectangle.
/// @param x Left edge in screen pixels.
/// @param y Top edge in screen pixels.
/// @param w Width in pixels.
/// @param h Height in pixels.
/// @param thickness Border width in pixels.
/// @param color The colour to draw the border in.
void UiInternal_PushBorder(int x, int y, int w, int h, int thickness, UiRgba color);

/// @param label The widget label, which carries its identity.
/// @return A stable identifier for it, seeded by the open identity scope, so
///         two containers may hold identically-named rows without colliding.
uint32_t UiInternal_Id(const char* label);

/// Open an identity scope. Everything asked for inside it is identified by the
/// scope as well as by its own label.
/// @param text Text distinguishing this scope from its siblings.
void UiInternal_PushId(const char* text);

/// @param index Index distinguishing this scope from its siblings.
void UiInternal_PushIdIndex(int index);

void UiInternal_PopId();

/// Start a new frame for the retained interaction state.
void UiInternal_StateBeginFrame();

/// Drop everything the interface remembers between frames.
void UiInternal_StateReset();

/// @param id The widget identity.
/// @return Its retained state, which is zeroed the first time it is asked for.
///         Never null: past the ceiling a widget gets scratch storage so it
///         still draws, and loses only what it would have remembered.
UiState* UiInternal_StateFor(uint32_t id);

/// @param seed The enclosing scope's hash.
/// @param text The text to fold in.
/// @return The combined hash.
uint32_t UiInternal_StateHash(uint32_t seed, const char* text);

/// Open a navigation group. Directional movement cycles within the active
/// group and moves between groups across it.
/// @param id The group's identity.
void UiInternal_BeginFocusGroup(uint32_t id);

void UiInternal_EndFocusGroup();

/// Advance and draw the front notification, if there is one.
/// @param dt Seconds since the previous frame.
void UiInternal_DrawToasts(float dt);

/// Drop every queued notification.
void UiInternal_ToastsReset();

/// Draw the drawn-fallback keyboard for whichever Ui_TextInput row is mid-edit
/// through it, if one is. Called once per frame after every panel has closed,
/// the same placement UiInternal_DrawToasts already has, because a
/// Ui_TextInput row is registered from inside the caller's own panel and the
/// modal this draws cannot open while one is still on the stack.
void UiInternal_DrawTextEditOverlay();

/// Abandon whatever dialog or Ui_TextInput field is open, cancelling a
/// platform dialog if one was. Called on a runtime reset, because the buffer
/// pointer a mid-edit Ui_TextInput session holds belongs to the scene being
/// torn down and must not be written to once it is.
void UiInternal_DialogReset();

/// Append a quad to whichever buffer the open layer writes to.
void UiInternal_PushOverlayQuad(const UiQuad& quad);

/// @return Whether the overlay layer is currently open.
bool UiInternal_InOverlay();


/// Record a widget as reachable by directional navigation, in call order, and
/// report a duplicate identity once per frame.
/// @param id The widget identity.
/// @return Whether this widget currently holds focus.
bool UiInternal_RegisterFocusable(uint32_t id);

/// The shared body of a full-width activatable row: takes the row, handles
/// focus, hover and press against the id given, and draws its background and
/// (when focused) border -- dimmed and unregistered when a Ui_BeginDisabled
/// scope is active. Every widget built from one row -- a button, a selectable,
/// an icon button, a table row -- is this plus whatever it draws inside the
/// rectangle it returns.
/// @param id The row's identity, already hashed.
/// @param highlight Whether to draw it as the current choice even when it is
///        not focused.
/// @param outX Receives the row's left edge.
/// @param outY Receives the row's top edge.
/// @param outW Receives the row's width; zero means nothing was drawn.
/// @return True on the frame the row is activated.
bool UiInternal_ActivatableRow(uint32_t id, bool highlight, int* outX, int* outY, int* outW);

/// @param x Left edge in screen pixels.
/// @param y Top edge in screen pixels.
/// @param w Width in pixels.
/// @param h Height in pixels.
/// @return Whether the visible cursor is inside the rectangle.
bool UiInternal_PointerOver(int x, int y, int w, int h);

/// Take the next row of the open panel and advance the layout cursor past it.
///
/// A row entirely outside the clip still advances the cursor but reports itself
/// invisible, so a caller can skip both drawing it and registering it as
/// reachable by directional navigation.
/// @param height Row height in pixels.
/// @param outX Receives the row's left edge.
/// @param outY Receives the row's top edge.
/// @param outW Receives the row's width.
/// @param outVisible Receives whether any part of the row survives the clip.
/// @return False when no panel is open, in which case nothing is written.
bool UiInternal_TakeRow(int height, int* outX, int* outY, int* outW, bool* outVisible);

/// Restore the default built-in theme and its font roles.
void UiInternal_ThemeReset();

/// Ask for the cooked font if it has not been asked for, and resolve its atlas
/// to a backend handle. Called once at the top of a frame, so the handle a
/// frame's quads carry cannot change part-way through it.
void UiInternal_UpdateFont();

/// Release the cooked font.
void UiInternal_FontShutdown();

/// Drop the cooked font without releasing it, because the runtime reset already
/// released every resource including pinned ones.
void UiInternal_FontForget();

/// @return The cooked font, or null when the built-in one is in use.
const Font* UiInternal_CookedFont();

/// @return The backend texture handle of the cooked font's atlas; zero when
///         there is no cooked font.
uint32_t UiInternal_AtlasTexture();

/// Where a resource handle stands, for a caller that only needs to know
/// whether to draw it, wait, or give up -- not why.
enum class UiTextureState : uint8_t
{
    Absent, ///< No such handle, the wrong resource type, or the renderer refused it.
    Loading,
    Ready
};

/// Resolve a resource handle to a backend texture handle and its pixel
/// dimensions, without touching its lifecycle. The caller owns the handle:
/// this neither loads, pins nor releases it, so it is safe to call every
/// frame against a handle a scene is loading in its own time.
/// @param handle Resource handle, as returned by Engine_Resource_Load or
///        Engine_Resource_LoadAuto; a negative handle is always Absent.
/// @param outTexture Receives the backend handle; zero unless the result is Ready.
/// @param outWidth Receives the texture's pixel width; zero unless Ready.
/// @param outHeight Receives the texture's pixel height; zero unless Ready.
/// @return Where the handle stands.
UiTextureState UiInternal_ResolveTexture(int32_t handle, uint32_t* outTexture, int* outWidth, int* outHeight);

/// Draw a string with whichever font is active.
/// @param x Left edge in screen pixels.
/// @param y Top edge in screen pixels.
/// @param scale Whole-pixel size of one font dot.
/// @param text The string; characters outside the font draw blank.
/// @param color The colour to draw in.
/// @return The x position just past the string.
int UiFont_Draw(int x, int y, int scale, const char* text, UiRgba color);
