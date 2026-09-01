#include <cstdio>
#include <cstring>
#include "Engine.h"

#include "platform/Platform.h"

#define MAX_IO_REQUESTS IO_ASYNC_MAX_REQUESTS

typedef enum
{
    IO_STATE_IDLE,
    IO_STATE_QUEUED,
    IO_STATE_COMPLETED,
    // Buffer is live on the main thread inside the callback.
    // The IO thread must not reuse this slot until it transitions back to IDLE.
    IO_STATE_DISPATCHING
} IORequestState;

typedef struct
{
    char filepath[IO_FILE_MAX_PATH];
    IO_Callback callback;
    void* userData;
    IORequestState state;
    void* loadedData;
    size_t loadedSize;
} IORequest;

static IORequest s_Requests[MAX_IO_REQUESTS];
static volatile bool s_IOThreadActive = false;
// Set by the worker as its last act. Shutdown waits on this rather than on a
// join: one supported platform cannot join a thread at all, and another closes
// the handle without waiting, so the acknowledgement has to come from the
// worker itself.
static volatile bool s_IOThreadExited = false;

static PlatformThread* s_IOThread = nullptr;
static PlatformSemaphore* s_IOMutex = nullptr;

// Binary semaphore protecting s_SharedReadBuffer.
// Initialised to 1 (buffer free). The IO thread acquires it before reading a
// file; the main thread releases it once the callback has finished consuming
// the data and the slot is marked IDLE.
static PlatformSemaphore* s_IOBufferSema = nullptr;

static PlatformSemaphore* s_FileSema = nullptr;

void Engine_IO_AcquireFileAccess()
{
    if (s_FileSema)
        Engine_GetPlatform()->SemaphoreWait(s_FileSema);
}

void Engine_IO_ReleaseFileAccess()
{
    if (s_FileSema)
        Engine_GetPlatform()->SemaphoreSignal(s_FileSema);
}

// Single shared read buffer — deliberately not one per slot.
// Using one buffer instead of one per slot keeps BSS inside the PS2 EE TLB
// coverage window. Per-slot buffers pushed .bss to virtual address 0x30000000,
// which has no TLB mapping, causing a store TLB miss cascade during the crt0
// BSS-zero loop at startup.
// Ownership alternates: IO thread acquires s_IOBufferSema before reading, main
// thread releases it after the callback returns — enforcing serial buffer use.
// 16-byte aligned: the strictest alignment any supported platform's transfer
// path requires of a source buffer.
static uint8_t s_SharedReadBuffer[IO_READ_BUFFER_SIZE] __attribute__((aligned(16)));

