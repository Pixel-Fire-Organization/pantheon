// ---------------------------------------------------------------------------
// GameAPI.cpp — engine-side implementation of the friendly C++ game API.
//
// Every function here is a thin marshalling wrapper over the same engine
// services the retired Lua bindings called (Renderer, EngineInput,
// Engine_Resource_*, core timing). Gameplay now runs natively: no interpreter
// and no FFI crossing in the per-frame hot loop.
// ---------------------------------------------------------------------------

#include "GameAPI.h"

#include "ui/EngineUi.h"

#include <cstdlib>
#include <cstring>

#include "core/EngineAchievement.h"
#include "core/EngineAction.h"
#include "core/EngineApp.h"
#include "core/EngineCore.h"
#include "core/EngineDebug.h"
#include "core/EngineInput.h"
#include "graphics/Primitives.h"
#include "graphics/Renderer.h"
#include "graphics/Types.h"
#include "level/EngineLevel.h"
#include "platform/Platform.h"
#include "resources/EngineResource.h"
#include "scenes/EngineScene.h"

// Defined in EngineApp.cpp — sets the internal exit flag polled by EngineExited().

namespace
{

    inline Color3 MakeColor(int r, int g, int b) { return Color3{static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f, static_cast<float>(b) / 255.0f}; }

    // Map a key name to the platform enum. Kept string-keyed because GameAPI is
    // the documented public surface and plain scalars are its stated design; the
    // enums are the platform interface, and this is the seam between them.
    KeyboardKey KeyFromName(const char* name)
    {
        if (!name || !name[0])
            return KeyboardKey::Unknown;

        // Single characters cover a-z and 0-9 without a 36-entry table.
        if (name[1] == '\0')
        {
            const char c = name[0];
            if (c >= 'a' && c <= 'z')
                return static_cast<KeyboardKey>(static_cast<uint16_t>(KeyboardKey::A) + (c - 'a'));
            if (c >= 'A' && c <= 'Z')
                return static_cast<KeyboardKey>(static_cast<uint16_t>(KeyboardKey::A) + (c - 'A'));
            if (c >= '0' && c <= '9')
                return static_cast<KeyboardKey>(static_cast<uint16_t>(KeyboardKey::Num0) + (c - '0'));
        }

        // f1..f12
        if ((name[0] == 'f' || name[0] == 'F') && name[1] >= '0' && name[1] <= '9')
        {
            int n = atoi(name + 1);
            if (n >= 1 && n <= 12)
                return static_cast<KeyboardKey>(static_cast<uint16_t>(KeyboardKey::F1) + (n - 1));
        }

        static const struct
        {
            const char* name;
            KeyboardKey key;
        } kNamed[] = {
            {"up", KeyboardKey::Up},
            {"down", KeyboardKey::Down},
            {"left", KeyboardKey::Left},
            {"right", KeyboardKey::Right},
            {"space", KeyboardKey::Space},
            {"enter", KeyboardKey::Enter},
            {"return", KeyboardKey::Enter},
            {"escape", KeyboardKey::Escape},
            {"esc", KeyboardKey::Escape},
            {"tab", KeyboardKey::Tab},
            {"backspace", KeyboardKey::Backspace},
            {"delete", KeyboardKey::Delete},
            {"insert", KeyboardKey::Insert},
            {"home", KeyboardKey::Home},
            {"end", KeyboardKey::End},
            {"pageup", KeyboardKey::PageUp},
            {"pagedown", KeyboardKey::PageDown},
            {"shift", KeyboardKey::LeftShift},
            {"lshift", KeyboardKey::LeftShift},
            {"rshift", KeyboardKey::RightShift},
            {"ctrl", KeyboardKey::LeftControl},
            {"lctrl", KeyboardKey::LeftControl},
            {"rctrl", KeyboardKey::RightControl},
            {"alt", KeyboardKey::LeftAlt},
            {"lalt", KeyboardKey::LeftAlt},
            {"ralt", KeyboardKey::RightAlt},
            {"minus", KeyboardKey::Minus},
            {"equal", KeyboardKey::Equal},
            {"comma", KeyboardKey::Comma},
            {"period", KeyboardKey::Period},
            {"slash", KeyboardKey::Slash},
            {"backslash", KeyboardKey::Backslash},
            {"semicolon", KeyboardKey::Semicolon},
            {"apostrophe", KeyboardKey::Apostrophe},
            {"grave", KeyboardKey::Grave},
            {"lbracket", KeyboardKey::LeftBracket},
            {"rbracket", KeyboardKey::RightBracket},
        };

        for (size_t i = 0; i < sizeof(kNamed) / sizeof(kNamed[0]); ++i)
        {
            if (strcmp(name, kNamed[i].name) == 0)
                return kNamed[i].key;
        }
        return KeyboardKey::Unknown;
    }

