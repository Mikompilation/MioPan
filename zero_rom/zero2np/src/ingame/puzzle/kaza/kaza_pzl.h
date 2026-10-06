/* ==========================================================================
 *  ingame/puzzle/kaza/kaza_pzl.h
 *
 *  The kazaguruma (pinwheel) puzzle (kaza_pzl.o): five panels that each turn
 *  in place until every one of them faces the right way.
 *
 *  The two boards -- this one and kaza2_pzl.o -- share these types and the
 *  puzzle_kaza_dat.o sprite table, but keep their own copy of the state, so
 *  kaza2_pzl.c includes this header.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_PUZZLE_KAZA_KAZA_PZL_H
#define _INGAME_PUZZLE_KAZA_KAZA_PZL_H

#include "eetypes.h"

#include "../../../graphics/graph3d/ctl/fixed_array.h"   /* fixed_array */

/* rot_num[] is each panel's quarter-turn count; the puzzle compares it (mod
 * four) against the answer the board was built with. */
typedef struct                      /* 0x24 */
{
    /* 0x00 */ char step;
    /* 0x01 */ char mode;
    /* 0x02 */ char next_mode;
    /* 0x03 */ char clear_flg;
    /* 0x04 */ int  snd_id;
    /* 0x08 */ char csr_yoko;
    /* 0x09 */ char csr_tate;
    /* 0x0a */ char exit_csr;
    /* 0x0b */ char rot_anim_flg;
    /* 0x0c */ fixed_array<int, 5> rot_num;
    /* 0x20 */ char remainder_frequency;    /* turns left before it gives up */
} KAZA_PZL_CTRL;

typedef struct                      /* 0x20 */
{
    /* 0x00 */ char  anim_step;
    /* 0x01 */ char  sub_anim_step;
    /* 0x02 */ char  rot_anim_step;
    /* 0x04 */ short flare_anim_timer;
    /* 0x06 */ short rot_anim_timer;
    /* 0x08 */ short anim_timer;
    /* 0x0a */ short sub_anim_timer;
    /* 0x0c */ fixed_array<float, 5> panel_rot;
} KAZA_PZL_DISP;

void KazaPuzzleExeInit(void);
int  KazaPuzzleMain(void);
void KazaPuzzleDispMain(void);
void KazaPuzzleCrossScreenDisp(int off_x, int off_y, u_char alpha);

#endif /* _INGAME_PUZZLE_KAZA_KAZA_PZL_H */
