#include <cmath>
#include <cstring>

#include "core/EngineDebug.h"
#include "Macros.h"
#include "Platform.h"

extern "C" {
#include <psp2/ctrl.h>
#include <psp2/touch.h>
}

static_assert(static_cast<uint16_t>(GamepadButton::Select) == SCE_CTRL_SELECT, "pad mask drift: Select");
static_assert(static_cast<uint16_t>(GamepadButton::L3) == SCE_CTRL_L3, "pad mask drift: L3");
static_assert(static_cast<uint16_t>(GamepadButton::R3) == SCE_CTRL_R3, "pad mask drift: R3");
static_assert(static_cast<uint16_t>(GamepadButton::Start) == SCE_CTRL_START, "pad mask drift: Start");
static_assert(static_cast<uint16_t>(GamepadButton::DPadUp) == SCE_CTRL_UP, "pad mask drift: DPadUp");
static_assert(static_cast<uint16_t>(GamepadButton::DPadRight) == SCE_CTRL_RIGHT, "pad mask drift: DPadRight");
static_assert(static_cast<uint16_t>(GamepadButton::DPadDown) == SCE_CTRL_DOWN, "pad mask drift: DPadDown");
static_assert(static_cast<uint16_t>(GamepadButton::DPadLeft) == SCE_CTRL_LEFT, "pad mask drift: DPadLeft");
static_assert(static_cast<uint16_t>(GamepadButton::L2) == SCE_CTRL_LTRIGGER, "pad mask drift: L2");
static_assert(static_cast<uint16_t>(GamepadButton::R2) == SCE_CTRL_RTRIGGER, "pad mask drift: R2");
static_assert(static_cast<uint16_t>(GamepadButton::L1) == SCE_CTRL_L1, "pad mask drift: L1");
static_assert(static_cast<uint16_t>(GamepadButton::R1) == SCE_CTRL_R1, "pad mask drift: R1");
static_assert(static_cast<uint16_t>(GamepadButton::Triangle) == SCE_CTRL_TRIANGLE, "pad mask drift: Triangle");
static_assert(static_cast<uint16_t>(GamepadButton::Circle) == SCE_CTRL_CIRCLE, "pad mask drift: Circle");
static_assert(static_cast<uint16_t>(GamepadButton::Cross) == SCE_CTRL_CROSS, "pad mask drift: Cross");
static_assert(static_cast<uint16_t>(GamepadButton::Square) == SCE_CTRL_SQUARE, "pad mask drift: Square");

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

    Vector2 NormalizeTouch(int16_t x, int16_t y, float rawWidth, float rawHeight)
    {
        float nx = static_cast<float>(x) / rawWidth;
        float ny = static_cast<float>(y) / rawHeight;
        if (nx < 0.0f)
            nx = 0.0f;
        if (nx > 1.0f)
            nx = 1.0f;
        if (ny < 0.0f)
            ny = 0.0f;
        if (ny > 1.0f)
            ny = 1.0f;
        return Vector2{nx, ny};
    }

    void EnsureSampling()
    {
        if (s_SamplingReady)
            return;

        sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);

        sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
        sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);

        s_SamplingReady = true;
    }
}

void VitaPlatform::PollInput()
{
    memcpy(m_padsPrev, m_pads, sizeof(m_pads));
    EnsureSampling();

    for (uint8_t port = 0; port < MAX_GAME_PAD_PORTS; ++port)
    {
        SceCtrlData data;
        memset(&data, 0, sizeof(data));

        const int got = sceCtrlPeekBufferPositive(port, &data, 1);

        if (port == 0 && !m_padReported)
        {
            m_padReported = true;
            if (got < 1)
                Engine_LogError("%s: pad 0 is not readable (sceCtrlPeekBufferPositive returned %d)", GetName(), got);
            else
                Engine_LogInfo("%s: pad 0 readable, buttons=0x%08X", GetName(), static_cast<unsigned>(data.buttons));
        }

        if (got < 1)
        {
            m_pads[port].connected = false;
            m_pads[port].buttons = 0;
            m_pads[port].stick[static_cast<uint8_t>(GamepadStick::Left)] = Vector2{0.0f, 0.0f};
            m_pads[port].stick[static_cast<uint8_t>(GamepadStick::Right)] = Vector2{0.0f, 0.0f};
            continue;
        }

        m_pads[port].connected = true;
        m_pads[port].buttons = TranslatePadButtons(static_cast<uint16_t>(data.buttons & 0xFFFFu));

        if (m_logInput && m_pads[port].buttons != m_padsPrev[port].buttons)
            Engine_LogInfo("%s: pad %u buttons 0x%04X -> 0x%04X", GetName(), port, m_padsPrev[port].buttons, m_pads[port].buttons);
        m_pads[port].stick[static_cast<uint8_t>(GamepadStick::Left)] = Vector2{NormalizeAxis(data.lx), NormalizeAxis(data.ly)};
        m_pads[port].stick[static_cast<uint8_t>(GamepadStick::Right)] = Vector2{NormalizeAxis(data.rx), NormalizeAxis(data.ry)};
    }

    PollTouch();
}

