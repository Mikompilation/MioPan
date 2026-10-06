// FILE: /home/zero_rom/zero2np/src/ingame/gameover/prg/gameover_menu.c
//
// The game-over menu's spine, and the per-phase callbacks for it and its
// three children.
//
// GID_GAMEOVER_MENU is a parent phase: gphase.c runs pre_GameOver_Menu()
// before and after_GameOver_Menu() after whichever child is active, so the
// background and the two black fades belong here and the children (top menu,
// load screen, album) only draw over them.  The step machine has an "open"
// state it never leaves on its own -- GameOverMenuFadeOutReq(), called from
// gameover_menu_top.c, is what pushes it to FADE_OUT.
//
// This file is savepoint_main.c with the names changed.  The two control
// blocks have identical layouts, the step machines are the same five states,
// and almost every function sits within two source lines of its twin.  It
// goes further than that: the background pak is *the same asset*
// (SAVEPOINT_BG_PK2) and it is drawn by savepoint_disp.o's own
// SavePoint_BgDisp() / SavePoint_BlackBgDisp(), so nothing about the
// backdrop is duplicated -- only the state that drives it.
//
// The two differences worth naming.  This screen claims its background out of
// the outgame heap (ol_loadGetHeap) rather than mem_util, because the room is
// being torn down around it; and step 4 leaves for GID_TITLE_TOP rather than
// back into the room, since there is no room left to return to.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), gameover_menu.o.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.

#include "gameover_menu.h"

#include "gameover_menu_load.h"                     /* GameOverLoad*          */
#include "gameover_menu_top.h"                      /* GameOverMenuTop*       */

#include "../../savepoint/savepoint_disp.h"         /* SavePoint_BgDisp       */
#include "../../menu/zero2_anim2d.h"                /* Zero2Anim2D_Fade*      */
#include "../../../album/prg/album.h"               /* AlbumInit / AlbumMain  */
#include "../../../common/ol_load.h"                /* ol_loadGetHeap         */
#include "../../../common/utility2.h"               /* PRINT_ASSERT           */
#include "../../../main/gphase.h"                   /* SetNextGPhase          */
#include "../../../system/eeiop/cddat.h"            /* GetFileSize / file ids */
#include "../../../system/eeiop/fileload.h"         /* FileLoadReqEE          */
#include "../../../system/eeiop/snd3d.h"            /* SND_3D_SET             */
#include "../../../system/eeiop/stream_auto.h"      /* StreamAutoPlay         */

/* Frames each of the two black fades takes.  The BGM fade-out is given the
 * same figure so the music and the picture leave together. */
#define GAMEOVER_MENU_FADE_TIME     30

static void GameOverMenuInit(void);
static void GameOverMenuCtrlInit(void);
static void GameOverMenuTexBackGroundLoadReq(void);
static int  GameOverMenuTexLoadWait(void);
static void GameOverMenuMain(void);
static void GameOverMenuMemFree(void);
static void GameOverMenuDispInit(void);
static void GameOverMenuDispMain(void);

static void              *gameover_bg_tex_addr;             /* sdata 3f0f88 */
static GAMEOVER_MENU_CTRL gameover_menu_ctrl;               /* sbss  3f4c98 */
static GAMEOVER_MENU_DISP gameover_menu_disp;               /* bss   4af6c0 */

/* ==========================================================================
 *  Set-up
 * ======================================================================== */

static void GameOverMenuInit(void)                                      /* 105 */
{
    GameOverMenuCtrlInit();                                             /* 109 */
    GameOverMenuDispInit();                                             /* 112 */
}

static void GameOverMenuCtrlInit(void)                                  /* 119 */
{
    /* stream_id is deliberately left alone -- init_GameOver_Menu() writes it
     * straight after this, from StreamAutoPlay(). */
    gameover_menu_ctrl.step = GAMEOVER_MENU_STEP_ENTRY;                 /* 122 */
}

