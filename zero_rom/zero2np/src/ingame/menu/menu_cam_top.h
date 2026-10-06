/* ==========================================================================
 *  ingame/menu/menu_cam_top.h
 *
 *  The camera menu's top page (menu_cam_top.o) -- the screen the player sees
 *  when the camera menu opens: the camera's own portrait on the left, and on
 *  the right three rows (addition functions / film / camera parts) plus the
 *  basic-performance ladder, the lens slots, the level gems and the spirit
 *  score.
 *
 *  Only four symbols are exported, and all four are reached from exactly one
 *  place -- menu_cam_main.o's three dispatch tables.  Everything else in the
 *  object (38 statics) is this page's own pad handling and drawing.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MENU_MENU_CAM_TOP_H
#define _INGAME_MENU_MENU_CAM_TOP_H

#include "eetypes.h"

/* The page's work block, straight out of types.txt.
 *
 * `mode` is the dispatch-table index -- 3 is the page proper and 0/1/2 are
 * the three sub-selectors it can open.  `step` is the phase ladder; there is
 * no load step here, because menu_cam_main.o owns both paks.
 *
 * The cursor is two-dimensional: `csr_yoko` picks the column (0 the camera
 * portrait, 1 the three rows) and `csr_tate` the row within the right-hand
 * column.  `csr_yoko_backup` survives a trip into the upgrade editor, so
 * coming back puts the cursor where it was -- MenuCamTopFirstInit() seeds it
 * to 1 and MenuCamTopCtrlInit() restores from it on every entry.
 *
 * `add_csr` and `equip_csr` are the sub-selectors' own cursors and persist
 * across openings of this page. */
typedef struct                      /* 0x7 */
{
    /* 0x0 */ char mode;
    /* 0x1 */ char step;
    /* 0x2 */ char csr_yoko;
    /* 0x3 */ char csr_tate;
    /* 0x4 */ char add_csr;
    /* 0x5 */ char equip_csr;
    /* 0x6 */ char csr_yoko_backup;
} MENU_CAM_TOP_CTRL;

/* The cross-fade and the cursor pulse.  `anim_step` / `anim_timer` are
 * Zero2Anim2D_InOutAnimCtrl()'s pair; `csr_anim_timer` is
 * Zero2Anim2D_CsrAnimCtrl()'s, and is reset every time a sub-selector opens
 * so its cursor starts bright. */
typedef struct                      /* 0x3 */
{
    /* 0x0 */ char anim_step;
    /* 0x1 */ char anim_timer;
    /* 0x2 */ char csr_anim_timer;
} MENU_CAM_TOP_DISP;

/* ------------------------------------------------------------------------ */

/* Seed csr_yoko_backup, then reset.  Called once, from MenuCamMain()'s
 * first-init step -- MenuCamTopInit() runs on every later entry and does not
 * touch the backup. */
void MenuCamTopFirstInit(void);          /* 0x1ee710 */

void MenuCamTopInit(void);               /* 0x1ee738 */

/* One frame of the page.  Returns non-zero once the page has finished
 * closing -- step OUT and the fade all the way out -- which is what puts
 * MenuCamModeMain()'s sub_step at 3 and lets the parked request through. */
int  MenuCamTopMain(void);               /* 0x1ee788 */

void MenuCamTopDisp(void);               /* 0x1ef7c0 */

#endif /* _INGAME_MENU_MENU_CAM_TOP_H */
