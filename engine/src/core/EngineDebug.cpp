#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include "Engine.h"
#include "core/EngineAction.h"
#include "core/EngineInput.h"
#include "graphics/DrawList.h"
#include "graphics/Renderer.h"
#include "platform/Platform.h"
#include "ui/EngineUi.h"

// Internal log severity levels for CustomLog(). Previously these matched
// raylib's TraceLogLevel by name (CustomLog was installed as its trace
// callback); now that raylib is gone they are just this file's own scheme.
// Values line up with LogLevel in PlatformKeys.h - CustomLog casts across.
enum
{
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR
};

static bool s_DebugOverlayVisible = false;


/// Rewrite %z length modifiers to plain int conversions where the two are
/// equivalent, for C libraries built without C99 format support.
/// @param text The caller's format string.
/// @param scratch Receives the rewritten format.
/// @param scratchSize Capacity of scratch.
/// @return `text` unchanged when no rewrite applies, otherwise `scratch`.
static const char* NormalizeLengthModifiers(const char* text, char* scratch, size_t scratchSize)
{
    if (sizeof(size_t) != sizeof(unsigned int))
        return text;

    size_t out = 0;
    bool inConversion = false;

    for (size_t in = 0; text[in] && out + 1 < scratchSize; ++in)
    {
        const char c = text[in];

        if (!inConversion)
        {
            if (c == '%' && text[in + 1] == '%')
            {
                if (out + 2 >= scratchSize)
                    break;
                scratch[out++] = c;
                scratch[out++] = text[++in];
                continue;
            }
            if (c == '%')
                inConversion = true;
            scratch[out++] = c;
            continue;
        }

        if (c == 'z')
            continue;

        if (strchr("diouxXeEfFgGaAcspn", c))
            inConversion = false;

        scratch[out++] = c;
    }

    scratch[out] = '\0';
    return scratch;
}

static void CustomLog(int logLevel, const char* text, va_list args)
{
    // High-frequency debug logs are completely muted to preserve bus bandwidth
    if (logLevel == LOG_DEBUG)
        return;

    char format[512];
    text = NormalizeLengthModifiers(text, format, sizeof(format));

    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), text, args);
    buffer[sizeof(buffer) - 1] = '\0';

    // Severity prefixing, the sink, and any character scrubbing are the
    // platform's business: the ps2client control-character rule that used to
    // live here would mangle UTF-8 on a desktop console for no reason.
    Platform* platform = Engine_GetPlatform();
    if (platform)
    {
        platform->ConsoleWrite(static_cast<LogLevel>(logLevel), buffer);
        return;
    }

    // Before a platform exists (argv parsing, registry errors) there is nowhere
    // else to go.
    printf("%s\n", buffer);
}

void Engine_InitDebug() { Engine_LogInfo("Engine Debug initialized."); }

void Engine_LogInfo(const char* text, ...)
{
    va_list args;
    va_start(args, text);
    CustomLog(LOG_INFO, text, args);
    va_end(args);
}

void Engine_LogError(const char* text, ...)
{
    va_list args;
    va_start(args, text);
    CustomLog(LOG_ERROR, text, args);
    va_end(args);
}

void Engine_DrawDebugOverlay()
{
    if (!s_DebugOverlayVisible || !Engine_Ui_IsActive())
        return;

    // Drawn as interface, not by a backend: it is interface, and a backend that
    // drew its own would be one more thing that differs between backends.
    const Renderer* renderer = Engine_GetRenderer();
    if (!renderer)
        return;

    const DrawStats stats = renderer->GetLastStats();
    const float dt = Engine_GetDeltaTime();

    Ui_BeginOverlay();
    Ui_BeginPanelSlot("OVERLAY", UiPanelSlot::TopRight);
    Ui_LabelValueFormat("FPS", "%.0f", (dt > 0.0f) ? static_cast<double>(1.0f / dt) : 0.0);
    Ui_LabelValueFormat("FRAME MS", "%.2f", static_cast<double>(dt * 1000.0f));
    Ui_LabelValueFormat("PRIMS", "%u", static_cast<unsigned>(stats.primitiveCount));
    Ui_LabelValueFormat("TRIS", "%u", static_cast<unsigned>(stats.trisSubmitted));
    Ui_LabelValueFormat("UI QUADS", "%u / %u", static_cast<unsigned>(Ui_QuadsUsed()), static_cast<unsigned>(Ui_QuadBudget()));
    Ui_EndPanel();
    Ui_EndOverlay();
}


