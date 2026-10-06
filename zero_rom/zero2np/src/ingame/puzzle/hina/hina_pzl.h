/* ==========================================================================
 *  ingame/puzzle/hina/hina_pzl.h
 *
 *  The hina-dan puzzle (hina_pzl.o): a 4x4 sliding-tile board of festival
 *  dolls, against a 30-second clock.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_PUZZLE_HINA_HINA_PZL_H
#define _INGAME_PUZZLE_HINA_HINA_PZL_H

#include "eetypes.h"

/* --------------------------------------------------------------------------
 *  Board and phase state.
 *
 *  `step` is the phase: 0 first frame, 1 playing, 2 leaving, 3 waiting for
 *  the finder's sound bank to come back.  `sub_step` selects which pad and
 *  display handler runs -- 0 board, 1 quit prompt, 2 cleared, 3 timed out --
 *  and is only ever changed through HinaPuzzleReqNextSubStep(), which fades
 *  the old one out first.
 *
 *  hina_pos[] is the live board; see puzzle_hina_dat.h for what a cell holds.
 * ------------------------------------------------------------------------ */
typedef struct                      /* 0x54 */
{
    /* 0x00 */ char step;
    /* 0x01 */ char sub_step;
    /* 0x02 */ char next_sub_step;
    /* 0x03 */ char csr_tate;       /* cursor row                            */
    /* 0x04 */ char csr_yoko;       /* cursor column                         */
    /* 0x05 */ char clear_flg;
    /* 0x06 */ char exit_csr;       /* quit prompt: 0 = yes, 1 = no          */
    /* 0x08 */ int  timer;          /* frames left                           */
    /* 0x0c */ int  hina_pos[4][4];
    /* 0x4c */ char in_anim_flg;    /* the fade-in has finished at least once */
    /* 0x50 */ int  snd_id;
} HINA_PZL_CTRL;

/* Display timers.  anim_* is the whole-screen black fade, sub_anim_* the
 * cross-fade between sub_steps, and the last three free-run the two glow
 * pulses and the smoke overlay. */
typedef struct                      /* 0xe */
{
    /* 0x0 */ char  anim_step;
    /* 0x2 */ short anim_timer;
    /* 0x4 */ char  sub_anim_step;
    /* 0x6 */ short sub_anim_timer;
    /* 0x8 */ short csr_anim_timer;
    /* 0xa */ short smoke_anim_timer;
    /* 0xc */ short stand_anim_timer;
} HINA_PZL_DISP;

/* The board is also drawn during GID_PUZZLE_CROSSFADE, before the puzzle
 * phase exists; this is the smoke timer that survives into it. */
typedef struct                      /* 0x2 */
{
    /* 0x0 */ short anim_timer;
} HINA_PZL_CROSS_DISP;

void HinaPuzzleExeInit(void);
void HinaPuzzleCrossDispInit(void);
int  HinaPuzzleMain(void);
void HinaPuzzleDispMain(void);
void HinaPuzzleCrossScreenDisp(int off_x, int off_y, u_char alpha);

/* Both are exported, but nothing outside this module calls them. */
void HinaPuzzleNumberDisp(int data, int num, float x, float y, u_char alpha,
                          int pri, u_char zero_flg);
void HinaPuzzleNumberDisp_One(int data, float x, float y, u_char alpha, int pri);

#endif /* _INGAME_PUZZLE_HINA_HINA_PZL_H */
