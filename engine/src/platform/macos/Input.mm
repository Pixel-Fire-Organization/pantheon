#import <GameController/GameController.h>

#include <cmath>
#include <cstring>

#include "Macros.h"
#include "Platform.h"
#include "core/EngineDebug.h"

namespace
{

    const float kTriggerPressThreshold = 0.5f;

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

    float ApplyDeadzone(float v)
    {
        if (v > 1.0f)
            v = 1.0f;
        if (v < -1.0f)
            v = -1.0f;
        return (fabsf(v) < INPUT_ANALOG_DEADZONE) ? 0.0f : v;
    }

    bool s_DiscoveryStarted = false;

} // namespace

void MacosPlatform::ApplyKeyboardPadMap()
{
    PadSnapshot& pad = m_pads[0];

    uint16_t buttons = pad.buttons;
    for (size_t i = 0; i < sizeof(kKeyboardPad) / sizeof(kKeyboardPad[0]); ++i)
    {
        if (m_keys.keys[static_cast<uint16_t>(kKeyboardPad[i].key)])
            buttons |= static_cast<uint16_t>(kKeyboardPad[i].button);
    }
    pad.buttons = buttons;

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

    if (kx != 0.0f || ky != 0.0f)
        stick = Vector2{kx, ky};
    pad.stick[static_cast<uint8_t>(GamepadStick::Left)] = stick;

    pad.connected = true;
}

void MacosPlatform::PollControllers()
{
    @autoreleasepool
    {
        if (!s_DiscoveryStarted)
        {
            s_DiscoveryStarted = true;
            [GCController startWirelessControllerDiscoveryWithCompletionHandler:^{
            }];
        }

        NSArray<GCController*>* controllers = [GCController controllers];
        for (uint8_t port = 0; port < MAX_GAME_PAD_PORTS; ++port)
        {
            PadSnapshot& pad = m_pads[port];
            GCExtendedGamepad* gamepad = (port < [controllers count]) ? [[controllers objectAtIndex:port] extendedGamepad] : nil;
            if (!gamepad)
            {
                memset(&pad, 0, sizeof(pad));
                continue;
            }

            pad.connected = true;

            uint16_t buttons = 0;
            if ([[gamepad buttonA] isPressed])
                buttons |= static_cast<uint16_t>(GamepadButton::Cross);
            if ([[gamepad buttonB] isPressed])
                buttons |= static_cast<uint16_t>(GamepadButton::Circle);
            if ([[gamepad buttonX] isPressed])
                buttons |= static_cast<uint16_t>(GamepadButton::Square);
            if ([[gamepad buttonY] isPressed])
                buttons |= static_cast<uint16_t>(GamepadButton::Triangle);
            if ([[[gamepad dpad] up] isPressed])
                buttons |= static_cast<uint16_t>(GamepadButton::DPadUp);
            if ([[[gamepad dpad] down] isPressed])
                buttons |= static_cast<uint16_t>(GamepadButton::DPadDown);
            if ([[[gamepad dpad] left] isPressed])
                buttons |= static_cast<uint16_t>(GamepadButton::DPadLeft);
            if ([[[gamepad dpad] right] isPressed])
                buttons |= static_cast<uint16_t>(GamepadButton::DPadRight);
            if ([[gamepad leftShoulder] isPressed])
                buttons |= static_cast<uint16_t>(GamepadButton::L1);
            if ([[gamepad rightShoulder] isPressed])
                buttons |= static_cast<uint16_t>(GamepadButton::R1);
            if ([[gamepad buttonMenu] isPressed])
                buttons |= static_cast<uint16_t>(GamepadButton::Start);
            if ([[gamepad buttonOptions] isPressed])
                buttons |= static_cast<uint16_t>(GamepadButton::Select);
            if ([[gamepad leftThumbstickButton] isPressed])
                buttons |= static_cast<uint16_t>(GamepadButton::L3);
            if ([[gamepad rightThumbstickButton] isPressed])
                buttons |= static_cast<uint16_t>(GamepadButton::R3);

            const float leftTrigger = [[gamepad leftTrigger] value];
            const float rightTrigger = [[gamepad rightTrigger] value];
            if (leftTrigger > kTriggerPressThreshold)
                buttons |= static_cast<uint16_t>(GamepadButton::L2);
            if (rightTrigger > kTriggerPressThreshold)
                buttons |= static_cast<uint16_t>(GamepadButton::R2);

            pad.buttons = buttons;
            pad.stick[static_cast<uint8_t>(GamepadStick::Left)] = Vector2{ApplyDeadzone([[[gamepad leftThumbstick] xAxis] value]), ApplyDeadzone(-[[[gamepad leftThumbstick] yAxis] value])};
            pad.stick[static_cast<uint8_t>(GamepadStick::Right)] = Vector2{ApplyDeadzone([[[gamepad rightThumbstick] xAxis] value]), ApplyDeadzone(-[[[gamepad rightThumbstick] yAxis] value])};
            pad.trigger[static_cast<uint8_t>(GamepadTrigger::Left)] = leftTrigger;
            pad.trigger[static_cast<uint8_t>(GamepadTrigger::Right)] = rightTrigger;
        }
    }
}

