/* ==========================================================================
 *  ingame/menu/menu_cmn_disp.h
 *
 *  The widget layer every in-game menu page draws from (menu_cmn_disp.o):
 *  the player-data plate down the left of the hub, the selected/unselected
 *  row frames, the two rules, the confirm and yes/no windows, and the shared
 *  digit run.  Eight exports; the five helpers the plate is built from are
 *  static and stay in the .c.
 *
 *  Every entry point is stateless -- the object has no work block and no
 *  file-scope data at all.  Each one re-uploads whichever pak it samples
 *  through PK2SendVram() and draws straight out of menu_cmn_dat[].
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MENU_MENU_CMN_DISP_H
#define _INGAME_MENU_MENU_CMN_DISP_H

#include "eetypes.h"

/* The player-data plate: the status frame, the health bar, the fitted
 * sub-function lenses with their levels, and the film readout.  Drawn at a
 * caller-chosen top-left; every part is offset from it. */
void MenuPlyrDataDisp(int x, int y, u_char alpha);              /* 0x1f2b08 */

/* `num` digits of `data` in the shared menu face, placed at x/y.  A thin
 * wrapper over DrawCmnNumberTex() that supplies menu_cmn_dat[15] (the zero
 * glyph) as the run's base sprite. */
void MenuNumberDisp(int data, int num, int x, int y, u_char alpha, int pri,
                    u_char zero_flg);                           /* 0x1f3448 */

/* The two modal windows, both 592x112 at off + (24, 178).  Confirm is the
 * frame on its own; YesNo adds the two answers and the cursor, which sits on
 * whichever side menu_yes_no_ctrl.csr names. */
void MenuCmnConfirmWinDisp(int off_x, int off_y, u_char alpha, u_int pri);
                                                                /* 0x1f3530 */
void MenuCmnYesNoWinDisp(int off_x, int off_y, u_char alpha, u_int pri);
                                                                /* 0x1f3580 */

/* One list row's frame, `w` wide.  Both are one 82-pixel half drawn twice,
 * mirrored, each scaled to w/2; they differ only in the art and in which pak
 * it comes from -- selected samples the background pak, unselected the
 * play-data one. */
void MenuCmnSelFrameDisp(float x, float y, float w, u_char alpha, u_int pri);
                                                                /* 0x1f3688 */
void MenuCmnNonSelFrameDisp(float x, float y, float w, u_char alpha, u_int pri);
                                                                /* 0x1f3808 */

/* The two rules: Tate is vertical and `h` tall, Yoko horizontal and `w` wide.
 * Each is a cap, a stretched middle and a mirrored cap; a length shorter than
 * the two caps together drops the middle and draws the caps overlapping. */
void MenuCmnLineTateDisp(float x, float y, float h, u_char alpha, u_int pri);
                                                                /* 0x1f3988 */
void MenuCmnLineYokoDisp(float x, float y, float w, u_char alpha, u_int pri);
                                                                /* 0x1f3b70 */

#endif /* _INGAME_MENU_MENU_CMN_DISP_H */