    // Map a button name to the engine enum. Mirrors the old input.is_pad_pressed
    // binding one-for-one so ported scripts behave identically.
    GamepadButton ButtonFromName(const char* btn)
    {
        if (strcmp(btn, "x") == 0)
            return GamepadButton::Cross;
        if (strcmp(btn, "cir") == 0)
            return GamepadButton::Circle;
        if (strcmp(btn, "squ") == 0)
            return GamepadButton::Square;
        if (strcmp(btn, "tri") == 0)
            return GamepadButton::Triangle;
        if (strcmp(btn, "dpad_up") == 0)
            return GamepadButton::DPadUp;
        if (strcmp(btn, "dpad_down") == 0)
            return GamepadButton::DPadDown;
        if (strcmp(btn, "dpad_left") == 0)
            return GamepadButton::DPadLeft;
        if (strcmp(btn, "dpad_right") == 0)
            return GamepadButton::DPadRight;
        if (strcmp(btn, "l1") == 0)
            return GamepadButton::L1;
        if (strcmp(btn, "l2") == 0)
            return GamepadButton::L2;
        if (strcmp(btn, "r1") == 0)
            return GamepadButton::R1;
        if (strcmp(btn, "r2") == 0)
            return GamepadButton::R2;
        if (strcmp(btn, "l3") == 0)
            return GamepadButton::L3;
        if (strcmp(btn, "r3") == 0)
            return GamepadButton::R3;
        if (strcmp(btn, "start") == 0)
            return GamepadButton::Start;
        if (strcmp(btn, "select") == 0)
            return GamepadButton::Select;
        Engine_LogError("[Game] IsPadPressed: unknown button '%s'", btn);
        return GamepadButton::Unknown;
    }

    // --- Action source name grammar ---------------------------------------------
    // "device.source" as used in game/config/actions.json, split apart at the
    // GameAPI boundary. This is deliberately its own grammar, not KeyFromName's
    // or ButtonFromName's: those predate Action and abbreviate ("x", "shift"),
    // while a source name here is the lowercased engine enumerator exactly as
    // tools/actions.py reads it -- so a prompt and the declaration that
    // produced it name the same control the same way. Gamepad buttons resolve
    // to a bit POSITION, matching ACTION_OVERLAY.md and tools/actions.py's
    // GAMEPAD_BUTTON_BIT table -- the two must be read together if either
    // changes.
    struct NamedButton
    {
        const char* name;
        uint16_t bit;
    };
    const NamedButton kGamepadButtons[] = {
        {"select", 0}, {"l3", 1}, {"r3", 2},  {"start", 3}, {"dpad_up", 4},   {"dpad_right", 5}, {"dpad_down", 6}, {"dpad_left", 7},
        {"l2", 8},     {"r2", 9}, {"l1", 10}, {"r1", 11},   {"triangle", 12}, {"circle", 13},    {"cross", 14},    {"square", 15},
    };
    const NamedButton kMouseButtons[] = {{"left", 0}, {"right", 1}, {"middle", 2}, {"extra1", 3}, {"extra2", 4}};

    // Indexed by KeyboardKey's own ordinal -- the lowercased spelling of each
    // enumerator in PlatformKeys.h, in the same order. Unknown has no source
    // name, since it is never a valid source.
    const char* const kKeyboardKeyNames[] = {
        "",
        "a",
        "b",
        "c",
        "d",
        "e",
        "f",
        "g",
        "h",
        "i",
        "j",
        "k",
        "l",
        "m",
        "n",
        "o",
        "p",
        "q",
        "r",
        "s",
        "t",
        "u",
        "v",
        "w",
        "x",
        "y",
        "z",
        "num0",
        "num1",
        "num2",
        "num3",
        "num4",
        "num5",
        "num6",
        "num7",
        "num8",
        "num9",
        "f1",
        "f2",
        "f3",
        "f4",
        "f5",
        "f6",
        "f7",
        "f8",
        "f9",
        "f10",
        "f11",
        "f12",
        "left",
        "right",
        "up",
        "down",
        "space",
        "enter",
        "escape",
        "tab",
        "backspace",
        "delete",
        "insert",
        "home",
        "end",
        "pageup",
        "pagedown",
        "leftshift",
        "rightshift",
        "leftcontrol",
        "rightcontrol",
        "leftalt",
        "rightalt",
        "minus",
        "equal",
        "leftbracket",
        "rightbracket",
        "semicolon",
        "apostrophe",
        "comma",
        "period",
        "slash",
        "backslash",
        "grave",
    };
    const size_t kKeyboardKeyNameCount = sizeof(kKeyboardKeyNames) / sizeof(kKeyboardKeyNames[0]);

