#include <cmath>
#include <cstring>

#include "core/EngineDebug.h"
#include "Macros.h"
#include "Platform.h"

extern "C" {
#include <pspctrl.h>
}

// The pad masks agree with the engine's values everywhere except the shoulders,
// which arrive on the second-row bits and are translated below.
static_assert(static_cast<uint16_t>(GamepadButton::Select) == PSP_CTRL_SELECT, "pad mask drift: Select");
static_assert(static_cast<uint16_t>(GamepadButton::Start) == PSP_CTRL_START, "pad mask drift: Start");
static_assert(static_cast<uint16_t>(GamepadButton::DPadUp) == PSP_CTRL_UP, "pad mask drift: DPadUp");
static_assert(static_cast<uint16_t>(GamepadButton::DPadRight) == PSP_CTRL_RIGHT, "pad mask drift: DPadRight");
static_assert(static_cast<uint16_t>(GamepadButton::DPadDown) == PSP_CTRL_DOWN, "pad mask drift: DPadDown");
static_assert(static_cast<uint16_t>(GamepadButton::DPadLeft) == PSP_CTRL_LEFT, "pad mask drift: DPadLeft");
static_assert(static_cast<uint16_t>(GamepadButton::L2) == PSP_CTRL_LTRIGGER, "pad mask drift: LTrigger");
static_assert(static_cast<uint16_t>(GamepadButton::R2) == PSP_CTRL_RTRIGGER, "pad mask drift: RTrigger");
static_assert(static_cast<uint16_t>(GamepadButton::Triangle) == PSP_CTRL_TRIANGLE, "pad mask drift: Triangle");
static_assert(static_cast<uint16_t>(GamepadButton::Circle) == PSP_CTRL_CIRCLE, "pad mask drift: Circle");
static_assert(static_cast<uint16_t>(GamepadButton::Cross) == PSP_CTRL_CROSS, "pad mask drift: Cross");
static_assert(static_cast<uint16_t>(GamepadButton::Square) == PSP_CTRL_SQUARE, "pad mask drift: Square");

namespace
{
    bool s_SamplingReady = false;

    float NormalizeAxis(uint8_t raw)
    {
        float v = (static_cast<float>(raw) - INPUT_ANALOG_RAW_CENTER) / INPUT_ANALOG_RAW_SCALE;
        if (v > 1.0f)
            v = 1.0f;
        if (v < -1.0f)
            v = -1.0f;
        return (fabsf(v) < INPUT_ANALOG_DEADZONE) ? 0.0f : v;
    }

    // The two physical shoulders are sampled on the second-row bits. Reported
    // literally, this pad would claim a second row it does not have and no
    // first row at all.
    uint16_t TranslatePadButtons(uint32_t raw)
    {
        uint16_t out = static_cast<uint16_t>(raw & 0xFFFFu);
        if (raw & PSP_CTRL_LTRIGGER)
            out |= static_cast<uint16_t>(GamepadButton::L1);
        if (raw & PSP_CTRL_RTRIGGER)
            out |= static_cast<uint16_t>(GamepadButton::R1);
        return static_cast<uint16_t>(out & ~(static_cast<uint16_t>(GamepadButton::L2) | static_cast<uint16_t>(GamepadButton::R2)));
    }
}

void PspPlatform::PollInput()
{
    memcpy(m_padsPrev, m_pads, sizeof(m_pads));

    if (!s_SamplingReady)
    {
        sceCtrlSetSamplingCycle(0);
        sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
        s_SamplingReady = true;
    }

    SceCtrlData data;
    memset(&data, 0, sizeof(data));

    if (sceCtrlReadBufferPositive(&data, 1) < 0)
    {
        m_pads[0].connected = false;
        m_pads[0].buttons = 0;
        m_pads[0].stick[static_cast<uint8_t>(GamepadStick::Left)] = Vector2{0.0f, 0.0f};
        m_pads[0].stick[static_cast<uint8_t>(GamepadStick::Right)] = Vector2{0.0f, 0.0f};
        return;
    }

    m_pads[0].connected = true;
    m_pads[0].buttons = TranslatePadButtons(data.Buttons);
    m_pads[0].stick[static_cast<uint8_t>(GamepadStick::Left)] = Vector2{NormalizeAxis(data.Lx), NormalizeAxis(data.Ly)};
    m_pads[0].stick[static_cast<uint8_t>(GamepadStick::Right)] = Vector2{0.0f, 0.0f};
    m_pads[0].trigger[static_cast<uint8_t>(GamepadTrigger::Left)] = (m_pads[0].buttons & static_cast<uint16_t>(GamepadButton::L1)) ? 1.0f : 0.0f;
    m_pads[0].trigger[static_cast<uint8_t>(GamepadTrigger::Right)] = (m_pads[0].buttons & static_cast<uint16_t>(GamepadButton::R1)) ? 1.0f : 0.0f;

    if (!m_padReported)
    {
        Engine_LogInfo("%s: pad connected", GetName());
        m_padReported = true;
    }

    if (m_logInput && m_pads[0].buttons != m_padsPrev[0].buttons)
        Engine_LogInfo("%s: pad buttons 0x%04X", GetName(), static_cast<unsigned>(m_pads[0].buttons));
}

