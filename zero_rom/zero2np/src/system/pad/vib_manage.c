// FILE: /home/zero_rom/zero2np/src/system/pad/vib_manage.c
//
// Timed vibration.  pad.o's VibrateRequest1/2 set a motor for one frame only:
// once pad.o has sent a non-zero pad_direct[] it queues VibrateRequest(id, 0, 0)
// behind it (0x22c4b4), so the next frame switches both motors off unless
// someone asks again.  Anything that wants a pulse of a given length therefore
// has to re-request it every frame.  This file is that: two frame countdowns,
// one per motor, and a pump that re-issues whichever is still running.
//
//   motor 1 (large, on/off)    vib1_time            VibrateRequest1(0, 1)
//   motor 2 (small, 0..255)    vib2_time, vib2_pow  VibrateRequest2(0, vib2_pow)
//
// SetVibrate()'s `type` picks the motor: 0 is the large one and anything else
// the small one.  `pow` is only stored for the small motor -- the large one has
// no strength -- which is why every type-0 caller passes a dummy 1.  Every
// arming call site in the build (EneHitEffectMain, EneDmgMain,
// IgEffectEneDmgReqZero) asks for the same pair, 30 frames of the large motor
// and 40 of the small one at full strength: the jolt of a ghost's hit.
// MisEnSubLoadWait()'s SetVibrate(0, 0, 0) is the one cancel, and it stops only
// the large motor.
//
// CallVibrate() runs once a frame from IngameDrawSub(), one_Story_Ene_Dead(),
// MoviePhaseJobAfter() and Graph2dMain(); a jal scan finds exactly those four
// sites.  Both counters are post-decremented whether or not they were still
// running -- the store sits in the delay slot of the branch that tests the old
// value -- so an idle counter keeps walking down past zero every frame and
// only the `> 0` test makes that harmless.  It would take 2^31 frames to wrap.
//
// Nothing here looks at opt_wrk.pad_vib.  The ROM gates the option one level
// down, inside each VibrateRequest*, so a pulse armed with vibration off still
// counts down normally and simply never reaches the pad.
//
// InitVibrate() is dead code: exported, and zero jal/j sites anywhere in the
// loadable segments.  The statics are .sbss and so start at zero anyway.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), vib_manage.o.
// All 3 ZERO2.MAP exports and all 3 globals.txt statics; .text is accounted
// for byte for byte (0x26cf80..0x26d00c = 0x10 + 0x20 + 0x5c, with no
// alignment fill and no unlisted body).  The object has no .rodata, .data or
// .sdata at all -- its only data is the 0xc bytes of .sbss below.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.  The ROM's brace style is K&R: each function's opening
// brace shares its signature's line (10, 20, 29) and SetVibrate's else arm is
// the `} else {` on line 24, which leaves exactly one line for each store.

#include "vib_manage.h"             // this file's public API

#include "pad.h"                    // VibrateRequest1 / VibrateRequest2

/* Frames left on the large motor (sbss 3f4ff0). */
static int vib1_time;
/* Frames left on the small motor (sbss 3f4ff4). */
static int vib2_time;
/* The small motor's strength, 0..255 (sbss 3f4ff8). */
static int vib2_pow;

// ──────────────────────────────────────────────────────────────────────
// Stop both motors' countdowns.  Three stores, the last in jr ra's delay slot.

void InitVibrate(void)                                                  /* 10 */
{
    vib1_time = 0;                                                      /* 11 */
    vib2_time = 0;                                                      /* 12 */
    vib2_pow  = 0;                                                      /* 13 */
}

// ──────────────────────────────────────────────────────────────────────
// Arm one motor for `time` frames.  The test is a plain zero compare (beq a0,
// zero), and the small motor's arm is the one that falls through, so it is
// written first.  The lone nop at 0x26cfa4 between the two returns is the
// left-over delay slot of the then-arm's jump to the epilogue, which reorg
// turned into a jr ra of its own; it carries no source.

void SetVibrate(int type, int time, int pow)                            /* 20 */
{
    if (type != 0)                                                      /* 21 */
    {
        vib2_time = time;                                               /* 22 */
        vib2_pow  = pow;                                                /* 23 */
    }
    else                                                                /* 24 */
    {
        vib1_time = time;                                               /* 25 */
    }
}

// ──────────────────────────────────────────────────────────────────────
// Re-issue whatever is still running.  Each test and its call share one source
// line: the jal sites carry no line note of their own, which GCC would have
// emitted had the call been on the next line.  The small motor's strength goes
// out with an lhu -- the low half of the int, which is the conversion to
// VibrateRequest2's u_short parameter.

void CallVibrate(void)                                                  /* 29 */
{
    if (vib1_time-- > 0) VibrateRequest1(0, 1);                         /* 30 */
    if (vib2_time-- > 0) VibrateRequest2(0, vib2_pow);                  /* 31 */
}