    KeyboardKey ActionKeyFromName(const char* name)
    {
        for (size_t i = 1; i < kKeyboardKeyNameCount; ++i)
        {
            if (strcmp(name, kKeyboardKeyNames[i]) == 0)
                return static_cast<KeyboardKey>(i);
        }
        return KeyboardKey::Unknown;
    }

    bool ActionSourceFromName(const char* device, const char* source, ActionSourceDevice* outDevice, ActionSourceKind* outKind, uint16_t* outCode)
    {
        if (!device || !source)
            return false;

        if (strcmp(device, "gamepad") == 0)
        {
            if (strcmp(source, "stick_left") == 0 || strcmp(source, "stick_right") == 0)
            {
                *outDevice = ActionSourceDevice::Gamepad;
                *outKind = ActionSourceKind::Stick;
                *outCode = static_cast<uint16_t>(strcmp(source, "stick_left") == 0 ? GamepadStick::Left : GamepadStick::Right);
                return true;
            }
            if (strcmp(source, "trigger_left") == 0 || strcmp(source, "trigger_right") == 0)
            {
                *outDevice = ActionSourceDevice::Gamepad;
                *outKind = ActionSourceKind::Trigger;
                *outCode = static_cast<uint16_t>(strcmp(source, "trigger_left") == 0 ? GamepadTrigger::Left : GamepadTrigger::Right);
                return true;
            }
            for (const NamedButton& b : kGamepadButtons)
            {
                if (strcmp(source, b.name) == 0)
                {
                    *outDevice = ActionSourceDevice::Gamepad;
                    *outKind = ActionSourceKind::Button;
                    *outCode = b.bit;
                    return true;
                }
            }
            return false;
        }
        if (strcmp(device, "keyboard") == 0)
        {
            const KeyboardKey key = ActionKeyFromName(source);
            if (key == KeyboardKey::Unknown)
                return false;
            *outDevice = ActionSourceDevice::Keyboard;
            *outKind = ActionSourceKind::Button;
            *outCode = static_cast<uint16_t>(key);
            return true;
        }
        if (strcmp(device, "mouse") == 0)
        {
            if (strcmp(source, "delta") == 0)
            {
                *outDevice = ActionSourceDevice::Mouse;
                *outKind = ActionSourceKind::Stick;
                *outCode = static_cast<uint16_t>(MouseAxis::Delta);
                return true;
            }
            for (const NamedButton& b : kMouseButtons)
            {
                if (strcmp(source, b.name) == 0)
                {
                    *outDevice = ActionSourceDevice::Mouse;
                    *outKind = ActionSourceKind::Button;
                    *outCode = b.bit;
                    return true;
                }
            }
            return false;
        }
        Engine_LogError("game: unknown action device '%s'", device);
        return false;
    }

    void ActionSourceToName(ActionSourceDevice device, ActionSourceKind kind, uint16_t code, const char** outDevice, const char** outSource)
    {
        switch (device)
        {
        case ActionSourceDevice::Gamepad:
            *outDevice = "gamepad";
            if (kind == ActionSourceKind::Stick)
            {
                *outSource = (code == static_cast<uint16_t>(GamepadStick::Left)) ? "stick_left" : "stick_right";
                return;
            }
            if (kind == ActionSourceKind::Trigger)
            {
                *outSource = (code == static_cast<uint16_t>(GamepadTrigger::Left)) ? "trigger_left" : "trigger_right";
                return;
            }
            for (const NamedButton& b : kGamepadButtons)
            {
                if (b.bit == code)
                {
                    *outSource = b.name;
                    return;
                }
            }
            *outSource = "?";
            return;
        case ActionSourceDevice::Keyboard:
            *outDevice = "keyboard";
            *outSource = (code < kKeyboardKeyNameCount) ? kKeyboardKeyNames[code] : "?";
            return;
        case ActionSourceDevice::Mouse:
            *outDevice = "mouse";
            if (kind == ActionSourceKind::Stick)
            {
                *outSource = "delta";
                return;
            }
            for (const NamedButton& b : kMouseButtons)
            {
                if (b.bit == code)
                {
                    *outSource = b.name;
                    return;
                }
            }
            *outSource = "?";
            return;
        case ActionSourceDevice::Touch:
        case ActionSourceDevice::Count:
        default:
            *outDevice = "?";
            *outSource = "?";
            return;
        }
    }

} // namespace

