#include <cmath>
#include <cstring>

#include "Macros.h"
#include "Platform.h"
#include "core/EngineDebug.h"

#include <windows.h>
#include <xinput.h>

namespace
{

    // XInput button mask -> engine GamepadButton mask. The engine speaks the PS2
    // layout, so the face buttons map by position: A->Cross, B->Circle,
    // X->Square, Y->Triangle.
    struct PadButtonMap
    {
        WORD xinput;
        GamepadButton engine;
    };

    const PadButtonMap kPadButtons[] = {
        {XINPUT_GAMEPAD_A, GamepadButton::Cross},
        {XINPUT_GAMEPAD_B, GamepadButton::Circle},
        {XINPUT_GAMEPAD_X, GamepadButton::Square},
        {XINPUT_GAMEPAD_Y, GamepadButton::Triangle},
        {XINPUT_GAMEPAD_DPAD_UP, GamepadButton::DPadUp},
        {XINPUT_GAMEPAD_DPAD_DOWN, GamepadButton::DPadDown},
        {XINPUT_GAMEPAD_DPAD_LEFT, GamepadButton::DPadLeft},
        {XINPUT_GAMEPAD_DPAD_RIGHT, GamepadButton::DPadRight},
        {XINPUT_GAMEPAD_START, GamepadButton::Start},
        {XINPUT_GAMEPAD_BACK, GamepadButton::Select},
        {XINPUT_GAMEPAD_LEFT_THUMB, GamepadButton::L3},
        {XINPUT_GAMEPAD_RIGHT_THUMB, GamepadButton::R3},
        {XINPUT_GAMEPAD_LEFT_SHOULDER, GamepadButton::L1},
        {XINPUT_GAMEPAD_RIGHT_SHOULDER, GamepadButton::R1},
    };

    // KeyboardKey -> Windows virtual-key code. Only the keys the engine names.
    int VirtualKeyFor(KeyboardKey key)
    {
        const uint16_t k = static_cast<uint16_t>(key);

        if (key >= KeyboardKey::A && key <= KeyboardKey::Z)
            return 'A' + (k - static_cast<uint16_t>(KeyboardKey::A));
        if (key >= KeyboardKey::Num0 && key <= KeyboardKey::Num9)
            return '0' + (k - static_cast<uint16_t>(KeyboardKey::Num0));
        if (key >= KeyboardKey::F1 && key <= KeyboardKey::F12)
            return VK_F1 + (k - static_cast<uint16_t>(KeyboardKey::F1));

        switch (key)
        {
        case KeyboardKey::Left:
            return VK_LEFT;
        case KeyboardKey::Right:
            return VK_RIGHT;
        case KeyboardKey::Up:
            return VK_UP;
        case KeyboardKey::Down:
            return VK_DOWN;
        case KeyboardKey::Space:
            return VK_SPACE;
        case KeyboardKey::Enter:
            return VK_RETURN;
        case KeyboardKey::Escape:
            return VK_ESCAPE;
        case KeyboardKey::Tab:
            return VK_TAB;
        case KeyboardKey::Backspace:
            return VK_BACK;
        case KeyboardKey::Delete:
            return VK_DELETE;
        case KeyboardKey::Insert:
            return VK_INSERT;
        case KeyboardKey::Home:
            return VK_HOME;
        case KeyboardKey::End:
            return VK_END;
        case KeyboardKey::PageUp:
            return VK_PRIOR;
        case KeyboardKey::PageDown:
            return VK_NEXT;
        case KeyboardKey::LeftShift:
            return VK_LSHIFT;
        case KeyboardKey::RightShift:
            return VK_RSHIFT;
        case KeyboardKey::LeftControl:
            return VK_LCONTROL;
        case KeyboardKey::RightControl:
            return VK_RCONTROL;
        case KeyboardKey::LeftAlt:
            return VK_LMENU;
        case KeyboardKey::RightAlt:
            return VK_RMENU;
        case KeyboardKey::Minus:
            return VK_OEM_MINUS;
        case KeyboardKey::Equal:
            return VK_OEM_PLUS;
        case KeyboardKey::LeftBracket:
            return VK_OEM_4;
        case KeyboardKey::RightBracket:
            return VK_OEM_6;
        case KeyboardKey::Semicolon:
            return VK_OEM_1;
        case KeyboardKey::Apostrophe:
            return VK_OEM_7;
        case KeyboardKey::Comma:
            return VK_OEM_COMMA;
        case KeyboardKey::Period:
            return VK_OEM_PERIOD;
        case KeyboardKey::Slash:
            return VK_OEM_2;
        case KeyboardKey::Backslash:
            return VK_OEM_5;
        case KeyboardKey::Grave:
            return VK_OEM_3;
        default:
            return 0;
        }
    }

