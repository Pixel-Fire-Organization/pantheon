#include <ctime>
#include <mach/mach_time.h>

#include "Platform.h"

namespace
{

    mach_timebase_info_data_t QueryTimebase()
    {
        mach_timebase_info_data_t info;
        mach_timebase_info(&info);
        return info;
    }

} // namespace

double MacosPlatform::GetTimeSeconds() const
{
    static const mach_timebase_info_data_t s_Timebase = QueryTimebase();
    static const uint64_t s_Origin = mach_absolute_time();

    const uint64_t ticks = mach_absolute_time() - s_Origin;
    return static_cast<double>(ticks) * static_cast<double>(s_Timebase.numer) / static_cast<double>(s_Timebase.denom) / 1.0e9;
}

void MacosPlatform::SleepMicros(uint32_t microseconds)
{
    struct timespec request;
    request.tv_sec = static_cast<time_t>(microseconds / 1000000u);
    request.tv_nsec = static_cast<long>(microseconds % 1000000u) * 1000L;

    struct timespec remaining;
    while (nanosleep(&request, &remaining) != 0)
        request = remaining;
}