[[noreturn]] void Engine_Panic(const char* message)
{
    const char* text = message ? message : "<no message>";
    Engine_LogError("!!! PANIC !!! %s", text);

    Platform* platform = Engine_GetPlatform();
    if (platform)
        platform->Panic(text);

    // A panic can precede platform construction, so there is a floor below the
    // platform: say it on whatever is available and stop.
    fprintf(stderr, "ERR : !!! PANIC !!! %s\n", text);
    fflush(stderr);
    abort();
}

// ---------------------------------------------------------------------------
// Performance Logger
// ---------------------------------------------------------------------------

static bool s_PerfLoggerEnabled = false;

const char* Engine_Debug_DescribeChord(DebugChord chord, char* buf, size_t bufSize)
{
    const Platform* platform = Engine_GetPlatform();
    const uint16_t mask = platform ? platform->GetDebugChord(chord) : 0u;
    if (!mask)
    {
        snprintf(buf, bufSize, "unavailable");
        return buf;
    }

    static const GamepadButton kDisplayOrder[] = {GamepadButton::L1,       GamepadButton::L2,     GamepadButton::R1,     GamepadButton::R2,       GamepadButton::L3,       GamepadButton::R3,
                                                  GamepadButton::Select,   GamepadButton::Start,  GamepadButton::DPadUp, GamepadButton::DPadDown, GamepadButton::DPadLeft, GamepadButton::DPadRight,
                                                  GamepadButton::Triangle, GamepadButton::Circle, GamepadButton::Cross,  GamepadButton::Square};

    size_t used = 0;
    buf[0] = '\0';
    for (size_t i = 0; i < sizeof(kDisplayOrder) / sizeof(kDisplayOrder[0]); ++i)
    {
        if ((mask & static_cast<uint16_t>(kDisplayOrder[i])) == 0u)
            continue;
        const int written = snprintf(buf + used, bufSize - used, "%s%s", used ? "+" : "", Platform_GamepadButtonName(kDisplayOrder[i]));
        if (written <= 0 || static_cast<size_t>(written) >= bufSize - used)
            break;
        used += static_cast<size_t>(written);
    }
    return buf;
}

bool Engine_Debug_IsChordHeld(DebugChord chord)
{
    const Platform* platform = Engine_GetPlatform();
    const uint16_t mask = platform ? platform->GetDebugChord(chord) : 0u;
    if (!mask)
        return false;

    for (uint32_t bit = 1u; bit <= 0x8000u; bit <<= 1)
    {
        if ((mask & bit) == 0u)
            continue;
        if (!IsGamePadButtonPressed(0, static_cast<GamepadButton>(bit)))
            return false;
    }
    return true;
}

bool Engine_Debug_WasChordPressed(DebugChord chord)
{
    if (!Engine_Debug_IsChordHeld(chord))
        return false;

    const Platform* platform = Engine_GetPlatform();
    const uint16_t mask = platform ? platform->GetDebugChord(chord) : 0u;
    for (uint32_t bit = 1u; bit <= 0x8000u; bit <<= 1)
    {
        if ((mask & bit) == 0u)
            continue;
        if (WasGamePadButtonPressed(0, static_cast<GamepadButton>(bit)))
            return true;
    }
    return false;
}

void Engine_PerfLogger_Init(bool enabled)
{
    s_PerfLoggerEnabled = enabled;
    if (enabled)
    {
        // The actual buttons are whatever the title bound EngineDebugPerfSnapshot
        // / EngineDebugOverlayToggle to (game/config/actions.json) -- no longer a
        // platform default, so this names the intents rather than describing a
        // fixed chord.
        Engine_LogInfo("[PerfLogger] Initialized. Hold the EngineDebugPerfSnapshot action for a console dump; "
                       "EngineDebugOverlayToggle toggles the on-screen overlay.");
    }
}

