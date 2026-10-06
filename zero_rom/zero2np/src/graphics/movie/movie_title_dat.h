/* ==========================================================================
 *  graphics/movie/movie_title_dat.h
 *
 *  movie_title_dat.o's tables.  The object has no code at all -- .data
 *  336ec8..338400 and one .rodata entry are the whole translation unit.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRAPHICS_MOVIE_MOVIE_TITLE_DAT_H
#define _GRAPHICS_MOVIE_MOVIE_TITLE_DAT_H

#include "../graph2d/g2d_draw.h"    /* SPRT_DAT                              */
#include "movie_title.h"            /* MOVIE_TITLE_DAT                       */

#ifdef __cplusplus
extern "C" {
#endif

/* Caption list per scene number, NULL in the last slot.  data 338280 */
extern MOVIE_TITLE_DAT *movie_title_dat[72];

/* The caption's backing plate: left cap, stretchable middle, right cap (the
 * left cap again, mirrored).  data 3383a0 */
extern SPRT_DAT movie_title_base_tex[3];

/* Message ids that are drawn whatever the subtitle option says, -1
 * terminated.  rdata 3c1a38 */
extern int every_disp_subtitles[33];

#ifdef __cplusplus
}
#endif

#endif /* _GRAPHICS_MOVIE_MOVIE_TITLE_DAT_H */
