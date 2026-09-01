#include <cmath>
#include <cstring>

#include "core/EngineDebug.h"
#include "Macros.h"
#include "Platform.h"

extern "C" {
#include <input.h>
#include <loadfile.h>
}

namespace
{

    pad_t* s_Pads[MAX_GAME_PAD_PORTS] = {nullptr, nullptr};
    bool s_PadSystemReady = false;

    // Map a raw analog byte [0,255] to [-1,+1] and apply a symmetric deadzone, so
    // sticks that rest off-centre (common on real hardware) produce no movement.
    float NormalizeAxis(uint8_t raw)
    {
        float v = (static_cast<float>(raw) - INPUT_ANALOG_RAW_CENTER) / INPUT_ANALOG_RAW_SCALE;
        if (v > 1.0f)
            v = 1.0f;
        if (v < -1.0f)
            v = -1.0f;
        return (fabsf(v) < INPUT_ANALOG_DEADZONE) ? 0.0f : v;
    }

    bool EnsurePadSystem()
    {
        if (s_PadSystemReady)
            return true;

        int ret = SifLoadModule("rom0:SIO2MAN", 0, nullptr);
        if (ret < 0)
        {
            Engine_LogError("PS2 input: SIO2MAN failed to load (%d)", ret);
            return false;
        }

        ret = SifLoadModule("rom0:PADMAN", 0, nullptr);
        if (ret < 0)
        {
            Engine_LogError("PS2 input: PADMAN failed to load (%d)", ret);
            return false;
        }

        padInit(0);
        s_PadSystemReady = true;
        return true;
    }

} // namespace

void Ps2Platform::PollInput()
{
    memcpy(m_padsPrev, m_pads, sizeof(m_pads));

    for (uint8_t port = 0; port < MAX_GAME_PAD_PORTS; ++port)
    {
        if (!s_Pads[port])
        {
            // Open port 0 lazily on first poll; other ports only if present.
            if (port == 0 && EnsurePadSystem())
                s_Pads[port] = pad_open(port, 0, MODE_ANALOG, true);

            if (!s_Pads[port])
            {
                m_pads[port].connected = false;
                m_pads[port].buttons = 0;
                m_pads[port].stick[0] = Vector2{0.0f, 0.0f};
                m_pads[port].stick[1] = Vector2{0.0f, 0.0f};
                continue;
            }
        }

        pad_wait(s_Pads[port]);
        pad_get_buttons(s_Pads[port]);

        // libpad reports buttons active-LOW; invert once here so every consumer
        // above this line reasons in active-high terms.
        const struct padButtonStatus* data = s_Pads[port]->buttons;
        m_pads[port].connected = true;
        m_pads[port].buttons = static_cast<uint16_t>(~data->btns);
        m_pads[port].stick[static_cast<uint8_t>(GamepadStick::Left)] = Vector2{NormalizeAxis(data->ljoy_h), NormalizeAxis(data->ljoy_v)};
        m_pads[port].stick[static_cast<uint8_t>(GamepadStick::Right)] = Vector2{NormalizeAxis(data->rjoy_h), NormalizeAxis(data->rjoy_v)};
    }
}

void Ps2Platform::ShutdownInput()
{
    for (uint8_t port = 0; port < MAX_GAME_PAD_PORTS; ++port)
    {
        if (!s_Pads[port])
            continue;
        pad_close(s_Pads[port]);
        s_Pads[port] = nullptr;
    }
}

uint16_t Ps2Platform::GetDebugChord(DebugChord chord) const
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

bool Ps2Platform::Gamepad_IsConnected(uint8_t port) const { return (port < MAX_GAME_PAD_PORTS) && m_pads[port].connected; }

bool Ps2Platform::Gamepad_IsButtonDown(uint8_t port, GamepadButton button) const
{
    if (port >= MAX_GAME_PAD_PORTS)
        return false;
    const uint16_t mask = static_cast<uint16_t>(button);
    return (m_pads[port].buttons & mask) == mask;
}

