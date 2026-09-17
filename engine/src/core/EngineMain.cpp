#include "core/EngineMain.h"

#include "GameAPI.h"
#include "core/CommandLine.h"
#include "core/EngineApp.h"
#include "core/EngineCore.h"
#include "core/EngineDebug.h"
#include "core/EngineNotice.h"
#include "core/EngineSubsystems.h"
#include "debug/EngineTestbed.h"
#include "graphics/Renderer.h"
#include "platform/Platform.h"
#include "platform/PlatformRegistry.h"

namespace
{

    // A single build ships exactly one platform, so --platform is a validation
    // and self-identification flag rather than a real switch. It is parsed anyway:
    // the error path has to exist, and a future multi-platform bundle uses it
    // unchanged.
    const CommandLineHelpEntry kHelp[] = {
        {"platform", "<name>", "Platform to run as; this build contains one (see below)"},
        {"renderer", "<name>", "Renderer backend to use; falls back if it cannot start"},
        {"help", nullptr, "Print this message and exit"},
    };

    // Every renderer the engine knows how to name. Which of them a given build
    // can actually construct is the platform's answer, not this table's.
    struct RendererName
    {
        RendererId id;
        const char* name;
    };

    const RendererName kAllRenderers[] = {
        {RendererId::GifTag, "giftag"}, {RendererId::Ps2Gl, "ps2gl"}, {RendererId::OpenGl, "opengl"}, {RendererId::WebGpu, "webgpu"}, {RendererId::Gxm, "gxm"},
        {RendererId::VitaGl, "vitagl"}, {RendererId::Gu, "gu"},       {RendererId::PspGl, "pspgl"},   {RendererId::Deko3d, "deko3d"}, {RendererId::Null, "null"},
    };

    const uint32_t kRendererCount = sizeof(kAllRenderers) / sizeof(kAllRenderers[0]);

    const char* RendererName_Of(RendererId id)
    {
        for (uint32_t i = 0; i < kRendererCount; ++i)
        {
            if (kAllRenderers[i].id == id)
                return kAllRenderers[i].name;
        }
        return "<unknown>";
    }

    void LogAvailableRenderers(const Platform& platform)
    {
        Engine_LogInfo("Renderers available on this build:");
        for (uint32_t i = 0; i < kRendererCount; ++i)
        {
            if (platform.SupportsRenderer(kAllRenderers[i].id))
                Engine_LogInfo("  %s%s", kAllRenderers[i].name, kAllRenderers[i].id == platform.GetDefaultRenderer() ? " (default)" : "");
        }
    }

    RendererId ParseRendererId(const CommandLine& cmd, const Platform& platform)
    {
        if (!cmd.HasOption("renderer"))
            return platform.GetDefaultRenderer();

        // Build the accepted-value table from what THIS platform supports, so the
        // error message never suggests a backend this binary cannot construct.
        CommandLineEnumEntry supported[kRendererCount];
        uint32_t count = 0;
        for (uint32_t i = 0; i < kRendererCount; ++i)
        {
            if (!platform.SupportsRenderer(kAllRenderers[i].id))
                continue;
            supported[count].name = kAllRenderers[i].name;
            supported[count].value = static_cast<int32_t>(kAllRenderers[i].id);
            ++count;
        }

        return static_cast<RendererId>(cmd.GetEnum("renderer", supported, count, static_cast<int32_t>(platform.GetDefaultRenderer())));
    }

    // Walk the platform's degradation chain until a backend starts or we run out.
    Renderer* CreateRendererWithFallback(Platform& platform, RendererId first, const EngineConfig& config)
    {
        RendererId id = first;
        while (id != RendererId::Unknown)
        {
            Renderer* renderer = platform.CreateRenderer(id, config);
            if (renderer && renderer->IsInitialized())
            {
                if (id != first)
                    Engine_LogInfo("Renderer '%s' started after '%s' failed.", RendererName_Of(id), RendererName_Of(first));
                return renderer;
            }

            // Say which backend failed and how. A bare "falling back" leaves the
            // reason in whatever the backend logged, unattributed - and reaching
            // the null backend silently looks like a working engine drawing
            // nothing.
            if (renderer)
            {
                Engine_LogError("Renderer '%s' was constructed but failed to initialise.", RendererName_Of(id));
                platform.DestroyRenderer(renderer);
            }
            else
            {
                Engine_LogError("Renderer '%s' could not be constructed.", RendererName_Of(id));
            }

            const RendererId next = platform.GetFallbackRenderer(id);
            if (next == RendererId::Unknown)
            {
                Engine_LogError("No fallback renderer left after '%s'; the engine has no renderer.", RendererName_Of(id));
                break;
            }

            Engine_LogError("Falling back from '%s' to '%s'.", RendererName_Of(id), RendererName_Of(next));
            id = next;
        }
        return nullptr;
    }

} // namespace

