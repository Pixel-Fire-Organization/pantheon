#include <cstdio>

#include "Platform.h"

#include <cstring>

#include "Macros.h"
#include "core/EngineDebug.h"
#include "graphics/NullRenderer.h"
#include "platform/PlatformRegistry.h"
#include "renderer/OpenGl.h"
#include "renderer/WebGpu.h"

Win32Platform::Win32Platform() : m_startupArgs(), m_memory(this)
{
    memset(&m_window, 0, sizeof(m_window));
    m_window.width = GFX_SCREEN_WIDTH;
    m_window.height = GFX_SCREEN_HEIGHT;
    memset(m_pads, 0, sizeof(m_pads));
    memset(m_padsPrev, 0, sizeof(m_padsPrev));
    memset(&m_keys, 0, sizeof(m_keys));
    memset(&m_keysPrev, 0, sizeof(m_keysPrev));
    memset(&m_mouse, 0, sizeof(m_mouse));
    memset(&m_mousePrev, 0, sizeof(m_mousePrev));
    m_dataRoot[0] = '\0';
    m_dialogResult = DialogStatus::Idle;

    m_startupArgs.commandLine = nullptr;
    m_startupArgs.argv = nullptr;
    m_startupArgs.argc = 0;
}

bool Win32Platform::Init(const StartupArgs& args)
{
    m_startupArgs = args;

    // On by default: the point is that an existing pad-only game just works.
    m_keyboardPadMap = !(args.commandLine && args.commandLine->HasOption("no-keyboard-pad"));
    m_logInput = args.commandLine && args.commandLine->HasOption("log-input");
    m_window.logInput = m_logInput;
    if (m_logInput)
        Engine_LogInfo("%s: --log-input active", GetName());

    if (!ResolveDataRoot())
        return false;

    Engine_LogInfo("%s: data root '%s'", GetName(), m_dataRoot);
    Engine_LogInfo("%s: keyboard->pad map %s", GetName(), m_keyboardPadMap ? "on (--no-keyboard-pad disables)" : "off");
    return true;
}

void Win32Platform::Shutdown()
{
    WindowClose();
    m_memory.Release();
}

const StartupArgs& Win32Platform::GetStartupArgs() const { return m_startupArgs; }

uint32_t Win32Platform::GetConstant(PlatformConstant key) const
{
    switch (key)
    {
    // Window size is live, not the compile-time default: this platform is
    // resizable, so a renderer that caches these must refresh them.
    case PlatformConstant::ScreenWidth:
        return m_window.width;
    case PlatformConstant::ScreenHeight:
        return m_window.height;
    case PlatformConstant::DisplayAspectX:
        return GFX_DISPLAY_ASPECT_X;
    case PlatformConstant::DisplayAspectY:
        return GFX_DISPLAY_ASPECT_Y;
    case PlatformConstant::TargetFrameMicros:
        return PLATFORM_TARGET_FRAME_MICROS;

    case PlatformConstant::MemoryTotalBudget:
        return MEM_LIMIT_TOTAL_BUDGET;
    case PlatformConstant::MemoryArenaConfigSize:
        return MEM_BLOCK_CONFIG_SIZE;
    case PlatformConstant::MemoryArenaConfigSlots:
        return MEM_BLOCK_CONFIG_SLOTS;
    case PlatformConstant::MemoryArenaLevelDataSize:
        return MEM_BLOCK_LEVEL_DATA_SIZE;
    case PlatformConstant::MemoryArenaLevelDataSlots:
        return MEM_BLOCK_LEVEL_DATA_SLOTS;
    case PlatformConstant::MemoryArenaRendererSize:
        return MEM_BLOCK_RENDERER_SIZE;
    case PlatformConstant::MemoryArenaRendererSlots:
        return MEM_BLOCK_RENDERER_SLOTS;
    case PlatformConstant::MemoryArenaSlotAlignment:
        return MEM_ARENA_SLOT_ALIGNMENT;
    case PlatformConstant::MemoryPoolMainSize:
        return MEM_POOL_MAIN_SIZE;
    case PlatformConstant::MemoryPoolChunkSize:
        return MEM_POOL_CHUNK_SIZE;

    case PlatformConstant::TextureBudgetBytes:
        return GFX_TEXTURE_BUDGET_BYTES;
    case PlatformConstant::MaxTextureBytes:
        return GFX_MAX_TEXTURE_WIDTH * GFX_MAX_TEXTURE_HEIGHT * 4u;
    case PlatformConstant::MaxTextureWidth:
        return GFX_MAX_TEXTURE_WIDTH;
    case PlatformConstant::MaxTextureHeight:
        return GFX_MAX_TEXTURE_HEIGHT;

    case PlatformConstant::MaxGamepadPorts:
        return MAX_GAME_PAD_PORTS;

    case PlatformConstant::ButtonIconFamily:
        return static_cast<uint32_t>(UiButtonIconFamily::Xbox);

    case PlatformConstant::Count:
        break;
    }

    // Unreachable: the switch above is exhaustive and has no default, so the
    // compiler refuses a missing key. If it is ever reached, returning a value
    // would be worse than stopping - a wrong budget is a bug that surfaces far
    // from its cause.
    char message[128];
    snprintf(message, sizeof(message), "%s has no value for platform constant '%s' (key %u)", GetName(), Platform_ConstantName(key), static_cast<unsigned>(key));
    Engine_Panic(message);
}

