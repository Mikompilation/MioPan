/* ==========================================================================
 *  ingame/menu/menu_top.h
 *
 *  The in-game menu's hub page (menu_top.o) -- the eight-row list that
 *  switches between the other pages, and the play-data panel beside it.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MENU_MENU_TOP_H
#define _INGAME_MENU_MENU_TOP_H

#include "eetypes.h"
#include "../../common/variable.h"          /* DATE_INFO */

/* MENU_TOP_DISP::anim_step.  The same ladder the rest of the menu uses, but
 * only 0..4 and with no SHOW state of its own: 2 is "open and taking input",
 * which is what MenuTopPad() gates on. */
#define MENU_TOP_ANIM_START     0   /* seed: reset the timer and go to IN     */
#define MENU_TOP_ANIM_IN        1
#define MENU_TOP_ANIM_SHOW      2
#define MENU_TOP_ANIM_OUT       3
#define MENU_TOP_ANIM_END       4

/* menu_wrk.step values the hub uses.  3 and 4 are the two ways out: 3 hands
 * over to another page, 4 leaves the menu entirely. */
#define MENU_TOP_STEP_INIT      0
#define MENU_TOP_STEP_LOAD      1
#define MENU_TOP_STEP_MAIN      2
#define MENU_TOP_STEP_MOVE      3
#define MENU_TOP_STEP_EXIT      4

typedef struct                      /* 0x20 */
{
    /* 0x00 */ u_char    anim_step;      /* MENU_TOP_ANIM_*                   */
    /* 0x01 */ char      anim_timer;
    /* 0x02 */ u_char    move_flg;       /* leaving to a page, not to the game */
    /* 0x04 */ int       now_time_cnt;   /* frames until the clock is re-read */
    /* 0x08 */ DATE_INFO now_time;
} MENU_TOP_DISP;

/* The chapter-title plate.  Its pak is chapter-dependent (or the mission
 * one), so it is claimed and loaded per visit rather than at boot. */
void GetMenuChapterTitleTexMem(void);       /* 0x208cc8 */
void MenuChapterTitleTexLoadReq(void);      /* 0x208d40 */
void LiberateMenuChapterTitleTexMem(void);  /* 0x209318 */

/* menu_ctrl[] row 8. */
void MenuTop(void);                         /* 0x208e00 */
void MenuTopDisp(void);                     /* 0x209398 */

#endif /* _INGAME_MENU_MENU_TOP_H */
