/* ==========================================================================
 *  ingame/menu/tim_dat/file_tex_dat.h
 *
 *  The two thumbnail paks the collected-documents page loads (file_tex_dat.o).
 *
 *  Pure data: the object has no .text at all, only these eight bytes of
 *  .sdata.  menu_file.c is the only consumer -- it loads file_tex_pack[0]
 *  alongside the photograph page and [1] alongside the map page, and hands
 *  each to PK2SendVramOne() one thumbnail at a time.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MENU_TIM_DAT_FILE_TEX_DAT_H
#define _INGAME_MENU_TIM_DAT_FILE_TEX_DAT_H

/* [0] the photograph thumbnails, [1] the map thumbnails. */
extern int file_tex_pack[2];        /* sdata 3f07b8 */

#endif /* _INGAME_MENU_TIM_DAT_FILE_TEX_DAT_H */
