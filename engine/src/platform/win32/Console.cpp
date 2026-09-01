#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Macros.h"
#include "Platform.h"

#include <windows.h>

#include <dbghelp.h>

void Win32Platform::ConsoleWrite(LogLevel level, const char* line)
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

    // Scrub non-printables: a corrupt string should produce a legible log line,
    // not an unreadable console. Same contract as every other platform.
    char scrubbed[LOG_STRING_MAX_SIZE * 4];
    size_t n = 0;
    for (const char* q = line; *q && n < sizeof(scrubbed) - 1; ++q)
    {
        const unsigned char c = static_cast<unsigned char>(*q);
        scrubbed[n++] = (c < 32 || c > 126) ? '?' : static_cast<char>(c);
    }
    scrubbed[n] = 0;

    printf("%s%s\n", prefix, scrubbed);

    // Flush every line. stdout is block-buffered when redirected to a file, so
    // without this a crash or a kill loses exactly the log you needed.
    fflush(stdout);

    // Also mirror to the debugger, so a build launched from Visual Studio or
    // WinDbg shows engine logs without a console window attached.
    if (IsDebuggerPresent())
    {
        OutputDebugStringA(prefix);
        OutputDebugStringA(scrubbed);
        OutputDebugStringA("\n");
    }
}

namespace
{

    // Resolve the running module directory so a dump lands beside the exe rather
    // than in whatever directory the process happened to start in.
    bool BuildDumpPath(char* out, size_t size)
    {
        char exePath[MAX_PATH];
        const DWORD n = GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        if (n == 0 || n >= MAX_PATH)
            return false;

        char* slash = strrchr(exePath, '\\');
        if (slash)
            *(slash + 1) = 0;
        else
            exePath[0] = 0;

        SYSTEMTIME st;
        GetLocalTime(&st);
        return snprintf(out, size, "%scrash_%04u%02u%02u_%02u%02u%02u.dmp", exePath, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond) > 0;
    }

    // A minidump is what makes a shipping-build report actionable: the message
    // alone rarely identifies which of several call paths reached the panic.
    bool WriteCrashDump(char* outPath, size_t size)
    {
        if (!BuildDumpPath(outPath, size))
            return false;

        HMODULE dbghelp = LoadLibraryA("dbghelp.dll");
        if (!dbghelp)
            return false;

        typedef BOOL(WINAPI * PFN_MiniDumpWriteDump)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
        PFN_MiniDumpWriteDump write = reinterpret_cast<PFN_MiniDumpWriteDump>(reinterpret_cast<void*>(GetProcAddress(dbghelp, "MiniDumpWriteDump")));
        if (!write)
        {
            FreeLibrary(dbghelp);
            return false;
        }

        HANDLE file = CreateFileA(outPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            FreeLibrary(dbghelp);
            return false;
        }

        const BOOL ok = write(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpNormal, nullptr, nullptr, nullptr);
        CloseHandle(file);
        FreeLibrary(dbghelp);
        return ok != FALSE;
    }

#ifdef DEBUG
    // Symbolised where symbols are available, addresses otherwise. Either is
    // enough to identify the call path; neither is worth failing a panic over.
    void CaptureStackText(char* out, size_t size)
    {
        out[0] = 0;
        void* frames[32];
        const USHORT count = CaptureStackBackTrace(1, 32, frames, nullptr);
        if (count == 0)
        {
            snprintf(out, size, "  <no frames captured>");
            return;
        }

        HANDLE proc = GetCurrentProcess();
        HMODULE dbghelp = LoadLibraryA("dbghelp.dll");
        typedef BOOL(WINAPI * PFN_SymInitialize)(HANDLE, PCSTR, BOOL);
        typedef BOOL(WINAPI * PFN_SymFromAddr)(HANDLE, DWORD64, PDWORD64, PSYMBOL_INFO);
        PFN_SymInitialize symInit = nullptr;
        PFN_SymFromAddr symFrom = nullptr;
        if (dbghelp)
        {
            symInit = reinterpret_cast<PFN_SymInitialize>(reinterpret_cast<void*>(GetProcAddress(dbghelp, "SymInitialize")));
            symFrom = reinterpret_cast<PFN_SymFromAddr>(reinterpret_cast<void*>(GetProcAddress(dbghelp, "SymFromAddr")));
        }
        if (symInit)
            symInit(proc, nullptr, TRUE);

        size_t used = 0;
        for (USHORT i = 0; i < count && used + 1 < size; ++i)
        {
            char line[256];
            bool named = false;
            if (symFrom)
            {
                char buffer[sizeof(SYMBOL_INFO) + 256];
                memset(buffer, 0, sizeof(buffer));
                PSYMBOL_INFO sym = reinterpret_cast<PSYMBOL_INFO>(buffer);
                sym->SizeOfStruct = sizeof(SYMBOL_INFO);
                sym->MaxNameLen = 255;
                DWORD64 displacement = 0;
                if (symFrom(proc, reinterpret_cast<DWORD64>(frames[i]), &displacement, sym))
                {
                    snprintf(line, sizeof(line), "  %s + 0x%llx\n", sym->Name, static_cast<unsigned long long>(displacement));
                    named = true;
                }
            }
            if (!named)
                snprintf(line, sizeof(line), "  0x%p\n", frames[i]);

            const size_t len = strlen(line);
            if (used + len + 1 >= size)
                break;
            memcpy(out + used, line, len + 1);
            used += len;
        }

        if (dbghelp)
            FreeLibrary(dbghelp);
    }
#endif

} // namespace

[[noreturn]] void Win32Platform::Panic(const char* message)
{
    const char* text = message ? message : "<no message>";

    printf("ERR : !!! PANIC !!! %s\n", text);
    fflush(stdout);

    char dumpPath[MAX_PATH];
    const bool haveDump = WriteCrashDump(dumpPath, sizeof(dumpPath));

    if (IsDebuggerPresent())
    {
        OutputDebugStringA("!!! PANIC !!! ");
        OutputDebugStringA(text);
        OutputDebugStringA("\n");
        DebugBreak();
    }

#ifdef DEBUG
    // Development build: report where it happened, so the box alone is enough
    // to start on without reproducing under a debugger.
    char body[4096];
    char stack[3072];
    CaptureStackText(stack, sizeof(stack));
    snprintf(body, sizeof(body), "%s\n\nStack:\n%s\n%s%s", text, stack, haveDump ? "Dump written to:\n" : "", haveDump ? dumpPath : "");
    MessageBoxA(nullptr, body, "Engine panic (debug)", MB_OK | MB_ICONERROR);
#else
    // Shipping build: no stack text. Give the player something they can send on.
    char body[1024];
    snprintf(body, sizeof(body), "The game stopped because of an internal error.\n\n%s\n\n%s%s", text, haveDump ? "Please send this file to the developers:\n" : "", haveDump ? dumpPath : "");
    MessageBoxA(nullptr, body, "Engine panic", MB_OK | MB_ICONERROR);
#endif

    // Must not return. abort() rather than a spin loop: on a desktop OS a hung
    // process is worse than a crashed one - it leaves no dump and has to be
    // killed by hand.
    abort();
}
