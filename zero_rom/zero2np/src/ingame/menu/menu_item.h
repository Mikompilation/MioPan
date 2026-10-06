/* ==========================================================================
 *  ingame/menu/menu_item.h
 *
 *  The in-game menu's inventory page (menu_item.o) -- the seven-row list of
 *  everything the player is carrying, the picture that cross-fades in beside
 *  it, and the "use this?" window.
 *
 *  Six exports; the twenty-two helpers they are built from are static and
 *  stay in the .c.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MENU_MENU_ITEM_H
#define _INGAME_MENU_MENU_ITEM_H

#include "eetypes.h"
#include "menu_cmn.h"                           /* MENU_REF_CTRL */

/* One list row.  disp_item[] is built by SetDispItemData() as a compacted
 * copy of the inventory -- only the ids the player actually holds, in id
 * order -- so the list can be walked without re-testing every item.
 *
 * 0xff in item_id is the "empty row" marker the first loop lays down. */
typedef struct                      /* 0x8 */
{
    /* 0x0 */ int item_id;
    /* 0x4 */ int have_num;
} DISP_ITEM_DATA;

/* The page's own state.  `sub_step` indexes menu_item_pad_func[]. */
typedef struct                      /* 0x10 */
{
    /* 0x0 */ u_char        sub_step;
    /* 0x1 */ u_char        cross_fade_flg;   /* which cross-fade slot is live */
    /* 0x2 */ u_char        conf_csr;         /* 0 = yes, 1 = no               */
    /* 0x4 */ MENU_REF_CTRL ref_ctrl;
} MENU_ITEM_CTRL;

typedef struct                      /* 0x4 */
{
    /* 0x0 */ char   anim_step;         /* MENU_ITEM_ANIM_*                  */
    /* 0x1 */ char   anim_timer;
    /* 0x2 */ u_char rgb;               /* the scrollbar arrows' pulse       */
    /* 0x3 */ char   scroll_timer;
} MENU_ITEM_DISP;

/* menu_ctrl[] row 0: one frame of the page, and its drawing. */
void MenuItem(void);                        /* 0x1f92c8 */
void MenuItemDisp(void);                    /* 0x1f9c80 */

/* The page's own pak.  menu_top.c starts the load as the hub fades out, so
 * the picture is resident by the time the page draws. */
void GetMenuItemTexMem(void);               /* 0x1f9108 */
void MenuItemTexLoadReq(void);              /* 0x1f9158 */
void LiberateMenuItemTexMem(void);          /* 0x1f9bf0 */
void MenuItemTexLoadCancel(void);           /* 0x1f9c20 */

#endif /* _INGAME_MENU_MENU_ITEM_H */
