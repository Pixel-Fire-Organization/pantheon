#include <cstdio>
#include <cstring>

#include "core/EngineCore.h"
#include "graphics/Renderer.h"
#include "Macros.h"
#include "Platform.h"
#include "ui/EngineUi.h"

extern "C" {
#include <pspiofilemgr.h>
}

namespace
{
    SceUID s_LogFile = -1;
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

    // Before Platform::Init the writable root is empty, and opening a relative
    // path fails. Do not latch on that: retry once it is set, or every later
    // line is lost too.
    SceUID LogFile(const char* writableRoot)
    {
        if (s_LogTried)
            return s_LogFile;

        if (!writableRoot || !writableRoot[0])
            return -1;

        s_LogTried = true;

        char path[IO_FILE_MAX_PATH];
        if (snprintf(path, sizeof(path), "%sengine.log", writableRoot) >= static_cast<int>(sizeof(path)))
            return -1;

        s_LogFile = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
        return s_LogFile;
    }
}

void PspPlatform::ConsoleWrite(LogLevel level, const char* line)
{
    if (!line)
        return;

    const char* prefix = PrefixFor(level);
    const size_t prefixLen = strlen(prefix);
    const size_t lineLen = strlen(line);

    sceIoWrite(1, prefix, prefixLen);
    sceIoWrite(1, line, lineLen);
    sceIoWrite(1, "\n", 1);

    const SceUID file = LogFile(m_writableRoot);
    if (file < 0)
        return;

    sceIoWrite(file, prefix, prefixLen);
    sceIoWrite(file, line, lineLen);
    sceIoWrite(file, "\n", 1);
}

void PspPlatform::CloseLog()
{
    if (s_LogFile >= 0)
        sceIoClose(s_LogFile);
    s_LogFile = -1;
    s_LogTried = false;
}

[[noreturn]] void PspPlatform::Panic(const char* message)
{
#ifdef DEBUG
    ConsoleWrite(LogLevel::Error, "!!! PSP PANIC !!!");
    ConsoleWrite(LogLevel::Error, message ? message : "<no message>");

    if (s_LogFile >= 0)
        sceIoClose(s_LogFile);
    s_LogFile = -1;

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
    if (s_LogFile >= 0)
        sceIoClose(s_LogFile);
    s_LogFile = -1;
#endif

    while (true)
        ;
}
