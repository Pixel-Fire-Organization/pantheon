#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Platform.h"
#include "core/EngineIO.h"

#include <switch.h>

namespace
{
    FILE* s_LogFile = nullptr;
    bool s_LogTried = false;

    const char* PrefixFor(LogLevel level)
    {
        if (level == LogLevel::Debug)
            return "DBG : ";
        if (level == LogLevel::Warning)
            return "WARN: ";
        if (level == LogLevel::Error)
            return "ERR : ";
        return "INFO: ";
    }

    FILE* LogFile(const char* writableRoot, bool writable)
    {
        if (s_LogTried)
            return s_LogFile;

        if (!writable || !writableRoot || !writableRoot[0])
            return nullptr;

        s_LogTried = true;

        char path[IO_FILE_MAX_PATH];
        if (snprintf(path, sizeof(path), "%sengine.log", writableRoot) >= static_cast<int>(sizeof(path)))
            return nullptr;

        s_LogFile = fopen(path, "w");
        return s_LogFile;
    }
} // namespace

void NxPlatform::ConsoleWrite(LogLevel level, const char* line)
{
    if (!line)
        return;

    const char* prefix = PrefixFor(level);

    char text[LOG_STRING_MAX_SIZE];
    const int written = snprintf(text, sizeof(text), "%s%s", prefix, line);
    if (written > 0)
    {
        const size_t length = (static_cast<size_t>(written) < sizeof(text)) ? static_cast<size_t>(written) : sizeof(text) - 1u;
        svcOutputDebugString(text, length);
    }

    FILE* file = LogFile(m_writableRoot, m_sdMounted);
    if (!file)
        return;

    fputs(prefix, file);
    fputs(line, file);
    fputc('\n', file);
    fflush(file);
}

void NxPlatform::CloseLog()
{
    if (s_LogFile)
        fclose(s_LogFile);
    s_LogFile = nullptr;
    s_LogTried = false;
}

[[noreturn]] void NxPlatform::Panic(const char* message)
{
    const char* text = message ? message : "<no message>";
    ConsoleWrite(LogLevel::Error, "!!! NX PANIC !!!");
    ConsoleWrite(LogLevel::Error, text);
    CloseLog();

    ErrorSystemConfig config;
    if (R_SUCCEEDED(errorSystemCreate(&config, text, nullptr)))
        errorSystemShow(&config);

    ShutdownApplet();
    exit(EXIT_FAILURE);
}
