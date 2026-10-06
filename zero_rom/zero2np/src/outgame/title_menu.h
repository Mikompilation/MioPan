/* ==========================================================================
 *  outgame/title_menu.h
 *
 *  The title menu (title_menu.c): the horizontal entry row, its cursor, and
 *  the phase each entry moves to.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _OUTGAME_TITLE_MENU_H
#define _OUTGAME_TITLE_MENU_H

#include <sys/types.h>              /* u_char */

/* title_menu.c's only static (sbss 3f4fe0).  The type is here rather than in
 * the .c because types.txt carries it alongside TITLE_WRK / TITLE_DISP_CTRL. */
typedef struct                      /* 0x1 */
{
    /* 0x0 */ char csr;             /* selected entry, 0..7 */
} TITLE_MENU_CTRL;

void TitleMenuCtrlInit(void);                                   /* 0x26a7f8 */
void TitleMenuMain(void);                                       /* 0x26a800 */
void TitleMenuDispMain(int off_x, int off_y, u_char alpha);     /* 0x26ac10 */

#endif /* _OUTGAME_TITLE_MENU_H */
