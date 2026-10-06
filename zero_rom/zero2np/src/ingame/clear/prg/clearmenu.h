/* ==========================================================================
 *  ingame/clear/prg/clearmenu.h
 *
 *  The clear-menu phase (clearmenu.o, .text 0x12f990) -- the parent of the
 *  three-row screen in clearmenu_top.o.
 *
 *  GID_CLEARMENU is a *parent* phase: its pre/after callbacks run every frame
 *  while one of three children -- GID_CLEARMENU_TOP, _SAVE or _ALBUM -- is the
 *  active phase.  So this module owns the background, the two black fades that
 *  bracket the whole visit and the BGM stream id, and the children only draw
 *  on top of it.
 *
 *  It is savepoint_main.o's twin, the same source with the names changed, and
 *  it borrows savepoint_disp.o outright: the background pak is
 *  SAVEPOINT_BG_PK2 and the per-frame draw is SavePoint_BgDisp().  The two
 *  places it deviates are both deliberate --
 *
 *    * the heap is ol_loadGetHeap()/ol_loadFreeHeap(), not mem_util*, because
 *      the clear menu hands over to the title screen and its buffers have to
 *      come out of the out-game heap the title will reset;
 *    * the black quad is drawn by this file's own ClearMenuFadeBlackBgDisp()
 *      rather than savepoint_disp.o's SavePoint_BlackBgDisp(), even though
 *      both compose exactly the same 640x448 half-black SQAR_DAT.
 *
 *  clear_menu_ctrl.step:
 *      0  entered; nothing done yet
 *      1  waiting on the background pak
 *      2  fading up from black (20 frames)
 *      3  open -- the children have the screen
 *      4  fading back down to black, then GID_TITLE_TOP
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_CLEAR_PRG_CLEARMENU_H
#define _INGAME_CLEAR_PRG_CLEARMENU_H

/* clear_menu_ctrl.step */
#define CLEAR_MENU_STEP_ENTRY       0
#define CLEAR_MENU_STEP_LOAD_WAIT   1
#define CLEAR_MENU_STEP_FADE_IN     2
#define CLEAR_MENU_STEP_OPEN        3
#define CLEAR_MENU_STEP_FADE_OUT    4

/* Note the member order: savepoint_main.o's twin struct has these the other
 * way round (stream_id first).  Here step is at 0x0. */
typedef struct                      /* 0x8 */
{
    /* 0x0 */ char step;
    /* 0x4 */ int  stream_id;       /* the clear BGM, held for the whole visit */
} CLEAR_MENU_CTRL;

/* The background's four independent animation counters. */
typedef struct                      /* 0x10 */
{
    /* 0x0 */ int fade_timer;       /* shared by both black fades             */
    /* 0x4 */ int bg_anim_timer;    /* drives all three background alphas     */
    /* 0x8 */ int moyou1_anim_timer;
    /* 0xc */ int moyou2_anim_timer;
} CLEAR_MENU_DISP;

/* Claim / load / release a pak out of the out-game heap.  clearmenu_top.c
 * drives all three for its own (language-dependent) text pak, which is why
 * they take the address of the pointer rather than touching
 * clear_bg_tex_addr directly. */
void GetClearMenuTexMem(void **tex_addr, int data_label);   /* 0x12faf0 */
void ClearMenuTexLoadReq(void *tex_addr, int data_label);   /* 0x12fb40 */
void LiberateClearMenuTexMem(void **tex_addr);              /* 0x12fce8 */

/* Start the closing fade.  Called by clearmenu_top.c when the player picks
 * "return to title"; it also fades the BGM out over the same 20 frames. */
void ClearMenuFadeOutReq(void);                             /* 0x12fc80 */

/* Told to the phase by init_GameResult(), which is what actually starts the
 * clear BGM -- the stream outlives the result screen and plays on under this
 * menu, so ClearMenuFadeOutReq() needs its id to take it down. */
void SetClearMenuStreamID(int stream_id);                   /* 0x12fcb8 */

#endif /* _INGAME_CLEAR_PRG_CLEARMENU_H */