    // XInput thumbsticks are signed shorts. Normalise to [-1,+1] and apply the
    // same deadzone rule the PS2 backend uses, so gameplay feels identical.
    float NormalizeStick(SHORT raw)
    {
        float v = static_cast<float>(raw) / 32767.0f;
        if (v > 1.0f)
            v = 1.0f;
        if (v < -1.0f)
            v = -1.0f;
        return (fabsf(v) < INPUT_ANALOG_DEADZONE) ? 0.0f : v;
    }

    // The default desktop binding. Chosen so the existing testbed scenes are
    // playable without documentation: arrows and WASD both move, Space/Enter
    // confirm, Escape/Backspace cancel.
    struct KeyPadBind
    {
        KeyboardKey key;
        GamepadButton button;
    };

    const KeyPadBind kKeyboardPad[] = {
        {KeyboardKey::Up, GamepadButton::DPadUp},     {KeyboardKey::W, GamepadButton::DPadUp},    {KeyboardKey::Down, GamepadButton::DPadDown},    {KeyboardKey::S, GamepadButton::DPadDown},
        {KeyboardKey::Left, GamepadButton::DPadLeft}, {KeyboardKey::A, GamepadButton::DPadLeft},  {KeyboardKey::Right, GamepadButton::DPadRight},  {KeyboardKey::D, GamepadButton::DPadRight},

        {KeyboardKey::Space, GamepadButton::Cross},   {KeyboardKey::Enter, GamepadButton::Cross}, {KeyboardKey::Backspace, GamepadButton::Circle}, {KeyboardKey::E, GamepadButton::Square},
        {KeyboardKey::Q, GamepadButton::Triangle},

        {KeyboardKey::Num1, GamepadButton::L1},       {KeyboardKey::Num2, GamepadButton::R1},     {KeyboardKey::Num3, GamepadButton::L2},          {KeyboardKey::Num4, GamepadButton::R2},
        {KeyboardKey::Num5, GamepadButton::L3},       {KeyboardKey::Num6, GamepadButton::R3},     {KeyboardKey::Tab, GamepadButton::Select},       {KeyboardKey::Escape, GamepadButton::Start},
    };

} // namespace