void Engine_PerfLogger_Tick()
{
    if (!s_PerfLoggerEnabled)
        return;

    // Heartbeat: one compact line every 50 frames (~1/s at 50fps). After a hang
    // the last heartbeat pins the frame/time of death and shows the heap trend.
    const uint32_t frame = Engine_GetFrameCount();
    if (frame != 0 && (frame % 50u) == 0u)
    {
        size_t heapUsed = 0;
        Engine_GetHeapStats(nullptr, &heapUsed, nullptr);
        Engine_LogInfo("[HB] frame=%u t=%.1fs heap=%zuKB fps=%.1f", frame, Engine_GetTotalTime(), heapUsed / 1024, Engine_GetFPS());
    }

    // The only way PerfLogger checks for input: Engine_Action_WasPressed
    // against the two reserved engine debug actions, never Input or a
    // platform chord directly. See docs/subsystems/ACTION.md.
    if (Engine_Action_WasPressed(Engine_Action_ReservedOverlayToggle()))
    {
        s_DebugOverlayVisible = !s_DebugOverlayVisible;
        Engine_LogInfo("[PerfLogger] Debug overlay visibility: %s", s_DebugOverlayVisible ? "ON" : "OFF");
    }

    if (!Engine_Action_WasPressed(Engine_Action_ReservedPerfSnapshot()))
        return;

    // --- Gather stats ---
    DrawStats ds{};
    Camera3D cam{};
    Renderer* r = Engine_GetRenderer();
    if (r)
    {
        ds = r->GetLastStats();
        cam = r->GetActiveCamera3D();
    }

    const double timeSec = Engine_GetTotalTime();
    const uint32_t frameNum = Engine_GetFrameCount();
    const uint32_t texUsed = Engine_Resource_GetTextureBudgetUsed();
    const uint32_t texBudget = Engine_Resource_GetTextureBudget();

    // --- Print snapshot ---
    Engine_LogInfo("[PERF] ========== PERFORMANCE SNAPSHOT ==========");
    Engine_LogInfo("[PERF] Time since start  : %.3f s", timeSec);
    Engine_LogInfo("[PERF] Frame number      : %u", frameNum);
    Engine_LogInfo("[PERF] Engine FPS        : %.3f", Engine_GetFPS());

    Engine_LogInfo("[PERF] --- Frame Breakdown (ms) ---");
    // Frame budget comes from the platform, not a region #ifdef: PAL and NTSC are
    // separate platforms now, and a desktop platform has its own answer.
    const float targetMs = PLATFORM_TARGET_FRAME_MICROS / 1000.0f;
    Engine_LogInfo("[PERF] Target Budget     : %5.2f ms (%.0f FPS)", targetMs, 1000.0f / targetMs);

    float logicMs = Engine_GetLogicTime() * 1000.0f;
    float renderMs = Engine_GetRenderTime() * 1000.0f;
    float waitMs = Engine_GetWaitTime() * 1000.0f;
    float totalMs = logicMs + renderMs + waitMs;

    Engine_LogInfo("[PERF] Game Logic        : %5.2f ms", logicMs);
    Engine_LogInfo("[PERF] C++ Render        : %5.2f ms", renderMs);
    Engine_LogInfo("[PERF] GPU Wait (Vsync)  : %5.2f ms", waitMs);
    Engine_LogInfo("[PERF] Present Wait      : %5.2f ms", ds.presentWaitMs);
    if (ds.geometryBuildMs > 0.0f || ds.geometryUploadMs > 0.0f)
    {
        Engine_LogInfo("[PERF]  +- Geometry build  : %5.2f ms", ds.geometryBuildMs);
        Engine_LogInfo("[PERF]  +- Geometry upload : %5.2f ms", ds.geometryUploadMs);
    }
    Engine_LogInfo("[PERF] Total Frame Time  : %5.2f ms", totalMs);

    Engine_LogInfo("[PERF] --- Renderer Throughput (measured) ---");
    Engine_LogInfo("[PERF] Tris submitted    : %u", ds.trisSubmitted);
    Engine_LogInfo("[PERF] Tris culled       : %u", ds.trisCulled);
    Engine_LogInfo("[PERF] Verts transformed : %u", ds.vertsTransformed);
    Engine_LogInfo("[PERF] Texture binds     : %u", ds.texBinds);
    if (ds.submitBufferCapacityBytes > 0)
    {
        const float pct = (100.0f * ds.submitBufferUsedBytes) / ds.submitBufferCapacityBytes;
        Engine_LogInfo("[PERF] Submit buffer     : %.1f / %.1f KB (%.0f%%)", ds.submitBufferUsedBytes / 1024.0f, ds.submitBufferCapacityBytes / 1024.0f, pct);
    }

    Engine_LogInfo("[PERF] --- Draw Lists ---");
    Engine_LogInfo("[PERF] Primitives        : %u / %d", ds.primitiveCount, GFX_MAX_DRAW_LIST_LENGTH);
    Engine_LogInfo("[PERF] Models            : %u / %d", ds.modelCount, GFX_MAX_DRAW_LIST_LENGTH);
    Engine_LogInfo("[PERF] Entries culled    : %u", ds.entriesCulled);

    Engine_LogInfo("[PERF] --- Camera State ---");
    Engine_LogInfo("[PERF] Pos               : (%.2f, %.2f, %.2f)", cam.position.x, cam.position.y, cam.position.z);
    Engine_LogInfo("[PERF] Target            : (%.2f, %.2f, %.2f)", cam.target.x, cam.target.y, cam.target.z);
    Engine_LogInfo("[PERF] FOV               : %.2f", cam.fovy);

    Engine_LogInfo("[PERF] --- Texture Memory ---");
    Engine_LogInfo("[PERF] Used              : %u / %u KB (%u%% full)", texUsed / 1024u, texBudget / 1024u, texBudget ? (texUsed * 100u / texBudget) : 0u);

    Engine_LogInfo("[PERF] --- Memory Management ---");
    size_t arenaTotalUsed = 0;
    size_t arenaTotalCap = 0;

    // 1. System heap, as the platform reports it
    size_t heapTotal = 0, heapUsed = 0, heapFree = 0;
    Engine_GetHeapStats(&heapTotal, &heapUsed, &heapFree);

    Engine_LogInfo("[PERF] System Heap (malloc) : %zu / %zu KB", heapUsed / 1024, heapTotal / 1024);

    // 2. Arenas (Sub-allocated from Heap)
    const char* arenaNames[] = {"Config", "LevelData", "Renderer"};
    for (int i = 0; i < (int)ARENA_COUNT; ++i)
    {
        size_t cap = 0, used = 0;
        Engine_GetArenaStats((ArenaType)i, &cap, &used);
        arenaTotalUsed += used;
        arenaTotalCap += cap;
    }

    Engine_LogInfo("[PERF]  +- Engine Arenas    : %zu KB (Reserved Total)", arenaTotalCap / 1024);
    for (int i = 0; i < (int)ARENA_COUNT; ++i)
    {
        size_t cap = 0, used = 0;
        Engine_GetArenaStats((ArenaType)i, &cap, &used);
        Engine_LogInfo("[PERF]  |   - %-10s : %zu / %zu KB (%zu%%)", arenaNames[i], used / 1024, cap / 1024, cap ? (used * 100 / cap) : 0);
    }

    // 3. Main Pool (Sub-allocated from Heap)
    size_t poolCap = 0, poolUsed = 0;
    Engine_GetPoolStatsMain(&poolCap, &poolUsed);
    Engine_LogInfo("[PERF]  +- Main Pool        : %zu / %zu KB (%zu%%)", poolUsed / 1024, poolCap / 1024, poolCap ? (poolUsed * 100 / poolCap) : 0);

    size_t miscUsed = heapUsed - arenaTotalCap - poolCap;
    Engine_LogInfo("[PERF]  +- Misc (RL/IO)     : %zu KB", miscUsed / 1024);

    Engine_LogInfo("[PERF] -------------------------");
    Engine_LogInfo("[PERF] Total EE RAM Used    : %zu / %zu KB (%zu%%)", heapUsed / 1024, heapTotal / 1024, heapTotal ? (heapUsed * 100 / heapTotal) : 0);

    Engine_LogInfo("[PERF] --- Controllers ---");
    for (uint8_t p = 0; p < MAX_GAME_PAD_PORTS; ++p)
    {
        if (IsGamePadInitialized(p))
            Engine_LogInfo("[PERF] Port %u            : CONNECTED", p);
    }

    Engine_LogInfo("[PERF] --- System ---");
    {
        const Platform* platform = Engine_GetPlatform();
        Engine_LogInfo("[PERF] Platform         : %s", platform ? platform->GetName() : "<none>");
    }

    Engine_LogInfo("[PERF] =============================================");
}
