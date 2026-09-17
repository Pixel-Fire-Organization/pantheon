#include <cstdlib>

#include "Platform.h"
#include "core/EngineDebug.h"

extern "C" {
#include <psp2/kernel/threadmgr.h>
}

namespace
{
    struct VitaThread
    {
        SceUID id;
        ThreadEntry entry;
        void* userData;
    };

    struct VitaSemaphore
    {
        SceUID id;
    };

    int ThreadTrampoline(SceSize argSize, void* argBlock)
    {
        if (argSize < sizeof(VitaThread*) || !argBlock)
            return -1;

        VitaThread* thread = *static_cast<VitaThread* const*>(argBlock);
        if (thread && thread->entry)
            thread->entry(thread->userData);
        return 0;
    }
} // namespace

PlatformThread* VitaPlatform::ThreadCreate(ThreadEntry entry, void* userData, size_t stackSize)
{
    if (!entry)
        return nullptr;

    VitaThread* thread = static_cast<VitaThread*>(malloc(sizeof(VitaThread)));
    if (!thread)
        return nullptr;

    thread->entry = entry;
    thread->userData = userData;

    thread->id = sceKernelCreateThread("engine_worker", &ThreadTrampoline, THREAD_DEFAULT_PRIORITY, static_cast<SceSize>(stackSize), 0, THREAD_DEFAULT_AFFINITY, nullptr);
    if (thread->id < 0)
    {
        Engine_LogError("%s: sceKernelCreateThread failed (0x%08X)", GetName(), static_cast<unsigned>(thread->id));
        free(thread);
        return nullptr;
    }

    VitaThread* argBlock = thread;
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

void VitaPlatform::ThreadDestroy(PlatformThread* thread)
{
    if (!thread)
        return;

    VitaThread* t = reinterpret_cast<VitaThread*>(thread);
    if (t->id >= 0)
    {
        sceKernelWaitThreadEnd(t->id, nullptr, nullptr);
        sceKernelDeleteThread(t->id);
    }
    free(t);
}

PlatformSemaphore* VitaPlatform::SemaphoreCreate(int32_t initialCount, int32_t maxCount)
{
    VitaSemaphore* sema = static_cast<VitaSemaphore*>(malloc(sizeof(VitaSemaphore)));
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

void VitaPlatform::SemaphoreWait(PlatformSemaphore* sema)
{
    if (sema)
        sceKernelWaitSema(reinterpret_cast<VitaSemaphore*>(sema)->id, 1, nullptr);
}

void VitaPlatform::SemaphoreSignal(PlatformSemaphore* sema)
{
    if (sema)
        sceKernelSignalSema(reinterpret_cast<VitaSemaphore*>(sema)->id, 1);
}

void VitaPlatform::SemaphoreDestroy(PlatformSemaphore* sema)
{
    if (!sema)
        return;

    VitaSemaphore* s = reinterpret_cast<VitaSemaphore*>(sema);
    if (s->id >= 0)
        sceKernelDeleteSema(s->id);
    free(s);
}
