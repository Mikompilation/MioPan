/* ==========================================================================
 *  ingame/menu/menu_photo.h
 *
 *  The in-game menu's photo album page (menu_photo.o) -- the 2x8 grid of
 *  sixteen thumbnails, the enlarged picture and its information window, and
 *  the two pop-up menus (per-photo protect/delete/sort, and the sort order
 *  itself).
 *
 *  Six exports; the twenty-six helpers they are built from are static and
 *  stay in the .c.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MENU_MENU_PHOTO_H
#define _INGAME_MENU_MENU_PHOTO_H

#include "eetypes.h"

/* The page's own state.  `step` is the inner state machine -- which of the
 * four pads runs and which pop-up is drawn -- and is deliberately separate
 * from menu_wrk.step, which is the page's own open/load/run/close ladder.
 *
 * The two cursors are the grid's: `csr_yoko` picks the column (0 or 1) and
 * `csr_tate` the row (0..7); csr_num[csr_tate][csr_yoko] turns the pair into
 * an album slot.  `sub_csr` and `sort_csr` are the two pop-up menus' rows.
 *
 * `photo_flg` is the enlarged picture's cache: 0 means "the selection moved,
 * decompress it again", 1 means "the work area already holds it".
 * `sort_flg` is the sort menu's direction toggle -- the same row picked twice
 * sorts the other way. */
typedef struct                      /* 0xb */
{
    /* 0x0 */ char   step;              /* MENU_PHOTO_MODE_*                 */
    /* 0x1 */ char   csr_yoko;          /* grid column, 0..1                 */
    /* 0x2 */ char   csr_tate;          /* grid row, 0..7                    */
    /* 0x3 */ char   sub_csr;           /* MENU_PHOTO_SUB_*                  */
    /* 0x4 */ char   sort_csr;          /* MENU_PHOTO_SORT_*                 */
    /* 0x5 */ char   photo_flg;         /* the big picture is decompressed   */
    /* 0x6 */ char   sort_flg;          /* 0 ascending, 1 descending         */
    /* 0x7 */ char   sub_anim_step;     /* the pop-up's own fade             */
    /* 0x8 */ char   sub_anim_timer;
    /* 0x9 */ char   csr_timer;         /* the shared cursor pulse           */
    /* 0xa */ u_char rgb;
} MENU_PHOTO_CTRL;

/* The page's open/close fade.  Unlike the notes and camera pages this one has
 * no second pair here -- the pop-up's fade lives on MENU_PHOTO_CTRL, because
 * MenuPhotoSubMenuDisp() drives it itself rather than through
 * MenuInOutAnimCtrl(). */
typedef struct                      /* 0x2 */
{
    /* 0x0 */ char anim_step;           /* MENU_PHOTO_ANIM_*                 */
    /* 0x1 */ char anim_timer;
} MENU_PHOTO_DISP;

/* menu_ctrl[] row 3: one frame of the page, and its drawing. */
void MenuPhoto(void);                       /* 0x201430 */
void MenuPhotoDisp(void);                   /* 0x202048 */

/* The page's own pak.  menu_top.c starts the load as the hub fades out, so
 * the album is resident by the time the page draws. */
void GetMenuPhotoTexMem(void);              /* 0x201368 */
void MenuPhotoTexLoadReq(void);             /* 0x2013b8 */
void LiberateMenuPhotoTexMem(void);         /* 0x201fc8 */
void MenuPhotoTexLoadCancel(void);          /* 0x201ff8 */

#endif /* _INGAME_MENU_MENU_PHOTO_H */
