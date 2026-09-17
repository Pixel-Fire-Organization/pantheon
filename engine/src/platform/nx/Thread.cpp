#include <cstdlib>

#include "Platform.h"
#include "core/EngineDebug.h"

#include <switch.h>

namespace
{
    const int NX_DEFAULT_CORE = -2;

    struct NxThread
    {
        Thread thread;
        ThreadEntry entry;
        void* userData;
    };

    struct NxSemaphore
    {
        Mutex mutex;
        CondVar condition;
        int32_t count;
        int32_t maxCount;
    };

    void ThreadTrampoline(void* arg)
    {
        NxThread* thread = static_cast<NxThread*>(arg);
        if (thread && thread->entry)
            thread->entry(thread->userData);
    }

    int WorkerCore()
    {
        uint64_t mask = 0;
        if (R_FAILED(svcGetInfo(&mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0)))
            return NX_DEFAULT_CORE;

        const uint32_t mainCore = svcGetCurrentProcessorNumber();
        const int coreCount = static_cast<int>(sizeof(mask) * 8u);
        for (int core = 0; core < coreCount; ++core)
        {
            if (static_cast<uint32_t>(core) != mainCore && (mask & (1ull << core)) != 0)
                return core;
        }
        return NX_DEFAULT_CORE;
    }
} // namespace

PlatformThread* NxPlatform::ThreadCreate(ThreadEntry entry, void* userData, size_t stackSize)
{
    if (!entry)
        return nullptr;

    NxThread* thread = static_cast<NxThread*>(calloc(1, sizeof(NxThread)));
    if (!thread)
        return nullptr;

    thread->entry = entry;
    thread->userData = userData;

    const int core = WorkerCore();
    Result rc = threadCreate(&thread->thread, &ThreadTrampoline, thread, nullptr, stackSize, PLATFORM_WORKER_THREAD_PRIORITY, core);
    if (R_FAILED(rc))
    {
        Engine_LogError("%s: threadCreate failed (0x%X, core %d)", GetName(), static_cast<unsigned>(rc), core);
        free(thread);
        return nullptr;
    }

    rc = threadStart(&thread->thread);
    if (R_FAILED(rc))
    {
        Engine_LogError("%s: threadStart failed (0x%X)", GetName(), static_cast<unsigned>(rc));
        threadClose(&thread->thread);
        free(thread);
        return nullptr;
    }

    return reinterpret_cast<PlatformThread*>(thread);
}

void NxPlatform::ThreadDestroy(PlatformThread* thread)
{
    if (!thread)
        return;

    NxThread* t = reinterpret_cast<NxThread*>(thread);
    threadWaitForExit(&t->thread);
    threadClose(&t->thread);
    free(t);
}

PlatformSemaphore* NxPlatform::SemaphoreCreate(int32_t initialCount, int32_t maxCount)
{
    if (maxCount <= 0 || initialCount < 0 || initialCount > maxCount)
        return nullptr;

    NxSemaphore* sema = static_cast<NxSemaphore*>(calloc(1, sizeof(NxSemaphore)));
    if (!sema)
        return nullptr;

    mutexInit(&sema->mutex);
    condvarInit(&sema->condition);
    sema->count = initialCount;
    sema->maxCount = maxCount;
    return reinterpret_cast<PlatformSemaphore*>(sema);
}

void NxPlatform::SemaphoreWait(PlatformSemaphore* sema)
{
    if (!sema)
        return;

    NxSemaphore* s = reinterpret_cast<NxSemaphore*>(sema);
    mutexLock(&s->mutex);
    while (s->count <= 0)
        condvarWait(&s->condition, &s->mutex);
    --s->count;
    mutexUnlock(&s->mutex);
}

void NxPlatform::SemaphoreSignal(PlatformSemaphore* sema)
{
    if (!sema)
        return;

    NxSemaphore* s = reinterpret_cast<NxSemaphore*>(sema);
    mutexLock(&s->mutex);
    if (s->count < s->maxCount)
        ++s->count;
    condvarWakeOne(&s->condition);
    mutexUnlock(&s->mutex);
}

void NxPlatform::SemaphoreDestroy(PlatformSemaphore* sema)
{
    if (sema)
        free(reinterpret_cast<NxSemaphore*>(sema));
}
