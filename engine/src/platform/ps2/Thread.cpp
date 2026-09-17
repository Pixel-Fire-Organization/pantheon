#include <cstdlib>
#include <malloc.h>

#include "Platform.h"
#include "core/EngineDebug.h"

extern "C" {
#include <kernel.h>
}

// The EE kernel wants a `void*` gp value for a new thread.
extern void* _gp;

namespace
{

    // The engine hands these out as opaque pointers, so the id and its stack can
    // be tracked together without the caller knowing either exists.
    struct Ps2Thread
    {
        void* stack;
        int32_t id;
    };

    struct Ps2Semaphore
    {
        int32_t id;
    };

} // namespace

PlatformThread* Ps2Platform::ThreadCreate(ThreadEntry entry, void* userData, size_t stackSize)
{
    if (!entry || stackSize == 0)
        return nullptr;

    Ps2Thread* thread = static_cast<Ps2Thread*>(malloc(sizeof(Ps2Thread)));
    if (!thread)
        return nullptr;

    // The kernel holds this pointer for the thread's lifetime, so it must stay
    // valid and 16-byte aligned. Never freed while the thread runs - equivalent
    // to the static buffer this replaced, without the per-caller .bss cost.
    thread->stack = memalign(16, stackSize);
    if (!thread->stack)
    {
        free(thread);
        return nullptr;
    }

    ee_thread_t param = {};
    param.func = reinterpret_cast<void*>(entry);
    param.stack = thread->stack;
    param.stack_size = static_cast<int>(stackSize);
    param.gp_reg = &_gp;
    param.initial_priority = PLATFORM_WORKER_THREAD_PRIORITY;

    thread->id = CreateThread(&param);
    if (thread->id < 0)
    {
        Engine_LogError("%s: CreateThread failed (%d)", GetName(), thread->id);
        free(thread->stack);
        free(thread);
        return nullptr;
    }

    StartThread(thread->id, userData);
    return reinterpret_cast<PlatformThread*>(thread);
}

void Ps2Platform::ThreadDestroy(PlatformThread* thread)
{
    // Deliberately does not delete the kernel thread or free its stack: there is
    // no join support yet, and reclaiming a running thread's stack would corrupt
    // it. Callers signal the thread to exit and leak the stack for the process
    // lifetime, which is what the IO subsystem already did.
    free(thread);
}

PlatformSemaphore* Ps2Platform::SemaphoreCreate(int32_t initialCount, int32_t maxCount)
{
    Ps2Semaphore* sema = static_cast<Ps2Semaphore*>(malloc(sizeof(Ps2Semaphore)));
    if (!sema)
        return nullptr;

    ee_sema_t param = {};
    param.init_count = initialCount;
    param.max_count = maxCount;
    param.option = 0;

    sema->id = CreateSema(&param);
    if (sema->id < 0)
    {
        Engine_LogError("%s: CreateSema failed (%d)", GetName(), sema->id);
        free(sema);
        return nullptr;
    }
    return reinterpret_cast<PlatformSemaphore*>(sema);
}

void Ps2Platform::SemaphoreWait(PlatformSemaphore* sema)
{
    if (sema)
        WaitSema(reinterpret_cast<Ps2Semaphore*>(sema)->id);
}

void Ps2Platform::SemaphoreSignal(PlatformSemaphore* sema)
{
    if (sema)
        SignalSema(reinterpret_cast<Ps2Semaphore*>(sema)->id);
}

void Ps2Platform::SemaphoreDestroy(PlatformSemaphore* sema)
{
    if (!sema)
        return;
    DeleteSema(reinterpret_cast<Ps2Semaphore*>(sema)->id);
    free(sema);
}
