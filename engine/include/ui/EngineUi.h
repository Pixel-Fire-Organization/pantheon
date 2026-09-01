#pragma once

#include <cstddef>
#include <cstdint>

#include "../graphics/Primitives.h"
#include "../graphics/ThemeFormat.h"
#include "../graphics/UI.h"
#include "../platform/PlatformKeys.h"
#include "UiThemeIds.h"

/// A colour role. Widgets ask for a role, never for a literal colour, so a theme
/// is one value that can be replaced whole. See docs/subsystems/UI.md.
enum class UiColor : uint8_t
{
    WindowBackground = 0,
    PanelBackground,
    Border,
    Header,
    Text,
    TextDim,
    TextAccent,
    TextWarn,
    TextDisabled,
    ItemBackground,
    ItemHovered,
    ItemActive,
    Focus,
    BarTrack,
    BarFill,
    BarFillWarn,
    Cursor,
    CursorOutline,

    Count
};

struct UiRgba
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};

/// Colours and metrics for the whole interface.
///
/// This layout is an on-disc contract: a cooked theme is a copy of this block,
/// not a parse of it. Fields may not be reordered or resized without a theme
/// format version change, and the build asserts its size against the value the
/// format declares. See docs/formats/THEME_FORMAT.md.
struct UiStyle
{
    UiRgba colors[static_cast<uint8_t>(UiColor::Count)];
    float repeatDelaySeconds;
    float repeatIntervalSeconds;
    int16_t panelPadding;
    int16_t itemSpacing;
    int16_t borderWidth;
    int16_t textScale;
    int16_t rowPadding;
    int16_t barHeight;
    int16_t cursorSize;
    int16_t screenMargin;
    int16_t panelGap;
    int16_t scrollBarWidth;
    int16_t menuBarHeight;
    int16_t caretWidth;
    int16_t iconSpacing;
    int16_t reserved[1];
};

/// A decoded theme: its style block and the font key serving each text role.
///
/// A theme is colours, metrics *and* fonts together, so the decode stages all
/// three into one object the caller owns. Nothing here is live until a caller
/// commits it, which is what lets a theme be inspected, or refused on a bad
/// metric, without its font keys reaching the interface.
struct UiTheme
{
    UiStyle style;
    char roleFont[static_cast<uint8_t>(UiFontRole::Count)][THEME_KEY_MAX];
};

/// Which device last moved the cursor. The stick source exists on every
/// platform; the others appear only where the platform reports the capability.
enum class UiPointerSource : uint8_t
{
    None = 0,
    Stick,
    Mouse,
    Touch,

    Count
};

/// The single cursor, whichever device is currently driving it.
struct UiPointer
{
    float x;
    float y;
    float stickSpeed;
    UiPointerSource source;
    bool down;
    bool pressed;
    bool visible;
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

/// Bring the interface up. Requires the renderer and input to be running.
/// @return False when the renderer is absent.
bool Engine_Ui_Init();

void Engine_Ui_Shutdown();

/// @return Whether widget calls will do anything this run.
bool Engine_Ui_IsActive();

/// Draw a line straight at a renderer with the built-in font, outside any
/// interface frame.
///
/// For the panic display, which runs when the interface may be down and which
/// never returns. It touches no subsystem state and allocates nothing.
/// @param renderer The renderer to draw into; must have a frame open.
/// @param x Left edge in framebuffer pixels.
/// @param y Top edge in framebuffer pixels.
/// @param scale Whole-pixel size of one font dot.
/// @param text The line to draw.
/// @param color The colour to draw in.
void Engine_DrawPanicText(class Renderer* renderer, int x, int y, int scale, const char* text, UiRgba color);

/// Open the frame: reads input into the cursor and navigation state, and clears
/// the quad buffer. Widget calls outside a frame are refused and reported.
void Ui_BeginFrame();

/// Close the frame: resolves focus for the next frame and submits the quads to
/// the active renderer.
void Ui_EndFrame();

/// Drop everything the interface remembers between scenes: focus, the container
/// stacks, and the cooked font, whose resources the runtime reset has already
/// released. Restores the default theme.
void Ui_ResetRuntimeState();

/// Which fixed region of the screen a panel occupies. Panels stay explicitly
/// placed; this is a small named set, not a layout solver.
enum class UiPanelSlot : uint8_t
{
    Full = 0,
    Left,
    Right,
    Top,
    Bottom,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,

    Count
};

/// How text sits within the width it is given.
enum class UiAlign : uint8_t
{
    Left = 0,
    Center,
    Right,

