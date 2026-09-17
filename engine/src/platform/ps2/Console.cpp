#include <cstdio>

#include "Macros.h"
#include "Platform.h"
#include "core/EngineCore.h"
#include "graphics/Renderer.h"
#include "ui/EngineUi.h"

void Ps2Platform::ConsoleWrite(LogLevel level, const char* line)
{
    if (!line)
        return;

    // High-frequency debug logs are muted: every line crosses the EE<->IOP bus to
    // ps2client, and the bandwidth shows up in frame time.
    if (level == LogLevel::Debug)
        return;

    const char* prefix = "INFO: ";
    if (level == LogLevel::Warning)
        prefix = "WARN: ";
    else if (level == LogLevel::Error)
        prefix = "ERR : ";

    // Scrub anything outside printable ASCII. ps2client/plink can hang the EE on
    // malformed or too-frequent control sequences, so this is a hardware
    // workaround rather than formatting - which is why it lives here and not in
    // shared engine code.
    char scrubbed[LOG_STRING_MAX_SIZE * 4];
    size_t n = 0;
    for (const char* p = line; *p && n < sizeof(scrubbed) - 1; ++p)
    {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c == '\n' || c == '\r' || c == '\t')
            continue;
        scrubbed[n++] = (c < 32 || c > 126) ? '?' : static_cast<char>(c);
    }
    scrubbed[n] = '\0';

    // One trailing newline only - safe for the kernel flush.
    printf("%s%s\n", prefix, scrubbed);
}

[[noreturn]] void Ps2Platform::Panic(const char* message)
{
#ifdef DEBUG
    printf("ERR : !!! PS2 PANIC !!! %s\n", message ? message : "<no message>");

    // Red screen of death, drawn with whichever renderer is live. Both PS2
    // backends implement this BeginFrame/ClearFrame/DrawRect2D/EndFrame path.
    Renderer* renderer = Engine_GetRenderer();
    if (renderer && renderer->IsInitialized())
    {
        while (true)
        {
            renderer->BeginFrame();
            renderer->ClearFrame(Color3{1.0f, 0.0f, 0.0f});

            const UiRgba white = UiRgba{255, 255, 255, 255};
            Engine_DrawPanicText(renderer, PANIC_UI_PADDING, PANIC_UI_PADDING, 2, "PANIC", white);
            Engine_DrawPanicText(renderer, PANIC_UI_PADDING, PANIC_UI_PADDING + 24, 1, message ? message : "<no message>", white);

            renderer->EndFrame();
        }
    }
#else
    UNUSED_VAR(message);
#endif

    // Never returns, with or without a renderer.
    while (true)
        ;
}