static void IOThreadEntry(void* arg)
{
    UNUSED_VAR(arg);
    Platform* platform = Engine_GetPlatform();

    while (s_IOThreadActive)
    {
        int reqIndex = -1;
        char filepath[IO_FILE_MAX_PATH];

        if (s_IOMutex)
        {
            platform->SemaphoreWait(s_IOMutex);
            for (int i = 0; i < MAX_IO_REQUESTS; ++i)
            {
                if (s_Requests[i].state == IO_STATE_QUEUED)
                {
                    reqIndex = i;
                    strncpy(filepath, s_Requests[i].filepath, IO_FILE_MAX_PATH - 1);
                    filepath[IO_FILE_MAX_PATH - 1] = '\0';
                    break;
                }
            }
            platform->SemaphoreSignal(s_IOMutex);
        }

        if (reqIndex != -1)
        {
            // Acquire the shared buffer before reading.
            // Blocks until Engine_IO_Update has finished dispatching the previous
            // callback — i.e. the callback has returned and s_SharedReadBuffer is
            // no longer referenced by the main thread.
            if (s_IOBufferSema)
                platform->SemaphoreWait(s_IOBufferSema);

            // Engine_IO_Shutdown signals the sema to unblock a waiting IO thread.
            // Exit immediately if that is why we woke up.
            if (!s_IOThreadActive)
                break;

            void* data = nullptr;
            size_t size = 0;

            // ARCHIVE SEAM: if a mounted archive holds this asset, read its span
            // straight from the container (one seek + read, no per-file open).
            // Engine_Archive_ReadSync takes the file-access semaphore itself.
            ArchiveLocator loc;
            if (Engine_Archive_Find(filepath, &loc))
            {
                if (loc.size > IO_READ_BUFFER_SIZE)
                {
                    Engine_LogError("IO: '%s' is %u bytes in archive, exceeds IO_READ_BUFFER_SIZE (%d). Rejected.", filepath, loc.size, IO_READ_BUFFER_SIZE);
                }
                else if (Engine_Archive_ReadSync(&loc, 0, s_SharedReadBuffer, loc.size))
                {
                    data = s_SharedReadBuffer;
                    size = loc.size;
                }
                else
                {
                    Engine_LogError("IO: archive read failed for '%s'", filepath);
                }

                if (s_IOMutex)
                {
                    platform->SemaphoreWait(s_IOMutex);
                    s_Requests[reqIndex].loadedData = data;
                    s_Requests[reqIndex].loadedSize = size;
                    s_Requests[reqIndex].state = IO_STATE_COMPLETED;
                    platform->SemaphoreSignal(s_IOMutex);
                }
                continue; // handled via archive; skip the loose-file path
            }

            // Loose-file fallback: no mounted archive holds it (host: dev builds,
            // and the loose->archive transition). Unchanged whole-file read.
            Engine_IO_AcquireFileAccess();
            FileHandle f = platform->FileOpen(filepath, FileMode::Read);
            if (f)
            {
                const uint64_t fileSize = platform->FileSize(f);

                if (fileSize > IO_READ_BUFFER_SIZE)
                {
                    Engine_LogError("IO: '%s' is %llu bytes, exceeds IO_READ_BUFFER_SIZE (%d). Rejected.", filepath, static_cast<unsigned long long>(fileSize), IO_READ_BUFFER_SIZE);
                    platform->FileClose(f);
                }
                else
                {
                    size = static_cast<size_t>(fileSize);
                    const size_t bytesRead = platform->FileRead(f, s_SharedReadBuffer, size);
                    platform->FileClose(f);
                    if (bytesRead != size)
                    {
                        Engine_LogError("IO: Short read for '%s': expected %zu, got %zu", filepath, size, bytesRead);
                        size = 0;
                    }
                    else
                    {
                        data = s_SharedReadBuffer;
                    }
                }
            }
            else
            {
                Engine_LogError("Failed to open %s", filepath);
            }
            Engine_IO_ReleaseFileAccess();

            if (s_IOMutex)
            {
                platform->SemaphoreWait(s_IOMutex);
                s_Requests[reqIndex].loadedData = data;
                s_Requests[reqIndex].loadedSize = size;
                s_Requests[reqIndex].state = IO_STATE_COMPLETED;
                platform->SemaphoreSignal(s_IOMutex);
            }
        }
        else
        {
            platform->SleepMicros(IO_THREAD_SLEEP_USEC);
        }
    }

    s_IOThreadExited = true;
}

bool Engine_IO_Init()
{
    Platform* platform = Engine_GetPlatform();
    if (!platform)
        return false;

    memset(s_Requests, 0, sizeof(s_Requests));
    s_IOThreadActive = true;
    s_IOThreadExited = false;

    s_FileSema = platform->SemaphoreCreate(1, 1);
    s_IOMutex = platform->SemaphoreCreate(1, 1);

    if (!s_IOMutex)
    {
        Engine_LogError("Failed to create IO semaphore!");
        return false;
    }

    // Buffer semaphore: 1 = s_SharedReadBuffer is free for the IO thread.
    // Acquiring it before a read and releasing it after the callback guarantees
    // the IO thread never overwrites the buffer while the main thread is inside
    // the callback using the pointer.
    s_IOBufferSema = platform->SemaphoreCreate(1, 1);
    if (!s_IOBufferSema)
    {
        Engine_LogError("Failed to create IO buffer semaphore!");
        platform->SemaphoreDestroy(s_IOMutex);
        s_IOMutex = nullptr;
        return false;
    }

    s_IOThread = platform->ThreadCreate(&IOThreadEntry, nullptr, IO_THREAD_STACK_SIZE);
    if (!s_IOThread)
    {
        Engine_LogError("Failed to create IO thread!");
        return false;
    }

    Engine_LogInfo("Async IO system initialized.");
    return true;
}

bool Engine_IO_ReadAsync(const char* filepath, IO_Callback callback, void* userData)
{
    Platform* platform = Engine_GetPlatform();
    if (!s_IOMutex || !platform)
        return false;

    bool queued = false;
    platform->SemaphoreWait(s_IOMutex);

    for (int i = 0; i < MAX_IO_REQUESTS; ++i)
    {
        if (s_Requests[i].state == IO_STATE_IDLE)
        {
            strncpy(s_Requests[i].filepath, filepath, IO_FILE_MAX_PATH - 1);
            s_Requests[i].filepath[IO_FILE_MAX_PATH - 1] = '\0';
            s_Requests[i].callback = callback;
            s_Requests[i].userData = userData;
            s_Requests[i].state = IO_STATE_QUEUED;
            queued = true;
            break;
        }
    }

    platform->SemaphoreSignal(s_IOMutex);

    if (!queued)
    {
        Engine_LogError("Failed to queue IO request for %s. Queue full.", filepath);
    }
    return queued;
}

