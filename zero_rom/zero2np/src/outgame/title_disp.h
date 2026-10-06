/* ==========================================================================
 *  outgame/title_disp.h
 *
 *  Title-screen drawing (title_disp.c): logos, menu arrows, caption strip and
 *  the scrolling background.  Partially reconstructed -- see the STATUS note
 *  at the top of title_disp.c.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _OUTGAME_TITLE_DISP_H
#define _OUTGAME_TITLE_DISP_H

#include <sys/types.h>              /* u_char */

void DispTitleCursorL(float x, float y, u_char alpha, u_char rgb);  /* 0x2698b8 */
void DispTitleCursorR(float x, float y, u_char alpha, u_char rgb);  /* 0x269958 */
void DispTitleZeroLogo(int off_x, int off_y, u_char alpha);         /* 0x2699f8 */
void DispTitleTecmoLogo(int off_x, int off_y, u_char alpha);        /* 0x269be0 */
void DispTitleBack(int *timer, void *tex_addr);                     /* 0x269cd0 */
void TitleCaptionDisp(int off_x, int off_y, u_char alpha);          /* 0x26a6f8 */

#endif /* _OUTGAME_TITLE_DISP_H */