    Count
};

// ---------------------------------------------------------------------------
// Containers and layout
// ---------------------------------------------------------------------------

/// Open a panel at an explicit screen position. Widgets stack inside it, in the
/// order they are asked for.
/// @param title Drawn as the panel header; pass an empty string for no header.
/// @param x Left edge in pixels.
/// @param y Top edge in pixels.
/// @param w Width in pixels.
/// @param h Height in pixels.
/// @return False when the panel is collapsed, so a caller may skip drawing
///         its content; true otherwise. Ui_EndPanel must be called either way.
bool Ui_BeginPanel(const char* title, int x, int y, int w, int h);

void Ui_EndPanel();

/// Override the next panel's background colour, in place of
/// UiColor::PanelBackground. One-shot, consumed by the next
/// Ui_BeginPanel/Ui_BeginPanelSlot/Ui_BeginModal call.
/// @param color The colour to fill the panel's background with.
void Ui_SetNextPanelBackgroundColor(UiRgba color);

/// Override the next panel's background with a stretched image instead of a
/// solid fill -- still exactly one quad, drawn in place of
/// UiColor::PanelBackground rather than as a layer over it. Falls back to the
/// solid fill while the texture is not ready, the same way any other resource
/// handle degrades. One-shot, consumed by the next
/// Ui_BeginPanel/Ui_BeginPanelSlot/Ui_BeginModal call.
/// @param handle Resource handle of the texture, owned by the caller: this
///        neither loads, pins nor releases it.
void Ui_SetNextPanelBackgroundImage(int32_t handle);

/// Make the next panel collapsible: its header becomes an activatable row
/// that toggles it, and its content is hidden -- drawn nowhere, unreachable
/// by navigation -- while collapsed, remembered the same way a tree node's
/// open state is. Has no effect on a panel with no title. One-shot, consumed
/// by the next Ui_BeginPanel/Ui_BeginPanelSlot/Ui_BeginModal call.
/// @param collapsible Whether the next panel should be collapsible.
void Ui_SetNextPanelCollapsible(bool collapsible);

/// Advance the layout cursor without drawing anything.
/// @param pixels How far down to move.
void Ui_Spacing(int pixels);

/// Place the next widget at an explicit width instead of the panel's full
/// content width, continuing the current row rather than starting a new one.
///
/// Called before the widget it positions, as many times in a row as the row
/// has widgets: the first call starts a run at the row's left edge -- whatever
/// was drawn immediately before, if anything, already took the full row and is
/// not part of it -- and each further call continues it, one widget-width to
/// the right of the last. A widget call with no Ui_SameLine before it always
/// starts its own fresh, full-width row and ends the run above it, so leaving
/// a call off is how a run closes.
///
/// Widgets sharing a row this way form a navigation run: Left and Right move
/// within it before they move between containers, and Up and Down step over
/// the whole run to the row above or below rather than through it one widget
/// at a time.
/// @param width Width in pixels for the next widget; zero or less gives it
///        whatever is left of the row, which is what the *last* widget of a
///        run should ask for. Passing zero for one that is not last claims the
///        whole remainder for it instead, leaving nothing for the calls after.
void Ui_SameLine(int width);

/// Override the width of the next widget only, independently of Ui_SameLine:
/// it applies whether that widget starts a fresh row or continues one
/// Ui_SameLine placed it on. One-shot -- consumed by the next widget call and
/// not retried if none follows before the row context changes (a fresh
/// panel, scroll region or column).
/// @param width Width in pixels for the next widget; zero or less leaves its
///        own computed width alone.
void Ui_SetNextItemWidth(int width);

/// Override the row height of the next widget only. Same one-shot rules as
/// Ui_SetNextItemWidth.
/// @param height Row height in pixels for the next widget; zero or less
///        leaves its own computed height alone.
void Ui_SetNextItemHeight(int height);

/// A full-width rule.
void Ui_Separator();

/// A vertical rule, for dividing cells placed with Ui_SameLine.
/// @param height Rule height in pixels.
void Ui_SeparatorVertical(int height);

/// A non-selectable category heading, used to group the rows under it.
/// @param text The heading.
void Ui_Header(const char* text);

/// Open a panel filling one of the named screen regions, with the theme's
/// screen margin and panel gap applied. Removes the need for every caller to
/// recompute the same halves of the screen.
/// @param title Drawn as the panel header; pass an empty string for no header.
/// @param slot Which region to fill.
/// @return The same as Ui_BeginPanel.
bool Ui_BeginPanelSlot(const char* title, UiPanelSlot slot);

/// @return The framebuffer width the interface is laid out against.
int Ui_ScreenWidth();

/// @return The framebuffer height the interface is laid out against.
int Ui_ScreenHeight();

/// Open a scrolling region inside the open panel.
///
/// Content is clipped to it and the scroll position is remembered between
/// frames; directional navigation scrolls the focused row into view, so a
/// region that does not fit is scrolled rather than paged. The right stick
/// also moves the band directly, independently of focus, which is the only
/// way to scroll a region with nothing focusable inside it. A row entirely
/// outside the region costs nothing: it is neither drawn nor focusable.
/// @param id Identity of the region, which is where its scroll position is filed.
/// @param height Region height in pixels.
/// @return False when no panel is open, in which case nothing is drawn.
bool Ui_BeginScroll(const char* id, int height);

void Ui_EndScroll();

/// A bordered, fixed-height scrolling list of choices -- Ui_BeginScroll and a
/// Ui_Selectable loop in one call, for a list too long to lay out one row at a
/// time. Inherits the no-nested-scrolling limit: opening one inside an already
/// open scrolling region is refused and reported, the same as Ui_BeginScroll.
/// @param id Identity of the region, which is where its scroll position is filed.
/// @param index Read for the current choice, written when it changes.
/// @param items The choices.
/// @param count How many entries `items` has.
/// @param height Region height in pixels.
/// @return True on the frame the value changed.
bool Ui_ListBox(const char* id, int* index, const char* const* items, int count, int height);

/// Divide the remaining panel width into equal columns.
/// @param count How many columns; one or fewer does nothing.
void Ui_BeginColumns(int count);

/// Move to the next column, resetting the layout cursor to the column top.
void Ui_NextColumn();

/// Close the column set, leaving the cursor below the tallest column.
void Ui_EndColumns();

/// Open a set of named, fixed-width columns for the rows that follow, laid out
/// with Ui_SameLine under the hood so a table is not a parallel layout system.
/// @param id Identity of the table, which the id scope for its rows nests in.
/// @param widths One entry per column; zero or less shares the width left
///        after the explicit ones equally among the columns that asked for it.
/// @param count How many entries `widths` has.
/// @return False when no panel is open, in which case nothing is drawn.
bool Ui_BeginTable(const char* id, const int* widths, int count);

/// A non-selectable row naming each column, in the header colour role.
/// @param labels One entry per column; a null entry leaves that column blank.
void Ui_TableHeader(const char* const* labels);

/// Open one selectable row and reset the column cursor Ui_TableCell advances.
/// @param id Identity of the row, which is also its widget identity.
/// @param selected Whether to draw it as the current choice.
/// @return True on the frame it is activated.
bool Ui_TableRow(const char* id, bool selected);

/// One cell of the open row, at the next column.
/// @param text The cell's text.
/// @param align How to sit it within the column's width.
/// @param role Which colour role to draw it in.
void Ui_TableCell(const char* text, UiAlign align, UiColor role);

void Ui_EndTable();

/// A row that opens and closes a nested group of rows.
/// @param label The name, which is also the widget identity.
/// @param defaultOpen Whether it starts open the first time it is seen.
/// @return Whether the group is open, and its contents should be drawn.
bool Ui_BeginTree(const char* label, bool defaultOpen);

void Ui_EndTree();

/// A strip of pages across the top of a panel. L1 and R1 step it directly, in
/// addition to focusing and accepting a title or pointing at one.
/// @param id Identity of the strip, which is where the active page is filed.
/// @return False when no panel is open.
bool Ui_BeginTabBar(const char* id);

/// @param label The tab's name.
/// @return Whether this tab is the active one, and its page should be drawn.
bool Ui_Tab(const char* label);

void Ui_EndTabBar();

/// Open an identity scope, so two containers may hold identically-named rows.
/// @param text Text distinguishing this scope from its siblings.
void Ui_PushId(const char* text);

/// @param index Index distinguishing this scope from its siblings.
void Ui_PushIdIndex(int index);

void Ui_PopId();

/// Open a scope that greys out and disables every activatable widget inside
/// it, without changing the layout: a disabled row still draws and still takes
/// its row, but is not reachable by focus or the pointer and always reports no
/// activation. Nests: an inner Ui_BeginDisabled(false) inside an active
/// disable cannot re-enable it, only an enclosing Ui_EndDisabled can.
/// @param disabled Whether this scope disables its contents.
void Ui_BeginDisabled(bool disabled);

void Ui_EndDisabled();

/// @return The left edge of the open panel's content, in screen pixels. Pairs
///         with Ui_ContentWidth for a raw draw call positioned exactly like a
///         row, without a Ui_BeginPanel(x,y,w,h) call recomputing it by hand.
int Ui_ContentX();

/// @return The width in pixels available inside the open panel.
int Ui_ContentWidth();

/// @return The height in pixels left in the open panel below the layout cursor.
///         What a scrolling region is usually given.
int Ui_ContentHeight();

/// @return The layout cursor's current x position in screen pixels: the next
///         widget's left edge, honouring a pending Ui_SetCursorX override or
///         wherever Ui_SameLine placed the pen, and the panel's own content
///         edge otherwise.
int Ui_CursorX();

/// @return The layout cursor's current y position in screen pixels.
int Ui_CursorY();

/// Move the layout cursor to an absolute y position, ending whatever row is
/// in progress the same way Ui_Spacing does: the next widget starts fresh at
/// this position rather than below the last row taken.
/// @param y The new cursor position in screen pixels.
void Ui_SetCursorY(int y);

/// Override the left edge of the next widget only, in place of the panel's
/// own content edge or wherever Ui_SameLine placed the pen. One-shot --
/// consumed by the next widget call and not retried if none follows before
/// the row context changes, the same rule Ui_SetNextItemWidth follows.
/// @param x The next widget's left edge in screen pixels.
void Ui_SetCursorX(int x);

/// Ui_SetCursorY and Ui_SetCursorX together.
/// @param x The next widget's left edge in screen pixels.
/// @param y The new cursor position in screen pixels.
void Ui_SetCursor(int x, int y);

// ---------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------

void Ui_Label(const char* text);

/// @param text The line to draw.
/// @param role Which colour role to draw it in.
void Ui_LabelColored(const char* text, UiColor role);

/// A label on the left and a value right-aligned against the panel edge.
/// @param label The name.
/// @param value The value, already formatted.
void Ui_LabelValue(const char* label, const char* value);

/// @param label The name, which is also the widget identity.
/// @return True on the frame it is activated.
bool Ui_Button(const char* label);

/// A row that can be picked out of a list.
/// @param label The name, which is also the widget identity.
/// @param selected Whether to draw it as the current choice.
/// @return True on the frame it is activated.
bool Ui_Selectable(const char* label, bool selected);

/// @param label The name, which is also the widget identity.
/// @param value Read for the current state, written when it changes.
/// @return True on the frame the value changed.
bool Ui_Checkbox(const char* label, bool* value);

/// A row whose identity is its label and whose right-hand text may change.
/// Use this rather than baking state into the label: identity is derived from
/// the label, so a label that changes is a different widget and focus jumps off
/// it at the moment the state it displays changes.
/// @param label The name, which is also the widget identity.
/// @param value Text drawn right-aligned; may differ every frame.
/// @param selected Whether to draw it as the current choice.
/// @return True on the frame it is activated.
bool Ui_SelectableValue(const char* label, const char* value, bool selected);

/// @param label The name, which is also the widget identity.
/// @param value Read for the current position, written when it changes.
/// @param minimum Lowest value the slider can reach.
/// @param maximum Highest value the slider can reach.
/// @return True on the frame the value changed.
bool Ui_SliderInt(const char* label, int* value, int minimum, int maximum);

/// @param label The name, which is also the widget identity.
/// @param value Read for the current position, written when it changes.
/// @param minimum Lowest value the slider can reach.
/// @param maximum Highest value the slider can reach.
/// @param step How far one press moves it.
/// @return True on the frame the value changed.
bool Ui_SliderFloat(const char* label, float* value, float minimum, float maximum, float step);

/// A value edited one step at a time, with an exact step rather than a grain.
/// @param label The name, which is also the widget identity.
/// @param value Read for the current value, written when it changes.
/// @param minimum Lowest value.
/// @param maximum Highest value.
/// @param step How far one press moves it.
/// @return True on the frame the value changed.
bool Ui_Stepper(const char* label, int* value, int minimum, int maximum, int step);

/// A choice cycled in place with Left and Right, rather than dropped down: on a
/// pad, cycling is a better gesture than opening a list, and a genuinely long
/// set belongs in Ui_ListBox instead.
/// @param label The name, which is also the widget identity.
/// @param index Read for the current choice, written when it changes.
/// @param items The choices; cycling wraps from the last back to the first.
/// @param count How many entries `items` has.
/// @return True on the frame the value changed.
bool Ui_Combo(const char* label, int* index, const char* const* items, int count);

/// One of a set of mutually exclusive choices.
/// @param label The name, which is also the widget identity.
/// @param value Read for the current choice, written when this one is picked.
/// @param option The value this row selects.
/// @return True on the frame it is picked.
bool Ui_Radio(const char* label, int* value, int option);

/// A row whose right-hand side is a colour swatch.
/// @param label The name.
/// @param color The colour to show.
void Ui_ColorSwatch(const char* label, UiRgba color);

/// A filled meter with its label and figures above it.
/// @param label The name.
/// @param value Current amount.
/// @param maximum Full-scale amount; a zero draws an empty track.
void Ui_Bar(const char* label, int value, int maximum);

/// A line strip over a fixed window of samples.
/// @param label The name.
/// @param values Samples, oldest first.
/// @param count How many samples to read.
/// @param minimum Value at the bottom of the plot.
/// @param maximum Value at the top of the plot.
/// @param marker A horizontal reference line; pass a value outside the range to
///        omit it.
void Ui_Plot(const char* label, const float* values, int count, float minimum, float maximum, float marker);

/// A framed box the caller can plot normalised points into, for visualising a
/// surface that has no pixel correspondence to the screen.
/// @param label The name.
/// @param height Box height in pixels.
void Ui_BeginPointBox(const char* label, int height);

/// @param nx Horizontal position in [0,1] across the open box.
/// @param ny Vertical position in [0,1] down the open box.
/// @param role Which colour role to draw the point in.
void Ui_Point(float nx, float ny, UiColor role);

void Ui_EndPointBox();

/// Text wrapped to the width available, broken at spaces.
/// @param text The prose to draw.
/// @param role Which colour role to draw it in.
void Ui_LabelWrapped(const char* text, UiColor role);

/// @param text The line to draw.
/// @param align How to sit it within the available width.
/// @param role Which colour role to draw it in.
void Ui_LabelAligned(const char* text, UiAlign align, UiColor role);

/// A line cut short with a trailing marker when it does not fit.
/// @param text The line to draw.
/// @param role Which colour role to draw it in.
void Ui_LabelEllipsized(const char* text, UiColor role);

/// A line cut short from the **front**, keeping the tail, with a leading
/// marker when it does not fit -- for a path or a key, where the identifying
/// part is the end rather than the start.
/// @param path The line to draw.
/// @param role Which colour role to draw it in.
void Ui_LabelPath(const char* path, UiColor role);

/// @param format A printf-style format string.
void Ui_LabelFormat(const char* format, ...) __attribute__((format(printf, 1, 2)));

/// A label on the left and a formatted value right-aligned against the edge.
/// @param label The name.
/// @param format A printf-style format string for the value.
void Ui_LabelValueFormat(const char* label, const char* format, ...) __attribute__((format(printf, 2, 3)));

// ---------------------------------------------------------------------------
// Layers
// ---------------------------------------------------------------------------

/// Open the overlay layer. Everything drawn until it closes is appended after
/// the interface, so it draws above all of it whatever order it was built in.
void Ui_BeginOverlay();

void Ui_EndOverlay();

/// Open a modal panel centred on the screen, over a dimmed backdrop.
/// @param title Drawn as the panel header.
/// @param w Width in pixels.
/// @param h Height in pixels.
/// @return True while the modal is open, so its contents should be drawn.
bool Ui_BeginModal(const char* title, int w, int h);

void Ui_EndModal();

/// Queue a notification. Shown one at a time in the corner, oldest first.
/// @param text The line to show.
/// @param seconds How long to show it.
void Ui_Toast(const char* text, float seconds);

// ---------------------------------------------------------------------------
// Raw drawing, for scenes that need to place something exactly
// ---------------------------------------------------------------------------

/// @param x Left edge in screen pixels.
/// @param y Top edge in screen pixels.
/// @param w Width in pixels.
/// @param h Height in pixels.
/// @param role Which colour role to fill with.
void Ui_Rect(int x, int y, int w, int h, UiColor role);

/// @param x Left edge in screen pixels.
/// @param y Top edge in screen pixels.
/// @param w Width in pixels.
/// @param h Height in pixels.
/// @param color The exact colour to fill with.
void Ui_RectRgba(int x, int y, int w, int h, UiRgba color);

/// A one-pixel-thick frame around a rectangle.
/// @param x Left edge in screen pixels.
/// @param y Top edge in screen pixels.
/// @param w Width in pixels.
/// @param h Height in pixels.
/// @param thickness Border width in pixels.
/// @param role Which colour role to draw the border in.
void Ui_RectOutline(int x, int y, int w, int h, int thickness, UiColor role);

/// @param x Left edge in screen pixels.
/// @param y Top edge in screen pixels.
/// @param scale Whole-pixel size of one font dot.
/// @param text The string; characters outside the font draw a substitute.
/// @param role Which colour role to draw in.
/// @return The x position just past the string.
int Ui_Text(int x, int y, int scale, const char* text, UiColor role);

/// @param x Left edge of the region in screen pixels.
/// @param y Top edge in screen pixels.
/// @param w Region width, which the text is aligned within.
/// @param scale Whole-pixel size of one font dot.
/// @param text The string to draw.
/// @param align How to sit it within the region.
/// @param role Which colour role to draw in.
/// @return The x position just past the string.
int Ui_TextAligned(int x, int y, int w, int scale, const char* text, UiAlign align, UiColor role);

/// A loaded texture as a row of the given height, width following its own
/// aspect. Requires Resource; without it, or for a handle that is loading,
/// absent, or not a texture, draws a placeholder frame rather than nothing, so
/// a missing image is visible rather than silently skipped.
/// @param handle Resource handle of the texture, owned by the caller: this
///        neither loads, pins nor releases it.
/// @param height Row height in pixels.
void Ui_Image(int32_t handle, int height);

/// A model or primitive, drawn fresh this frame and shown as a square row of
/// the given height -- see Renderer::RenderToImage3D for what draws it and
/// its one-target-per-backend limit. Draws the same placeholder frame
/// Ui_Image does on a handle that is not ready, for a backend that has no
/// offscreen-target support (some do not; see the renderer's own spec).
/// @param what The model or primitive to draw.
/// @param camera Where to look from.
/// @param height Row height in pixels, and the rendered image's width.
/// @param clearColor The image's background.
void Ui_Image3D(const Renderable3D& what, const Camera3D& camera, int height, const Color3& clearColor);

/// The same texture, placed exactly and optionally cropped to a sub-rectangle.
/// @param handle Resource handle, as Ui_Image.
/// @param x Left edge in screen pixels.
/// @param y Top edge in screen pixels.
/// @param w Width in pixels.
/// @param h Height in pixels.
/// @param u0 Left texture coordinate, normalised over the full unsigned range.
/// @param v0 Top texture coordinate.
/// @param u1 Right texture coordinate; pass 65535 for the texture's own edge.
/// @param v1 Bottom texture coordinate; pass 65535 for the texture's own edge.
/// @param tint Modulates the texture; UiColor::Text draws it unmodified against
///        a light theme, so a genuinely neutral draw should read from a role
///        that resolves to white.
void Ui_ImageAt(int32_t handle, int x, int y, int w, int h, uint16_t u0, uint16_t v0, uint16_t u1, uint16_t v1, UiColor tint);

// ---------------------------------------------------------------------------
// Input and navigation
// ---------------------------------------------------------------------------

/// @return True on the frame the back action was taken.
bool Ui_WasBackPressed();

/// @return The cursor, whichever device is currently driving it.
const UiPointer& Ui_GetPointer();

// ---------------------------------------------------------------------------
// Style
// ---------------------------------------------------------------------------

const UiStyle& Ui_GetStyle();
void Ui_SetStyle(const UiStyle& style);

/// @param theme Which built-in theme to build.
/// @return Its colours and metrics. Defined by the generated theme table.
UiStyle Ui_BuiltinTheme(UiBuiltinTheme theme);

/// @param theme Which built-in theme to name.
/// @return Its declared name.
const char* Ui_BuiltinThemeName(UiBuiltinTheme theme);

/// @param theme Which built-in theme to read.
/// @param role Which text role to read.
/// @return The font key that theme's declaration resolves for that role, or
///         an empty string when it falls through to the built-in font.
const char* Ui_BuiltinRoleFont(UiBuiltinTheme theme, UiFontRole role);

/// @return The built-in default theme, so a caller can restore it after editing.
UiStyle Ui_DefaultStyle();

/// Replace the live theme with one of the themes built into the binary. Needs
/// no filesystem, which is what makes it available before one exists.
/// @param theme Which built-in theme to use.
void Ui_SetBuiltinTheme(UiBuiltinTheme theme);

/// Replace the live theme with a cooked one.
///
/// The file is validated whole — identity, layout version, size, integrity and
/// the sanity of its values — before anything is written, and committed in one
/// step, so a theme that fails any check leaves the live theme exactly as it
/// was, its font roles included. A refusal is reported naming the check that
/// failed. Blocking: the read is drained before the theme is judged, so the
/// call either applies the theme or refuses it, never neither.
/// @param key Asset key of the cooked theme.
/// @return False when the theme is absent, fails a check, or the resource
///         subsystem is not loaded.
bool Ui_LoadTheme(const char* key);

/// @return The name of the theme in use.
const char* Ui_ThemeName();

/// Decode and validate a cooked theme payload into a theme object.
///
/// The resource subsystem's entry point for a theme asset. A theme is copied
/// into live state rather than parsed, so this is where identity, layout
/// version, size and integrity are checked; nothing downstream would catch a
/// bad one. Nothing live is touched — a decoded theme is a candidate, and a
/// theme that is only inspected never reaches the interface.
/// @param data The payload, immediately after the asset header.
/// @param size Payload length in bytes.
/// @param outTheme Filled on success, untouched on failure.
/// @return False when any check fails; the reason is reported.
bool Ui_ThemeDecode(const void* data, size_t size, UiTheme* outTheme);

/// @param role Which text role to read.
/// @return The asset key of the font serving that role, or an empty string when
///         the role falls through to the built-in font.
const char* Ui_RoleFont(UiFontRole role);

/// @param role Which colour to read.
/// @return The colour that role currently resolves to.
UiRgba Ui_GetColor(UiColor role);

/// @param role Which colour to write.
/// @param value The colour to use from now on.
void Ui_SetColor(UiColor role, UiRgba value);

/// @param role The role to name.
/// @return Its short name, for a style editor.
const char* Ui_ColorName(UiColor role);

// ---------------------------------------------------------------------------
// Font
// ---------------------------------------------------------------------------

/// Whole-pixel dimensions of one cell of the **built-in** font at scale 1.
///
/// These describe the fallback only. Nothing should lay out against them: a
/// cooked font is proportional and supplies its own metrics, so use
/// Ui_TextWidth and Ui_TextHeight instead.
enum : int
{
    UI_FALLBACK_GLYPH_W = 5,
    UI_FALLBACK_GLYPH_H = 7,
    UI_FALLBACK_GLYPH_ADVANCE = 6
};

/// @return Whether a cooked font is in use; false means the built-in one is.
bool Ui_FontIsCooked();

/// @param scale Whole-pixel size of one font dot.
/// @param text The string to measure.
/// @param maxWidth The width to fit within, in screen pixels.
/// @return How many bytes of `text` fit within maxWidth.
int Ui_TextFit(int scale, const char* text, int maxWidth);

/// The mirror of Ui_TextFit: finds how much may be dropped from the **front**
/// rather than the back.
/// @param scale Whole-pixel size of one font dot.
/// @param text The string to measure.
/// @param maxWidth The width to fit within, in screen pixels.
/// @return A pointer into `text` at the longest tail that fits `maxWidth`.
const char* Ui_TextFitTail(int scale, const char* text, int maxWidth);

/// @param scale Whole-pixel size of one font dot.
/// @param text The string to measure.
/// @return Its width in screen pixels.
int Ui_TextWidth(int scale, const char* text);

/// @param scale Whole-pixel size of one font dot.
/// @return A line's height in screen pixels.
int Ui_TextHeight(int scale);

/// @param scale Whole-pixel size of one font dot.
/// @param text The string to measure.
/// @return How many quads drawing it would cost, so a caller can check itself
///         against the per-frame budget before committing to a layout.
int Ui_MeasureTextQuads(int scale, const char* text);

// ---------------------------------------------------------------------------
// Icons
// ---------------------------------------------------------------------------

/// One entry in the atlas's icon and controller-glyph cell set. Drawn from the
/// cooked font's atlas alongside its glyphs when the font declares a cell for
/// it; drawn as a short ASCII stand-in otherwise, so a hint bar stays readable
/// with the fallback font or an atlas that predates a given icon.
enum class UiIcon : uint8_t
{
    None = 0,

