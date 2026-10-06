// FILE: /home/zero_rom/zero2np/src/graphics/effect/effect_pak.c
//
// COMPLETE.  All three ZERO2.MAP .text symbols plus the module's three
// statics.  Reserve2DPacket() and Reserve2DPacket_Load() really are empty in
// this build -- 8 bytes of `jr ra` each -- so nothing ever fills draw_pri[]
// and SortEffectPacket() always sorts an empty list.  Both facts are the
// ROM's, not a reconstruction gap.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), 0x0015bf58.

#include "effect_pak.h"

#include <stdio.h>

/* --------------------------------------------------------------------------
 *  The effect packet sort list.
 *
 *  One (priority, packet index) pair per reserved 2D effect packet.  The two
 *  counters are the high-water marks the overflow test below reads: draw_pri[]
 *  has room for 4096 entries and the 2D packet ring for 8192 quadwords, and
 *  the constants in SortEffectPacket() are exactly those two capacities.
 * ------------------------------------------------------------------------ */
static int    ndpkt;                                              /* sdata 3efeb8 */
static int    ndpri;                                              /* sdata 3efebc */
static u_int  draw_pri[4096][2];                                  /* bss 4667b0 */

/* Reservation was compiled out of this build; the call sites survive because
 * they pin the ROM's statement order in every packet builder that uses them. */
void Reserve2DPacket(u_int pri)
{
    (void)pri;
}

void Reserve2DPacket_Load(void)
{
}

/* --------------------------------------------------------------------------
 *  Order the reserved 2D effect packets by priority.                ROM 44
 *
 *  A plain selection sort -- the inner pass drags the largest remaining
 *  priority into slot i, so the list comes out *descending*.  The comparison
 *  is unsigned (`sltu`), so a priority with the top bit set sorts first rather
 *  than last.
 *
 *  Overflow is not clamped: the whole list is discarded and both counters are
 *  reset, which drops a frame of effect packets rather than scribbling past
 *  either buffer.
 * ------------------------------------------------------------------------ */
void SortEffectPacket(void)
{
    int    i, j;
    u_int  tmp;

    if (ndpri >= 4096 || ndpkt >= 8192)                             /* 49 */
    {
        printf("2D-PacketBuffer is Over!! [%d,%d]\n", ndpri, ndpkt); /* 50 */
        ndpri = 0;                                                  /* 51 */
        ndpkt = 0;                                                  /* 52 */
        return;                                                     /* 53 */
    }

    for (i = 0; i < ndpri - 1; i++)                                 /* 57 */
    {
        for (j = i + 1; j < ndpri; j++)                             /* 58 */
        {
            if (draw_pri[i][0] < draw_pri[j][0])                    /* 59 */
            {
                tmp = draw_pri[j][0];                               /* 60 */
                draw_pri[j][0] = draw_pri[i][0];                    /* 61 */
                draw_pri[i][0] = tmp;                               /* 62 */

                tmp = draw_pri[j][1];                               /* 63 */
                draw_pri[j][1] = draw_pri[i][1];                    /* 64 */
                draw_pri[i][1] = tmp;                               /* 65 */
            }
        }                                                           /* 67 */
    }                                                               /* 68 */
}                                                                   /* 69 */
