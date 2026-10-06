/* ==========================================================================
 *  ingame/loading/loading_dat.h
 *
 *  Declares the loading-screen sprite table loading_tex[] (defined in
 *  loading_dat.c): the compact SPRT_DAT source records LoadingBgDisp() and
 *  LoadingNowLoadingDisp() (loading.c) index by layer.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_LOADING_LOADING_DAT_H
#define _INGAME_LOADING_LOADING_DAT_H

#include "../../graphics/graph2d/g2d_draw.h"    /* SPRT_DAT */

extern SPRT_DAT loading_tex[5];         /* data 3199d8 */

#endif /* _INGAME_LOADING_LOADING_DAT_H */
