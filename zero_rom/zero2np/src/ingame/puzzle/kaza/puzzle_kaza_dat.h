/* ==========================================================================
 *  ingame/puzzle/kaza/puzzle_kaza_dat.h
 *
 *  The two kazaguruma (pinwheel) boards' answer tables and sprite table
 *  (puzzle_kaza_dat.o), a data-only module shared by kaza_pzl.o and
 *  kaza2_pzl.o.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_PUZZLE_KAZA_PUZZLE_KAZA_DAT_H
#define _INGAME_PUZZLE_KAZA_PUZZLE_KAZA_DAT_H

#include "../../../graphics/graph2d/g2d_draw.h"          /* SPRT_DAT     */
#include "../../../graphics/graph3d/ctl/fixed_array.h"   /* reference_fixed_array */

#define PUZZLE_KAZA_TEX_NUM 58

/* One four-entry colour ring per panel: the colours of its four wings, read
 * clockwise.  A quarter-turn of the panel shifts which entry a given wing
 * shows, so GetKazaPuzzlePinWheelWingColor() is just this table indexed by
 * (wing + turns) mod 4. */
extern reference_fixed_array<int, 4> kaza_panel_center;      /* sdata 3f3b20 */
extern reference_fixed_array<int, 4> kaza_panel_left_up;     /* sdata 3f3b28 */
extern reference_fixed_array<int, 4> kaza_panel_right_up;    /* sdata 3f3b30 */
extern reference_fixed_array<int, 4> kaza_panel_right_down;  /* sdata 3f3b38 */
extern reference_fixed_array<int, 4> kaza_panel_left_down;   /* sdata 3f3b40 */

extern reference_fixed_array<int, 4> kaza2_panel_center;     /* sdata 3f3b48 */
extern reference_fixed_array<int, 4> kaza2_panel_left_up;    /* sdata 3f3b50 */
extern reference_fixed_array<int, 4> kaza2_panel_right_up;   /* sdata 3f3b58 */
extern reference_fixed_array<int, 4> kaza2_panel_right_down; /* sdata 3f3b60 */
extern reference_fixed_array<int, 4> kaza2_panel_left_down;  /* sdata 3f3b68 */

/* 0..1   the two-piece background
 * 2..6   the five panel shadows (12..16)
 * 7..11  emboss shadows, 2..6 emboss highlights
 * 17..20 the selection flare, one per cursor cell
 * 22..26 the five panels themselves
 * 27     the "turns left" plate, 28 the vignette, 29..38 its digits
 * 41..50 the Spanish digit set, 51 its plate
 * 51..52 the second board's background  */
extern SPRT_DAT puzzle_kaza_tex[PUZZLE_KAZA_TEX_NUM];        /* data 33d7f0 */

#endif /* _INGAME_PUZZLE_KAZA_PUZZLE_KAZA_DAT_H */
