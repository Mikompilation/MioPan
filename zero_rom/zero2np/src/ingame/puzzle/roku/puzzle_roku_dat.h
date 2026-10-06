/* ==========================================================================
 *  ingame/puzzle/roku/puzzle_roku_dat.h
 *
 *  The rokumen (six-face) puzzle's answer, book titles and sprite table
 *  (puzzle_roku_dat.o), a data-only module.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_PUZZLE_ROKU_PUZZLE_ROKU_DAT_H
#define _INGAME_PUZZLE_ROKU_PUZZLE_ROKU_DAT_H

#include "../../../graphics/graph2d/g2d_draw.h"          /* SPRT_DAT     */
#include "../../../graphics/graph3d/ctl/fixed_array.h"   /* reference_fixed_array */

#define PUZZLE_ROKU_TEX_NUM 33

/* The order the five books have to end up in, left to right.  Books are
 * numbered 8..12, which is also their puzzle_roku_tex[] index. */
extern reference_fixed_array<int, 5> six_puzzle_answer;     /* sdata 3f3b88 */

extern int   six_pzl_book_label[5][2];                      /* data 33df30 */
extern float shelf_book_x[5];                               /* data 33df58 */

/* 0..1   the two-piece background
 * 2      the read-message glow
 * 3..7   the shelf cursor, one per slot
 * 8..12  the five books on the shelf
 * 13     the book-select window plate, 14..18 the books in it
 * 19..22 the left/right arrows and their plates
 * 23..32 the caption plates and button prompts  */
extern SPRT_DAT puzzle_roku_tex[PUZZLE_ROKU_TEX_NUM];       /* data 33df70 */

#endif /* _INGAME_PUZZLE_ROKU_PUZZLE_ROKU_DAT_H */
