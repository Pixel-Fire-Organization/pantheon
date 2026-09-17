#include <cmath>
#include <cstring>

#include "Macros.h"
#include "Platform.h"
#include "core/EngineDebug.h"

#include <switch.h>

namespace
{
    struct ButtonMapping
    {
        u64 hid;
        GamepadButton engine;
    };

    const ButtonMapping kButtonMap[] = {
        {HidNpadButton_A, GamepadButton::Cross},       {HidNpadButton_B, GamepadButton::Circle},
        {HidNpadButton_X, GamepadButton::Triangle},    {HidNpadButton_Y, GamepadButton::Square},
        {HidNpadButton_L, GamepadButton::L1},          {HidNpadButton_R, GamepadButton::R1},
        {HidNpadButton_ZL, GamepadButton::L2},         {HidNpadButton_ZR, GamepadButton::R2},
        {HidNpadButton_StickL, GamepadButton::L3},     {HidNpadButton_StickR, GamepadButton::R3},
        {HidNpadButton_Minus, GamepadButton::Select},  {HidNpadButton_Plus, GamepadButton::Start},
        {HidNpadButton_Up, GamepadButton::DPadUp},     {HidNpadButton_Right, GamepadButton::DPadRight},
        {HidNpadButton_Down, GamepadButton::DPadDown}, {HidNpadButton_Left, GamepadButton::DPadLeft},
    };

    PadState s_Pads[MAX_GAME_PAD_PORTS];

    uint16_t TranslateButtons(u64 raw)
    {
        uint16_t out = 0;
        for (size_t i = 0; i < sizeof(kButtonMap) / sizeof(kButtonMap[0]); ++i)
        {
            if (raw & kButtonMap[i].hid)
                out |= static_cast<uint16_t>(kButtonMap[i].engine);
        }
        return out;
    }

    float NormalizeAxis(int32_t raw)
    {
        float v = static_cast<float>(raw) / static_cast<float>(INPUT_STICK_RAW_MAX);
        if (v > 1.0f)
            v = 1.0f;
        if (v < -1.0f)
            v = -1.0f;
        return (fabsf(v) < INPUT_ANALOG_DEADZONE) ? 0.0f : v;
    }

    Vector2 NormalizeStick(const HidAnalogStickState& state) { return Vector2{NormalizeAxis(state.x), -NormalizeAxis(state.y)}; }
} // namespace

void NxPlatform::InitInput()
{
    padConfigureInput(MAX_GAME_PAD_PORTS, HidNpadStyleSet_NpadFullCtrl);

    padInitializeWithMask(&s_Pads[0], (1ull << HidNpadIdType_No1) | (1ull << HidNpadIdType_Handheld));
    for (uint8_t port = 1; port < MAX_GAME_PAD_PORTS; ++port)
        padInitializeWithMask(&s_Pads[port], 1ull << (static_cast<unsigned>(HidNpadIdType_No1) + port));

    hidInitializeTouchScreen();
}

void NxPlatform::PollInput()
{
    PumpAppletMessages();

    memcpy(m_padsPrev, m_pads, sizeof(m_pads));

    for (uint8_t port = 0; port < MAX_GAME_PAD_PORTS; ++port)
    {
        PadState& pad = s_Pads[port];
        padUpdate(&pad);

        PadSnapshot& snapshot = m_pads[port];
        const bool wasConnected = snapshot.connected;
        snapshot.connected = padIsConnected(&pad);
        if (!snapshot.connected)
        {
            memset(&snapshot, 0, sizeof(snapshot));
        }
        else
        {
            snapshot.buttons = TranslateButtons(padGetButtons(&pad));
            snapshot.stick[static_cast<uint8_t>(GamepadStick::Left)] = NormalizeStick(padGetStickPos(&pad, 0));
            snapshot.stick[static_cast<uint8_t>(GamepadStick::Right)] = NormalizeStick(padGetStickPos(&pad, 1));
            snapshot.trigger[static_cast<uint8_t>(GamepadTrigger::Left)] = (snapshot.buttons & static_cast<uint16_t>(GamepadButton::L2)) ? 1.0f : 0.0f;
            snapshot.trigger[static_cast<uint8_t>(GamepadTrigger::Right)] = (snapshot.buttons & static_cast<uint16_t>(GamepadButton::R2)) ? 1.0f : 0.0f;
        }

        if (snapshot.connected != wasConnected)
            Engine_LogInfo("%s: pad %u %s", GetName(), static_cast<unsigned>(port), snapshot.connected ? "connected" : "disconnected");

        if (m_logInput && snapshot.buttons != m_padsPrev[port].buttons)
            Engine_LogInfo("%s: pad %u buttons 0x%04X", GetName(), static_cast<unsigned>(port), static_cast<unsigned>(snapshot.buttons));
    }

    m_touchCount = 0;
    HidTouchScreenState touch;
    memset(&touch, 0, sizeof(touch));
    if (!m_docked && hidGetTouchScreenStates(&touch, 1) > 0)
    {
        const int32_t available = (touch.count < 0) ? 0 : touch.count;
        const uint8_t count = static_cast<uint8_t>((available < INPUT_TOUCH_MAX_CONTACTS) ? available : INPUT_TOUCH_MAX_CONTACTS);
        for (uint8_t i = 0; i < count; ++i)
        {
            const HidTouchState& report = touch.touches[i];
            float x = static_cast<float>(report.x) / static_cast<float>(INPUT_TOUCH_RAW_WIDTH);
            float y = static_cast<float>(report.y) / static_cast<float>(INPUT_TOUCH_RAW_HEIGHT);
            m_touches[i].position = Vector2{(x > 1.0f) ? 1.0f : x, (y > 1.0f) ? 1.0f : y};
            m_touches[i].force = 1.0f;
            m_touches[i].id = static_cast<uint8_t>(report.finger_id);
        }
        m_touchCount = count;
    }
}

