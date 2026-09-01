#include "Platform.h"

#include <cstdio>
#include <cstring>

extern "C" {
#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
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

    SceUID LogFile(const char* writableRoot)
    {
        if (s_LogTried)
            return s_LogFile;

        // Before Platform::Init the writable root is empty, and opening a
        // relative path fails. Do not latch on that: retry once it is set, or
        // every later line is lost too.
        if (!writableRoot || !writableRoot[0])
            return -1;

        s_LogTried = true;

        char path[80];
        snprintf(path, sizeof(path), "%sengine.log", writableRoot);
        s_LogFile = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
        return s_LogFile;
    }
} // namespace

void VitaPlatform::ConsoleWrite(LogLevel level, const char* line)
{
    if (!line)
        return;

    const char* prefix = PrefixFor(level);
    sceClibPrintf("%s%s\n", prefix, line);

    const SceUID file = LogFile(m_writableRoot);
    if (file < 0)
        return;

    sceIoWrite(file, prefix, strlen(prefix));
    sceIoWrite(file, line, strlen(line));
    sceIoWrite(file, "\n", 1);
}

void VitaPlatform::CloseLog()
{
    if (s_LogFile >= 0)
        sceIoClose(s_LogFile);
    s_LogFile = -1;
    s_LogTried = false;
}

[[noreturn]] void VitaPlatform::Panic(const char* message)
{
    ConsoleWrite(LogLevel::Error, "!!! VITA PANIC !!!");
    ConsoleWrite(LogLevel::Error, message ? message : "<no message>");

    if (s_LogFile >= 0)
        sceIoClose(s_LogFile);

    sceKernelExitProcess(1);

    while (true)
        ;
}
