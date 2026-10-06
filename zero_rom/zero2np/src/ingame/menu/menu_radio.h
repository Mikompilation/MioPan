/* ==========================================================================
 *  ingame/menu/menu_radio.h
 *
 *  The in-game menu's crystal-radio page (menu_radio.o) -- every spirit
 *  stone the player has picked up, and the recording each one plays back
 *  through the radio.
 *
 *  Seven exports; the thirty-one helpers they are built from are static and
 *  stay in the .c.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MENU_MENU_RADIO_H
#define _INGAME_MENU_MENU_RADIO_H

#include "eetypes.h"
#include "menu_cmn.h"                           /* MENU_REF_CTRL */

#include "../../graphics/graph3d/ctl/fixed_array.h"

/* One list row.  MenuRadioSetDispData() builds disp_crystal_data[] as a
 * compacted copy of the 40 crystal labels -- only the ones the player holds,
 * in label order -- so the list walks without re-testing every entry.
 *
 * `state` is the crystal.o value latched at that moment and is never read
 * back: MenuRadioNonHearFrameDisp() calls GetPlyrCrystalState() live
 * instead.  It is dead in this build and is here because the ROM writes it.
 * Same shape as DISP_SOUL_LIST_DATA's and DISP_MEMO_DATA's latched fields. */
typedef struct                      /* 0x8 */
{
    /* 0x0 */ int crystal_id;
    /* 0x4 */ int state;
} DISP_CRYSTAL_DATA;

/* The page's own state.  `sub_step` picks which of the three pad handlers
 * MenuRadio() runs and which of the three drawing bodies MenuRadioDisp()
 * runs -- both are plain switches here rather than the dispatch tables
 * menu_item.o and menu_memo.o use. */
typedef struct                      /* 0x14 */
{
    /* 0x00 */ u_char        sub_step;          /* MENU_RADIO_SUB_*          */
    /* 0x01 */ u_char        cross_fade_flg;    /* live picture slot         */
    /* 0x04 */ MENU_REF_CTRL ref_ctrl;
    /* 0x10 */ int           stream_id;         /* unused -- see the .c      */
} MENU_RADIO_CTRL;

/* The page's open/close fade, the cursor pulse, and the crystal picture's
 * own second fade -- which runs independently of the page's, because the
 * crystal brightens while its recording plays. */
typedef struct                      /* 0xc */
{
    /* 0x0 */ char   anim_step;             /* MENU_RADIO_ANIM_*             */
    /* 0x1 */ char   anim_timer;
    /* 0x2 */ char   crystal_anim_step;     /* MENU_RADIO_CRYSTAL_ANIM_*     */
    /* 0x3 */ char   crystal_anim_timer;
    /* 0x4 */ char   crystal_alpha;
    /* 0x5 */ char   crystal_flare_alpha;
    /* 0x6 */ u_char rgb;                   /* the cursor pulse              */
    /* 0x7 */ char   scroll_timer;
    /* 0x8 */ int    title_timer;           /* frames the recording has run  */
} MENU_RADIO_DISP;

/* menu_ctrl[] row 6: one frame of the page, and its drawing. */
void MenuRadio(void);                       /* 0x2042b0 */
void MenuRadioDisp(void);                   /* 0x204988 */

/* The page's own pak.  menu_top.c starts the load as the hub fades out, so
 * the background is resident by the time the page draws.  There is one pak
 * per language (MENU_RADIO_PK2 + GetLanguage()), because the button caption
 * plates carry baked words. */
void GetMenuRadioTexMem(void);              /* 0x204158 */
void MenuRadioTexLoadReq(void);             /* 0x2041a8 */
void LiberateMenuRadioTexMem(void);         /* 0x2048d8 */
void MenuRadioTexLoadCancel(void);          /* 0x204908 */

/* Fade out whatever recording is playing and forget its id.  Called on the
 * way out of the page and whenever a new crystal is started. */
void MenuRadioStreamStop(void);             /* 0x2048a0 */

#endif /* _INGAME_MENU_MENU_RADIO_H */
