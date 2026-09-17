#include <cstdlib>

#include "Platform.h"
#include "core/EngineDebug.h"

#include <windows.h>

namespace
{

    struct Win32Thread
    {
        HANDLE handle;
        ThreadEntry entry;
        void* userData;
    };

    struct Win32Semaphore
    {
        HANDLE handle;
    };

    // Win32 thread procedures return DWORD; the engine's ThreadEntry returns
    // void, so this adapts between them.
    DWORD WINAPI ThreadTrampoline(LPVOID param)
    {
        Win32Thread* thread = static_cast<Win32Thread*>(param);
        if (thread && thread->entry)
            thread->entry(thread->userData);
        return 0;
    }

} // namespace

PlatformThread* Win32Platform::ThreadCreate(ThreadEntry entry, void* userData, size_t stackSize)
{
    if (!entry)
        return nullptr;

    Win32Thread* thread = static_cast<Win32Thread*>(malloc(sizeof(Win32Thread)));
    if (!thread)
        return nullptr;

    thread->entry = entry;
    thread->userData = userData;

    // Unlike the EE, the OS owns the stack: passing a size is a hint and the
    // stack is committed lazily, so there is nothing for us to allocate or leak.
    thread->handle = CreateThread(nullptr, static_cast<SIZE_T>(stackSize), &ThreadTrampoline, thread, 0, nullptr);
    if (!thread->handle)
    {
        Engine_LogError("%s: CreateThread failed (%lu)", GetName(), GetLastError());
        free(thread);
        return nullptr;
    }

    return reinterpret_cast<PlatformThread*>(thread);
}

void Win32Platform::ThreadDestroy(PlatformThread* thread)
{
    if (!thread)
        return;

    Win32Thread* t = reinterpret_cast<Win32Thread*>(thread);
    if (t->handle)
        CloseHandle(t->handle);
    free(t);
}

PlatformSemaphore* Win32Platform::SemaphoreCreate(int32_t initialCount, int32_t maxCount)
{
    Win32Semaphore* sema = static_cast<Win32Semaphore*>(malloc(sizeof(Win32Semaphore)));
    if (!sema)
        return nullptr;

    sema->handle = CreateSemaphoreA(nullptr, initialCount, maxCount, nullptr);
    if (!sema->handle)
    {
        Engine_LogError("%s: CreateSemaphore failed (%lu)", GetName(), GetLastError());
        free(sema);
        return nullptr;
    }
    return reinterpret_cast<PlatformSemaphore*>(sema);
}

void Win32Platform::SemaphoreWait(PlatformSemaphore* sema)
{
    if (sema)
        WaitForSingleObject(reinterpret_cast<Win32Semaphore*>(sema)->handle, INFINITE);
}

void Win32Platform::SemaphoreSignal(PlatformSemaphore* sema)
{
    if (sema)
        ReleaseSemaphore(reinterpret_cast<Win32Semaphore*>(sema)->handle, 1, nullptr);
}

void Win32Platform::SemaphoreDestroy(PlatformSemaphore* sema)
{
    if (!sema)
        return;

    Win32Semaphore* s = reinterpret_cast<Win32Semaphore*>(sema);
    if (s->handle)
        CloseHandle(s->handle);
    free(s);
}
