#include "Platform.h"

extern "C" {
#include <pspkernel.h>
#include <psprtc.h>
}

double PspPlatform::GetTimeSeconds() const
{
    static const uint64_t s_Origin = sceKernelGetSystemTimeWide();
    return static_cast<double>(sceKernelGetSystemTimeWide() - s_Origin) / 1000000.0;
}

void PspPlatform::SleepMicros(uint32_t microseconds) { sceKernelDelayThread(microseconds); }
