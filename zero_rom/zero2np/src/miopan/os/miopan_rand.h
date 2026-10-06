#ifndef MIOPAN_RAND_H
#define MIOPAN_RAND_H

#ifdef __cplusplus
extern "C" {
#endif

// Host randomness wrapper.  Confines SDL's PRNG to the miopan layer the same
// way miopan_time.h confines SDL's clocks -- and, more to the point, pins the
// range every reconstructed rand() site sees.
//
// The EE linked newlib's rand.o (ZERO2.MAP 0x2a4298, dragged in by
// accessory.o) whose RAND_MAX is 0x7fffffff, so every ROM expression of the
// form `rand() / RAND_MAX`, `rand() - 0x3fffffff` or `rand() % n` was written
// against a 31-bit result.  The host's rand() is whatever its C runtime feels
// like: MSVC and MinGW both cap at 0x7fff, glibc at 0x7fffffff.  That 65536x
// spread is not cosmetic -- accessory.c's `(rand() - 0x3fffffff) / 2147483520`
// collapses to a constant -0.5 on a 15-bit runtime, so every scattered
// accessory blows the same way, and foot_se.c's `rand() % 30` loses most of
// its spread.  Which of the two you get depends on the toolchain, so the same
// build tree misbehaves differently per platform.
//
// MioPan_Rand() restores the ROM's range everywhere, and takes its bits from
// SDL's PRNG so the stream itself is identical across OS and compiler.

// The EE's RAND_MAX.  MioPan_Rand() returns a value in [0, MIOPAN_RAND_MAX].
#define MIOPAN_RAND_MAX 0x7fffffff

// (float)MIOPAN_RAND_MAX as GCC 2.96-ee spelled it.  The EE compiler truncated
// float literals to 24 bits rather than rounding to nearest, so the ROM's
// divisor is lit4 0x3ee9e8 == 2147483520.0f (0x4effffff) where a conforming
// compiler emits 2147483648.0f.  Use this wherever the disassembly shows that
// constant and the port's arithmetic matches the ROM bit-for-bit.
#define MIOPAN_RAND_MAXF 2147483520.0f

// Uniform pseudo-random integer in [0, MIOPAN_RAND_MAX] -- the ROM's rand().
int MioPan_Rand(void);

// Reseed the stream.  Passing 0 asks SDL for a time-based seed, which is
// SDL_srand()'s own convention and so NOT the C srand(0).
//
// Nothing has to call this: the stream starts from a fixed seed, which is what
// the ROM did as well.  The game never references srand -- ZERO2.MAP names
// only `rand` as accessory.o's undefined symbol -- so newlib's _seed stayed at
// its static initialiser and a real PS2 replayed the same sequence every boot.
void MioPan_SRand(unsigned long long seed);

#ifdef __cplusplus
}
#endif

#endif // MIOPAN_RAND_H