/* ==========================================================================
 *  Background pak
 *
 *  The Get/LoadReq/LoadWait/Liberate quad other screens use, except that the
 *  first, second and fourth take the address of the caller's pointer so
 *  gameover_menu_top.c can drive its own pak through the same code.
 * ======================================================================== */

static void GameOverMenuTexBackGroundLoadReq(void)                      /* 130 */
{
    if (gameover_bg_tex_addr != nullptr) {                              /* 132 */
        LiberateGameOverMenuTexMem(&gameover_bg_tex_addr);              /* 133 */
    }

    GetGameOverMenuTexMem(&gameover_bg_tex_addr, SAVEPOINT_BG_PK2);     /* 138 */
    GameOverMenuTexLoadReq(gameover_bg_tex_addr, SAVEPOINT_BG_PK2);     /* 141 */
}

void GetGameOverMenuTexMem(void **tex_addr, int data_label)             /* 151 */
{
    /* Redundant against the caller above, which already freed it; kept
     * because gameover_menu_top.c's loader relies on it too. */
    if (*tex_addr != nullptr) {                                         /* 154 */
        LiberateGameOverMenuTexMem(tex_addr);                           /* 155 */
    }

    *tex_addr = ol_loadGetHeap((int)GetFileSize(data_label));           /* 159 */
}

void GameOverMenuTexLoadReq(void *tex_addr, int data_label)             /* 169 */
{
    FileLoadReqEE(data_label, tex_addr, 2, nullptr, nullptr);           /* 173 */
}

static int GameOverMenuTexLoadWait(void)                                /* 183 */
{
    if (FileLoadIsEnd2(SAVEPOINT_BG_PK2, gameover_bg_tex_addr) != 0) {  /* 191 */
        return 1;
    }

    return 0;                                                           /* 196 */
}

/* ==========================================================================
 *  Per-frame control
 * ======================================================================== */

/* Step 3 is a real, empty case: while the menu is open there is nothing for
 * the parent to do, and the transition out of it comes from
 * GameOverMenuFadeOutReq().  The jump table at .rodata 3b3d60 has a slot for
 * it pointing straight at the break label, and the default arm asserts, so it
 * is an intentional case rather than a hole. */
static void GameOverMenuMain(void)                                      /* 206 */
{
    switch (gameover_menu_ctrl.step) {                                  /* 209 */
    case GAMEOVER_MENU_STEP_ENTRY:
        gameover_menu_ctrl.step = GAMEOVER_MENU_STEP_LOAD_WAIT;         /* 211 */
        break;                                                          /* 212 */

    case GAMEOVER_MENU_STEP_LOAD_WAIT:
        if (GameOverMenuTexLoadWait() != 0) {                           /* 214 */
            gameover_menu_ctrl.step = GAMEOVER_MENU_STEP_FADE_IN;       /* 217 */
        }
        break;

    case GAMEOVER_MENU_STEP_FADE_IN:
        if (gameover_menu_disp.fade_timer >= GAMEOVER_MENU_FADE_TIME) { /* 219 */
            gameover_menu_ctrl.step = GAMEOVER_MENU_STEP_OPEN;          /* 220 */
        }
        break;                                                          /* 222 */

    case GAMEOVER_MENU_STEP_OPEN:
        break;

    case GAMEOVER_MENU_STEP_FADE_OUT:
        if (gameover_menu_disp.fade_timer >= GAMEOVER_MENU_FADE_TIME) { /* 226 */
            SetNextGPhase(GID_TITLE_TOP);                               /* 228 */
        }
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 232 */
        break;
    }
}

/* The player has chosen to leave.  Takes the music with it. */
void GameOverMenuFadeOutReq(void)                                       /* 241 */
{
    gameover_menu_ctrl.step       = GAMEOVER_MENU_STEP_FADE_OUT;        /* 244 */
    gameover_menu_disp.fade_timer = 0;                                  /* 245 */

    StreamAutoFadeOut(gameover_menu_ctrl.stream_id,
                      GAMEOVER_MENU_FADE_TIME);                         /* 248 */
}

