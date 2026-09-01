#include "core/EngineInput.h"

#include "core/EngineSubsystems.h"
#include "platform/Platform.h"

namespace
{
    bool s_Active = false;

    // One place where "is input available" is decided, so a disabled subsystem
    // and an absent platform produce the same neutral answer.
    Platform* ActivePlatform() { return s_Active ? Engine_GetPlatform() : nullptr; }
} // namespace

bool Engine_Input_Init()
{
    s_Active = true;
    return true;
}

void Engine_Input_Shutdown() { s_Active = false; }

bool Engine_Input_IsActive() { return s_Active; }

// Every query reads the snapshot the platform filled in PollInput() this frame.
// No device is touched here, which is what makes two queries in one frame agree.

bool IsGamePadInitialized(uint8_t port)
{
    Platform* p = ActivePlatform();
    return p && p->Gamepad_IsConnected(port);
}

bool IsGamePadButtonPressed(uint8_t port, GamepadButton button)
{
    Platform* p = ActivePlatform();
    return p && p->Gamepad_IsButtonDown(port, button);
}

bool WasGamePadButtonPressed(uint8_t port, GamepadButton button)
{
    Platform* p = ActivePlatform();
    return p && p->Gamepad_WasButtonPressed(port, button);
}

bool WasGamePadButtonReleased(uint8_t port, GamepadButton button)
{
    Platform* p = ActivePlatform();
    return p && p->Gamepad_WasButtonReleased(port, button);
}

Vector2 GetGamePadAxis(uint8_t port, GamepadStick stick)
{
    Platform* p = ActivePlatform();
    return p ? p->Gamepad_GetStick(port, stick) : Vector2{0.0f, 0.0f};
}

float GetGamePadTrigger(uint8_t port, GamepadTrigger trigger)
{
    Platform* p = ActivePlatform();
    return p ? p->Gamepad_GetTrigger(port, trigger) : 0.0f;
}

bool IsKeyDown(KeyboardKey key)
{
    Platform* p = ActivePlatform();
    return p && p->Keyboard_IsKeyDown(key);
}

bool WasKeyPressed(KeyboardKey key)
{
    Platform* p = ActivePlatform();
    return p && p->Keyboard_WasKeyPressed(key);
}

bool WasKeyReleased(KeyboardKey key)
{
    Platform* p = ActivePlatform();
    return p && p->Keyboard_WasKeyReleased(key);
}

bool IsMouseButtonDown(MouseButton button)
{
    Platform* p = ActivePlatform();
    return p && p->Mouse_IsButtonDown(button);
}

bool WasMouseButtonPressed(MouseButton button)
{
    Platform* p = ActivePlatform();
    return p && p->Mouse_WasButtonPressed(button);
}

Vector2 GetMousePosition()
{
    Platform* p = ActivePlatform();
    return p ? p->Mouse_GetPosition() : Vector2{0.0f, 0.0f};
}

Vector2 GetMouseDelta()
{
    Platform* p = ActivePlatform();
    return p ? p->Mouse_GetDelta() : Vector2{0.0f, 0.0f};
}

float GetMouseWheelDelta()
{
    Platform* p = ActivePlatform();
    return p ? p->Mouse_GetWheelDelta() : 0.0f;
}