void Win32Platform::ApplyKeyboardPadMap()
{
    // Folded onto port 0 and OR-ed in, so a keyboard and a real controller can be
    // used at the same time rather than one shadowing the other.
    PadSnapshot& pad = m_pads[0];

    uint16_t buttons = pad.buttons;
    bool anyKey = false;
    for (size_t i = 0; i < sizeof(kKeyboardPad) / sizeof(kKeyboardPad[0]); ++i)
    {
        if (!m_keys.keys[static_cast<uint16_t>(kKeyboardPad[i].key)])
            continue;
        buttons |= static_cast<uint16_t>(kKeyboardPad[i].button);
        anyKey = true;
    }
    pad.buttons = buttons;

    // WASD and the arrows also drive the left stick, so analog movement code
    // (the swarm scene) responds to a keyboard too.
    Vector2 stick = pad.stick[static_cast<uint8_t>(GamepadStick::Left)];
    float kx = 0.0f, ky = 0.0f;
    if (m_keys.keys[static_cast<uint16_t>(KeyboardKey::A)] || m_keys.keys[static_cast<uint16_t>(KeyboardKey::Left)])
        kx -= 1.0f;
    if (m_keys.keys[static_cast<uint16_t>(KeyboardKey::D)] || m_keys.keys[static_cast<uint16_t>(KeyboardKey::Right)])
        kx += 1.0f;
    if (m_keys.keys[static_cast<uint16_t>(KeyboardKey::W)] || m_keys.keys[static_cast<uint16_t>(KeyboardKey::Up)])
        ky -= 1.0f;
    if (m_keys.keys[static_cast<uint16_t>(KeyboardKey::S)] || m_keys.keys[static_cast<uint16_t>(KeyboardKey::Down)])
        ky += 1.0f;

    // Only override the physical stick when a key is actually held, so a real
    // controller is not zeroed by an idle keyboard.
    if (kx != 0.0f || ky != 0.0f)
        stick = Vector2{kx, ky};
    pad.stick[static_cast<uint8_t>(GamepadStick::Left)] = stick;

    // Report port 0 as present: without this IsGamePadInitialized(0) is false and
    // game code may skip reading input entirely.
    if (anyKey || kx != 0.0f || ky != 0.0f || pad.connected)
        pad.connected = true;
    else
        pad.connected = true; // a keyboard is always available on this platform
}

