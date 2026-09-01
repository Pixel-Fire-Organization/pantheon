#include "platform/PlatformRegistry.h"

#include <cstring>

#include "core/EngineDebug.h"

namespace
{

    struct RegistryEntry
    {
        const char* name;
        PlatformFactory factory;
        PlatformId id;
    };

    // Constant-initialised (all zero) before any dynamic initialiser runs, so a
    // platform registering from static-init time always sees a valid table.
    RegistryEntry s_Entries[PLATFORM_MAX_REGISTERED];
    uint32_t s_Count = 0;

    bool EqualsIgnoreCase(const char* a, const char* b)
    {
        if (!a || !b)
            return false;
        while (*a && *b)
        {
            char ca = *a;
            char cb = *b;
            if (ca >= 'A' && ca <= 'Z')
                ca = static_cast<char>(ca - 'A' + 'a');
            if (cb >= 'A' && cb <= 'Z')
                cb = static_cast<char>(cb - 'A' + 'a');
            if (ca != cb)
                return false;
            ++a;
            ++b;
        }
        return *a == '\0' && *b == '\0';
    }

} // namespace

bool PlatformRegistry::Register(PlatformId id, const char* name, PlatformFactory factory)
{
    if (!name || !factory || id == PlatformId::Unknown)
        return false;

    if (s_Count >= PLATFORM_MAX_REGISTERED)
    {
        Engine_LogError("PlatformRegistry: table full (%d); '%s' was not registered.", PLATFORM_MAX_REGISTERED, name);
        return false;
    }

    for (uint32_t i = 0; i < s_Count; ++i)
    {
        if (s_Entries[i].id == id)
        {
            Engine_LogError("PlatformRegistry: '%s' collides with already-registered '%s'.", name, s_Entries[i].name);
            return false;
        }
    }

    s_Entries[s_Count].id = id;
    s_Entries[s_Count].name = name;
    s_Entries[s_Count].factory = factory;
    ++s_Count;
    return true;
}

Platform* PlatformRegistry::Create(PlatformId id)
{
    for (uint32_t i = 0; i < s_Count; ++i)
    {
        if (s_Entries[i].id == id)
            return s_Entries[i].factory();
    }
    return nullptr;
}

PlatformId PlatformRegistry::FindByName(const char* name)
{
    for (uint32_t i = 0; i < s_Count; ++i)
    {
        if (EqualsIgnoreCase(s_Entries[i].name, name))
            return s_Entries[i].id;
    }
    return PlatformId::Unknown;
}

uint32_t PlatformRegistry::GetCount() { return s_Count; }

PlatformId PlatformRegistry::GetIdAt(uint32_t index) { return (index < s_Count) ? s_Entries[index].id : PlatformId::Unknown; }

const char* PlatformRegistry::GetNameAt(uint32_t index) { return (index < s_Count) ? s_Entries[index].name : nullptr; }

void PlatformRegistry::LogRegistered()
{
    if (s_Count == 0)
    {
        Engine_LogError("PlatformRegistry: no platforms are compiled into this build.");
        return;
    }

    for (uint32_t i = 0; i < s_Count; ++i)
        Engine_LogInfo("PlatformRegistry:   %s", s_Entries[i].name);
}

const char* Platform_ConstantName(PlatformConstant key)
{
    switch (key)
    {
    case PlatformConstant::ScreenWidth:
        return "ScreenWidth";
    case PlatformConstant::ScreenHeight:
        return "ScreenHeight";
    case PlatformConstant::DisplayAspectX:
        return "DisplayAspectX";
    case PlatformConstant::DisplayAspectY:
        return "DisplayAspectY";
    case PlatformConstant::TargetFrameMicros:
        return "TargetFrameMicros";
    case PlatformConstant::MemoryTotalBudget:
        return "MemoryTotalBudget";
    case PlatformConstant::MemoryArenaConfigSize:
        return "MemoryArenaConfigSize";
    case PlatformConstant::MemoryArenaConfigSlots:
        return "MemoryArenaConfigSlots";
    case PlatformConstant::MemoryArenaLevelDataSize:
        return "MemoryArenaLevelDataSize";
    case PlatformConstant::MemoryArenaLevelDataSlots:
        return "MemoryArenaLevelDataSlots";
    case PlatformConstant::MemoryArenaRendererSize:
        return "MemoryArenaRendererSize";
    case PlatformConstant::MemoryArenaRendererSlots:
        return "MemoryArenaRendererSlots";
    case PlatformConstant::MemoryArenaSlotAlignment:
        return "MemoryArenaSlotAlignment";
    case PlatformConstant::MemoryPoolMainSize:
        return "MemoryPoolMainSize";
    case PlatformConstant::MemoryPoolChunkSize:
        return "MemoryPoolChunkSize";
    case PlatformConstant::TextureBudgetBytes:
        return "TextureBudgetBytes";
    case PlatformConstant::MaxTextureBytes:
        return "MaxTextureBytes";
    case PlatformConstant::MaxTextureWidth:
        return "MaxTextureWidth";
    case PlatformConstant::MaxTextureHeight:
        return "MaxTextureHeight";
    case PlatformConstant::MaxGamepadPorts:
        return "MaxGamepadPorts";
    case PlatformConstant::ButtonIconFamily:
        return "ButtonIconFamily";
    case PlatformConstant::Count:
        break;
    }
    return "<unknown>";
}

const char* Platform_GamepadButtonName(GamepadButton button)
{
    switch (button)
    {
    case GamepadButton::Select:
        return "SELECT";
    case GamepadButton::L3:
        return "L3";
    case GamepadButton::R3:
        return "R3";
    case GamepadButton::Start:
        return "START";
    case GamepadButton::DPadUp:
        return "UP";
    case GamepadButton::DPadRight:
        return "RIGHT";
    case GamepadButton::DPadDown:
        return "DOWN";
    case GamepadButton::DPadLeft:
        return "LEFT";
    case GamepadButton::L2:
        return "L2";
    case GamepadButton::R2:
        return "R2";
    case GamepadButton::L1:
        return "L1";
    case GamepadButton::R1:
        return "R1";
    case GamepadButton::Triangle:
        return "TRIANGLE";
    case GamepadButton::Circle:
        return "CIRCLE";
    case GamepadButton::Cross:
        return "CROSS";
    case GamepadButton::Square:
        return "SQUARE";
    case GamepadButton::Unknown:
        break;
    }
    return "?";
}

const char* Platform_CapabilityName(PlatformCapability key)
{
    switch (key)
    {
    case PlatformCapability::Gamepad:
        return "Gamepad";
    case PlatformCapability::Keyboard:
        return "Keyboard";
    case PlatformCapability::Mouse:
        return "Mouse";
    case PlatformCapability::AnalogTriggers:
        return "AnalogTriggers";
    case PlatformCapability::ResizableWindow:
        return "ResizableWindow";
    case PlatformCapability::AsyncIo:
        return "AsyncIo";
    case PlatformCapability::FileWrite:
        return "FileWrite";
    case PlatformCapability::Touch:
        return "Touch";
    case PlatformCapability::SystemDialog:
        return "SystemDialog";
    case PlatformCapability::TextCharacters:
        return "TextCharacters";
    case PlatformCapability::Count:
        break;
    }
    return "<unknown>";
}