namespace game
{

    // --- Time -------------------------------------------------------------------
    float GetTime() { return Engine_GetTotalTime(); }
    float GetDeltaTime() { return Engine_GetDeltaTime(); }

    // --- Lifecycle / debug ------------------------------------------------------
    void Exit() { EngineApp_OnExitRequested(); }

    void Log(const char* msg) { Engine_LogInfo("[Game] %s", msg); }

    const char* MakePath(const char* relativePath)
    {
        static char pathBuf[IO_FILE_MAX_PATH];
        const char* token = Engine_GetResourceLocationToken();
        Engine_BuildPath(token ? token : "cdrom0:", relativePath, pathBuf, IO_FILE_MAX_PATH);
        return pathBuf;
    }

    // --- Camera -----------------------------------------------------------------
    void SetCamera3D(float posX, float posY, float posZ, float targetX, float targetY, float targetZ, float fovy)
    {
        Renderer* r = Engine_GetRenderer();
        if (!r)
            return;

        Camera3D cam;
        cam.position = Vector3{posX, posY, posZ};
        cam.target = Vector3{targetX, targetY, targetZ};
        cam.up = Vector3{0.0f, 1.0f, 0.0f};
        cam.fovy = fovy;
        cam.projection = CAMERA_PERSPECTIVE;

        r->SetCamera3D(static_cast<CameraID>(0), cam);
        r->SetActiveCamera3D(static_cast<CameraID>(0));
    }

    // --- Frame / background -----------------------------------------------------
    void Clear(int r, int g, int b)
    {
        Renderer* renderer = Engine_GetRenderer();
        if (renderer)
            renderer->ClearFrame(MakeColor(r, g, b));
    }

    // --- 2D primitives ------------------------------------------------------------
    void DrawRect(int x, int y, int width, int height, int r, int g, int b)
    {
        Renderer* renderer = Engine_GetRenderer();
        if (renderer)
            renderer->DrawRect2D(x, y, width, height, MakeColor(r, g, b));
    }

    // --- 3D primitives ----------------------------------------------------------
    void DrawGrid(int slices, float spacing)
    {
        Renderer* r = Engine_GetRenderer();
        if (r)
            r->DrawGrid(slices, spacing);
    }

    void DrawCube(float x, float y, float z, float size, int r, int g, int b)
    {
        Renderer* renderer = Engine_GetRenderer();
        if (renderer)
            renderer->AddPrimitiveToDrawList(Primitive3D::Cube, Vector3{x, y, z}, Vector3{0.f, 0.f, 0.f}, Vector3{size, size, size}, MakeColor(r, g, b));
    }

    void DrawSphere(float x, float y, float z, float size, int r, int g, int b)
    {
        Renderer* renderer = Engine_GetRenderer();
        if (renderer)
            renderer->AddPrimitiveToDrawList(Primitive3D::Sphere, Vector3{x, y, z}, Vector3{0.f, 0.f, 0.f}, Vector3{size, size, size}, MakeColor(r, g, b));
    }

    void DrawCylinder(float x, float y, float z, float size, int r, int g, int b)
    {
        Renderer* renderer = Engine_GetRenderer();
        if (renderer)
            renderer->AddPrimitiveToDrawList(Primitive3D::Cylinder, Vector3{x, y, z}, Vector3{0.f, 0.f, 0.f}, Vector3{size, size, size}, MakeColor(r, g, b));
    }

