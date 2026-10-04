#include <cstdlib>
#include <pthread.h>

#include "Platform.h"
#include "core/EngineDebug.h"

namespace
{

    const size_t kStackGranule = 16 * 1024;

    struct MacosThread
    {
        pthread_t handle;
    };

    struct ThreadStart
    {
        ThreadEntry entry;
        void* userData;
    };

    struct MacosSemaphore
    {
        pthread_mutex_t mutex;
        pthread_cond_t condition;
        int32_t count;
        int32_t maxCount;
    };

    void* ThreadTrampoline(void* param)
    {
        ThreadStart* start = static_cast<ThreadStart*>(param);
        const ThreadEntry entry = start->entry;
        void* userData = start->userData;
        free(start);
        entry(userData);
        return nullptr;
    }

} // namespace

PlatformThread* MacosPlatform::ThreadCreate(ThreadEntry entry, void* userData, size_t stackSize)
{
    if (!entry)
        return nullptr;

    MacosThread* thread = static_cast<MacosThread*>(malloc(sizeof(MacosThread)));
    ThreadStart* start = static_cast<ThreadStart*>(malloc(sizeof(ThreadStart)));
    if (!thread || !start)
    {
        free(thread);
        free(start);
        return nullptr;
    }
    start->entry = entry;
    start->userData = userData;

    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
    if (stackSize > 0)
        pthread_attr_setstacksize(&attributes, ((stackSize + kStackGranule - 1u) / kStackGranule) * kStackGranule);

    const int result = pthread_create(&thread->handle, &attributes, &ThreadTrampoline, start);
    pthread_attr_destroy(&attributes);
    if (result != 0)
    {
        Engine_LogError("%s: pthread_create failed (%d)", GetName(), result);
        free(thread);
        free(start);
        return nullptr;
    }

    return reinterpret_cast<PlatformThread*>(thread);
}

void MacosPlatform::ThreadDestroy(PlatformThread* thread) { free(reinterpret_cast<MacosThread*>(thread)); }

PlatformSemaphore* MacosPlatform::SemaphoreCreate(int32_t initialCount, int32_t maxCount)
{
    if (maxCount <= 0 || initialCount < 0 || initialCount > maxCount)
        return nullptr;

    MacosSemaphore* sema = static_cast<MacosSemaphore*>(malloc(sizeof(MacosSemaphore)));
    if (!sema)
        return nullptr;

    if (pthread_mutex_init(&sema->mutex, nullptr) != 0)
    {
        free(sema);
        return nullptr;
    }
    if (pthread_cond_init(&sema->condition, nullptr) != 0)
    {
        pthread_mutex_destroy(&sema->mutex);
        free(sema);
        return nullptr;
    }

    sema->count = initialCount;
    sema->maxCount = maxCount;
    return reinterpret_cast<PlatformSemaphore*>(sema);
}

void MacosPlatform::SemaphoreWait(PlatformSemaphore* sema)
{
    if (!sema)
        return;

    MacosSemaphore* s = reinterpret_cast<MacosSemaphore*>(sema);
    pthread_mutex_lock(&s->mutex);
    while (s->count == 0)
        pthread_cond_wait(&s->condition, &s->mutex);
    --s->count;
    pthread_mutex_unlock(&s->mutex);
}

void MacosPlatform::SemaphoreSignal(PlatformSemaphore* sema)
{
    if (!sema)
        return;

    MacosSemaphore* s = reinterpret_cast<MacosSemaphore*>(sema);
    pthread_mutex_lock(&s->mutex);
    if (s->count < s->maxCount)
    {
        ++s->count;
        pthread_cond_signal(&s->condition);
    }
    pthread_mutex_unlock(&s->mutex);
}

void MacosPlatform::SemaphoreDestroy(PlatformSemaphore* sema)
{
    if (!sema)
        return;

    MacosSemaphore* s = reinterpret_cast<MacosSemaphore*>(sema);
    pthread_cond_destroy(&s->condition);
    pthread_mutex_destroy(&s->mutex);
    free(s);
}
