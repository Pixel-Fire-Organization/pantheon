#pragma once

#include <cstdint>

// ---------------------------------------------------------------------------
// Enum-class keys for every generic Platform accessor.
//
// The Platform interface never exposes a stringly-typed or index-typed getter:
// constants, capabilities and input devices are all addressed by a scoped enum
// so a typo is a compile error and the valid set is discoverable from one file.
//
// Every enum carries an explicit underlying type (ABI + size control) and a
// trailing `Count` member used to size lookup tables inside implementations.
// ---------------------------------------------------------------------------

// --- Platform identity ------------------------------------------------------
// Concrete, selectable platforms only. There is deliberately no bare `Ps2` and no
// bare `Vita`: both family bases are abstract and cannot be instantiated, so a
// build always resolves to one variant.
enum class PlatformId : uint8_t
{
    Unknown = 0,
    Ps2Pal,
    Ps2Ntsc,
    Win32,
    Vita,
    VitaTv,
    Psp,
    Nx,

    Count
};

// --- Renderer identity ------------------------------------------------------
// Ps2Gl, VitaGl and OpenGl are UNRELATED backends despite the shared naming
// lineage. They share no code. OpenGl names one interface with two separate
// implementations, one per platform that hosts it.
enum class RendererId : uint8_t
{
    Unknown = 0,
    Null, // headless; every platform supports it
    Ps2Gl, // PS2 only - ps2gl library
    GifTag, // PS2 only - direct GS packets via packet2/draw
    OpenGl, // desktop OpenGL - Win32 over WGL, nx over Mesa/EGL; separate implementations
    WebGpu, // desktop only - wgpu-native
    Gxm, // Vita only - sceGxm packets, shaders compiled at build time
    VitaGl, // Vita only - vitaGL, a fixed-function subset over sceGxm
    Gu, // PSP only - sceGu display lists against the graphics engine
    PspGl, // PSP only - pspgl, a fixed-function subset over the same hardware
    Deko3d, // nx only - deko3d command lists, shaders compiled at build time

    Count
};

// --- Runtime-queryable platform constants -----------------------------------
// Values shared engine code needs but that vary per platform. Compile-time
// values (anything sizing a static array) live in the selected platform's
// Constants.h instead, reached via "PlatformConstants.h".
//
// All values are uint32_t. Ratios that would otherwise need a float are split
// into an X/Y pair (DisplayAspectX/Y) and fixed-point values are scaled by a
// documented factor, so the table stays one type.
enum class PlatformConstant : uint16_t
{
    // Display
    ScreenWidth = 0,
    ScreenHeight,
    DisplayAspectX, // e.g. 4 - projection uses DisplayAspectX / DisplayAspectY
    DisplayAspectY, // e.g. 3 - NOT the framebuffer ratio (PS2 pixels are non-square)
    TargetFrameMicros, // 20000 (50Hz PAL) / 16667 (60Hz NTSC) / 16667 (desktop vsync)

    // Memory map - the platform owns its own budget and enforces it
    MemoryTotalBudget,
    MemoryArenaConfigSize,
    MemoryArenaConfigSlots,
    MemoryArenaLevelDataSize,
    MemoryArenaLevelDataSlots,
    MemoryArenaRendererSize,
    MemoryArenaRendererSlots,
    MemoryArenaSlotAlignment,
    MemoryPoolMainSize,
    MemoryPoolChunkSize,

    // Texture budget - bytes, not GS pages, so desktop platforms can answer too
    TextureBudgetBytes, // total, across every resident texture
    MaxTextureBytes, // cap for any single texture
    MaxTextureWidth,
    MaxTextureHeight,

    // Input
    MaxGamepadPorts,

    // Which controller glyph family a button prompt should draw. Answers as a
    // ButtonIconFamily value; see its declaration below. A platform fact, not a
    // theme choice -- a theme has no way to know what hardware it is running on.
    ButtonIconFamily,

    Count
};

/// Which controller glyph family a button prompt should draw. What a platform
/// answers for PlatformConstant::ButtonIconFamily, cast from the GetConstant
/// result.
enum class UiButtonIconFamily : uint8_t
{
    PlayStation = 0,
    Xbox,
    Keyboard,
    Nintendo,

    Count
};

// --- Optional platform features ---------------------------------------------
// Gameplay and engine code branch on capability, never on platform identity.
enum class PlatformCapability : uint8_t
{
    Gamepad = 0,
    Keyboard,
    Mouse,
    AnalogTriggers,
    ResizableWindow,
    AsyncIo,
    FileWrite,
    Touch,

