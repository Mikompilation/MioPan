/* ==========================================================================
 *  ingame/menu/menu_memo.h
 *
 *  The in-game menu's notes page (menu_memo.o) -- the list of every memo the
 *  player has picked up, the paper it is written on, and the paged reader
 *  that opens on top of it.
 *
 *  Six exports; the twenty-four helpers they are built from are static and
 *  stay in the .c.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MENU_MENU_MEMO_H
#define _INGAME_MENU_MENU_MEMO_H

#include "eetypes.h"
#include "menu_cmn.h"                           /* MENU_REF_CTRL */

/* One list row.  MenuMemoSetDispData() builds disp_memo_data[] as a compacted
 * copy of the twenty memo slots -- only the ones the player holds, in label
 * order -- so the list walks without re-testing every memo.
 *
 * `state` and `msg_step` are the memo.o values latched at that moment -- and
 * neither is ever read back.  menu_memo.o stores both and then calls
 * GetMemoState() / GetMemoMsgStep() live at every use site, so the two
 * fields are dead in this build; they are here because the ROM writes
 * them. */
typedef struct                      /* 0x8 */
{
    /* 0x0 */ int    memo_label;
    /* 0x4 */ u_char state;
    /* 0x5 */ u_char msg_step;
} DISP_MEMO_DATA;

/* The page's own state.  `mode` indexes both menu_memo_pad_func[] and
 * menu_memo_disp_func[]; `sub_step` is the mode-change animation's own step
 * and reuses menu_wrk.step's numbering (MAIN / MOVE). */
typedef struct                      /* 0x10 */
{
    /* 0x0 */ u_char        mode;           /* MENU_MEMO_MODE_*              */
    /* 0x1 */ u_char        next_mode;      /* what the fade is moving to    */
    /* 0x2 */ u_char        sub_step;       /* MENU_MEMO_SUB_*               */
    /* 0x4 */ MENU_REF_CTRL ref_ctrl;
} MENU_MEMO_CTRL;

/* Two independent open/close fades: the page's own, and the inner one the
 * reader rides in on. */
typedef struct                      /* 0x6 */
{
    /* 0x0 */ char   anim_step;         /* MENU_MEMO_ANIM_*                  */
    /* 0x1 */ char   anim_timer;
    /* 0x2 */ u_char rgb;               /* the cursor pulse                  */
    /* 0x3 */ char   scroll_timer;
    /* 0x4 */ char   sub_anim_step;
    /* 0x5 */ char   sub_anim_timer;
} MENU_MEMO_DISP;

/* menu_ctrl[] row 5: one frame of the page, and its drawing. */
void MenuMemo(void);                        /* 0x1ffc10 */
void MenuMemoDisp(void);                    /* 0x200388 */

/* The page's own pak.  menu_top.c starts the load as the hub fades out, so
 * the paper is resident by the time the page draws. */
void GetMenuMemoTexMem(void);               /* 0x1ffa88 */
void MenuMemoTexLoadReq(void);              /* 0x1ffad8 */
void LiberateMenuMemoTexMem(void);          /* 0x2002f0 */
void MenuMemoTexLoadCancel(void);           /* 0x200320 */

#endif /* _INGAME_MENU_MENU_MEMO_H */
