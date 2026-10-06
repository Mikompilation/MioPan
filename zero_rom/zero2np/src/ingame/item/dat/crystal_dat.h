/* ==========================================================================
 *  ingame/item/dat/crystal_dat.h
 *
 *  Subtitle timing for the spirit-stone (crystal) audio recordings
 *  (crystal_dat.o).  Pure data -- the object has no .text at all.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_ITEM_DAT_CRYSTAL_DAT_H
#define _INGAME_ITEM_DAT_CRYSTAL_DAT_H

#include "eetypes.h"

/* One subtitle line: which message to show, and the movie frame range to show
 * it over.  The ROM has a single MOVIE_TITLE_DAT (types.txt) shared with the
 * cutscene captions, so the type comes from its owner rather than being
 * declared a second time here -- the duplicate was a conflicting typedef the
 * moment one TU pulled in both headers, which menu_radio.c is the first to
 * do. */
#include "../../../graphics/movie/movie_title.h"     /* MOVIE_TITLE_DAT */

/* 40 crystals plus the fallback row at [40]. */
#define CRYSTAL_TITLE_DAT_MAX 41

/* The individual tables are globals in the ROM rather than statics, so they
 * are declared here even though only crystal_title_dat[] reaches them. */
extern MOVIE_TITLE_DAT crystal_title_dummy[1];      /* data 2d8e60 */
extern MOVIE_TITLE_DAT crystal_title_000[5];
extern MOVIE_TITLE_DAT crystal_title_001[9];
extern MOVIE_TITLE_DAT crystal_title_002[7];
extern MOVIE_TITLE_DAT crystal_title_003[7];
extern MOVIE_TITLE_DAT crystal_title_004[7];
extern MOVIE_TITLE_DAT crystal_title_005[5];
extern MOVIE_TITLE_DAT crystal_title_006[8];
extern MOVIE_TITLE_DAT crystal_title_007[9];
extern MOVIE_TITLE_DAT crystal_title_008[4];
extern MOVIE_TITLE_DAT crystal_title_009[10];
extern MOVIE_TITLE_DAT crystal_title_010[5];
extern MOVIE_TITLE_DAT crystal_title_011[6];
extern MOVIE_TITLE_DAT crystal_title_012[8];
extern MOVIE_TITLE_DAT crystal_title_013[7];
extern MOVIE_TITLE_DAT crystal_title_014[7];
extern MOVIE_TITLE_DAT crystal_title_015[5];
extern MOVIE_TITLE_DAT crystal_title_016[6];
extern MOVIE_TITLE_DAT crystal_title_017[8];
extern MOVIE_TITLE_DAT crystal_title_018[7];
extern MOVIE_TITLE_DAT crystal_title_019[6];
extern MOVIE_TITLE_DAT crystal_title_020[10];
extern MOVIE_TITLE_DAT crystal_title_021[10];
extern MOVIE_TITLE_DAT crystal_title_022[7];
extern MOVIE_TITLE_DAT crystal_title_023[5];
extern MOVIE_TITLE_DAT crystal_title_024[5];
extern MOVIE_TITLE_DAT crystal_title_025[4];
extern MOVIE_TITLE_DAT crystal_title_026[5];
extern MOVIE_TITLE_DAT crystal_title_027[6];
extern MOVIE_TITLE_DAT crystal_title_028[8];
extern MOVIE_TITLE_DAT crystal_title_029[7];
extern MOVIE_TITLE_DAT crystal_title_030[7];
extern MOVIE_TITLE_DAT crystal_title_031[8];
extern MOVIE_TITLE_DAT crystal_title_032[9];
extern MOVIE_TITLE_DAT crystal_title_033[6];
extern MOVIE_TITLE_DAT crystal_title_034[6];
extern MOVIE_TITLE_DAT crystal_title_035[8];
extern MOVIE_TITLE_DAT crystal_title_036[9];
extern MOVIE_TITLE_DAT crystal_title_037[8];
extern MOVIE_TITLE_DAT crystal_title_038[6];
extern MOVIE_TITLE_DAT crystal_title_039[8];

extern MOVIE_TITLE_DAT *crystal_title_dat[CRYSTAL_TITLE_DAT_MAX];   /* data 2d9bc8 */

#endif /* _INGAME_ITEM_DAT_CRYSTAL_DAT_H */
