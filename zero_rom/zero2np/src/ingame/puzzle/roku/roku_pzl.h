/* ==========================================================================
 *  ingame/puzzle/roku/roku_pzl.h
 *
 *  The rokumen (six-face) puzzle (roku_pzl.o): five books to be taken off a
 *  shelf and put back in the order the riddle asks for.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_PUZZLE_ROKU_ROKU_PZL_H
#define _INGAME_PUZZLE_ROKU_ROKU_PZL_H

#include "eetypes.h"

#include "../../../graphics/graph3d/ctl/fixed_array.h"   /* fixed_array */

/* have_book[] is the hand, book_shelf[] the five shelf slots, and
 * order_enter_book[] the order they were put back in -- which is what
 * six_puzzle_answer[] is compared against. */
typedef struct                      /* 0x48 */
{
    /* 0x00 */ char step;
    /* 0x01 */ char sub_step;
    /* 0x02 */ char next_sub_step;
    /* 0x03 */ char clear_flg;
    /* 0x04 */ char book_sel_csr;
    /* 0x05 */ char next_book_csr;
    /* 0x06 */ char book_shelf_csr;
    /* 0x07 */ char exit_csr;
    /* 0x08 */ int  snd_id;
    /* 0x0c */ fixed_array<int, 5> have_book;
    /* 0x20 */ fixed_array<int, 5> book_shelf;
    /* 0x34 */ fixed_array<int, 5> order_enter_book;
} SIX_PZL_CTRL;

typedef struct                      /* 0x10 */
{
    /* 0x0 */ char  anim_step;
    /* 0x1 */ char  sub_anim_step;
    /* 0x2 */ short anim_timer;
    /* 0x4 */ short sub_anim_timer;
    /* 0x6 */ char  move_anim_step;
    /* 0x7 */ char  move_rot;
    /* 0x8 */ char  msg_anim_step;
    /* 0x9 */ char  cap_win_anim_flg;
    /* 0xa */ short move_anim_timer;
    /* 0xc */ short msg_anim_timer;
    /* 0xe */ short shelf_anim_timer;
} SIX_PZL_DISP;

void SixPuzzleExeInit(void);
int  SixPuzzleMain(void);
void SixPuzzleDispMain(void);
void SixPuzzleCrossScreenDisp(int off_x, int off_y, u_char alpha);

#endif /* _INGAME_PUZZLE_ROKU_ROKU_PZL_H */