    void DrawCubeTextured(float x, float y, float z, float size, int textureId)
    {
        Renderer* renderer = Engine_GetRenderer();
        if (renderer)
            renderer->AddPrimitiveToDrawList(Primitive3D::Cube, Vector3{x, y, z}, Vector3{0.f, 0.f, 0.f}, Vector3{size, size, size}, Color3{1.0f, 1.0f, 1.0f}, static_cast<int32_t>(textureId));
    }

    // --- Input ------------------------------------------------------------------
    bool IsPadPressed(int pad, const char* button)
    {
        GamepadButton b = ButtonFromName(button);
        if (b == GamepadButton::Unknown)
            return false;
        return IsGamePadButtonPressed(static_cast<uint8_t>(pad), b);
    }

    void GetJoyAxis(int pad, const char* side, float* outX, float* outY)
    {
        float x = 0.0f, y = 0.0f;
        GamepadStick joy = GamepadStick::Count;
        if (strcmp(side, "left") == 0)
            joy = GamepadStick::Left;
        else if (strcmp(side, "right") == 0)
            joy = GamepadStick::Right;
        else
            Engine_LogError("[Game] GetJoyAxis: unknown side '%s' (expected 'left' or 'right')", side);

        if (joy != GamepadStick::Count)
        {
            const Vector2 v = GetGamePadAxis(static_cast<uint8_t>(pad), joy);
            x = v.x;
            y = v.y;
        }
        if (outX)
            *outX = x;
        if (outY)
            *outY = y;
    }

    bool WasPadPressed(int pad, const char* button)
    {
        GamepadButton b = ButtonFromName(button);
        if (b == GamepadButton::Unknown)
            return false;
        return WasGamePadButtonPressed(static_cast<uint8_t>(pad), b);
    }

    bool IsKeyDown(const char* key) { return ::IsKeyDown(KeyFromName(key)); }

    bool WasKeyPressed(const char* key) { return ::WasKeyPressed(KeyFromName(key)); }

    bool IsMouseButtonDown(int button)
    {
        if (button < 0 || button >= static_cast<int>(MouseButton::Count))
            return false;
        return ::IsMouseButtonDown(static_cast<MouseButton>(button));
    }

    bool WasMouseButtonPressed(int button)
    {
        if (button < 0 || button >= static_cast<int>(MouseButton::Count))
            return false;
        return ::WasMouseButtonPressed(static_cast<MouseButton>(button));
    }

    void GetMousePosition(float* outX, float* outY)
    {
        const Vector2 p = ::GetMousePosition();
        if (outX)
            *outX = p.x;
        if (outY)
            *outY = p.y;
    }

    void GetMouseDelta(float* outX, float* outY)
    {
        const Vector2 d = ::GetMouseDelta();
        if (outX)
            *outX = d.x;
        if (outY)
            *outY = d.y;
    }

    float GetMouseWheel() { return ::GetMouseWheelDelta(); }

    namespace
    {
        TouchSurface ParseTouchSurface(const char* surface)
        {
            if (!surface)
                return TouchSurface::Count;
            if (strcmp(surface, "front") == 0)
                return TouchSurface::Front;
            if (strcmp(surface, "rear") == 0)
                return TouchSurface::Rear;
            Engine_LogError("game: unknown touch surface '%s'", surface);
            return TouchSurface::Count;
        }
    } // namespace

    int GetTouchCount(const char* surface)
    {
        Platform* platform = Engine_GetPlatform();
        const TouchSurface s = ParseTouchSurface(surface);
        if (!platform || s == TouchSurface::Count)
            return 0;
        return static_cast<int>(platform->Touch_GetContactCount(s));
    }

    bool GetTouch(const char* surface, int index, float* outX, float* outY)
    {
        Platform* platform = Engine_GetPlatform();
        const TouchSurface s = ParseTouchSurface(surface);
        if (!platform || s == TouchSurface::Count || index < 0 || index > 255)
            return false;

        TouchContact contact;
        if (!platform->Touch_GetContact(s, static_cast<uint8_t>(index), &contact))
            return false;

        if (outX)
            *outX = contact.position.x;
        if (outY)
            *outY = contact.position.y;
        return true;
    }

