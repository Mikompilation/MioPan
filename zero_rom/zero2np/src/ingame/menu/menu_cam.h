/* ==========================================================================
 *  ingame/menu/menu_cam.h
 *
 *  The in-game menu's camera page (menu_cam.o) -- menu_ctrl[] row 2.  Two
 *  functions, no data: both forward straight to menu_cam_main.o.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MENU_MENU_CAM_H
#define _INGAME_MENU_MENU_CAM_H

/* menu_ctrl[] row 2: one frame of the page, and its drawing. */
void MenuCam(void);                     /* 0x1e7288 */
void MenuCamDisp(void);                 /* 0x1e7310 */

#endif /* _INGAME_MENU_MENU_CAM_H */