static void GameOverMenuMemFree(void)                                   /* 260 */
{
    LiberateGameOverMenuTexMem(&gameover_bg_tex_addr);                  /* 264 */
}

void LiberateGameOverMenuTexMem(void **tex_addr)                        /* 272 */
{
    if (*tex_addr != nullptr) {                                         /* 275 */
        ol_loadFreeHeap(*tex_addr);                                     /* 276 */
        *tex_addr = nullptr;                                            /* 277 */
    }
}

/* ==========================================================================
 *  Per-frame draw
 * ======================================================================== */

static void GameOverMenuDispInit(void)                                  /* 290 */
{
    gameover_menu_disp.fade_timer        = 0;                           /* 293 */
    gameover_menu_disp.bg_anim_timer     = 0;                           /* 294 */
    gameover_menu_disp.moyou1_anim_timer = 0;                           /* 295 */
    gameover_menu_disp.moyou2_anim_timer = 0;                           /* 296 */
}

/* From step 2 on -- so the background starts animating underneath the black
 * while it is still clearing off, rather than popping in when it goes. */
static void GameOverMenuDispMain(void)                                  /* 304 */
{
    if (gameover_menu_ctrl.step >= GAMEOVER_MENU_STEP_FADE_IN) {        /* 308 */
        SavePoint_BgDisp(&gameover_menu_disp.bg_anim_timer,
                         &gameover_menu_disp.moyou1_anim_timer,
                         &gameover_menu_disp.moyou2_anim_timer,
                         gameover_bg_tex_addr);                         /* 310 */
    }
}

/* ==========================================================================
 *  GID_GAMEOVER_MENU -- the parent phase
 * ======================================================================== */

/* Everything the three children will need is claimed here, in one go: this is
 * the only point at which all of them are guaranteed off the heap.  The album
 * is handed ol_loadGetHeap/ol_loadFreeHeap so it allocates out of the same
 * outgame block as the rest of the screen. */
void init_GameOver_Menu(void)                                           /* 323 */
{
    GameOverMenuInit();                                                 /* 325 */

    GameOverMenuTexBackGroundLoadReq();                                 /* 328 */

    GameOverMenuTopFirstInit();                                         /* 331 */
    GameOverMenuTopBackGroundLoadReq();                                 /* 334 */

    GetGameOverLoadTexMem();                                            /* 337 */
    GameOverLoadDataLoadReq();                                          /* 339 */

    AlbumBackGroundLoadReq(ol_loadGetHeap, ol_loadFreeHeap);            /* 342 */

    gameover_menu_ctrl.stream_id =
        StreamAutoPlay(BGM004_MENU1_STR, BGM004_MENU1_HXD, 20, 0, 1,
                       0x3200, 0, (SND_3D_SET *)nullptr);               /* 346 */
}

GPHASE_ENUM pre_GameOver_Menu(GPHASE_ENUM dummy)                        /* 350 */
{
    (void)dummy;

    GameOverMenuMain();                                                 /* 352 */
    GameOverMenuDispMain();                                             /* 355 */

    return GPHASE_CONTINUE;                                             /* 357 */
}

/* Named for what the *black* does, not the picture: step 2 runs FadeOut
 * (black clearing off, menu appearing) and step 4 runs FadeIn (black closing
 * over it).  Both share fade_timer, and GameOverMenuMain() flips the step the
 * same frame the counter completes -- which is what keeps
 * Zero2Anim2D_FadeOutAnimCtrl's return-0x80 bug from ever showing, since pre_
 * runs before after_ in the same frame. */
