/* ==========================================================================
 *  ingame/menu/menu_soul.h
 *
 *  The in-game menu's ghost list ("soul list") page (menu_soul.o) -- every
 *  ghost the player has photographed, the best score each was shot at, and
 *  the cross-faded picture and description beside the list.
 *
 *  Eight exports; the thirty-four helpers they are built from are static and
 *  stay in the .c.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MENU_MENU_SOUL_H
#define _INGAME_MENU_MENU_SOUL_H

#include "eetypes.h"
#include "menu_cmn.h"                           /* MENU_REF_CTRL */

#include "../../common/save_data.h"             /* MC_SAVE_DATA  */
#include "../../graphics/graph3d/ctl/fixed_array.h"

/* One list row.  MenuSoulSetDispData() builds disp_soul_list_data[] as a
 * compacted copy of the 176 ghost list labels -- only the ones the player
 * holds, in label order -- so the list walks without re-testing every entry.
 *
 * `state` is the soul_list.o value latched at that moment and is never read
 * back: every consumer calls GetPlyrSoulListState() live instead.  It is
 * dead in this build and is here because the ROM writes it.  Same shape as
 * DISP_MEMO_DATA's two latched fields. */
typedef struct                      /* 0x8 */
{
    /* 0x0 */ int ghost_list_label;
    /* 0x4 */ int state;
} DISP_SOUL_LIST_DATA;

/* The page's own state.  `mode` picks which of the three pad handlers and
 * which of the three drawing bodies MenuSoul() / MenuSoulDisp() run -- both
 * are plain switches here rather than the dispatch tables menu_item.o and
 * menu_memo.o use. */
typedef struct                      /* 0x20 */
{
    /* 0x00 */ char              mode;                  /* MENU_SOUL_MODE_* */
    /* 0x01 */ char              cross_fade_flg;        /* live picture slot */
    /* 0x04 */ MENU_REF_CTRL     ref_ctrl;
    /* 0x10 */ int               before_list_data_pos;  /* row before a move */
    /* 0x14 */ fixed_array<int,3> max_score_order;      /* the top 3 scores  */
} MENU_SOUL_CTRL;

/* The page's open/close fade plus the cursor pulse.  disp_msg_id[] is per
 * cross-fade slot, so the outgoing description keeps being drawn while the
 * incoming one fades up; -1 means "nothing to say". */
typedef struct                      /* 0xc */
{
    /* 0x0 */ char   anim_step;         /* MENU_SOUL_ANIM_*                  */
    /* 0x1 */ char   anim_timer;
    /* 0x2 */ u_char rgb;               /* the cursor pulse                  */
    /* 0x3 */ char   scroll_timer;
    /* 0x4 */ fixed_array<int,2> disp_msg_id;
} MENU_SOUL_DISP;

/* menu_ctrl[] row 7: one frame of the page, and its drawing. */
void MenuSoul(void);                        /* 0x2068e8 */
void MenuSoulDisp(void);                    /* 0x2070b8 */

/* The page's own pak.  menu_top.c starts the load as the hub fades out, so
 * the background is resident by the time the page draws. */
void GetMenuSoulTexMem(void);               /* 0x206770 */
void MenuSoulTexLoadReq(void);              /* 0x2067c0 */
void LiberateMenuSoulTexMem(void);          /* 0x206fd0 */
void MenuSoulTexLoadCancel(void);           /* 0x207000 */

/* Clears the "the list is complete" badge, so the congratulation message is
 * shown again on a fresh game.  soul_list.o's PlyrSoulListInit() is the one
 * caller. */
void MenuSoulListCompFlgInit(void);         /* 0x206660 */

/* Save-block descriptor for that badge -- one byte, and one of the 49 blocks
 * in a save slot (system/mc/dat/save_data.c's save_game_data[]). */
void SetSave_ListCompDispFlg(MC_SAVE_DATA *data);   /* 0x208b80 */

#endif /* _INGAME_MENU_MENU_SOUL_H */
