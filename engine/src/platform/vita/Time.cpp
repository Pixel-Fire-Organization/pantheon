#include "Platform.h"

extern "C" {
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
}

double VitaPlatform::GetTimeSeconds() const
{
    static const SceUInt64 s_Origin = sceKernelGetProcessTimeWide();
    return static_cast<double>(sceKernelGetProcessTimeWide() - s_Origin) / 1000000.0;
}

void VitaPlatform::SleepMicros(uint32_t microseconds) { sceKernelDelayThread(microseconds); }
