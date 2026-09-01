#pragma once

#include <cstdint>

#include "../graphics/Types.h"
#include "../platform/PlatformKeys.h"

bool Engine_Input_Init();
void Engine_Input_Shutdown();
bool Engine_Input_IsActive();

// --- Gamepad ----------------------------------------------------------------
bool IsGamePadInitialized(uint8_t port);
bool IsGamePadButtonPressed(uint8_t port, GamepadButton button);
bool WasGamePadButtonPressed(uint8_t port, GamepadButton button); // rising edge
bool WasGamePadButtonReleased(uint8_t port, GamepadButton button); // falling edge
Vector2 GetGamePadAxis(uint8_t port, GamepadStick stick);
float GetGamePadTrigger(uint8_t port, GamepadTrigger trigger);

// --- Keyboard ---------------------------------------------------------------
bool IsKeyDown(KeyboardKey key);
bool WasKeyPressed(KeyboardKey key);
bool WasKeyReleased(KeyboardKey key);

// --- Mouse ------------------------------------------------------------------
bool IsMouseButtonDown(MouseButton button);
bool WasMouseButtonPressed(MouseButton button);
Vector2 GetMousePosition();
Vector2 GetMouseDelta();
float GetMouseWheelDelta();
