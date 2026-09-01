#include <cstdlib>

#include "core/EngineDebug.h"
#include "Platform.h"

extern "C" {
#include <pspkernel.h>
}

namespace
{
    struct PspThread
    {
        SceUID id;
        ThreadEntry entry;
        void* userData;
    };

    struct PspSemaphore
    {
        SceUID id;
    };

    int ThreadTrampoline(SceSize argSize, void* argBlock)
    {
        if (argSize < sizeof(PspThread*) || !argBlock)
            return -1;

        PspThread* thread = *static_cast<PspThread* const*>(argBlock);
        if (thread && thread->entry)
            thread->entry(thread->userData);
        return 0;
    }
}

PlatformThread* PspPlatform::ThreadCreate(ThreadEntry entry, void* userData, size_t stackSize)
{
    if (!entry)
        return nullptr;

    PspThread* thread = static_cast<PspThread*>(malloc(sizeof(PspThread)));
    if (!thread)
        return nullptr;

    thread->entry = entry;
    thread->userData = userData;

    thread->id = sceKernelCreateThread("engine_worker", &ThreadTrampoline, PLATFORM_WORKER_THREAD_PRIORITY, static_cast<SceSize>(stackSize), THREAD_ATTR_USER, nullptr);
    if (thread->id < 0)
    {
        Engine_LogError("%s: sceKernelCreateThread failed (0x%08X)", GetName(), static_cast<unsigned>(thread->id));
        free(thread);
        return nullptr;
    }

    PspThread* argBlock = thread;
    const int started = sceKernelStartThread(thread->id, sizeof(argBlock), &argBlock);
    if (started < 0)
    {
        Engine_LogError("%s: sceKernelStartThread failed (0x%08X)", GetName(), static_cast<unsigned>(started));
        sceKernelDeleteThread(thread->id);
        free(thread);
        return nullptr;
    }

    return reinterpret_cast<PlatformThread*>(thread);
}

void PspPlatform::ThreadDestroy(PlatformThread* thread)
{
    if (!thread)
        return;

    PspThread* t = reinterpret_cast<PspThread*>(thread);
    if (t->id >= 0)
    {
        sceKernelWaitThreadEnd(t->id, nullptr);
        sceKernelDeleteThread(t->id);
    }
    free(t);
}

PlatformSemaphore* PspPlatform::SemaphoreCreate(int32_t initialCount, int32_t maxCount)
{
    PspSemaphore* sema = static_cast<PspSemaphore*>(malloc(sizeof(PspSemaphore)));
    if (!sema)
        return nullptr;

    sema->id = sceKernelCreateSema("engine_sema", 0, initialCount, maxCount, nullptr);
    if (sema->id < 0)
    {
        Engine_LogError("%s: sceKernelCreateSema failed (0x%08X)", GetName(), static_cast<unsigned>(sema->id));
        free(sema);
        return nullptr;
    }
    return reinterpret_cast<PlatformSemaphore*>(sema);
}

void PspPlatform::SemaphoreWait(PlatformSemaphore* sema)
{
    if (sema)
        sceKernelWaitSema(reinterpret_cast<PspSemaphore*>(sema)->id, 1, nullptr);
}

void PspPlatform::SemaphoreSignal(PlatformSemaphore* sema)
{
    if (sema)
        sceKernelSignalSema(reinterpret_cast<PspSemaphore*>(sema)->id, 1);
}

void PspPlatform::SemaphoreDestroy(PlatformSemaphore* sema)
{
    if (!sema)
        return;

    PspSemaphore* s = reinterpret_cast<PspSemaphore*>(sema);
    if (s->id >= 0)
        sceKernelDeleteSema(s->id);
    free(s);
}
