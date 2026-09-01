#include "Platform.h"

#include "Exit.h"

extern "C" {
#include <pspkernel.h>
}

namespace
{
    volatile int g_ExitRequested = 0;
    SceUID g_CallbackThread = -1;

    int ExitCallback(int /*arg1*/, int /*arg2*/, void* /*common*/)
    {
        g_ExitRequested = 1;
        return 0;
    }

    int CallbackThread(SceSize /*args*/, void* /*argp*/)
    {
        const int callbackId = sceKernelCreateCallback("EngineExit", ExitCallback, nullptr);
        sceKernelRegisterExitCallback(callbackId);
        sceKernelSleepThreadCB();
        return 0;
    }
}

void PspExit_Install()
{
    g_CallbackThread = sceKernelCreateThread("EngineExitCb", CallbackThread, PLATFORM_WORKER_THREAD_PRIORITY, 0x1000, THREAD_ATTR_USER, nullptr);
    if (g_CallbackThread >= 0)
        sceKernelStartThread(g_CallbackThread, 0, nullptr);
}

bool PspExit_Requested() { return g_ExitRequested != 0; }

void PspExit_Finish(int /*code*/)
{
    sceKernelExitGame();
    for (;;)
    {
    }
}

bool PspPlatform::WindowOpen(const WindowDesc& /*desc*/) { return true; }

void PspPlatform::WindowClose() {}

bool PspPlatform::WindowShouldClose() const { return PspExit_Requested(); }

void PspPlatform::GetFramebufferSize(uint32_t* outWidth, uint32_t* outHeight) const
{
    if (outWidth)
        *outWidth = GFX_SCREEN_WIDTH;
    if (outHeight)
        *outHeight = GFX_SCREEN_HEIGHT;
}

void* PspPlatform::GetNativeWindowHandle() const { return nullptr; }
