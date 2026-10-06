/* ==========================================================================
 *  ingame/puzzle/hina/puzzle_hina_dat.h
 *
 *  The hina-dan puzzle's board layout and sprite table (puzzle_hina_dat.o),
 *  a data-only module.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_PUZZLE_HINA_PUZZLE_HINA_DAT_H
#define _INGAME_PUZZLE_HINA_PUZZLE_HINA_DAT_H

#include "../../../graphics/graph2d/g2d_draw.h"      /* SPRT_DAT */

#define PUZZLE_HINA_TEX_NUM 45

/* The 4x4 board as it is dealt.  A cell holds a doll number (2..10, which is
 * also its puzzle_hina_tex[] index), -1 for "no cell on this shelf" or -2 for
 * the one empty slot the dolls slide into. */
extern int   hina_first_pos[4][4];                          /* data 33d110 */

/* Dolls 9 and 10 are nailed down: the cursor refuses to land on them and they
 * never move.  Two entries, scanned linearly. */
extern int   no_move_hina[2];                               /* sdata 3f3b00 */

/* Screen position of each cell, and of the glow that marks the selected doll.
 * -1 marks a cell that does not exist -- the tables carry the same holes as
 * hina_first_pos. */
extern float hina_pos_x[4][4];                              /* data 33d150 */
extern float hina_pos_y[4][4];                              /* data 33d190 */
extern float hina_flea_pos_x[4][4];                         /* data 33d1d0 */
extern float hina_flea_pos_y[4][4];                         /* data 33d210 */

/* 0..1   the two-piece background
 * 2..10  the nine dolls
 * 11..17 their selected-glow versions (doll number + 9)
 * 18..19 the timer plate
 * 20..29 white digits, 30..39 red digits
 * 40..42 the caption plate, its text, and the vignette
 * 43..44 the two candle stands  */
extern SPRT_DAT puzzle_hina_tex[PUZZLE_HINA_TEX_NUM];       /* data 33d250 */

#endif /* _INGAME_PUZZLE_HINA_PUZZLE_HINA_DAT_H */