void MacosPlatform::PollInput()
{
    PumpMessages();

    memcpy(m_padsPrev, m_pads, sizeof(m_pads));
    m_keysPrev = m_keys;
    m_mousePrev = m_mouse;

    PollControllers();

    for (uint16_t k = 0; k < static_cast<uint16_t>(KeyboardKey::Count); ++k)
        m_keys.keys[k] = m_window.keyDown[k] || m_window.keyHit[k];
    memset(m_window.keyHit, 0, sizeof(m_window.keyHit));

    for (uint8_t b = 0; b < static_cast<uint8_t>(MouseButton::Count); ++b)
        m_mouse.buttons[b] = (b < 8) && (m_window.mouseDown[b] || m_window.mouseHit[b]);
    memset(m_window.mouseHit, 0, sizeof(m_window.mouseHit));

    m_mouse.position = Vector2{m_window.mouseX, m_window.mouseY};

    m_mouse.wheel = m_window.wheelDelta;
    m_window.wheelDelta = 0.0f;

    if (m_keyboardPadMap)
        ApplyKeyboardPadMap();

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

uint16_t MacosPlatform::GetDebugChord(DebugChord chord) const
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

bool MacosPlatform::Gamepad_IsConnected(uint8_t port) const { return (port < MAX_GAME_PAD_PORTS) && m_pads[port].connected; }

bool MacosPlatform::Gamepad_IsButtonDown(uint8_t port, GamepadButton button) const
{
    if (port >= MAX_GAME_PAD_PORTS)
        return false;
    const uint16_t mask = static_cast<uint16_t>(button);
    return (m_pads[port].buttons & mask) == mask;
}

bool MacosPlatform::Gamepad_WasButtonPressed(uint8_t port, GamepadButton button) const
{
    if (port >= MAX_GAME_PAD_PORTS)
        return false;
    const uint16_t mask = static_cast<uint16_t>(button);
    return ((m_pads[port].buttons & mask) == mask) && ((m_padsPrev[port].buttons & mask) != mask);
}

bool MacosPlatform::Gamepad_WasButtonReleased(uint8_t port, GamepadButton button) const
{
    if (port >= MAX_GAME_PAD_PORTS)
        return false;
    const uint16_t mask = static_cast<uint16_t>(button);
    return ((m_pads[port].buttons & mask) != mask) && ((m_padsPrev[port].buttons & mask) == mask);
}

Vector2 MacosPlatform::Gamepad_GetStick(uint8_t port, GamepadStick stick) const
{
    if (port >= MAX_GAME_PAD_PORTS || stick >= GamepadStick::Count)
        return Vector2{0.0f, 0.0f};
    return m_pads[port].stick[static_cast<uint8_t>(stick)];
}

float MacosPlatform::Gamepad_GetTrigger(uint8_t port, GamepadTrigger trigger) const
{
    if (port >= MAX_GAME_PAD_PORTS || trigger >= GamepadTrigger::Count)
        return 0.0f;
    return m_pads[port].trigger[static_cast<uint8_t>(trigger)];
}

bool MacosPlatform::Keyboard_IsKeyDown(KeyboardKey key) const
{
    const uint16_t k = static_cast<uint16_t>(key);
    return (k < static_cast<uint16_t>(KeyboardKey::Count)) && m_keys.keys[k];
}

bool MacosPlatform::Keyboard_WasKeyPressed(KeyboardKey key) const
{
    const uint16_t k = static_cast<uint16_t>(key);
    if (k >= static_cast<uint16_t>(KeyboardKey::Count))
        return false;
    return m_keys.keys[k] && !m_keysPrev.keys[k];
}

bool MacosPlatform::Keyboard_WasKeyReleased(KeyboardKey key) const
{
    const uint16_t k = static_cast<uint16_t>(key);
    if (k >= static_cast<uint16_t>(KeyboardKey::Count))
        return false;
    return !m_keys.keys[k] && m_keysPrev.keys[k];
}

bool MacosPlatform::Mouse_IsButtonDown(MouseButton button) const
{
    const uint8_t b = static_cast<uint8_t>(button);
    return (b < static_cast<uint8_t>(MouseButton::Count)) && m_mouse.buttons[b];
}

bool MacosPlatform::Mouse_WasButtonPressed(MouseButton button) const
{
    const uint8_t b = static_cast<uint8_t>(button);
    if (b >= static_cast<uint8_t>(MouseButton::Count))
        return false;
    return m_mouse.buttons[b] && !m_mousePrev.buttons[b];
}

Vector2 MacosPlatform::Mouse_GetPosition() const { return m_mouse.position; }

Vector2 MacosPlatform::Mouse_GetDelta() const { return Vector2{m_mouse.position.x - m_mousePrev.position.x, m_mouse.position.y - m_mousePrev.position.y}; }

float MacosPlatform::Mouse_GetWheelDelta() const { return m_mouse.wheel; }

uint8_t MacosPlatform::Touch_GetContactCount(TouchSurface surface) const
{
    UNUSED_VAR(surface);
    return 0;
}

bool MacosPlatform::Touch_GetContact(TouchSurface surface, uint8_t index, TouchContact* outContact) const
{
    UNUSED_VAR(surface);
    UNUSED_VAR(index);
    UNUSED_VAR(outContact);
    return false;
}

uint32_t MacosPlatform::Keyboard_PopCharacters(char* outBuffer, uint32_t bufferSize)
{
    const uint32_t count = (m_window.charCount < bufferSize) ? m_window.charCount : bufferSize;
    for (uint32_t i = 0; i < count; ++i)
        outBuffer[i] = m_window.charBuffer[i];

    const uint32_t remaining = m_window.charCount - count;
    for (uint32_t i = 0; i < remaining; ++i)
        m_window.charBuffer[i] = m_window.charBuffer[count + i];
    m_window.charCount = remaining;

    return count;
}
