#include <cstdio>
#include <cstring>

#include "Platform.h"

#include "core/EngineDebug.h"
#include "graphics/NullRenderer.h"
#include "platform/PlatformRegistry.h"
#include "renderer/Gu.h"
#include "renderer/PspGl.h"

PspPlatform::PspPlatform()
    : m_startupArgs()
    , m_logInput(false)
    , m_padReported(false)
    , m_initialised(false)
    , m_resourceToken("")
    , m_dialogKind(DialogKind::Count)
    , m_dialogResultBuffer(nullptr)
    , m_dialogResultBufferSize(0)
    , m_memory(this)
{
    memset(m_pads, 0, sizeof(m_pads));
    memset(m_padsPrev, 0, sizeof(m_padsPrev));
    m_resourceRoot[0] = '\0';
    m_writableRoot[0] = '\0';

    m_startupArgs.commandLine = nullptr;
    m_startupArgs.argv = nullptr;
    m_startupArgs.argc = 0;
}

bool PspPlatform::Init(const StartupArgs& args)
{
    m_startupArgs = args;
    m_logInput = args.commandLine && args.commandLine->HasOption("log-input");

    ResolveDeviceToken((args.argc > 0 && args.argv) ? args.argv[0] : nullptr);

    Engine_LogInfo("%s: assets '%s'", GetName(), m_resourceToken);
    if (m_writableRoot[0] != '\0')
        Engine_LogInfo("%s: writable '%s', log at %sengine.log", GetName(), m_writableRoot, m_writableRoot);
    else
        Engine_LogInfo("%s: no writable storage on this launch", GetName());
    if (m_logInput)
        Engine_LogInfo("%s: --log-input active", GetName());

    m_initialised = true;
    return true;
}

void PspPlatform::Shutdown()
{
    Engine_LogInfo("%s: shutting down", GetName());
    WindowClose();
    m_memory.Release();
    m_initialised = false;
    CloseLog();
}

const StartupArgs& PspPlatform::GetStartupArgs() const { return m_startupArgs; }

uint32_t PspPlatform::GetConstant(PlatformConstant key) const
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
        return static_cast<uint32_t>(UiButtonIconFamily::PlayStation);

    case PlatformConstant::Count:
        break;
    }

    char message[128];
    snprintf(message, sizeof(message), "%s has no value for platform constant '%s' (key %u)", GetName(), Platform_ConstantName(key), static_cast<unsigned>(key));
    Engine_Panic(message);
}

bool PspPlatform::HasCapability(PlatformCapability key) const
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
        return false;
    case PlatformCapability::AsyncIo:
        return true;
    case PlatformCapability::FileWrite:
        return m_writableRoot[0] != '\0';
    case PlatformCapability::Touch:
        return false;
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

uint16_t PspPlatform::GetDebugChord(DebugChord chord) const
{
    switch (chord)
    {
    case DebugChord::PerfSnapshot:
        return static_cast<uint16_t>(GamepadButton::L1) | static_cast<uint16_t>(GamepadButton::R1) | static_cast<uint16_t>(GamepadButton::Select);
    case DebugChord::OverlayToggle:
        return static_cast<uint16_t>(GamepadButton::L1) | static_cast<uint16_t>(GamepadButton::R1) | static_cast<uint16_t>(GamepadButton::Start);
    case DebugChord::DebugMenu:
        return static_cast<uint16_t>(GamepadButton::Select) | static_cast<uint16_t>(GamepadButton::Start);
    case DebugChord::Count:
        break;
    }
    return 0;
}

bool PspPlatform::SupportsRenderer(RendererId id) const
{
    return id == RendererId::Gu || id == RendererId::PspGl || id == RendererId::Null;
}

RendererId PspPlatform::GetDefaultRenderer() const { return RendererId::Gu; }

RendererId PspPlatform::GetFallbackRenderer(RendererId failed) const
{
    switch (failed)
    {
    case RendererId::Gu:
        return RendererId::PspGl;
    case RendererId::PspGl:
        return RendererId::Null;
    default:
        return RendererId::Unknown;
    }
}

Renderer* PspPlatform::CreateRenderer(RendererId id, const EngineConfig& config)
{
    switch (id)
    {
    case RendererId::Gu:
        return new GuRenderer(config);
    case RendererId::PspGl:
        return new PspGlRenderer(config);
    case RendererId::Null:
        return new NullRenderer();
    default:
        Engine_LogError("%s: renderer id %u is not available on this platform", GetName(), static_cast<unsigned>(id));
        return nullptr;
    }
}

void PspPlatform::DestroyRenderer(Renderer* renderer)
{
    if (!renderer)
        return;
    renderer->Shutdown();
    delete renderer;
}

PLATFORM_DEFINE_BUILTIN(PlatformId::Psp, "psp", PspPlatform)