bool Ps2Platform::Gamepad_WasButtonPressed(uint8_t port, GamepadButton button) const
{
    if (port >= MAX_GAME_PAD_PORTS)
        return false;
    const uint16_t mask = static_cast<uint16_t>(button);
    const bool now = (m_pads[port].buttons & mask) == mask;
    const bool before = (m_padsPrev[port].buttons & mask) == mask;
    return now && !before;
}

bool Ps2Platform::Gamepad_WasButtonReleased(uint8_t port, GamepadButton button) const
{
    if (port >= MAX_GAME_PAD_PORTS)
        return false;
    const uint16_t mask = static_cast<uint16_t>(button);
    const bool now = (m_pads[port].buttons & mask) == mask;
    const bool before = (m_padsPrev[port].buttons & mask) == mask;
    return !now && before;
}

Vector2 Ps2Platform::Gamepad_GetStick(uint8_t port, GamepadStick stick) const
{
    if (port >= MAX_GAME_PAD_PORTS || stick >= GamepadStick::Count)
        return Vector2{0.0f, 0.0f};
    return m_pads[port].stick[static_cast<uint8_t>(stick)];
}

float Ps2Platform::Gamepad_GetTrigger(uint8_t port, GamepadTrigger trigger) const
{
    // The DualShock 2 has pressure-sensitive L2/R2, but the engine does not read
    // pressure data, so report the digital state as 0.0 / 1.0 rather than
    // pretending to an analog range that is never populated.
    const GamepadButton button = (trigger == GamepadTrigger::Left) ? GamepadButton::L2 : GamepadButton::R2;
    return Gamepad_IsButtonDown(port, button) ? 1.0f : 0.0f;
}

// --- Absent devices ---------------------------------------------------------
// HasCapability reports Keyboard and Mouse as false; these keep game code that
// uses them compiling and returning nothing, rather than the platform faking a
// device it does not have.

bool Ps2Platform::Keyboard_IsKeyDown(KeyboardKey key) const
{
    UNUSED_VAR(key);
    return false;
}

bool Ps2Platform::Keyboard_WasKeyPressed(KeyboardKey key) const
{
    UNUSED_VAR(key);
    return false;
}

bool Ps2Platform::Keyboard_WasKeyReleased(KeyboardKey key) const
{
    UNUSED_VAR(key);
    return false;
}

bool Ps2Platform::Mouse_IsButtonDown(MouseButton button) const
{
    UNUSED_VAR(button);
    return false;
}

bool Ps2Platform::Mouse_WasButtonPressed(MouseButton button) const
{
    UNUSED_VAR(button);
    return false;
}

Vector2 Ps2Platform::Mouse_GetPosition() const { return Vector2{0.0f, 0.0f}; }

Vector2 Ps2Platform::Mouse_GetDelta() const { return Vector2{0.0f, 0.0f}; }

float Ps2Platform::Mouse_GetWheelDelta() const { return 0.0f; }

uint8_t Ps2Platform::Touch_GetContactCount(TouchSurface surface) const
{
    UNUSED_VAR(surface);
    return 0;
}

bool Ps2Platform::Touch_GetContact(TouchSurface surface, uint8_t index, TouchContact* outContact) const
{
    UNUSED_VAR(surface);
    UNUSED_VAR(index);
    UNUSED_VAR(outContact);
    return false;
}

uint32_t Ps2Platform::Keyboard_PopCharacters(char* outBuffer, uint32_t bufferSize)
{
    UNUSED_VAR(outBuffer);
    UNUSED_VAR(bufferSize);
    return 0;
}

bool Ps2Platform::Dialog_Open(const DialogRequest& request)
{
    UNUSED_VAR(request);
    return false;
}

DialogStatus Ps2Platform::Dialog_Poll() { return DialogStatus::Idle; }

void Ps2Platform::Dialog_Cancel() {}
