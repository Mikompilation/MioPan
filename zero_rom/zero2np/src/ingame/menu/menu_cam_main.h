/* ==========================================================================
 *  ingame/menu/menu_cam_main.h
 *
 *  The camera menu's frame (menu_cam_main.o) -- the two paks both camera
 *  pages draw from, the step ladder that loads them, the two-slot dispatch
 *  between the top page and the upgrade editor, and the shared widgets those
 *  pages use to draw a lens, an addition function, a camera part and a
 *  number.
 *
 *  Twenty exports; the six helpers they are built from are static and stay in
 *  the .c.
 *
 *  This module is the *only* place either camera pak is claimed, loaded or
 *  freed, which is why GetMenuCameraPk2Addr() / GetMenuCameraEdtPk2Addr()
 *  exist -- menu_cam_top.o and menu_cam_edit.o have no buffers of their own.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MENU_MENU_CAM_MAIN_H
#define _INGAME_MENU_MENU_CAM_MAIN_H

#include "eetypes.h"

/* The whole work block, straight out of types.txt.  `step` is the outer
 * loader ladder and `sub_step` the inner one that runs the two pages; `mode`
 * and `next_mode` index the three dispatch tables below.
 *
 * `init_type` is who opened the menu -- 0 from menu_cam.o (the in-game menu's
 * camera page) and 1 from setup.c's Mission Mode camera screen.  The two
 * pages read it back through GetMenuCamInitType(). */
typedef struct                      /* 0x6 */
{
    /* 0x0 */ char sub_step;
    /* 0x1 */ char step;
    /* 0x2 */ char mode;
    /* 0x3 */ char next_mode;
    /* 0x4 */ char exit_flg;
    /* 0x5 */ char init_type;
} MENU_CAM_MAIN_CTRL;

/* MENU_CAM_MAIN_CTRL::mode / next_mode -- the two dispatch-table slots.
 * These names are the port's; the ROM writes the literals. */
#define MENU_CAM_MODE_TOP       0
#define MENU_CAM_MODE_EDIT      1
#define MENU_CAM_MODE_NUM       2

/* ------------------------------------------------------------------------ */

/* Reset the work block and record who is opening the menu. */
void MenuCamMainInit(char init_type);

/* Claim both camera paks out of the caller's heap and start their loads.
 * The two callbacks are the heap the menu should use: mem_util's inside the
 * game (menu.c) and ol_load's outside it (outgame.c / title.c), which is what
 * lets the same screen serve both.  Asserts if a previous pair is still
 * installed -- MenuCamMainMemFree() is what clears them. */
void MenuCamMainBackGroundLoadReq(void *(*mem_get)(int),
                                  void (*mem_free)(void *));

/* One frame.  Non-zero once the player has left the camera menu. */
int  MenuCamMain(void);

/* Draw the page that is up, once both paks are resident. */
void MenuCamMainDisp(void);

/* Cancel both loads, hand both buffers back and drop the callbacks. */
void MenuCamMainMemFree(void);

/* Leave the camera menu, and the two page changes.  All three only park a
 * request; MenuCamModeMain() acts on it when the running page reports done. */
void MenuCamExitReq(void);
void MenuCamGoToTopReq(void);
void MenuCamGoToEditReq(void);

int   GetMenuCamInitType(void);
void *GetMenuCameraPk2Addr(void);
void *GetMenuCameraEdtPk2Addr(void);

/* How much of the camera the player has.  The first three count raised bits
 * in CCameraPowerUp; note GetHaveReinforcedLensNum() starts at 1, because
 * CAMERA_SUB_FUNC_NONE owns bit 0 and is not a lens. */
int GetHaveAddFuncNum(void);
int GetHaveEquipFuncNum(void);
int GetHaveReinforcedLensNum(void);

/* How many of the three tray slots have a sub-function on them. */
int GetEquipReinforcedLensNum(void);

/* The shared widgets.  All three take a menu_camera_tex[] index out of a
 * private table and draw nothing at all for a -1 entry (no table in this
 * build has one -- it is provision for art that does not exist).
 *
 * `lens_label` is a CAMERA_SUB_FUNC_ENUM and the icon is tinted with that
 * function's own equip_func_tbl[] colour; `flg` on the parts widget picks the
 * fitted art over the unfitted. */
void MenuCamCmnReinforcedLensDisp(float x, float y, u_char alpha,
                                  int lens_label);
void MenuCamCmnAdditionalFunctionDisp(float x, float y, u_char alpha,
                                      int add_label);
void MenuCamCmnEquipFunctionDisp(float x, float y, u_char alpha,
                                 int parts_label, u_char flg);

/* `num` digits of `data` in one of the camera page's three digit faces:
 * type 0 is the 14x17 small one, types 1 and 2 the two 17x21 colours.  Any
 * other type asserts and draws with off_x 0, stacking every digit. */
void MenuCamNumberDisp(int data, int num, int x, int y, u_char alpha, int pri,
                       u_char type, u_char zero_flg);

#endif /* _INGAME_MENU_MENU_CAM_MAIN_H */
