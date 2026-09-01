#include <cstdio>
#include <cstring>

#include "Platform.h"

#include "core/EngineDebug.h"
#include "Macros.h"
#include "graphics/NullRenderer.h"
#include "platform/PlatformRegistry.h"
#include "renderer/Gxm.h"
#include "renderer/VitaGl.h"

extern "C" {
#include <psp2/io/stat.h>
}

#ifndef VITA_TITLE_ID_STR
#define VITA_TITLE_ID_STR "PSEN00001"
#endif

VitaPlatform::VitaPlatform() : m_startupArgs(), m_logInput(false), m_padReported(false), m_resourceToken("app0:"), m_memory(this), m_initialised(false)
{
    memset(m_pads, 0, sizeof(m_pads));
    memset(m_padsPrev, 0, sizeof(m_padsPrev));
    memset(m_touch, 0, sizeof(m_touch));
    m_writableRoot[0] = '\0';

    m_dialogKind = DialogKind::Count;
    m_dialogBody[0] = '\0';
    memset(&m_msgUserParam, 0, sizeof(m_msgUserParam));
    memset(m_imeTitle, 0, sizeof(m_imeTitle));
    memset(m_imeInitial, 0, sizeof(m_imeInitial));
    memset(m_imeInput, 0, sizeof(m_imeInput));
    m_dialogResultBuffer = nullptr;
    m_dialogResultBufferSize = 0;

    m_startupArgs.commandLine = nullptr;
    m_startupArgs.argv = nullptr;
    m_startupArgs.argc = 0;
}

bool VitaPlatform::Init(const StartupArgs& args)
{
    m_startupArgs = args;
    m_logInput = args.commandLine && args.commandLine->HasOption("log-input");

    snprintf(m_writableRoot, sizeof(m_writableRoot), "ux0:data/%s/", VITA_TITLE_ID_STR);

    char dir[64];
    snprintf(dir, sizeof(dir), "ux0:data/%s", VITA_TITLE_ID_STR);
    sceIoMkdir(dir, 0777);

    Engine_LogInfo("%s: assets '%s', writable '%s'", GetName(), m_resourceToken, m_writableRoot);
    Engine_LogInfo("%s: log at %sengine.log", GetName(), m_writableRoot);
    if (m_logInput)
        Engine_LogInfo("%s: --log-input active", GetName());
    m_initialised = true;
    return true;
}

void VitaPlatform::Shutdown()
{
    Engine_LogInfo("%s: shutting down", GetName());
    WindowClose();
    m_memory.Release();
    m_initialised = false;
    CloseLog();
}

const StartupArgs& VitaPlatform::GetStartupArgs() const { return m_startupArgs; }

uint32_t VitaPlatform::GetConstant(PlatformConstant key) const
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

bool VitaPlatform::HasCapability(PlatformCapability key) const
{
    switch (key)
    {
    case PlatformCapability::Gamepad:
    case PlatformCapability::AsyncIo:
        return true;

    case PlatformCapability::FileWrite:
        return true;

    case PlatformCapability::Keyboard:
    case PlatformCapability::Mouse:
        return false;

    case PlatformCapability::AnalogTriggers:
        return false;

    case PlatformCapability::ResizableWindow:
        return false;

    case PlatformCapability::Touch:
        return HasTouchSurfaces();

    // sceMsgDialog and sceImeDialog cover every DialogKind this contract has,
    // through the same sceCommonDialog service the trophy setup dialog already
    // drives. No character channel: typed text only ever arrives through the
    // IME dialog, not key by key.
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

bool VitaPlatform::SupportsRenderer(RendererId id) const { return id == RendererId::Gxm || id == RendererId::VitaGl || id == RendererId::Null; }

RendererId VitaPlatform::GetDefaultRenderer() const { return RendererId::Gxm; }

RendererId VitaPlatform::GetFallbackRenderer(RendererId failed) const
{
    switch (failed)
    {
    case RendererId::Gxm:
        return RendererId::VitaGl;
    case RendererId::VitaGl:
        return RendererId::Null;
    default:
        return RendererId::Unknown;
    }
}

Renderer* VitaPlatform::CreateRenderer(RendererId id, const EngineConfig& config)
{
    switch (id)
    {
    case RendererId::Gxm:
        return new GxmRenderer(config);
    case RendererId::VitaGl:
        return new VitaGlRenderer(config);
    case RendererId::Null:
        return new NullRenderer();
    default:
        Engine_LogError("%s: renderer id %u is not available on this platform", GetName(), static_cast<unsigned>(id));
        return nullptr;
    }
}

void VitaPlatform::DestroyRenderer(Renderer* renderer)
{
    if (!renderer)
        return;
    renderer->Shutdown();
    delete renderer;
}