void Engine_IO_Update()
{
    Platform* platform = Engine_GetPlatform();
    if (!s_IOMutex || !platform)
        return;

    platform->SemaphoreWait(s_IOMutex);

    for (int i = 0; i < MAX_IO_REQUESTS; ++i)
    {
        if (s_Requests[i].state == IO_STATE_COMPLETED)
        {
            // Copy callback data to locals before releasing the mutex.
            // The callback (e.g. Internal_OnAsyncLoadComplete) may itself call
            // Engine_IO_ReadAsync for dependency loads, which acquires s_IOMutex —
            // invoking it while the lock is held would deadlock.
            IO_Callback cb = s_Requests[i].callback;
            void* data = s_Requests[i].loadedData;
            size_t size = s_Requests[i].loadedSize;
            void* userData = s_Requests[i].userData;

            // Transition to DISPATCHING before releasing the mutex. The IO thread
            // only picks up QUEUED slots, so this keeps s_SharedReadBuffer protected
            // for the full duration of the callback — preventing a requeue of slot i
            // from racing with the callback's use of the buffer pointer.
            s_Requests[i].state = IO_STATE_DISPATCHING;

            platform->SemaphoreSignal(s_IOMutex);

            if (cb)
            {
                cb(data, size, userData);
            }
            // data points into s_SharedReadBuffer (or NULL on error) — no free() needed.

            // Re-acquire to mark the slot idle and continue the scan.
            platform->SemaphoreWait(s_IOMutex);
            s_Requests[i].loadedData = nullptr;
            s_Requests[i].state = IO_STATE_IDLE;

            // Release the shared read buffer AFTER setting the slot to IDLE, so
            // that any new request the callback may have queued during DISPATCHING
            // is already visible to the IO thread when it next scans.
            if (s_IOBufferSema)
                platform->SemaphoreSignal(s_IOBufferSema);
        }
    }

    platform->SemaphoreSignal(s_IOMutex);
}

bool Engine_IO_Drain()
{
    Platform* platform = Engine_GetPlatform();
    if (!s_IOMutex || !platform)
        return true;

    for (uint32_t spin = 0; spin < IO_DRAIN_MAX_SPINS; ++spin)
    {
        Engine_IO_Update();

        bool busy = false;
        platform->SemaphoreWait(s_IOMutex);
        for (int i = 0; i < MAX_IO_REQUESTS; ++i)
        {
            if (s_Requests[i].state != IO_STATE_IDLE)
            {
                busy = true;
                break;
            }
        }
        platform->SemaphoreSignal(s_IOMutex);

        if (!busy)
            return true;

        platform->SleepMicros(IO_THREAD_SLEEP_USEC);
    }

    Engine_LogError("IO: drain gave up with requests still outstanding");
    return false;
}

void Engine_IO_Shutdown()
{
    Platform* platform = Engine_GetPlatform();
    s_IOThreadActive = false;

    // Wake the IO thread if it is blocked on the buffer semaphore.
    // Without this signal the thread would stall indefinitely after shutdown.
    if (s_IOBufferSema && platform)
        platform->SemaphoreSignal(s_IOBufferSema);

    bool exited = (s_IOThread == nullptr);
    if (s_IOThread && platform)
    {
        for (uint32_t spin = 0; spin < IO_DRAIN_MAX_SPINS && !exited; ++spin)
        {
            if (s_IOThreadExited)
                exited = true;
            else
                platform->SleepMicros(IO_THREAD_SLEEP_USEC);
        }

        platform->ThreadDestroy(s_IOThread);
        s_IOThread = nullptr;
    }

    if (!exited)
    {
        // Everything below frees state the worker still reads. Leaving it
        // allocated leaks at process exit; freeing it corrupts a live thread.
        Engine_LogError("IO: the worker did not acknowledge shutdown; leaving its semaphores allocated");
        return;
    }

    if (platform)
    {
        if (s_IOBufferSema)
            platform->SemaphoreDestroy(s_IOBufferSema);
        if (s_IOMutex)
            platform->SemaphoreDestroy(s_IOMutex);
        if (s_FileSema)
            platform->SemaphoreDestroy(s_FileSema);
    }
    s_IOBufferSema = nullptr;
    s_IOMutex = nullptr;
    s_FileSema = nullptr;
}