    ButtonSouth,
    ButtonEast,
    ButtonWest,
    ButtonNorth,

    // The Xbox and Nintendo families' face-button letters, which the two
    // families place on different buttons. Distinct cells rather than a
    // family flag on the four above, because they are genuinely different
    // glyphs, not a recolouring: Ui_ButtonIcon is the only thing that chooses
    // between them, so a caller asking by icon rather than by button still
    // gets exactly the cell it names.
    ButtonA,
    ButtonB,
    ButtonX,
    ButtonY,

    L1,
    R1,
    L2,
    R2,
    DPad,
    StickLeft,
    StickRight,

    Check,
    Cross,
    Warning,
    Info,
    Folder,
    File,
    ChevronDown,
    ChevronRight,

    Count
};

/// @param icon The icon to draw; UiIcon::None draws nothing.
/// @param role Which colour role to draw it in.
void Ui_Icon(UiIcon icon, UiColor role);

/// @param x Left edge in screen pixels.
/// @param y Top edge in screen pixels.
/// @param scale Whole-pixel size of one font dot; matches Ui_Text's scale.
/// @param icon The icon to draw; UiIcon::None draws nothing and returns `x`.
/// @param role Which colour role to draw it in.
/// @return The x position just past the icon.
int Ui_IconAt(int x, int y, int scale, UiIcon icon, UiColor role);

/// A button labelled with both an icon and text.
/// @param label The name, which is also the widget identity.
/// @param icon Drawn before the label; UiIcon::None omits it.
/// @return True on the frame it is activated.
bool Ui_IconButton(const char* label, UiIcon icon);

/// @param icon The icon to check.
/// @return Whether the active font's atlas declares a cell for it. False means
///         Ui_Icon falls back to an ASCII stand-in rather than drawing nothing.
bool Ui_IconExists(UiIcon icon);

/// @param button The pad button a prompt is naming.
/// @return The icon this platform draws for it -- PlayStation shapes, Xbox or
///         Nintendo letters, or a keyboard key, per
///         PlatformConstant::ButtonIconFamily --
///         so a prompt shows the symbol the player is looking at rather than a
///         name that may not match the hardware. UiIcon::None for a button with
///         no icon in the active set.
UiIcon Ui_ButtonIcon(GamepadButton button);

/// One entry in a Ui_HintBar strip: an icon or a short label, and the action it
/// names.
struct UiHint
{
    UiIcon icon;
    const char* text;
};

/// A centred strip of icon-and-text prompts along the bottom of the screen,
/// drawn in the overlay layer so a scene cannot paint over it.
/// @param hints The prompts to show, left to right.
/// @param count How many entries `hints` has.
void Ui_HintBar(const UiHint* hints, int count);

// ---------------------------------------------------------------------------
// Menus
// ---------------------------------------------------------------------------

/// Open a strip of menu titles docked to the top of a frame: inside an open
/// panel it takes the panel's own top edge, outside one it takes the top of
/// the screen. Everything between this and Ui_EndMenuBar must be Ui_BeginMenu.
/// @return False when a menu bar is already open.
bool Ui_BeginMenuBar();

void Ui_EndMenuBar();

/// One title on an open menu bar. An open menu's items draw in the overlay
/// layer, below the title, so a menu's own draw order is still readable from
/// the calls that built it even though it draws above the rest of the frame.
/// Submenus are not supported: called while another menu is already open, this
/// always returns false.
///
/// May also be called right after Ui_SplitButton, with the same id the split
/// button was given, instead of from inside a menu bar: the chevron opens the
/// same kind of dropdown a menu bar title does, anchored under the button
/// rather than under a title cell.
/// @param id The menu's identity, and the title text when opened from a menu
///        bar.
/// @return True while this menu is open, so its items should follow.
bool Ui_BeginMenu(const char* id);

/// One row of an open menu. Selecting it closes the menu.
/// @param label The item's text.
/// @param shortcut Drawn right-aligned as a hint; pass an empty string for none.
/// @return True on the frame it is selected.
bool Ui_MenuItem(const char* label, const char* shortcut);

void Ui_EndMenu();

/// A button with a second, narrow zone next to it that opens a menu -- the
/// primary action and a set of related ones in one control. Follow with
/// Ui_BeginMenu(label) to draw the menu's items; the chevron zone is what
/// opens it.
/// @param label The name, which is also the widget identity and the id
///        Ui_BeginMenu must be given to draw this button's menu.
/// @param icon Drawn in the chevron zone; UiIcon::ChevronDown is conventional.
/// @return True on the frame the primary zone is activated.
bool Ui_SplitButton(const char* label, UiIcon icon);

// ---------------------------------------------------------------------------
// Dialogs and text entry
// ---------------------------------------------------------------------------
//
// Three mechanisms serve these, first that the platform offers: the host's own
// dialog (PlatformCapability::SystemDialog), a direct character channel
// (PlatformCapability::TextCharacters, text entry only), or the interface's own
// drawn modal, which is always available and is what PS2 draws. Which one ran
// is not observable from the return value -- the contract is the same either
// way. Text entry supports appending and backspacing from the end only; there
// is no mid-string caret placement.

/// How a dialog opened by this API currently stands.
enum class UiDialogResult : uint8_t
{
    None = 0, ///< Not open; the identity has never been asked for, or already resolved.
    Pending, ///< Open. Call again next frame with the same id to keep driving it.
    Accepted,
    Cancelled,