bool Win32Platform::HasCapability(PlatformCapability key) const
{
    switch (key)
    {
    case PlatformCapability::Gamepad: // XInput
    case PlatformCapability::Keyboard:
    case PlatformCapability::Mouse:
    case PlatformCapability::AnalogTriggers: // XInput reports real trigger pressure
    case PlatformCapability::AsyncIo:
    case PlatformCapability::FileWrite:
    case PlatformCapability::ResizableWindow:
        return true;

    case PlatformCapability::Touch:
        return false;

    // MessageBox blocks the calling thread but the Dialog_Open/Dialog_Poll
    // contract hides that: the first poll simply reports the result Open
    // already has. WM_CHAR is a genuine character channel, independent of the
    // keyboard-to-virtual-pad bridge.
    case PlatformCapability::SystemDialog:
    case PlatformCapability::TextCharacters:
        return true;

    case PlatformCapability::Count:
        break;
    }

    // No default above, so a forgotten capability fails to build. Reaching here
    // means the sentinel was queried, which is a caller bug: answering "false"
    // would hide it.
    char message[128];
    snprintf(message, sizeof(message), "%s was asked for platform capability '%s' (key %u)", GetName(), Platform_CapabilityName(key), static_cast<unsigned>(key));
    Engine_Panic(message);
}

// ---------------------------------------------------------------------------
// Renderers
// ---------------------------------------------------------------------------

bool Win32Platform::SupportsRenderer(RendererId id) const { return id == RendererId::WebGpu || id == RendererId::OpenGl || id == RendererId::Null; }

RendererId Win32Platform::GetDefaultRenderer() const { return RendererId::WebGpu; }

RendererId Win32Platform::GetFallbackRenderer(RendererId failed) const
{
    switch (failed)
    {
    case RendererId::WebGpu:
        return RendererId::OpenGl;
    case RendererId::OpenGl:
        return RendererId::Null;
    default:
        return RendererId::Unknown; // nothing left to try
    }
}

Renderer* Win32Platform::CreateRenderer(RendererId id, const EngineConfig& config)
{
    switch (id)
    {
    case RendererId::WebGpu:
        return new WebGpuRenderer(config);
    case RendererId::Null:
        return new NullRenderer();
    case RendererId::OpenGl:
        return new OpenGlRenderer(config);
    default:
        Engine_LogError("%s: renderer id %u is not available on this platform", GetName(), static_cast<unsigned>(id));
        return nullptr;
    }
}

void Win32Platform::DestroyRenderer(Renderer* renderer)
{
    if (!renderer)
        return;
    renderer->Shutdown();
    delete renderer;
}

PLATFORM_DEFINE_BUILTIN(PlatformId::Win32, "win32", Win32Platform)
