/* ==========================================================================
 *  ingame/menu/menu.h
 *
 *  The in-game menu's spine (menu.o) -- the nine-page dispatcher, the shared
 *  work block every page reads, and the animated shoji backdrop they all
 *  draw over.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MENU_H
#define _INGAME_MENU_H

#include "eetypes.h"

/* --------------------------------------------------------------------------
 *  Which page is up.  menu_wrk.menu_step indexes menu_ctrl[9] in menu.c, so
 *  these are that table's order and nothing else may be assumed about them.
 *  8 (Top) is the hub MenuWrkInit() opens on; MapViewInit() opens on 0.
 * ------------------------------------------------------------------------ */
#define MENU_STEP_MAP           0
#define MENU_STEP_ITEM          1
#define MENU_STEP_CAM           2
#define MENU_STEP_PHOTO         3
#define MENU_STEP_FILE          4
#define MENU_STEP_MEMO          5
#define MENU_STEP_RADIO         6
#define MENU_STEP_SOUL          7
#define MENU_STEP_TOP           8
#define MENU_STEP_NUM           9

/* MENU_DISP_CTRL::menu_bg_anim -- the backdrop's own state machine, driven by
 * MenuBg_AnimCtrl() once a frame.  MenuDispMain() only lets a page draw in
 * IN_END and OUT, and MenuOutCheck() reports the menu closed at OUT_END. */
#define MENU_BG_ANIM_NONE       0   /* nothing started; MenuDispMain seeds IN */
#define MENU_BG_ANIM_IN         1   /* shoji sliding in,  16 frames           */
#define MENU_BG_ANIM_IN_END     2   /* open; the lantern loop runs            */
#define MENU_BG_ANIM_OUT        3   /* closing, bg_anim_out_timer counting    */
#define MENU_BG_ANIM_OUT_END    4   /* closed                                 */

/* MENU_DISP_CTRL::tourou_anim_step -- the lantern layer's own state.  1 is
 * never used; the ROM goes straight from NONE to LOOP. */
#define MENU_TOUROU_ANIM_NONE       0
#define MENU_TOUROU_ANIM_LOOP       2   /* the 1860-frame authored cycle      */
#define MENU_TOUROU_ANIM_OUT        3   /* fading out over 8 frames           */
#define MENU_TOUROU_ANIM_OUT_END    4   /* gone                               */

/* --------------------------------------------------------------------------
 *  Fixed EE load addresses of the two paks the backdrop samples.
 *
 *  outgame.c's IngameLoadOnce() is what puts them there, once, at boot:
 *      LoadReq(GetLanguage() + MENU_BG_PK2, 0x19368c0)
 *      LoadReq(MENU_TOUROU_PK2,             0x1950ec0)
 *  and every menu module then hands the raw address to PK2SendVram().  The
 *  ROM computes the literal fresh at all 21 use sites across menu*.o.
 * ------------------------------------------------------------------------ */
#define MENU_BG_TEX_ADRS        0x019368c0
#define MENU_TOUROU_TEX_ADRS    0x01950ec0
#define MENU_PLAYDATA_TEX_ADRS  0x01973cc0
#define MENU_STATUS_TEX_ADRS    0x019981c0

/* --------------------------------------------------------------------------
 *  Work blocks
 * ------------------------------------------------------------------------ */

/* Shared by every page.  `step` is the page's own sub-state and means
 * whatever that page wants; everything else belongs to menu.c. */
typedef struct                      /* 0x10 */
{
    /* 0x0 */ u_char menu_step;         /* which page (MENU_STEP_*)          */
    /* 0x1 */ u_char top_cursor;        /* the hub's cursor, kept across     */
    /* 0x2 */ u_char step;              /* the current page's sub-state      */
    /* 0x3 */ u_char menu_out_flg;      /* a close has been requested        */
    /* 0x4 */ u_char menu_sys_flg;      /* the page callback may run         */
    /* 0x5 */ char   next_menu_step;    /* -1 = stay; else switch next frame */
    /* 0x8 */ int    cursor;            /* the current page's cursor         */
    /* 0xc */ int    stream_id;         /* the menu BGM, -1 when not playing */
} MENU_WRK;

/* The backdrop's animation state.  All of it is private to menu.c except
 * anim_step, which menu.c writes and the pages read. */
typedef struct                      /* 0x12 */
{
    /* 0x00 */ short anim_step;
    /* 0x02 */ short menu_bg_anim;          /* MENU_BG_ANIM_*                */
    /* 0x04 */ short bg_anim_timer;         /* drives the slide-in           */
    /* 0x06 */ short bg_anim_out_timer;     /* counts down through the out   */
    /* 0x08 */ char  tourou_anim_step;      /* MENU_TOUROU_ANIM_*            */
    /* 0x09 */ char  tourou_out_timer;
    /* 0x0a */ short tourou_anim_timer;     /* 0..1859, wraps back to 960    */
    /* 0x0c */ short moyou1_anim_timer;     /* 0..899                        */
    /* 0x0e */ short moyou2_anim_timer;     /* 0..599                        */
    /* 0x10 */ char  bganim_in_to_out;      /* closed before it finished     */
} MENU_DISP_CTRL;

/* --------------------------------------------------------------------------
 *  Globals
 * ------------------------------------------------------------------------ */

extern MENU_WRK menu_wrk;           /* data  321660 */

/* Non-zero while GID_STORY_MAP is up rather than GID_STORY_MENU -- the map
 * page reads it to know it was entered directly. */
extern char map_view_flg;           /* sdata 3f2b00 */

/* --------------------------------------------------------------------------
 *  Entry points
 * ------------------------------------------------------------------------ */

/* One frame of the menu.  Runs the current page, then reports 1 once the
 * backdrop's out animation has finished -- which is what takes ingame.c's
 * one_Story_Menu() / one_Story_Map() back to the game. */
int  MenuMain(void);                            /* 0x1e5850 */

/* Open the menu: reset the work block, capture the frame behind it, request
 * the backdrop paks and take the streaming exclusive slot. */
void MenuIn(void);                              /* 0x1e5920 */

/* Ask the menu to close.  Both callers are pages (menu_top.o and
 * menu_map.o); menu.c never calls it itself. */
void MenuOutReq(void);                          /* 0x1e59d8 */

/* 1 once a requested close has actually finished animating. */
int  MenuOutCheck(void);                        /* 0x1e5a50 */

/* Queue a page change for the next frame.  SetMenuStep() applies it. */
void SetNextMenuStep(int next_step);            /* 0x1e5a80 */

/* Apply a queued page change.  MenuMain() calls this first thing. */
void SetMenuStep(void);                         /* 0x1e5a90 */

/* Give every page's textures back and stop the menu BGM. */
void MenuRelease(void);                         /* 0x1e5ac0 */

/* Capture the frame behind the menu and reset the backdrop state. */
void MenuDispInit(void);                        /* 0x1e5b90 */

/* One frame of the backdrop plus the current page's own drawing. */
void MenuDispMain(void);                        /* 0x1e5c88 */

/* Open the menu straight on the map page.  ingame.c's init_Story_Map(). */
void MapViewInit(void);                         /* 0x1e7258 */

#endif /* _INGAME_MENU_H */