    Count
};

/// A one-button acknowledgement. Call every frame while a caller-owned flag
/// says it should be open, outside any panel -- the placement Ui_BeginModal
/// already requires, since the drawn fallback is exactly that modal.
/// @param id Identity; distinguishes this dialog from any other open at once.
/// @param title Drawn as the dialog's header.
/// @param body The message.
/// @return Pending while open; Accepted exactly once, the frame it closes.
UiDialogResult Ui_MessageDialog(const char* id, const char* title, const char* body);

/// As Ui_MessageDialog, with an accept and a cancel action.
/// @return Accepted or Cancelled exactly once, the frame one is chosen.
UiDialogResult Ui_ConfirmDialog(const char* id, const char* title, const char* body);

/// As Ui_ConfirmDialog, editing a caller-owned string instead of showing a
/// fixed message. `buffer` holds the value shown on open; on the frame this
/// returns Accepted it holds what the player entered; on Cancelled it is left
/// exactly as it was, so the caller never keeps a second copy to restore.
/// @param buffer The text, edited in place; always NUL-terminated.
/// @param size Capacity of buffer, including the terminator; at most
///        UI_TEXT_INPUT_MAX is ever held open for editing.
UiDialogResult Ui_TextDialog(const char* id, const char* title, char* buffer, size_t size);

/// A row that edits a caller-owned string in place, like any other value
/// widget: click or Accept starts editing, Accept commits, Back cancels and
/// restores what was there before. Unlike Ui_TextDialog this is not a modal --
/// on a platform with a character channel it edits inline, in the row, with no
/// dialog at all.
/// @param label The field's name, and its identity.
/// @param buffer The text, edited in place; always NUL-terminated.
/// @param size Capacity of buffer, including the terminator; at most
///        UI_TEXT_INPUT_MAX is ever held open for editing.
/// @return True on the frame the buffer's content changed.
bool Ui_TextInput(const char* label, char* buffer, size_t size);

// ---------------------------------------------------------------------------
// Budgets
// ---------------------------------------------------------------------------

/// @return How many focusable widgets were registered this frame.
uint32_t Ui_FocusablesUsed();

/// @return How many focusable widgets fit on this platform.
uint32_t Ui_FocusableBudget();

/// @return How many widgets currently remember something between frames.
uint32_t Ui_StatesUsed();

/// @return How many may on this platform.
uint32_t Ui_StateBudget();

/// @return The overlay quads used so far this frame.
uint32_t Ui_OverlayQuadsUsed();

/// @return The per-frame overlay quad budget on this platform.
uint32_t Ui_OverlayQuadBudget();

/// @return The deepest the clip stack has been this frame.
uint32_t Ui_ClipDepthUsed();

/// @return How deeply containers may nest on this platform.
uint32_t Ui_ClipDepthBudget();

/// @return The quads used so far this frame.
uint32_t Ui_QuadsUsed();

/// @return The per-frame quad budget on this platform.
uint32_t Ui_QuadBudget();

/// Check a layout against the operative quad ceiling before committing to it
/// -- pairs with Ui_MeasureTextQuads, so a caller can measure a string and
/// decide whether to draw it in full or fall back to something shorter.
/// @param quads How many more quads the caller is about to draw.
/// @return Whether they would fit: inside an open Ui_BeginBudget scope,
///         against what that scope has left; otherwise against the frame's
///         own quad budget.
bool Ui_WouldFit(int quads);

/// Cap what the widgets between this and Ui_EndBudget may add to the frame's
/// quad count, so one runaway block -- a list with no length limit of its
/// own -- cannot blank the rest of the screen. The excess is dropped and
/// reported once, naming the container. Does not apply to the overlay layer,
/// which already has its own separate, protected budget. At most one scope
/// is open at a time; a nested call is refused.
/// @param quads The most this scope may add.
void Ui_BeginBudget(int quads);

void Ui_EndBudget();

/// @return How many quads the most recently closed Ui_BeginBudget scope
///         actually added -- which container is eating the budget, observable
///         instead of guessed.
uint32_t Ui_ContainerQuadsUsed();

/// @return How many draw-call runs this frame's quads would coalesce into: a
///         run opens at the first quad and at every quad after whose texture
///         differs from the one before it. A solid fill samples no texture
///         (see the *A solid fill never samples a font atlas* contract), so a
///         row alternating solid and glyph content opens one run per
///         alternation -- this is what C1 made worth watching.
uint32_t Ui_RunsUsed();

/// @return The run ceiling the desktop and Vita renderers enforce. PS2's
///         GIFTAG path has no equivalent limit of its own, so there this is a
///         guide rather than an enforced ceiling.
uint32_t Ui_RunBudget();