int Engine_Main(int argc, char** argv)
{
    CommandLine commandLine;
    commandLine.Parse(argc, argv);

    StartupArgs args;
    args.commandLine = &commandLine;
    args.argv = argv;
    args.argc = argc;

    // Exactly one platform is compiled into this binary (CMake decides which).
    // Calling it directly - rather than looking it up in the registry - is what
    // keeps the linker from dropping it out of the static library.
    Platform* platform = Platform_CreateBuiltin();
    if (!platform)
    {
        Engine_LogError("No platform is compiled into this build.");
        return -1;
    }

    if (commandLine.HasOption("platform"))
    {
        const PlatformId requested = PlatformRegistry::FindByName(commandLine.GetString("platform", ""));
        if (requested != platform->GetId())
        {
            Engine_LogError("This build contains: %s", platform->GetName());
            PlatformRegistry::LogRegistered();
        }
    }

    if (commandLine.HasOption("help"))
    {
        commandLine.PrintHelp(kHelp, sizeof(kHelp) / sizeof(kHelp[0]));
        PlatformRegistry::LogRegistered();
        LogAvailableRenderers(*platform);
        return 0;
    }

    Engine_SetPlatform(platform);
    if (!platform->Init(args))
    {
        Engine_LogError("Platform '%s' failed to initialise.", platform->GetName());
        return -1;
    }

    EngineConfig config;
    config.windowTitle = "PS2 Engine";
    config.resourceLocationToken = platform->GetResourceToken();
    config.subsystems = nullptr;
    config.subsystemCount = 0;

    // Before memory, before the renderer: the game says what it needs, and the
    // engine brings up exactly that.
    GameConfigure(&config);
    if (!config.subsystems || config.subsystemCount == 0)
        config.subsystems = Engine_Subsystems_Default(&config.subsystemCount);
    Engine_Subsystems_Set(config.subsystems, config.subsystemCount);
    Engine_Testbed_RequestSubsystems();
    Engine_Notice_RequestSubsystems(platform);

    WindowDesc window;
    window.title = config.windowTitle;
    window.width = platform->GetConstant(PlatformConstant::ScreenWidth);
    window.height = platform->GetConstant(PlatformConstant::ScreenHeight);
    window.resizable = platform->HasCapability(PlatformCapability::ResizableWindow);
    window.vsync = true;
    if (!platform->WindowOpen(window))
    {
        Engine_LogError("Platform '%s' could not open a window.", platform->GetName());
        return -1;
    }

    // Memory BEFORE the renderer: every backend takes its geometry staging
    // buffer from ARENA_RENDERER slot 0 in its constructor, so a renderer built
    // first would silently get a null arena and fail to initialise.
    if (!Engine_InitMemory(platform))
    {
        Engine_LogError("Engine memory could not be reserved on '%s'.", platform->GetName());
        return -1;
    }

    Renderer* renderer = CreateRendererWithFallback(*platform, ParseRendererId(commandLine, *platform), config);
    if (!renderer)
    {
        Engine_LogError("No renderer could be started on '%s'.", platform->GetName());
        return -1;
    }

    Engine_LogInfo("Engine starting: platform=%s, built %s %s", platform->GetName(), __DATE__, __TIME__);

    if (!EngineStart(config, platform, renderer))
        return -1;

    while (!EngineExited() && !platform->WindowShouldClose())
        EngineUpdate();

    EngineStop();
    platform->WindowClose();
    platform->Shutdown();
    return 0;
}