bool NxPlatform::Gamepad_IsConnected(uint8_t port) const
{
    if (port >= MAX_GAME_PAD_PORTS)
        return false;
    return m_pads[port].connected;
}

bool NxPlatform::Gamepad_IsButtonDown(uint8_t port, GamepadButton button) const
{
    if (port >= MAX_GAME_PAD_PORTS)
        return false;
    const uint16_t mask = static_cast<uint16_t>(button);
    return mask != 0 && (m_pads[port].buttons & mask) == mask;
}

bool NxPlatform::Gamepad_WasButtonPressed(uint8_t port, GamepadButton button) const
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

bool NxPlatform::Gamepad_WasButtonReleased(uint8_t port, GamepadButton button) const
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

Vector2 NxPlatform::Gamepad_GetStick(uint8_t port, GamepadStick stick) const
{
    if (port >= MAX_GAME_PAD_PORTS || stick >= GamepadStick::Count)
        return Vector2{0.0f, 0.0f};
    return m_pads[port].stick[static_cast<uint8_t>(stick)];
}

float NxPlatform::Gamepad_GetTrigger(uint8_t port, GamepadTrigger trigger) const
{
    if (port >= MAX_GAME_PAD_PORTS || trigger >= GamepadTrigger::Count)
        return 0.0f;
    return m_pads[port].trigger[static_cast<uint8_t>(trigger)];
}

bool NxPlatform::Keyboard_IsKeyDown(KeyboardKey key) const
{
    UNUSED_VAR(key);
    return false;
}

bool NxPlatform::Keyboard_WasKeyPressed(KeyboardKey key) const
{
    UNUSED_VAR(key);
    return false;
}

bool NxPlatform::Keyboard_WasKeyReleased(KeyboardKey key) const
{
    UNUSED_VAR(key);
    return false;
}

bool NxPlatform::Mouse_IsButtonDown(MouseButton button) const
{
    UNUSED_VAR(button);
    return false;
}

bool NxPlatform::Mouse_WasButtonPressed(MouseButton button) const
{
    UNUSED_VAR(button);
    return false;
}

Vector2 NxPlatform::Mouse_GetPosition() const { return Vector2{0.0f, 0.0f}; }

Vector2 NxPlatform::Mouse_GetDelta() const { return Vector2{0.0f, 0.0f}; }

float NxPlatform::Mouse_GetWheelDelta() const { return 0.0f; }

uint8_t NxPlatform::Touch_GetContactCount(TouchSurface surface) const { return (surface == TouchSurface::Front) ? m_touchCount : 0u; }

bool NxPlatform::Touch_GetContact(TouchSurface surface, uint8_t index, TouchContact* outContact) const
{
    if (surface != TouchSurface::Front || index >= m_touchCount || !outContact)
        return false;
    *outContact = m_touches[index];
    return true;
}

uint32_t NxPlatform::Keyboard_PopCharacters(char* outBuffer, uint32_t bufferSize)
{
    UNUSED_VAR(outBuffer);
    UNUSED_VAR(bufferSize);
    return 0;
}
