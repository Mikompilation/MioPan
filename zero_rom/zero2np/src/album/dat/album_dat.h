/* ==========================================================================
 *  album/dat/album_dat.h
 *
 *  The photo album's sprite bank and the three language-dependent x tables
 *  the save screen places its album name and arrows with.  album_dat.o has no
 *  code of its own -- its 0xd4 of .text is nothing but the fixed_array.h
 *  boilerplate every TU in this folder carries.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _ALBUM_DAT_ALBUM_DAT_H
#define _ALBUM_DAT_ALBUM_DAT_H

#include "../../graphics/graph2d/g2d_draw.h"     /* SPRT_DAT */

/* 434 records covering every album page: the edit page's two albums, the
 * photo viewer's frame, the info window, the confirm window and the save
 * screen.  See album_dat.c for the group map. */
#define ALBUM_TEX_NUM   434

extern SPRT_DAT album_tex[ALBUM_TEX_NUM];       /* data 2d4788 */

/* Per album type, per language (GetLanguage() 0..4).  Types 5 and 6 have no
 * save-screen presence, so their rows are all -1 -- AlbumSaveSel* reject them
 * with a PRINT_WARNING before ever reaching these. */
extern const int album_name_x_tbl[7][5];        /* rdata 3a0230 */
extern const int album_left_csr_x[7][5];        /* rdata 3a02c0 */
extern const int album_right_csr_x[7][5];       /* rdata 3a0350 */

#endif /* _ALBUM_DAT_ALBUM_DAT_H */
