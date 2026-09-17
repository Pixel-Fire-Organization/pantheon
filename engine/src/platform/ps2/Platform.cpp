#include <cstdio>

#include "Platform.h"

#include <cstring>

extern "C" {
#include <kernel.h>
}

#include "PlatformConstants.h"
#include "core/EngineDebug.h"
#include "graphics/NullRenderer.h"
#include "renderer/GifTag.h"
#include "renderer/Ps2Gl.h"

Ps2Platform::Ps2Platform() : m_startupArgs(), m_deviceToken("cdrom0:"), m_memory(this), m_initialised(false)
{
    memset(m_pads, 0, sizeof(m_pads));
    memset(m_padsPrev, 0, sizeof(m_padsPrev));
    m_startupArgs.commandLine = nullptr;
    m_startupArgs.argv = nullptr;
    m_startupArgs.argc = 0;
}

bool Ps2Platform::Init(const StartupArgs& args)
{
    m_startupArgs = args;

    // argv[0] on PS2 is the boot path ("cdrom0:\MAIN.ELF;1", "host:main.elf").
    // The command line keeps it as positional 0, so the device token is derived
    // from there rather than from a separate engine-side parse.
    const char* bootPath = args.commandLine ? args.commandLine->GetPositional(0) : nullptr;
    m_deviceToken = ResolveDeviceToken(bootPath);

    Engine_LogInfo("%s: device token '%s' (from '%s')", GetName(), m_deviceToken, bootPath ? bootPath : "<none>");

    ChangeThreadPriority(GetThreadId(), PLATFORM_MAIN_THREAD_PRIORITY);

    m_initialised = true;
    return true;
}

void Ps2Platform::Shutdown()
{
    ShutdownInput();
    m_memory.Release();
    m_initialised = false;
}

const StartupArgs& Ps2Platform::GetStartupArgs() const { return m_startupArgs; }

uint32_t Ps2Platform::GetConstant(PlatformConstant key) const
{
    switch (key)
    {
    case PlatformConstant::ScreenWidth:
        return GFX_SCREEN_WIDTH;
    case PlatformConstant::ScreenHeight:
        return GFX_SCREEN_HEIGHT;
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

    // GS texture memory is budgeted in pages; report it in bytes so shared
    // code and desktop platforms can speak one unit.
    case PlatformConstant::TextureBudgetBytes:
        return static_cast<uint32_t>(GFX_GS_TEXTURE_PAGE_BUDGET) * GFX_GS_PAGE_WIDTH_PSM32 * GFX_GS_PAGE_HEIGHT_PSM32 * 4u;
    case PlatformConstant::MaxTextureBytes:
        return static_cast<uint32_t>(GFX_MAX_TEXTURE_GS_PAGES) * GFX_GS_PAGE_WIDTH_PSM32 * GFX_GS_PAGE_HEIGHT_PSM32 * 4u;
    case PlatformConstant::MaxTextureWidth:
        return GFX_MAX_TEXTURE_WIDTH;
    case PlatformConstant::MaxTextureHeight:
        return GFX_MAX_TEXTURE_HEIGHT;

    case PlatformConstant::MaxGamepadPorts:
        return MAX_GAME_PAD_PORTS;

    case PlatformConstant::ButtonIconFamily:
        return static_cast<uint32_t>(UiButtonIconFamily::PlayStation);

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

bool Ps2Platform::HasCapability(PlatformCapability key) const
{
    switch (key)
    {
    case PlatformCapability::Gamepad:
        return true;
    case PlatformCapability::AsyncIo:
        return true;

    // No keyboard, no mouse, no analog triggers (the DualShock 2 reports
    // L2/R2 pressure, but the engine does not surface it), and the GS
    // framebuffer size is fixed at build time.
    case PlatformCapability::Keyboard:
    case PlatformCapability::Touch:
    case PlatformCapability::Mouse:
    case PlatformCapability::AnalogTriggers:
    case PlatformCapability::ResizableWindow:
        return false;

    // No keyboard means no character channel, and there is no system dialog
    // service on this hardware: text entry is the interface's own drawn
    // on-screen keyboard, unconditionally.
    case PlatformCapability::SystemDialog:
    case PlatformCapability::TextCharacters:
        return false;

    // Writing to the boot device is not supported: cdrom0 is read-only and
    // no other device is mounted by default.
    case PlatformCapability::FileWrite:
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

bool Ps2Platform::SupportsRenderer(RendererId id) const { return id == RendererId::GifTag || id == RendererId::Ps2Gl || id == RendererId::Null; }

RendererId Ps2Platform::GetDefaultRenderer() const
{
    // GIFTAG is the default: it builds GS packets directly instead of going
    // through ps2gl's VU1 microcode path.
    return RendererId::GifTag;
}

RendererId Ps2Platform::GetFallbackRenderer(RendererId failed) const
{
    switch (failed)
    {
    case RendererId::GifTag:
        return RendererId::Ps2Gl;
    case RendererId::Ps2Gl:
        return RendererId::Null;
    default:
        return RendererId::Unknown; // nothing left to try
    }
}

Renderer* Ps2Platform::CreateRenderer(RendererId id, const EngineConfig& config)
{
    // `new` for the single renderer instance is the documented exception to the
    // engine's no-new rule (its lifetime is the process); the platform owns that
    // exception now that it owns backend construction.
    switch (id)
    {
    case RendererId::GifTag:
        return new GifTagRenderer(config);
    case RendererId::Ps2Gl:
        return new Ps2GlRenderer(config);
    case RendererId::Null:
        return new NullRenderer();
    default:
        Engine_LogError("%s: renderer id %u is not available on this platform", GetName(), static_cast<unsigned>(id));
        return nullptr;
    }
}

void Ps2Platform::DestroyRenderer(Renderer* renderer)
{
    if (!renderer)
        return;
    renderer->Shutdown();
    delete renderer;
}
