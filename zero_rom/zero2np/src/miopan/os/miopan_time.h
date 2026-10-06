#ifndef MIOPAN_TIME_H
#define MIOPAN_TIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Host timing wrapper.  Confines SDL timing (SDL_Delay / performance counters /
// SDL_GetCurrentTime / SDL_TimeToDateTime) to the miopan layer so the SDK shims
// don't call SDL directly.

// Sleep the calling thread for at least `ms` milliseconds.
void MioPan_Sleep(unsigned int ms);

// Sleep the calling thread for at least `ns` nanoseconds.
void MioPan_SleepNs(uint64_t ns);

// Milliseconds since some fixed, unspecified epoch (monotonic-ish).
uint64_t MioPan_GetTicksMs(void);

// High-resolution monotonic timing, using SDL's performance counter units.
uint64_t MioPan_GetPerformanceCounter(void);
uint64_t MioPan_GetPerformanceFrequency(void);

// Broken-down local wall-clock time.  Returns non-zero on success; on failure
// the fields are zeroed and 0 is returned.  Fields use natural ranges
// (year = full year e.g. 2026, month 1-12, day 1-31, hour 0-23, ...).
typedef struct
{
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
} MioPan_DateTime;

int MioPan_GetLocalTime(MioPan_DateTime *out);

#ifdef __cplusplus
}
#endif

#endif /* MIOPAN_TIME_H */