    bool HasInputDevice(const char* device)
    {
        Platform* platform = Engine_GetPlatform();
        if (!platform || !device)
            return false;

        if (strcmp(device, "gamepad") == 0)
            return platform->HasCapability(PlatformCapability::Gamepad);
        if (strcmp(device, "keyboard") == 0)
            return platform->HasCapability(PlatformCapability::Keyboard);
        if (strcmp(device, "mouse") == 0)
            return platform->HasCapability(PlatformCapability::Mouse);
        if (strcmp(device, "touch") == 0)
            return platform->HasCapability(PlatformCapability::Touch);

        Engine_LogError("game::HasInputDevice: unknown device '%s'", device);
        return false;
    }

    // --- Achievements -----------------------------------------------------------
    bool UnlockAchievement(int id)
    {
        if (id < 0)
        {
            Engine_LogError("game::UnlockAchievement: negative id %d", id);
            return false;
        }
        return Engine_Achievement_Unlock(static_cast<uint32_t>(id));
    }

    bool IsAchievementUnlocked(int id) { return (id >= 0) && Engine_Achievement_IsUnlocked(static_cast<uint32_t>(id)); }

    bool HasAchievements() { return Engine_Achievement_IsAvailable(); }

    void StartAchievementsUI() { Engine_Achievement_OpenScreen(); }

    void StopAchievementsUI() { Engine_Achievement_CloseScreen(); }

    bool IsAchievementsUIOpen() { return Engine_Achievement_IsScreenOpen(); }

    // --- Actions ------------------------------------------------------------
    bool ActionHeld(int id) { return id >= 0 && Engine_Action_IsHeld(static_cast<ActionId>(id)); }

    bool ActionPressed(int id) { return id >= 0 && Engine_Action_WasPressed(static_cast<ActionId>(id)); }

    bool ActionReleased(int id) { return id >= 0 && Engine_Action_WasReleased(static_cast<ActionId>(id)); }

    int ActionPressedCount(int id) { return (id >= 0) ? static_cast<int>(Engine_Action_PressCount(static_cast<ActionId>(id))) : 0; }

    bool ActionRepeatTick(int id) { return id >= 0 && Engine_Action_WasRepeatTick(static_cast<ActionId>(id)); }

    float ActionAxis1d(int id) { return (id >= 0) ? Engine_Action_GetAxis1d(static_cast<ActionId>(id)) : 0.0f; }

    void ActionAxis2d(int id, float* outX, float* outY)
    {
        if (id < 0)
        {
            if (outX)
                *outX = 0.0f;
            if (outY)
                *outY = 0.0f;
            return;
        }
        Engine_Action_GetAxis2d(static_cast<ActionId>(id), outX, outY);
    }

    float ActionScalar(int id) { return (id >= 0) ? Engine_Action_GetScalar(static_cast<ActionId>(id)) : 0.0f; }

    bool GetActionPrompt(int id, const char** outDevice, const char** outSource)
    {
        if (id < 0)
            return false;
        ActionSourceDevice device;
        ActionSourceKind kind;
        uint16_t code;
        if (!Engine_Action_GetPrompt(static_cast<ActionId>(id), &device, &kind, &code))
            return false;
        ActionSourceToName(device, kind, code, outDevice, outSource);
        return true;
    }

    bool RebindAction(int id, int bindingIndex, int sourceSlot, const char* device, const char* source)
    {
        if (id < 0 || bindingIndex < 0 || sourceSlot < 0)
            return false;
        ActionSourceDevice parsedDevice;
        ActionSourceKind parsedKind;
        uint16_t parsedCode;
        if (!ActionSourceFromName(device, source, &parsedDevice, &parsedKind, &parsedCode))
        {
            Engine_LogError("game::RebindAction: unknown source '%s.%s'", device ? device : "?", source ? source : "?");
            return false;
        }
        return Engine_Action_Rebind(static_cast<ActionId>(id), static_cast<uint8_t>(bindingIndex), static_cast<uint8_t>(sourceSlot), parsedDevice, parsedKind, parsedCode);
    }

    void RestoreActionDefault(int id)
    {
        if (id >= 0)
            Engine_Action_RestoreDefault(static_cast<ActionId>(id));
    }

    bool SaveActionOverlay() { return Engine_Action_SaveOverlay(); }

    bool PushActionContext(int context) { return context >= 0 && Engine_Action_PushContext(static_cast<ActionContextId>(context)); }

    void PopActionContext() { Engine_Action_PopContext(); }