void VitaPlatform::PollTouch()
{
    if (!HasTouchSurfaces())
        return;

    static const struct
    {
        SceUInt32 port;
        float rawWidth;
        float rawHeight;
    } kPanels[static_cast<uint8_t>(TouchSurface::Count)] = {
        {SCE_TOUCH_PORT_FRONT, INPUT_TOUCH_FRONT_RAW_WIDTH, INPUT_TOUCH_FRONT_RAW_HEIGHT},
        {SCE_TOUCH_PORT_BACK, INPUT_TOUCH_REAR_RAW_WIDTH, INPUT_TOUCH_REAR_RAW_HEIGHT},
    };

    for (uint8_t surface = 0; surface < static_cast<uint8_t>(TouchSurface::Count); ++surface)
    {
        m_touch[surface].count = 0;

        SceTouchData data;
        memset(&data, 0, sizeof(data));
        if (sceTouchPeek(kPanels[surface].port, &data, 1) < 0)
            continue;

        uint32_t reports = data.reportNum;
        if (reports > INPUT_TOUCH_MAX_CONTACTS)
            reports = INPUT_TOUCH_MAX_CONTACTS;

        for (uint32_t i = 0; i < reports; ++i)
        {
            TouchContact& contact = m_touch[surface].contacts[i];
            contact.position = NormalizeTouch(data.report[i].x, data.report[i].y, kPanels[surface].rawWidth, kPanels[surface].rawHeight);
            contact.force = static_cast<float>(data.report[i].force) / 255.0f;
            contact.id = data.report[i].id;
        }
        m_touch[surface].count = static_cast<uint8_t>(reports);
    }
}

uint8_t VitaPlatform::Touch_GetContactCount(TouchSurface surface) const
{
    if (surface >= TouchSurface::Count)
        return 0;
    return m_touch[static_cast<uint8_t>(surface)].count;
}

bool VitaPlatform::Touch_GetContact(TouchSurface surface, uint8_t index, TouchContact* outContact) const
{
    if (surface >= TouchSurface::Count || !outContact)
        return false;

    const TouchSnapshot& snapshot = m_touch[static_cast<uint8_t>(surface)];
    if (index >= snapshot.count)
        return false;

    *outContact = snapshot.contacts[index];
    return true;
}

bool VitaPlatform::Gamepad_IsConnected(uint8_t port) const { return (port < MAX_GAME_PAD_PORTS) && m_pads[port].connected; }

bool VitaPlatform::Gamepad_IsButtonDown(uint8_t port, GamepadButton button) const
{
    if (port >= MAX_GAME_PAD_PORTS)
        return false;
    const uint16_t mask = static_cast<uint16_t>(button);
    return (m_pads[port].buttons & mask) == mask;
}

bool VitaPlatform::Gamepad_WasButtonPressed(uint8_t port, GamepadButton button) const
{
    if (port >= MAX_GAME_PAD_PORTS)
        return false;
    const uint16_t mask = static_cast<uint16_t>(button);
    const bool now = (m_pads[port].buttons & mask) == mask;
    const bool before = (m_padsPrev[port].buttons & mask) == mask;
    return now && !before;
}

bool VitaPlatform::Gamepad_WasButtonReleased(uint8_t port, GamepadButton button) const
{
    if (port >= MAX_GAME_PAD_PORTS)
        return false;
    const uint16_t mask = static_cast<uint16_t>(button);
    const bool now = (m_pads[port].buttons & mask) == mask;
    const bool before = (m_padsPrev[port].buttons & mask) == mask;
    return !now && before;
}

Vector2 VitaPlatform::Gamepad_GetStick(uint8_t port, GamepadStick stick) const
{
    if (port >= MAX_GAME_PAD_PORTS || stick >= GamepadStick::Count)
        return Vector2{0.0f, 0.0f};
    return m_pads[port].stick[static_cast<uint8_t>(stick)];
}

float VitaPlatform::Gamepad_GetTrigger(uint8_t port, GamepadTrigger trigger) const
{
    const GamepadButton button = (trigger == GamepadTrigger::Left) ? GamepadButton::L2 : GamepadButton::R2;
    return Gamepad_IsButtonDown(port, button) ? 1.0f : 0.0f;
}

bool VitaPlatform::Keyboard_IsKeyDown(KeyboardKey key) const
{
    UNUSED_VAR(key);
    return false;
}

bool VitaPlatform::Keyboard_WasKeyPressed(KeyboardKey key) const
{
    UNUSED_VAR(key);
    return false;
}

bool VitaPlatform::Keyboard_WasKeyReleased(KeyboardKey key) const
{
    UNUSED_VAR(key);
    return false;
}

bool VitaPlatform::Mouse_IsButtonDown(MouseButton button) const
{
    UNUSED_VAR(button);
    return false;
}

bool VitaPlatform::Mouse_WasButtonPressed(MouseButton button) const
{
    UNUSED_VAR(button);
    return false;
}

Vector2 VitaPlatform::Mouse_GetPosition() const { return Vector2{0.0f, 0.0f}; }

Vector2 VitaPlatform::Mouse_GetDelta() const { return Vector2{0.0f, 0.0f}; }

float VitaPlatform::Mouse_GetWheelDelta() const { return 0.0f; }
