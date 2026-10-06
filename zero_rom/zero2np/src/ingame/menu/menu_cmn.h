/* ==========================================================================
 *  ingame/menu/menu_cmn.h
 *
 *  The in-game menu pages' shared machinery (menu_cmn.o).
 *
 *  Four unrelated services live in this one file: the scrolling-list cursor
 *  arithmetic every list page uses, the yes/no and confirm pad handlers, the
 *  two-slot cross-fade that swaps a page's background texture, and the
 *  double-buffered texture slot pair.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MENU_MENU_CMN_H
#define _INGAME_MENU_MENU_CMN_H

#include "eetypes.h"

/* Cross-fade buffers, and double-buffer slots.  Both are two. */
#define MENU_CROSS_FADE_NUM     2

/* --------------------------------------------------------------------------
 *  Scrolling-list cursor
 *
 *  A list page keeps one of these plus its own `cursor`.  The pair splits the
 *  list two ways: `data_pos` is where the selection sits in the whole list,
 *  `disp_start_pos` is which entry the window starts at, and `cursor` is the
 *  row inside that window.  The four movement helpers keep all three
 *  consistent and report whether `data_pos` actually moved, which is what the
 *  caller gates its cursor sound on.
 * ------------------------------------------------------------------------ */
typedef struct                      /* 0xc */
{
    /* 0x0 */ int disp_start_pos;   /* first list entry shown in the window  */
    /* 0x4 */ int data_pos;         /* selected entry, over the whole list   */
    /* 0x8 */ int data_num;         /* how many entries the list has         */
} MENU_REF_CTRL;

/* The shared yes/no window's cursor: 0 = yes, 1 = no. */
typedef struct                      /* 0x1 */
{
    /* 0x0 */ char csr;
} MENU_YES_NO_CTRL;

extern MENU_YES_NO_CTRL menu_yes_no_ctrl;   /* sdata 3f2c80 */

/* --------------------------------------------------------------------------
 *  Entry points
 * ------------------------------------------------------------------------ */

/* Seed the shared yes/no window's cursor.  Every caller opens on "yes". */
void MenuYesNoCtrlInit(int csr);                                /* 0x1f1a80 */

void MenuRefCtrlInit(MENU_REF_CTRL *ref_ctrl, int data_num);    /* 0x1f1a88 */

/* Move the selection.  `disp_num` is how many rows the window shows and
 * `disp_num_max` how many it can show; the two differ only for a list
 * shorter than the window, which is what keeps a short list from scrolling.
 * All four return non-zero when data_pos changed. */
int MenuRefMovePadLup(MENU_REF_CTRL *ref_ctrl, int *cursor,
                      int disp_num, int disp_num_max);          /* 0x1f1a98 */
int MenuRefMovePadLdown(MENU_REF_CTRL *ref_ctrl, int *cursor,
                        int disp_num, int disp_num_max);        /* 0x1f1b20 */
int MenuRefMovePageUp(MENU_REF_CTRL *ref_ctrl, int *cursor,
                      int disp_num, int disp_num_max);          /* 0x1f1b90 */
int MenuRefMovePageDown(MENU_REF_CTRL *ref_ctrl, int *cursor,
                        int disp_num, int disp_num_max);        /* 0x1f1be0 */

/* One frame of the confirm window's pad: 1 if either button dismissed it. */
int MenuCmnConfirmPad(void);                                    /* 0x1f1c60 */

/* One frame of the yes/no window's pad.  0 = still open, 1 = yes chosen,
 * 2 = no chosen (or cancelled).  Left/right walk menu_yes_no_ctrl.csr. */
int MenuCmnYesNoPad(void);                                      /* 0x1f1cd8 */

/* Non-zero once every outstanding file load has finished. */
int MenuLoadWait(void);                                         /* 0x1f1df8 */

/* --------------------------------------------------------------------------
 *  Cross-fade
 *
 *  Two independent slots, each holding one texture and one fade.  A page asks
 *  for a texture with MenuCrossFadeInStart(), which kicks a four-step loader
 *  and a 20-frame fade up; MenuCrossFadeOutStart() runs the same fade down.
 *  MenuCmnCrossFade() pumps the loader and GetMenuCrossFadeAlpha() the fade,
 *  both once a frame for both slots.
 * ------------------------------------------------------------------------ */
void  MenuCrossFadeInit(void);                                  /* 0x1f1e18 */
void  MenuCmnCrossFade(void);                                   /* 0x1f1fb8 */
void  MenuCrossFadeInStart(int buff_label, int data_label);     /* 0x1f20d8 */
void  MenuCrossFadeOutStart(int buff_label);                    /* 0x1f2190 */
void  LiberateAllMenuCrossFadeTexMem(void);                     /* 0x1f2218 */
void  LiberateMenuCrossFadeTexMem(int buff_label);              /* 0x1f2240 */
void  MenuCrossFadeTexLoadCancel(int buff_label);               /* 0x1f2300 */

/* Advances both slots' fades and writes this frame's alpha for each.
 * `fade_alpha` is an array of MENU_CROSS_FADE_NUM. */
void  GetMenuCrossFadeAlpha(u_char *fade_alpha);                /* 0x1f2400 */

void *GetCrossFadeDataAddr(int buff_label);                     /* 0x1f25d0 */

/* Non-zero when the slot has something worth drawing this frame: loaded, and
 * mid-fade rather than idle. */
int   CheckCrossFadeDisp(int buff_label);                       /* 0x1f2640 */

/* --------------------------------------------------------------------------
 *  Double buffer
 *
 *  Two heap slots and a flag saying which is current.  A page loads the next
 *  page's texture into the other slot while the current one is still on
 *  screen, then flips with MenuDBuffChange().
 * ------------------------------------------------------------------------ */
void   MenuDBuffCtrlInit(void);                                 /* 0x1f26e8 */
void   MenuDBuffChange(void);                                   /* 0x1f2718 */
void   MenuDBuffLoadReq(int load_file);                         /* 0x1f2730 */
void   MenuDBuffRelease(u_char flg);                            /* 0x1f2848 */
void   MenuDBuffAllRelease(void);                               /* 0x1f28b0 */
u_char GetMenuDBuffFlg(void);                                   /* 0x1f28d8 */
u_int *GetMenuDBuffAddr(u_char flg);                            /* 0x1f28e8 */

/* --------------------------------------------------------------------------
 *  Odds and ends
 * ------------------------------------------------------------------------ */

/* Point a loaded TIM2 at a VRAM page and queue both transfers.  Unlike
 * PK2SendVram() this patches the picture header's TEX0 first, so the pak's
 * own authored TBP0/CBP are ignored and the caller's are used. */
void MenuTim2SendVram(u_int *tim2_addr, int tbp, int cbp);      /* 0x1f2908 */

/* The menu's window open/close fade: 10 frames in, 5 out. */
void MenuInOutAnimCtrl(char *anim_step, char *anim_timer, u_char *alpha);
                                                                /* 0x1f29a0 */

/* The menu's cursor pulse: a 45-frame 64 -> 128 -> 64 ramp on *rgb. */
void MenuCsrAnimCtrl(char *timer, u_char *rgb);                 /* 0x1f29d0 */

#endif /* _INGAME_MENU_MENU_CMN_H */
