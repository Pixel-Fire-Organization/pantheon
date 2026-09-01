#include "Platform.h"

#include <windows.h>

// ---------------------------------------------------------------------------
// QueryPerformanceCounter, not clock().
//
// clock() on Windows is wall-clock since process start but only ~1 ms
// resolution, which is a sixteenth of a 60 Hz frame - far too coarse to measure
// the logic/render/wait split the perf logger reports. QPC is monotonic and
// sub-microsecond.
// ---------------------------------------------------------------------------

namespace
{

    LARGE_INTEGER QueryFrequency()
    {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        return freq;
    }

    LARGE_INTEGER QueryOrigin()
    {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return now;
    }

} // namespace

double Win32Platform::GetTimeSeconds() const
{
    // Frequency is fixed for the life of the process, and the origin makes the
    // returned values small enough to keep double precision comfortable.
    static const LARGE_INTEGER s_Frequency = QueryFrequency();
    static const LARGE_INTEGER s_Origin = QueryOrigin();

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart - s_Origin.QuadPart) / static_cast<double>(s_Frequency.QuadPart);
}

void Win32Platform::SleepMicros(uint32_t microseconds)
{
    // Sleep() has millisecond granularity. Round up so a sub-millisecond request
    // still yields the CPU rather than spinning; the IO worker is the only caller
    // and it just wants to back off.
    const DWORD ms = static_cast<DWORD>((microseconds + 999u) / 1000u);
    Sleep(ms);
}
