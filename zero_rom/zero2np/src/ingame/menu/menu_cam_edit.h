/* ==========================================================================
 *  ingame/menu/menu_cam_edit.h
 *
 *  The camera menu's upgrade editor (menu_cam_edit.o) -- the page reached by
 *  pressing CROSS on the camera portrait of the top page.  Three things can
 *  be done here, and they are the three rows of its own menu: fit a lens into
 *  one of the camera's sub-function slots, spend a level gem on an upgrade
 *  slot, and spend spirit points to raise an upgrade a grade.
 *
 *  Only three symbols are exported, and all three are reached from exactly
 *  one place -- menu_cam_main.o's dispatch tables.  Everything else in the
 *  object (78 statics, the largest count in the folder) is this page's own
 *  pad handling and drawing.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MENU_MENU_CAM_EDIT_H
#define _INGAME_MENU_MENU_CAM_EDIT_H

#include "eetypes.h"

#include "menu_cmn.h"                   /* MENU_REF_CTRL */

/* The page's work block, straight out of types.txt.
 *
 * `mode` is the index into both dispatch tables -- twelve slots covering the
 * top menu, the two selectors and every error/confirm window.  `step` is the
 * phase ladder; there is no load step, because menu_cam_main.o has both paks
 * resident before this page is ever dispatched to.
 *
 * There are four cursors and they are independent: `menu_csr` walks the three
 * rows of the page's own menu, `equip_pos_csr` the three sub-function slots,
 * `lens_csr` the row inside the lens list's window, and `edit_sel_csr` the
 * combined list the gem and power-up selectors share -- 0..2 the three basic
 * performances, 3.. the lens rows below them.  `conf_csr` is the yes/no
 * window's, and opens on 1 (no).
 *
 * `sp_equip_init_flg` and `edit_init_flg` are one-shot latches: each says
 * "the *other* selector was the last one open, so reset my scroll".  That is
 * what lets a trip through the gem selector and back leave the lens list
 * where it was, but a trip through the lens equip screen reset it.
 *
 * `gem_anim_flg` is up while a gem is being awarded; MenuCamEditMain() skips
 * the pad entirely for as long as it is, and the two gem drawing functions
 * are what lower it. */
typedef struct                      /* 0x18 */
{
    /* 0x00 */ char mode;
    /* 0x01 */ char step;
    /* 0x02 */ char menu_csr;
    /* 0x03 */ char equip_pos_csr;
    /* 0x04 */ char lens_csr;
    /* 0x05 */ char edit_sel_csr;
    /* 0x06 */ char conf_csr;
    /* 0x07 */ char sp_equip_init_flg;
    /* 0x08 */ char edit_init_flg;
    /* 0x09 */ char gem_anim_flg;
    /* 0x0c */ MENU_REF_CTRL ref_ctrl;
} MENU_CAM_EDIT_CTRL;

/* The cross-fade, the two cursor pulses and the idle gem shimmer.
 * `anim_step` / `anim_timer` are Zero2Anim2D_InOutAnimCtrl()'s pair;
 * `csr_anim_timer` and `scroll_anim_timer` are two separate
 * Zero2Anim2D_CsrAnimCtrl() counters, so the slot cursor and the scrollbar
 * do not pulse in step. */
typedef struct                      /* 0x6 */
{
    /* 0x0 */ char      anim_step;
    /* 0x1 */ char      anim_timer;
    /* 0x2 */ char      csr_anim_timer;
    /* 0x3 */ char      scroll_anim_timer;
    /* 0x4 */ short int gem_anim_timer;
} MENU_CAM_EDIT_DISP;

/* One row of the lens list: the compacted list of sub-functions the player
 * actually owns.  A whole struct for one int, but it is what the ROM's
 * fixed_array<DISP_LENS_DATA,10> holds. */
typedef struct                      /* 0x4 */
{
    /* 0x0 */ int lens_label;       /* a CAMERA_SUB_FUNC_ENUM, 1..9 */
} DISP_LENS_DATA;

/* One gem award in flight.  `data_pos` is -1 when idle -- note MenuCamEdit-
 * Init() seeds both of these with memset(-1), so all four bytes go to 0xff
 * and `timer` starts at -1 too; the requesters zero it. */
typedef struct                      /* 0x4 */
{
    /* 0x0 */ char      data_pos;   /* which row, or -1 for "nothing running" */
    /* 0x1 */ char      lv;         /* which gem in the row lights up        */
    /* 0x2 */ short int timer;
} GEM_ANIM_CTRL;

/* ------------------------------------------------------------------------ */

/* Reset the work block, rebuild the lens list from what the player owns, and
 * clear both gem animations.  Called from MenuCamModeMain() every time the
 * editor is entered. */
void MenuCamEditInit(void);             /* 0x1e7418 */

/* One frame.  Returns non-zero once the page has finished closing -- step
 * OUT and the fade all the way out -- which is what lets MenuCamModeMain()
 * release the parked page-change request. */
int  MenuCamEditMain(void);             /* 0x1e75c8 */

void MenuCamEditDisp(void);             /* 0x1e9160 */

#endif /* _INGAME_MENU_MENU_CAM_EDIT_H */
