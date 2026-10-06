/* ==========================================================================
 *  system/eeiop/spu_voice.c
 *
 *  SPU2 voice allocator.  Nothing more than a bitmask per core; the IOP owns
 *  the hardware, the EE only has to agree with itself about which voice
 *  numbers are in play.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include <stdio.h>

#include "spu_voice.h"

#include "../../common/utility2.h"  /* PRINT_ASSERT */

int core_voices[2];                                                          /* sdata 3f49d0 */

void InitSPUVoice(void)
{
    core_voices[0] = 0;                                                      /* 11 */
    core_voices[1] = 0;                                                      /* 12 */
}                                                                            /* 14 */

void InitSPUVoiceCore(int core)
{
    core_voices[core] = 0;                                                   /* 17 */
}

int GetSPUVoiceCore(int core)
{
    int i;

    for (i = 0; i < SPU_VOICE_MAX; i++)                                      /* 28 */
    {
        if ((core_voices[core] >> i & 1) == 0)                               /* 29 */
        {
            core_voices[core] |= 1 << i;                                     /* 30 */
            return i;                                                        /* 31 */
        }
    }

    return -1;                                                               /* 36 */
}

void FreeSPUVoiceCore(int core, int voice_no)
{
    /* -1 is GetSPUVoiceCore()'s failure value, so a caller that never checked
     * it would otherwise clear every bit of the mask. */
    if (voice_no == -1)                                                      /* 40 */
    {
        PRINT_ASSERT("NoVoice");                                             /* 41 */
        return;
    }

    core_voices[core] &= ~(1 << voice_no);                                   /* 44 */
}
