/* ==========================================================================
 *  outgame/mis_sel_disp.h
 *
 *  The mission-select screen's drawing half (mis_sel_disp.c): the six visible
 *  list rows with their ranks, times and scores, the achievement counters
 *  above them, the scrollbar, the cursor, the slide-out mini menu, and the
 *  caption window's cross-fade.
 *
 *  Only six of its sixteen functions are exported; mission_sel.c drives the
 *  screen through those.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _OUTGAME_MIS_SEL_DISP_H
#define _OUTGAME_MIS_SEL_DISP_H

#include <sys/types.h>              /* u_char */

/* h:m:s in the shared message font, at an arbitrary position.  A negative
 * component prints as dashes rather than a number, which is how a mission with
 * no time set is shown. */
void MissionDrawTime(int iHour, int iMin, int iSec, int iOffX, int iOffY,
                     u_char ucAlpha);                       /* 0x2128a8 */

/* The screen's caption group.  Ignores both offsets. */
void MissionCaptionDisp(int off_x, int off_y, u_char alpha);            /* 0x212d98 */

/* The whole list: six rows starting at iTopID, with the cursor on row iCsr. */
void MissionDrawSelect(void *pMisTexAddr, int iTopID, int iCsr,
                       u_char ucAlpha);                     /* 0x212ef8 */

/* The Album/Save menu that slides out beside the cursor.  `fMove` is 0..1
 * across the slide and `iFlg` is "not fully out yet" -- when it is 0 the menu
 * is drawn at full width and the master alpha is scaled down instead. */
void MissionDrawMiniMenu(void *pOutGameTex, void *pMisTexAddr, int iCsr,
                         u_char ucMstAlpha, float fMove, int iSelCsr,
                         int iFlg);                         /* 0x213118 */

/* Cross-fade the caption window to a new message.  Returns 1 when the message
 * actually changed, which is what mission_sel.c plays the cursor cue on. */
int  MisFadeSetMsg(int iNewMsg);                            /* 0x2134a0 */

/* Draw the caption window and both sides of the cross-fade. */
void MisFadeProc(u_char ucMstAlpha);                        /* 0x2135b0 */

#endif /* _OUTGAME_MIS_SEL_DISP_H */
