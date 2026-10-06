/* ==========================================================================
 *  ingame/clear/prg/clearmenu_top.h
 *
 *  The clear menu's three-row top screen (clearmenu_top.o, .text 0x130088).
 *
 *  Save Game / Album / Return to Title, each behind a yes-no confirm window.
 *  The screen is savepoint_top.o's twin -- the same source with the names
 *  changed -- and shares its whole drawing layer: SavePoint_MenuWinDisp(),
 *  SavePointTopCaptionDisp() and SavePoint_MenuConfWinDisp() are
 *  savepoint_disp.o's own exports, called straight from here.  The only art
 *  this file owns is the two-piece title plate, gameclear_tex[48..49].
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_CLEAR_PRG_CLEARMENU_TOP_H
#define _INGAME_CLEAR_PRG_CLEARMENU_TOP_H

#include "eetypes.h"

/* clear_menu_top_ctrl.step */
#define CLEAR_MENU_TOP_STEP_INIT        0
#define CLEAR_MENU_TOP_STEP_LOAD_WAIT   1
#define CLEAR_MENU_TOP_STEP_OPEN        2
#define CLEAR_MENU_TOP_STEP_DECIDED     3

/* clear_menu_top_ctrl.mode -- which of the two pad handlers is live */
#define CLEAR_MENU_TOP_MODE_MENU        0
#define CLEAR_MENU_TOP_MODE_CONF        1

/* clear_menu_top_ctrl.csr -- the three rows, in draw order */
#define CLEAR_MENU_TOP_CSR_SAVE         0
#define CLEAR_MENU_TOP_CSR_ALBUM        1
#define CLEAR_MENU_TOP_CSR_EXIT         2
#define CLEAR_MENU_TOP_CSR_NUM          3

typedef struct                      /* 0x4 */
{
    /* 0x0 */ char step;
    /* 0x1 */ char mode;
    /* 0x2 */ char csr;
    /* 0x3 */ char conf_csr;        /* 0 = yes, 1 = no */
} CLEAR_MENU_TOP_CTRL;

typedef struct                      /* 0x4 */
{
    /* 0x0 */ char anim_step;       /* the menu window    */
    /* 0x1 */ char anim_timer;
    /* 0x2 */ char conf_anim_step;  /* the confirm window */
    /* 0x3 */ char conf_anim_timer;
} CLEAR_MENU_TOP_DISP;

/* Called once by init_ClearMenu(), before the phase is ever entered. */
void ClearMenuTopFirstInit(void);           /* 0x130160 */

/* Called on every entry to GID_CLEARMENU_TOP. */
void ClearMenuTopInit(void);                /* 0x1301a0 */

void ClearMenuTopBackGroundLoadReq(void);   /* 0x1301f0 */
void ClearMenuTopMemFree(void);             /* 0x130810 */

void ClearMenuTopMain(void);                /* 0x130288 */
void ClearMenuTopDisp(void);                /* 0x130850 */

#endif /* _INGAME_CLEAR_PRG_CLEARMENU_TOP_H */
