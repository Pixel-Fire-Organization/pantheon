#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <execinfo.h>

#include "Macros.h"
#include "Platform.h"
#include "platform/macos/PanicAlert.h"

void MacosPlatform::ConsoleWrite(LogLevel level, const char* line)
{
    if (!line)
        return;

    if (level == LogLevel::Debug)
        return;

    const char* prefix = "INFO: ";
    if (level == LogLevel::Warning)
        prefix = "WARN: ";
    else if (level == LogLevel::Error)
        prefix = "ERR : ";

    char scrubbed[LOG_STRING_MAX_SIZE * 4];
    size_t n = 0;
    for (const char* q = line; *q && n < sizeof(scrubbed) - 1; ++q)
    {
        const unsigned char c = static_cast<unsigned char>(*q);
        scrubbed[n++] = (c < 32 || c > 126) ? '?' : static_cast<char>(c);
    }
    scrubbed[n] = 0;

    printf("%s%s\n", prefix, scrubbed);
    fflush(stdout);
}

namespace
{

#ifdef DEBUG
    void CaptureStackText(char* out, size_t size)
    {
        out[0] = 0;
        void* frames[32];
        const int count = backtrace(frames, 32);
        if (count <= 1)
        {
            snprintf(out, size, "  <no frames captured>");
            return;
        }

        char** symbols = backtrace_symbols(frames, count);
        if (!symbols)
        {
            snprintf(out, size, "  <symbols unavailable>");
            return;
        }

        size_t used = 0;
        for (int i = 1; i < count; ++i)
        {
            const size_t len = strlen(symbols[i]);
            if (used + len + 4 >= size)
                break;
            used += static_cast<size_t>(snprintf(out + used, size - used, "  %s\n", symbols[i]));
        }
        free(symbols);
    }
#endif

} // namespace

[[noreturn]] void MacosPlatform::Panic(const char* message)
{
    const char* text = message ? message : "<no message>";

    printf("ERR : !!! PANIC !!! %s\n", text);
    fflush(stdout);

#ifdef DEBUG
    char body[4096];
    char stack[3072];
    CaptureStackText(stack, sizeof(stack));
    snprintf(body, sizeof(body), "%s\n\nStack:\n%s", text, stack);
    Macos_ShowPanicAlert("Engine panic (debug)", body);
#else
    char body[1024];
    snprintf(body, sizeof(body), "The game stopped because of an internal error.\n\n%s", text);
    Macos_ShowPanicAlert("Engine panic", body);
#endif

    abort();
}
