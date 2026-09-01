#include <ctime>

#include "Platform.h"

extern "C" {
#include <delaythread.h>
}

double Ps2Platform::GetTimeSeconds() const
{
    // clock() on the EE is 32-bit microseconds and wraps to 0 after ~71.6 min.
    // Track the wraps here rather than in every caller: a single backwards step
    // would otherwise make one frame's dt hugely negative and teleport anything
    // that integrates it.
    static double s_WrapOffset = 0.0;
    static double s_LastRaw = 0.0;

    const double raw = static_cast<double>(clock()) / CLOCKS_PER_SEC;
    if (raw < s_LastRaw)
        s_WrapOffset += s_LastRaw;
    s_LastRaw = raw;

    return s_WrapOffset + raw;
}

void Ps2Platform::SleepMicros(uint32_t microseconds) { DelayThread(static_cast<int>(microseconds)); }
