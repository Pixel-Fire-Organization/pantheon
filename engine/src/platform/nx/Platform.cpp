#include <cstdio>
#include <cstring>

#include "Platform.h"

#include "core/EngineDebug.h"
#include "graphics/NullRenderer.h"
#include "platform/PlatformRegistry.h"
#include "TitleInfo.h"
#include "renderer/Deko3d.h"
#include "renderer/OpenGl.h"

#include <switch.h>

NxPlatform::NxPlatform()
    : m_touchCount(0)
    , m_startupArgs()
    , m_resourceToken("")
    , m_tickOrigin(armGetSystemTick())
    , m_suspendedTicks(0)
    , m_suspendStartTick(0)
    , m_framebufferWidth(GFX_SCREEN_WIDTH)
    , m_framebufferHeight(GFX_SCREEN_HEIGHT)
    , m_dialogResult(DialogStatus::Idle)
    , m_suspended(false)
    , m_docked(false)
    , m_exitRequested(false)
    , m_romfsMounted(false)
    , m_sdMounted(false)
    , m_exitLocked(false)
    , m_logInput(false)
    , m_initialised(false)
    , m_memory(this)
{
    memset(m_pads, 0, sizeof(m_pads));
    memset(m_padsPrev, 0, sizeof(m_padsPrev));
    memset(m_touches, 0, sizeof(m_touches));
    m_writableRoot[0] = '\0';

    m_startupArgs.commandLine = nullptr;
    m_startupArgs.argv = nullptr;
    m_startupArgs.argc = 0;
}

bool NxPlatform::Init(const StartupArgs& args)
{
    m_startupArgs = args;
    m_logInput = args.commandLine && args.commandLine->HasOption("log-input");

    InitApplet();

    const AppletType appletType = appletGetAppletType();
    if (appletType != AppletType_Application && appletType != AppletType_SystemApplication)
        Engine_LogInfo("%s: launched as an applet (type %d); the process memory allowance is the small one", GetName(), static_cast<int>(appletType));

    const Result romfs = romfsInit();
    if (R_FAILED(romfs))
    {
        Engine_LogError("%s: the executable carries no file system (romfsInit 0x%X); package it as an .nro with its assets", GetName(), static_cast<unsigned>(romfs));
        return false;
    }
    m_romfsMounted = true;
    m_resourceToken = "romfs:";

    const int rootWritten = snprintf(m_writableRoot, sizeof(m_writableRoot), "sdmc:/switch/%s/", TITLE_NAME);
    if (rootWritten < 0 || static_cast<size_t>(rootWritten) >= sizeof(m_writableRoot))
        m_writableRoot[0] = '\0';
    m_sdMounted = m_writableRoot[0] != '\0' && EnsureWritableRoot();

    InitInput();

    Engine_LogInfo("%s: assets '%s', %s", GetName(), m_resourceToken, m_docked ? "docked" : "handheld");
    if (m_sdMounted)
        Engine_LogInfo("%s: writable '%s', log at %sengine.log", GetName(), m_writableRoot, m_writableRoot);
    else
        Engine_LogInfo("%s: SD card not writable; no log file and no saves", GetName());
    if (m_logInput)
        Engine_LogInfo("%s: --log-input active", GetName());

    m_initialised = true;
    return true;
}

void NxPlatform::Shutdown()
{
    Engine_LogInfo("%s: shutting down", GetName());
    WindowClose();
    m_memory.Release();
    if (m_romfsMounted)
        romfsExit();
    m_romfsMounted = false;
    m_initialised = false;
    CloseLog();
    ShutdownApplet();
}

const StartupArgs& NxPlatform::GetStartupArgs() const { return m_startupArgs; }

uint32_t NxPlatform::GetConstant(PlatformConstant key) const
{
    switch (key)
    {
    case PlatformConstant::ScreenWidth:
        return m_framebufferWidth;
    case PlatformConstant::ScreenHeight:
        return m_framebufferHeight;
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
        return static_cast<uint32_t>(UiButtonIconFamily::Nintendo);

    case PlatformConstant::Count:
        break;
    }

    char message[128];
    snprintf(message, sizeof(message), "%s has no value for platform constant '%s' (key %u)", GetName(), Platform_ConstantName(key), static_cast<unsigned>(key));
    Engine_Panic(message);
}

bool NxPlatform::HasCapability(PlatformCapability key) const
{
    switch (key)
    {
    case PlatformCapability::Gamepad:
        return true;
    case PlatformCapability::Keyboard:
        return false;
    case PlatformCapability::Mouse:
        return false;
    case PlatformCapability::AnalogTriggers:
        return false;
    case PlatformCapability::ResizableWindow:
        return true;
    case PlatformCapability::AsyncIo:
        return true;
    case PlatformCapability::FileWrite:
        return m_sdMounted;
    case PlatformCapability::Touch:
        return !m_docked;
    case PlatformCapability::SystemDialog:
        return true;
    case PlatformCapability::TextCharacters:
        return false;

    case PlatformCapability::Count:
        break;
    }

    char message[128];
    snprintf(message, sizeof(message), "%s was asked for platform capability '%s' (key %u)", GetName(), Platform_CapabilityName(key), static_cast<unsigned>(key));
    Engine_Panic(message);
}

uint16_t NxPlatform::GetDebugChord(DebugChord chord) const
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

bool NxPlatform::SupportsRenderer(RendererId id) const
{
    return id == RendererId::Deko3d || id == RendererId::OpenGl || id == RendererId::Null;
}

RendererId NxPlatform::GetDefaultRenderer() const { return RendererId::Deko3d; }

RendererId NxPlatform::GetFallbackRenderer(RendererId failed) const
{
    switch (failed)
    {
    case RendererId::Deko3d:
        return RendererId::OpenGl;
    case RendererId::OpenGl:
        return RendererId::Null;
    default:
        return RendererId::Unknown;
    }
}

Renderer* NxPlatform::CreateRenderer(RendererId id, const EngineConfig& config)
{
    switch (id)
    {
    case RendererId::Deko3d:
        return new Deko3dRenderer(config);
    case RendererId::OpenGl:
        return new OpenGlRenderer(config);
    case RendererId::Null:
        return new NullRenderer();
    default:
        Engine_LogError("%s: renderer id %u is not available on this platform", GetName(), static_cast<unsigned>(id));
        return nullptr;
    }
}

void NxPlatform::DestroyRenderer(Renderer* renderer)
{
    if (!renderer)
        return;
    renderer->Shutdown();
    delete renderer;
}

PLATFORM_DEFINE_BUILTIN(PlatformId::Nx, "nx", NxPlatform)