    // Whether Platform::Dialog_Open answers with a host-owned system dialog for
    // every DialogKind, message boxes and text input alike, rather than the
    // interface's own drawn fallback.
    SystemDialog,

    // Whether Platform::Keyboard_PopCharacters ever returns anything: a direct
    // typing channel, as opposed to a system dialog handling text entry on the
    // platform's behalf. A platform may have neither, in which case the
    // interface's own on-screen keyboard is what draws.
    TextCharacters,

    Count
};

enum class TouchSurface : uint8_t
{
    Front = 0,
    Rear,

    Count
};

// --- Gamepad ----------------------------------------------------------------
// Values match the PS2 pad button mask so the PS2 backend needs no translation
// table. Other platforms map their native codes onto these.
enum class GamepadButton : uint16_t
{
    Unknown = 0, // never a valid query

    Select = 0x0001,
    L3 = 0x0002,
    R3 = 0x0004,
    Start = 0x0008,

    DPadUp = 0x0010,
    DPadRight = 0x0020,
    DPadDown = 0x0040,
    DPadLeft = 0x0080,

    // Shoulder masks come from ps2sdk's libpad.h. NOTE: the legacy
    // EngineInput.h GamePadButton enum has all four of these wrong (it reads
    // L1=0x0800 L2=0x0400 R1=0x0200 R2=0x0100), so L1/R1 and L2/R2 are
    // transposed there. These are the hardware-correct values.
    L2 = 0x0100,
    R2 = 0x0200,
    L1 = 0x0400,
    R1 = 0x0800,

    Triangle = 0x1000,
    Circle = 0x2000,
    Cross = 0x4000,
    Square = 0x8000,
};

enum class GamepadStick : uint8_t
{
    Left = 0,
    Right,

    Count
};

enum class GamepadTrigger : uint8_t
{
    Left = 0,
    Right,

    Count
};

// --- Debug chords -----------------------------------------------------------
// Held button combinations that reach the debug tooling. The engine names the
// intent; the platform names the buttons, because not every pad has the same
// ones. See docs/subsystems/DEBUG.md.
enum class DebugChord : uint8_t
{
    PerfSnapshot,
    OverlayToggle,
    DebugMenu,

    Count
};

// --- Keyboard ---------------------------------------------------------------
// A desktop-sized set. PS2 reports PlatformCapability::Keyboard as false and its
// Keyboard_* queries always return false, so game code compiles unchanged.
enum class KeyboardKey : uint16_t
{
    Unknown = 0,

    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,

    Num0,
    Num1,
    Num2,
    Num3,
    Num4,
    Num5,
    Num6,
    Num7,
    Num8,
    Num9,

    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,

    Left,
    Right,
    Up,
    Down,

    Space,
    Enter,
    Escape,
    Tab,
    Backspace,
    Delete,
    Insert,
    Home,
    End,
    PageUp,
    PageDown,

    LeftShift,
    RightShift,
    LeftControl,
    RightControl,
    LeftAlt,
    RightAlt,

    Minus,
    Equal,
    LeftBracket,
    RightBracket,
    Semicolon,
    Apostrophe,
    Comma,
    Period,
    Slash,
    Backslash,
    Grave,

    Count
};

enum class MouseButton : uint8_t
{
    Left = 0,
    Right,
    Middle,
    Extra1,
    Extra2,

    Count
};

// A mouse's positional channel, addressed the same way a gamepad's sticks and
// triggers are: by a small dense enum rather than a bare index. One member for
// now (the frame-to-frame delta); wheel is a scalar today and has no axis form.
enum class MouseAxis : uint8_t
{
    Delta = 0,

    Count
};

// --- Logging ----------------------------------------------------------------
// Severity handed to Platform::ConsoleWrite. The platform decides the sink
// (ps2client stdout, OutputDebugString, a console) and any prefixing.
enum class LogLevel : uint8_t
{
    Debug = 0,
    Info,
    Warning,
    Error,

    Count
};

// --- Filesystem -------------------------------------------------------------
enum class FileMode : uint8_t
{
    Read = 0,
    Write,

    Count
};

// Names for diagnostics. A platform that fails to answer a key panics naming it,
// which is the difference between an actionable message and a number.
const char* Platform_ConstantName(PlatformConstant key);
const char* Platform_CapabilityName(PlatformCapability key);

/// @param button A single-bit button value.
/// @return Its short name, or "?" when the value is not one button.
const char* Platform_GamepadButtonName(GamepadButton button);
