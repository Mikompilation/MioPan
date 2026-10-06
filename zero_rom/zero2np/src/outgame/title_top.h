/* ==========================================================================
 *  outgame/title_top.h
 *
 *  The title top screen (title_top.c): the ZERO / TECMO logos, the PRESS
 *  START plate, and the input test that moves on to the title menu.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _OUTGAME_TITLE_TOP_H
#define _OUTGAME_TITLE_TOP_H

#include <sys/types.h>              /* u_char */

void TitleTopMain(void);                                        /* 0x26b2f8 */
void TitleTopDispMain(int off_x, int off_y, u_char alpha);      /* 0x26b380 */

#endif /* _OUTGAME_TITLE_TOP_H */