GPHASE_ENUM after_GameOver_Menu(GPHASE_ENUM result)                     /* 360 */
{
    u_char alpha;

    (void)result;

    if (gameover_menu_ctrl.step == GAMEOVER_MENU_STEP_FADE_IN) {        /* 367 */
        alpha = Zero2Anim2D_FadeOutAnimCtrl(&gameover_menu_disp.fade_timer,
                                            GAMEOVER_MENU_FADE_TIME);   /* 368 */
        SavePoint_BlackBgDisp(alpha);                                   /* 370 */
    }
    else if (gameover_menu_ctrl.step == GAMEOVER_MENU_STEP_FADE_OUT) {  /* 372 */
        alpha = Zero2Anim2D_FadeInAnimCtrl(&gameover_menu_disp.fade_timer,
                                           GAMEOVER_MENU_FADE_TIME);    /* 373 */
        SavePoint_BlackBgDisp(alpha);                                   /* 375 */
    }

    return GPHASE_CONTINUE;                                             /* 379 */
}

void end_GameOver_Menu(void)                                            /* 382 */
{
    GameOverMenuMemFree();                                              /* 384 */
    GameOverMenuTopMemFree();                                           /* 387 */
    ReleaseGameOverLoadTexMem();                                        /* 390 */
    AlbumEnd();                                                         /* 393 */

    StreamAutoAllStop();                                                /* 396 */
}

/* ==========================================================================
 *  GID_GAMEOVER_MENU_TOP
 * ======================================================================== */

void init_GameOver_Menu_Top(void)                                       /* 403 */
{
    GameOverMenuTopInit();                                              /* 405 */
}

/* Nothing runs until the parent has the pak (step > 1), and the menu stops
 * accepting input once the closing fade has started (step < 4) while still
 * being drawn through it. */
GPHASE_ENUM one_GameOver_Menu_Top(GPHASE_ENUM dummy)                    /* 408 */
{
    (void)dummy;

    if (gameover_menu_ctrl.step >= GAMEOVER_MENU_STEP_FADE_IN) {        /* 409 */
        if (gameover_menu_ctrl.step < GAMEOVER_MENU_STEP_FADE_OUT) {    /* 410 */
            GameOverMenuTopMain();                                      /* 412 */
        }

        GameOverMenuTopDisp();                                          /* 416 */
    }

    return GPHASE_CONTINUE;                                             /* 419 */
}

void end_GameOver_Menu_Top(void)                                        /* 422 */
{
}

/* ==========================================================================
 *  GID_GAMEOVER_MENU_LOAD
 * ======================================================================== */

void init_GameOver_Menu_Load(void)                                      /* 428 */
{
    GameOverLoadInit();                                                 /* 430 */
}

GPHASE_ENUM one_GameOver_Menu_Load(GPHASE_ENUM dummy)                   /* 433 */
{
    (void)dummy;

    GameOverLoadMain();                                                 /* 435 */
    GameOverLoadDispMain();                                             /* 438 */

    return GPHASE_CONTINUE;                                             /* 440 */
}

void end_GameOver_Menu_Load(void)                                       /* 443 */
{
    GameOverLoadEnd();                                                  /* 445 */
}

/* ==========================================================================
 *  GID_GAMEOVER_MENU_ALBUM
 * ======================================================================== */

/* mode 0 -- the ingame album.  The title screen's own album phase passes 1. */
void init_GameOver_Menu_Album(void)                                     /* 451 */
{
    AlbumInit(0);                                                       /* 453 */
}

GPHASE_ENUM one_GameOver_Menu_Album(GPHASE_ENUM dummy)                  /* 456 */
{
    (void)dummy;

    /* AlbumMain() is still a stub returning 0 ("still open"), so this phase
     * cannot currently be left -- the same dead end GID_SAVEPOINT_ALBUM has.
     * album.o is what fixes both. */
    if (AlbumMain() != 0) {                                             /* 457 */
        SetNextGPhase(GID_GAMEOVER_MENU_TOP);                           /* 458 */
    }

    AlbumDispMain();                                                    /* 462 */

    return GPHASE_CONTINUE;                                             /* 464 */
}

void end_GameOver_Menu_Album(void)                                      /* 467 */
{
}
