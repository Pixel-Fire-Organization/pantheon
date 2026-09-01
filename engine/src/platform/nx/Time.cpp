#include "Platform.h"

#include <switch.h>

double NxPlatform::GetTimeSeconds() const
{
    const uint64_t now = m_suspended ? m_suspendStartTick : armGetSystemTick();
    const uint64_t elapsed = now - m_tickOrigin - m_suspendedTicks;
    return static_cast<double>(elapsed) / static_cast<double>(armGetSystemTickFreq());
}

void NxPlatform::SleepMicros(uint32_t microseconds) { svcSleepThread(static_cast<int64_t>(microseconds) * 1000); }
