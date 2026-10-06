#include "miopan_rand.h"

#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>   // SDL_GetPerformanceCounter, for MioPan_SRand(0)

namespace {

// Private LCG state rather than SDL's global one.  SDL_rand()/SDL_srand() are
// process-wide, so anything else linked in that reseeds them would perturb the
// game's stream; keeping our own state makes MioPan_Rand() reproducible no
// matter what else touches SDL.
//
// The seed echoes newlib's: its _seed is statically initialised to 1 and the
// game never calls srand, so the EE's stream was the same on every boot.
// MioPan_SRand() is there for anyone who wants variety instead.
Uint64 g_rand_state = 1;

} // namespace

extern "C" {

int MioPan_Rand(void)
{
    // SDL_rand_bits_r() advances a 64-bit LCG and returns its TOP 32 bits (the
    // ones with the long period), so the high bits are the good ones and a
    // plain >> 1 is the right way down to 31.  That yields [0, 0x7fffffff]
    // inclusive -- exactly newlib's range -- with no modulo bias, which
    // SDL_rand(MIOPAN_RAND_MAX) could not do: it is a half-open [0, n) and
    // would never return MIOPAN_RAND_MAX itself.
    return (int)(SDL_rand_bits_r(&g_rand_state) >> 1);
}

void MioPan_SRand(unsigned long long seed)
{
    // SDL_srand()'s convention: 0 means "pick something time-based for me".
    g_rand_state = seed ? (Uint64)seed : SDL_GetPerformanceCounter();
}

} // extern "C"