void Win32Platform::PollInput()
{
    // The message queue carries resize, close and wheel events; draining it is
    // part of reading input rather than something a caller must remember.
    PumpMessages();

    memcpy(m_padsPrev, m_pads, sizeof(m_pads));
    m_keysPrev = m_keys;
    m_mousePrev = m_mouse;

    // --- Gamepads (XInput) --------------------------------------------------
    for (uint8_t port = 0; port < MAX_GAME_PAD_PORTS; ++port)
    {
        XINPUT_STATE state;
        memset(&state, 0, sizeof(state));

        if (XInputGetState(port, &state) != ERROR_SUCCESS)
        {
            memset(&m_pads[port], 0, sizeof(m_pads[port]));
            continue;
        }

        const XINPUT_GAMEPAD& pad = state.Gamepad;
        m_pads[port].connected = true;

        uint16_t buttons = 0;
        for (size_t i = 0; i < sizeof(kPadButtons) / sizeof(kPadButtons[0]); ++i)
        {
            if (pad.wButtons & kPadButtons[i].xinput)
                buttons |= static_cast<uint16_t>(kPadButtons[i].engine);
        }

        // XInput exposes the triggers as analog only. Report them as pressed
        // once past the deadzone so L2/R2 behave like the digital PS2 buttons.
        if (pad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
            buttons |= static_cast<uint16_t>(GamepadButton::L2);
        if (pad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
            buttons |= static_cast<uint16_t>(GamepadButton::R2);

        m_pads[port].buttons = buttons;
        m_pads[port].stick[static_cast<uint8_t>(GamepadStick::Left)] = Vector2{NormalizeStick(pad.sThumbLX), NormalizeStick(-pad.sThumbLY)};
        m_pads[port].stick[static_cast<uint8_t>(GamepadStick::Right)] = Vector2{NormalizeStick(pad.sThumbRX), NormalizeStick(-pad.sThumbRY)};
        m_pads[port].trigger[static_cast<uint8_t>(GamepadTrigger::Left)] = static_cast<float>(pad.bLeftTrigger) / 255.0f;
        m_pads[port].trigger[static_cast<uint8_t>(GamepadTrigger::Right)] = static_cast<float>(pad.bRightTrigger) / 255.0f;
    }

    // --- Keyboard -----------------------------------------------------------
    // From the message queue, not GetAsyncKeyState: a key tapped and released
    // inside one frame must still register, and input must stop when the window
    // loses focus.
    for (uint16_t k = 0; k < static_cast<uint16_t>(KeyboardKey::Count); ++k)
    {
        const int vk = VirtualKeyFor(static_cast<KeyboardKey>(k));
        if (vk <= 0 || vk >= 256)
        {
            m_keys.keys[k] = false;
            continue;
        }
        m_keys.keys[k] = m_window.keyDown[vk] || m_window.keyHit[vk];
    }
    memset(m_window.keyHit, 0, sizeof(m_window.keyHit));

    // --- Mouse --------------------------------------------------------------
    for (uint8_t b = 0; b < static_cast<uint8_t>(MouseButton::Count); ++b)
    {
        m_mouse.buttons[b] = (b < 8) && (m_window.mouseDown[b] || m_window.mouseHit[b]);
    }
    memset(m_window.mouseHit, 0, sizeof(m_window.mouseHit));

    // Client-space, so a caller gets coordinates that match the framebuffer
    // rather than the desktop.
    POINT cursor;
    if (GetCursorPos(&cursor))
    {
        if (m_window.hwnd)
            ScreenToClient(static_cast<HWND>(m_window.hwnd), &cursor);
        m_mouse.position = Vector2{static_cast<float>(cursor.x), static_cast<float>(cursor.y)};
    }

    // Accumulated by WM_MOUSEWHEEL since the last poll, then consumed.
    m_mouse.wheel = m_window.wheelDelta;
    m_window.wheelDelta = 0.0f;

    // Last, so it sees this frame's keys and can OR onto the pad snapshot.
    if (m_keyboardPadMap)
        ApplyKeyboardPadMap();

    // --log-input: report presses as they land, so a binding that never
    // reaches game code can be traced to the key, the map, or the game.
    if (m_logInput)
    {
        for (uint16_t k = 0; k < static_cast<uint16_t>(KeyboardKey::Count); ++k)
        {
            if (m_keys.keys[k] && !m_keysPrev.keys[k])
                Engine_LogInfo("input: key %u down", static_cast<unsigned>(k));
        }
        if (m_pads[0].buttons != m_padsPrev[0].buttons)
            Engine_LogInfo("input: pad0 buttons 0x%04X (connected=%d)", m_pads[0].buttons, m_pads[0].connected ? 1 : 0);
    }
}

// --- Gamepad ----------------------------------------------------------------

uint16_t Win32Platform::GetDebugChord(DebugChord chord) const
{
    switch (chord)
    {
    case DebugChord::PerfSnapshot:
        return static_cast<uint16_t>(GamepadButton::L1) | static_cast<uint16_t>(GamepadButton::L2) | static_cast<uint16_t>(GamepadButton::R1) | static_cast<uint16_t>(GamepadButton::R2);
    case DebugChord::OverlayToggle:
        return static_cast<uint16_t>(GamepadButton::L1) | static_cast<uint16_t>(GamepadButton::L2) | static_cast<uint16_t>(GamepadButton::L3) | static_cast<uint16_t>(GamepadButton::R3);
    case DebugChord::DebugMenu:
        return static_cast<uint16_t>(GamepadButton::Select) | static_cast<uint16_t>(GamepadButton::Start);
    case DebugChord::Count:
        break;
    }
    return 0;
}

bool Win32Platform::Gamepad_IsConnected(uint8_t port) const { return (port < MAX_GAME_PAD_PORTS) && m_pads[port].connected; }

bool Win32Platform::Gamepad_IsButtonDown(uint8_t port, GamepadButton button) const
{
    if (port >= MAX_GAME_PAD_PORTS)
        return false;
    const uint16_t mask = static_cast<uint16_t>(button);
    return (m_pads[port].buttons & mask) == mask;
}

bool Win32Platform::Gamepad_WasButtonPressed(uint8_t port, GamepadButton button) const
{
    if (port >= MAX_GAME_PAD_PORTS)
        return false;
    const uint16_t mask = static_cast<uint16_t>(button);
    return ((m_pads[port].buttons & mask) == mask) && ((m_padsPrev[port].buttons & mask) != mask);
}

bool Win32Platform::Gamepad_WasButtonReleased(uint8_t port, GamepadButton button) const
{
    if (port >= MAX_GAME_PAD_PORTS)
        return false;
    const uint16_t mask = static_cast<uint16_t>(button);
    return ((m_pads[port].buttons & mask) != mask) && ((m_padsPrev[port].buttons & mask) == mask);
}

Vector2 Win32Platform::Gamepad_GetStick(uint8_t port, GamepadStick stick) const
{
    if (port >= MAX_GAME_PAD_PORTS || stick >= GamepadStick::Count)
        return Vector2{0.0f, 0.0f};
    return m_pads[port].stick[static_cast<uint8_t>(stick)];
}

float Win32Platform::Gamepad_GetTrigger(uint8_t port, GamepadTrigger trigger) const
{
    if (port >= MAX_GAME_PAD_PORTS || trigger >= GamepadTrigger::Count)
        return 0.0f;
    return m_pads[port].trigger[static_cast<uint8_t>(trigger)];
}

// --- Keyboard ---------------------------------------------------------------

bool Win32Platform::Keyboard_IsKeyDown(KeyboardKey key) const
{
    const uint16_t k = static_cast<uint16_t>(key);
    return (k < static_cast<uint16_t>(KeyboardKey::Count)) && m_keys.keys[k];
}

bool Win32Platform::Keyboard_WasKeyPressed(KeyboardKey key) const
{
    const uint16_t k = static_cast<uint16_t>(key);
    if (k >= static_cast<uint16_t>(KeyboardKey::Count))
        return false;
    return m_keys.keys[k] && !m_keysPrev.keys[k];
}

bool Win32Platform::Keyboard_WasKeyReleased(KeyboardKey key) const
{
    const uint16_t k = static_cast<uint16_t>(key);
    if (k >= static_cast<uint16_t>(KeyboardKey::Count))
        return false;
    return !m_keys.keys[k] && m_keysPrev.keys[k];
}

// --- Mouse ------------------------------------------------------------------

bool Win32Platform::Mouse_IsButtonDown(MouseButton button) const
{
    const uint8_t b = static_cast<uint8_t>(button);
    return (b < static_cast<uint8_t>(MouseButton::Count)) && m_mouse.buttons[b];
}

bool Win32Platform::Mouse_WasButtonPressed(MouseButton button) const
{
    const uint8_t b = static_cast<uint8_t>(button);
    if (b >= static_cast<uint8_t>(MouseButton::Count))
        return false;
    return m_mouse.buttons[b] && !m_mousePrev.buttons[b];
}

Vector2 Win32Platform::Mouse_GetPosition() const { return m_mouse.position; }

Vector2 Win32Platform::Mouse_GetDelta() const { return Vector2{m_mouse.position.x - m_mousePrev.position.x, m_mouse.position.y - m_mousePrev.position.y}; }

float Win32Platform::Mouse_GetWheelDelta() const { return m_mouse.wheel; }

uint8_t Win32Platform::Touch_GetContactCount(TouchSurface surface) const
{
    UNUSED_VAR(surface);
    return 0;
}

bool Win32Platform::Touch_GetContact(TouchSurface surface, uint8_t index, TouchContact* outContact) const
{
    UNUSED_VAR(surface);
    UNUSED_VAR(index);
    UNUSED_VAR(outContact);
    return false;
}

uint32_t Win32Platform::Keyboard_PopCharacters(char* outBuffer, uint32_t bufferSize)
{
    const uint32_t count = (m_window.charCount < bufferSize) ? m_window.charCount : bufferSize;
    for (uint32_t i = 0; i < count; ++i)
        outBuffer[i] = m_window.charBuffer[i];

    // Anything left over did not fit this poll; shift it down rather than
    // dropping it, so a caller that pops in small batches still sees every
    // character eventually instead of losing whatever arrived between polls.
    const uint32_t remaining = m_window.charCount - count;
    for (uint32_t i = 0; i < remaining; ++i)
        m_window.charBuffer[i] = m_window.charBuffer[count + i];
    m_window.charCount = remaining;

    return count;
}