bool PspPlatform::Gamepad_IsConnected(uint8_t port) const
{
    if (port >= MAX_GAME_PAD_PORTS)
        return false;
    return m_pads[port].connected;
}

bool PspPlatform::Gamepad_IsButtonDown(uint8_t port, GamepadButton button) const
{
    if (port >= MAX_GAME_PAD_PORTS)
        return false;
    const uint16_t mask = static_cast<uint16_t>(button);
    return mask != 0 && (m_pads[port].buttons & mask) == mask;
}

bool PspPlatform::Gamepad_WasButtonPressed(uint8_t port, GamepadButton button) const
{
    if (port >= MAX_GAME_PAD_PORTS)
        return false;
    const uint16_t mask = static_cast<uint16_t>(button);
    if (mask == 0)
        return false;
    const bool now = (m_pads[port].buttons & mask) == mask;
    const bool before = (m_padsPrev[port].buttons & mask) == mask;
    return now && !before;
}

bool PspPlatform::Gamepad_WasButtonReleased(uint8_t port, GamepadButton button) const
{
    if (port >= MAX_GAME_PAD_PORTS)
        return false;
    const uint16_t mask = static_cast<uint16_t>(button);
    if (mask == 0)
        return false;
    const bool now = (m_pads[port].buttons & mask) == mask;
    const bool before = (m_padsPrev[port].buttons & mask) == mask;
    return !now && before;
}

Vector2 PspPlatform::Gamepad_GetStick(uint8_t port, GamepadStick stick) const
{
    if (port >= MAX_GAME_PAD_PORTS || stick >= GamepadStick::Count)
        return Vector2{0.0f, 0.0f};
    return m_pads[port].stick[static_cast<uint8_t>(stick)];
}

float PspPlatform::Gamepad_GetTrigger(uint8_t port, GamepadTrigger trigger) const
{
    if (port >= MAX_GAME_PAD_PORTS || trigger >= GamepadTrigger::Count)
        return 0.0f;
    return m_pads[port].trigger[static_cast<uint8_t>(trigger)];
}

bool PspPlatform::Keyboard_IsKeyDown(KeyboardKey key) const
{
    UNUSED_VAR(key);
    return false;
}

bool PspPlatform::Keyboard_WasKeyPressed(KeyboardKey key) const
{
    UNUSED_VAR(key);
    return false;
}

bool PspPlatform::Keyboard_WasKeyReleased(KeyboardKey key) const
{
    UNUSED_VAR(key);
    return false;
}

bool PspPlatform::Mouse_IsButtonDown(MouseButton button) const
{
    UNUSED_VAR(button);
    return false;
}

bool PspPlatform::Mouse_WasButtonPressed(MouseButton button) const
{
    UNUSED_VAR(button);
    return false;
}

Vector2 PspPlatform::Mouse_GetPosition() const { return Vector2{0.0f, 0.0f}; }

Vector2 PspPlatform::Mouse_GetDelta() const { return Vector2{0.0f, 0.0f}; }

float PspPlatform::Mouse_GetWheelDelta() const { return 0.0f; }

uint8_t PspPlatform::Touch_GetContactCount(TouchSurface surface) const
{
    UNUSED_VAR(surface);
    return 0;
}

bool PspPlatform::Touch_GetContact(TouchSurface surface, uint8_t index, TouchContact* outContact) const
{
    UNUSED_VAR(surface);
    UNUSED_VAR(index);
    UNUSED_VAR(outContact);
    return false;
}

uint32_t PspPlatform::Keyboard_PopCharacters(char* outBuffer, uint32_t bufferSize)
{
    UNUSED_VAR(outBuffer);
    UNUSED_VAR(bufferSize);
    return 0;
}
