#include "miopan_time.h"

#include <string.h>

#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_time.h>

extern "C" {

void MioPan_Sleep(unsigned int ms)
{
    SDL_Delay((Uint32)ms);
}

void MioPan_SleepNs(uint64_t ns)
{
    SDL_DelayNS((Uint64)ns);
}

uint64_t MioPan_GetTicksMs(void)
{
    return (uint64_t)SDL_GetTicks();
}

uint64_t MioPan_GetPerformanceCounter(void)
{
    return (uint64_t)SDL_GetPerformanceCounter();
}

uint64_t MioPan_GetPerformanceFrequency(void)
{
    return (uint64_t)SDL_GetPerformanceFrequency();
}

int MioPan_GetLocalTime(MioPan_DateTime *out)
{
    SDL_Time     now;
    SDL_DateTime dt;

    if (out == 0)
    {
        return 0;
    }

    if (!SDL_GetCurrentTime(&now) || !SDL_TimeToDateTime(now, &dt, true))
    {
        memset(out, 0, sizeof(*out));
        return 0;
    }

    out->year   = dt.year;
    out->month  = dt.month;
    out->day    = dt.day;
    out->hour   = dt.hour;
    out->minute = dt.minute;
    out->second = dt.second;
    return 1;
}

}