    // --- Resources --------------------------------------------------------------
    int LoadResource(const char* type, const char* path)
    {
        ResourceType t = RES_TEXTURE;
        if (strcmp(type, "TEXTURE") == 0)
            t = RES_TEXTURE;
        else if (strcmp(type, "MODEL") == 0)
            t = RES_MODEL;
        else if (strcmp(type, "SOUND") == 0)
            t = RES_SOUND;
        else if (strcmp(type, "FONT") == 0)
            t = RES_FONT;
        else
            Engine_LogError("[Game] LoadResource: unknown type '%s'", type);

        return static_cast<int>(Engine_Resource_Load(t, path));
    }

    bool IsResourceReady(int handle) { return Engine_Resource_IsReady(static_cast<int32_t>(handle)); }

    // --- Entities / spawning ----------------------------------------------------
    namespace
    {
        SpawnHandler s_SpawnHandler = nullptr;
    }

    void SetSpawnHandler(SpawnHandler handler) { s_SpawnHandler = handler; }

    // --- Levels -----------------------------------------------------------------
    namespace
    {
        Level s_GameLevel;
        bool s_LevelLoaded = false;
    } // namespace

    bool LoadLevel(const char* name)
    {
        if (s_LevelLoaded)
        {
            Engine_Level_Unload(&s_GameLevel, false);
            s_LevelLoaded = false;
        }
        memset(&s_GameLevel, 0, sizeof(s_GameLevel));
        strncpy(s_GameLevel.name, name, sizeof(s_GameLevel.name) - 1);
        s_LevelLoaded = Engine_Level_Load(&s_GameLevel);
        return s_LevelLoaded;
    }

    void UnloadLevel()
    {
        if (s_LevelLoaded)
        {
            Engine_Level_Unload(&s_GameLevel, false);
            s_LevelLoaded = false;
        }
    }

    void SetStreamingCenter(float x, float y, float z)
    {
        (void)y; // streaming is on the X/Z ground plane
        if (s_LevelLoaded)
            Engine_Level_SetStreamingCenter(x, z);
    }

    // --- Scenes -------------------------------------------------------------
    void SetMainScene(Scene* scene) { Engine_Scene_SetMain(scene); }

    void SwitchScene(Scene* scene) { Engine_Scene_Switch(scene); }

    void ReloadScene() { Engine_Scene_Reload(); }

    bool IsSceneLoading() { return Engine_Scene_IsLoading(); }

    float GetSceneLoadProgress() { return Engine_Scene_GetLoadProgress(); }

    void DrawRect(int x, int y, int width, int height, int r, int g, int b, int a)
    {
        Renderer* renderer = Engine_GetRenderer();
        if (!renderer)
            return;

        Quad2D quad;
        quad.x = x;
        quad.y = y;
        quad.w = width;
        quad.h = height;
        quad.texture = 0;
        quad.u0 = 0;
        quad.v0 = 0;
        quad.u1 = 0;
        quad.v1 = 0;
        quad.r = static_cast<uint8_t>(r);
        quad.g = static_cast<uint8_t>(g);
        quad.b = static_cast<uint8_t>(b);
        quad.a = static_cast<uint8_t>(a);
        renderer->DrawQuad2D(quad);
    }

    int ScreenWidth() { return Ui_ScreenWidth(); }

    int ScreenHeight() { return Ui_ScreenHeight(); }

    void Text(int x, int y, const char* text) { Ui_Text(x, y, Ui_GetStyle().textScale, text, UiColor::Text); }

    int TextWidth(const char* text) { return Ui_TextWidth(Ui_GetStyle().textScale, text); }

    int TextHeight() { return Ui_TextHeight(Ui_GetStyle().textScale); }

    void Panel(const char* title, int x, int y, int width, int height) { Ui_BeginPanel(title, x, y, width, height); }

    void EndPanel() { Ui_EndPanel(); }

    void Label(const char* text) { Ui_Label(text); }

    bool Button(const char* label) { return Ui_Button(label); }

    void Toast(const char* text, float seconds) { Ui_Toast(text, seconds); }

} // namespace game

void Engine_Game_ResetState()
{
    game::UnloadLevel();
    game::SetSpawnHandler(nullptr);
}

bool Engine_Game_DispatchSpawn(const game::EntitySpawn& spawn)
{
    if (!game::s_SpawnHandler)
    {
        Engine_LogError("[Game] no spawn handler registered — cannot spawn '%s'", spawn.classname ? spawn.classname : "(null)");
        return false;
    }
    return game::s_SpawnHandler(spawn);
}
