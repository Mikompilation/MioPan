// FILE: /home/zero_rom/zero2np/src/ingame/clear/prg/clearmenu.c
//
// The clear menu's spine, and the per-phase callbacks for it and its three
// children.
//
// GID_CLEARMENU is a parent phase: gphase.c runs pre_ClearMenu() before and
// after_ClearMenu() after whichever child is active, so the background and the
// two black fades belong here and the children (top menu, save screen, album)
// only draw over them.  That is also why the step machine has an "open" state
// it never leaves on its own -- ClearMenuFadeOutReq(), from clearmenu_top.c's
// ClearMenuEndReq(), is what pushes it to FADE_OUT.
//
// The whole file is savepoint_main.o's twin, the same source with the names
// changed, and it borrows savepoint_disp.o rather than duplicating it: the
// background pak is SAVEPOINT_BG_PK2 and the per-frame draw is
// SavePoint_BgDisp().  Three things genuinely differ, all deliberate --
//
//   * the heap is ol_loadGetHeap()/ol_loadFreeHeap() where savepoint's is
//     mem_util*, because this menu hands over to the title screen and its
//     buffers have to come out of the out-game heap;
//   * the load priority is 6, the lowest anything in the tree asks for
//     (savepoint's is 2) -- nothing is competing for the drive here;
//   * the black quad goes through this file's own ClearMenuFadeBlackBgDisp()
//     rather than savepoint_disp.o's SavePoint_BlackBgDisp(), even though the
//     two compose exactly the same 640x448 half-black SQAR_DAT.
//
// The fades are asymmetric on purpose.  Step 2 runs Zero2Anim2D_FadeOut over
// the black quad (black clearing off, revealing the menu); step 4 runs FadeIn
// (black closing over it).  Both share clear_menu_disp.fade_timer, and
// ClearMenuMain() flips the step the same frame the counter completes --
// which is what keeps Zero2Anim2D_FadeOutAnimCtrl's return-0x80 bug from ever
// showing, since pre_ runs before after_ in the same frame.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), clearmenu.o.
// All 18 ZERO2.MAP exports plus the 9 statics.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.

#include "clearmenu.h"

#include "clearmenu_top.h"                          /* ClearMenuTop*          */

#include "../../savepoint/savepoint_disp.h"         /* SavePoint_BgDisp       */
#include "../../menu/zero2_anim2d.h"                /* Zero2Anim2D_Fade*      */
#include "../../../album/prg/album.h"               /* AlbumInit / AlbumMain  */
#include "../../../common/ol_load.h"                /* ol_loadGetHeap         */
#include "../../../common/utility2.h"               /* PRINT_ASSERT           */
#include "../../../graphics/graph2d/g2d_draw.h"     /* DISP_SQAR / DispSqrD   */
#include "../../../main/gphase.h"                   /* SetNextGPhase          */
#include "../../../main/phasefunc.h"                /* GPHASE_ENUM            */
#include "../../../save_load/prg/game_data_save.h"  /* GameDataSave*          */
#include "../../../system/eeiop/cddat.h"            /* GetFileSize / file ids */
#include "../../../system/eeiop/fileload.h"         /* FileLoadReqEE          */
#include "../../../system/eeiop/stream_auto.h"      /* StreamAutoFadeOut      */

/* Frames each of the two black fades takes.  The BGM fade-out is given the
 * same figure so the music and the picture leave together.  savepoint_main.o
 * uses 30 for the identical construct. */
#define CLEAR_MENU_FADE_TIME        20

static void ClearMenuInit(void);
static void ClearMenuCtrlInit(void);
static void ClearMenuBackGroundLoadReq(void);
static int  ClearMenuTexLoadWait(void);
static void ClearMenuMain(void);
static void ClearMenuMemFree(void);
static void ClearMenuDispInit(void);
static void ClearMenuDisp(void);
static void ClearMenuFadeBlackBgDisp(int off_x, int off_y, u_char alpha);

static void           *clear_bg_tex_addr;                   /* sdata 3ef850 */
static CLEAR_MENU_CTRL clear_menu_ctrl;                     /* sbss  3f4af8 */
static CLEAR_MENU_DISP clear_menu_disp;                     /* bss   422150 */

/* ==========================================================================
 *  Set-up
 * ======================================================================== */

static void ClearMenuInit(void)                                         /* 110 */
{
    ClearMenuCtrlInit();                                                /* 114 */
    ClearMenuDispInit();                                                /* 117 */
}

static void ClearMenuCtrlInit(void)                                     /* 124 */
{
    /* stream_id is deliberately left alone -- it arrives from
     * init_GameResult() long before this phase is entered, and the BGM it
     * names is already playing. */
    clear_menu_ctrl.step = CLEAR_MENU_STEP_ENTRY;                       /* 127 */
}

/* ==========================================================================
 *  Background pak
 *
 *  The Get/LoadReq/LoadWait/Liberate quad, except that the first, second and
 *  fourth take the address of the caller's pointer so clearmenu_top.c can
 *  drive its own pak through the same code.
 *
 *  The pak is savepoint's: the clear menu reuses that screen's whole backdrop
 *  and its draw, so there is no clear-menu background art of its own.
 * ======================================================================== */

static void ClearMenuBackGroundLoadReq(void)                            /* 135 */
{
    if (clear_bg_tex_addr != nullptr) {                                 /* 137 */
        LiberateClearMenuTexMem(&clear_bg_tex_addr);                    /* 138 */
    }

    GetClearMenuTexMem(&clear_bg_tex_addr, SAVEPOINT_BG_PK2);           /* 143 */
    ClearMenuTexLoadReq(clear_bg_tex_addr, SAVEPOINT_BG_PK2);           /* 146 */
}

void GetClearMenuTexMem(void **tex_addr, int data_label)                /* 156 */
{
    /* Redundant against the caller above, which already freed it; kept
     * because clearmenu_top.c's loader relies on it too. */
    if (*tex_addr != nullptr) {                                         /* 159 */
        LiberateClearMenuTexMem(tex_addr);                              /* 160 */
    }

    *tex_addr = ol_loadGetHeap((int)GetFileSize(data_label));           /* 164 */
}

void ClearMenuTexLoadReq(void *tex_addr, int data_label)                /* 173 */
{
    FileLoadReqEE(data_label, tex_addr, 6, nullptr, nullptr);           /* 177 */
}

static int ClearMenuTexLoadWait(void)                                   /* 187 */
{
    if (FileLoadIsEnd2(SAVEPOINT_BG_PK2, clear_bg_tex_addr) != 0) {     /* 195 */
        return 1;
    }

    return 0;                                                           /* 200 */
}

/* ==========================================================================
 *  Per-frame control
 * ======================================================================== */

/* Step 3 is a real, empty case: while the menu is open there is nothing for
 * the parent to do, and the transition out of it comes from
 * ClearMenuFadeOutReq().  The jump table at .rodata 0x3a3490 has a slot for it
 * pointing straight at the break label, and the default arm asserts, so it is
 * an intentional case rather than a hole.
 *
 * Step 4 leaves for GID_TITLE_TOP rather than a fade-out phase of its own --
 * this is the end of the playthrough, so the next thing to run is the title
 * screen, which reclaims the whole out-game heap these buffers came from. */
static void ClearMenuMain(void)                                         /* 210 */
{
    switch (clear_menu_ctrl.step) {                                     /* 213 */
    case CLEAR_MENU_STEP_ENTRY:
        clear_menu_ctrl.step = CLEAR_MENU_STEP_LOAD_WAIT;               /* 215 */
        break;                                                          /* 216 */

    case CLEAR_MENU_STEP_LOAD_WAIT:
        if (ClearMenuTexLoadWait() != 0) {                              /* 218 */
            clear_menu_ctrl.step = CLEAR_MENU_STEP_FADE_IN;             /* 219 */
        }
        break;                                                          /* 221 */

    case CLEAR_MENU_STEP_FADE_IN:
        if (clear_menu_disp.fade_timer >= CLEAR_MENU_FADE_TIME) {       /* 223 */
            clear_menu_ctrl.step = CLEAR_MENU_STEP_OPEN;                /* 224 */
        }
        break;                                                          /* 226 */

    case CLEAR_MENU_STEP_OPEN:
        break;

    case CLEAR_MENU_STEP_FADE_OUT:
        if (clear_menu_disp.fade_timer >= CLEAR_MENU_FADE_TIME) {       /* 230 */
            SetNextGPhase(GID_TITLE_TOP);                               /* 232 */
        }
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 236 */
        break;
    }
}

/* The player has chosen to leave.  Takes the music with it -- the clear BGM
 * init_GameResult() started is still playing under the menu. */
void ClearMenuFadeOutReq(void)                                          /* 245 */
{
    clear_menu_ctrl.step       = CLEAR_MENU_STEP_FADE_OUT;              /* 248 */
    clear_menu_disp.fade_timer = 0;                                     /* 249 */

    StreamAutoFadeOut(clear_menu_ctrl.stream_id, CLEAR_MENU_FADE_TIME); /* 252 */
}

void SetClearMenuStreamID(int stream_id)                                /* 261 */
{
    clear_menu_ctrl.stream_id = stream_id;                              /* 264 */
}

static void ClearMenuMemFree(void)                                      /* 276 */
{
    LiberateClearMenuTexMem(&clear_bg_tex_addr);                        /* 280 */
}

void LiberateClearMenuTexMem(void **tex_addr)                           /* 288 */
{
    if (*tex_addr != nullptr) {                                         /* 291 */
        ol_loadFreeHeap(*tex_addr);                                     /* 292 */
        *tex_addr = nullptr;                                            /* 293 */
    }
}

/* ==========================================================================
 *  Per-frame draw
 * ======================================================================== */

static void ClearMenuDispInit(void)                                     /* 306 */
{
    clear_menu_disp.fade_timer        = 0;                              /* 309 */
    clear_menu_disp.bg_anim_timer     = 0;                              /* 310 */
    clear_menu_disp.moyou1_anim_timer = 0;                              /* 311 */
    clear_menu_disp.moyou2_anim_timer = 0;                              /* 312 */
}

/* From step 2 on -- so the background starts animating underneath the fade-up
 * rather than appearing when it finishes. */
static void ClearMenuDisp(void)                                         /* 320 */
{
    if (clear_menu_ctrl.step >= CLEAR_MENU_STEP_FADE_IN) {              /* 324 */
        SavePoint_BgDisp(&clear_menu_disp.bg_anim_timer,
                         &clear_menu_disp.moyou1_anim_timer,
                         &clear_menu_disp.moyou2_anim_timer,
                         clear_bg_tex_addr);                            /* 326 */
    }
}

/* The full-screen black quad both fades run through.  savepoint_disp.o exports
 * SavePoint_BlackBgDisp() for exactly this and composes the same record, but
 * this file does not call it -- the initialiser is its own .rodata blob at
 * 0x3a34a8, copied into the frame.
 *
 * off_x / off_y are dead: both call sites pass 0 and neither is read.  Another
 * of the tree's display helpers that takes offsets and ignores them. */
static void ClearMenuFadeBlackBgDisp(int off_x, int off_y, u_char alpha) /* 338 */
{
    SQAR_DAT  black_bg = { 640, 448, 0, 0, 0, 0, 0, 0, 0x80 };          /* 340 */
    DISP_SQAR dsq;

    (void)off_x;
    (void)off_y;

    CopySqrDToSqr(&dsq, &black_bg);                                     /* 344 */

    /* A shift, not a divide: the ROM emits a bare `sra` with none of
     * expand_divmod's bias sequence. */
    dsq.alpha = (u_char)(dsq.alpha * alpha >> 7);                       /* 345 */

    DispSqrD(&dsq);                                                     /* 346 */
}

/* ==========================================================================
 *  GID_CLEARMENU -- the parent phase
 * ======================================================================== */

/* Claims every screen the menu can reach, not just its own: the save screen
 * and the album are children of this phase and have nowhere else to load
 * from.  Both take the out-game heap's allocator pair by pointer. */
void init_ClearMenu(void)                                               /* 358 */
{
    ClearMenuInit();                                                    /* 360 */
    ClearMenuTopFirstInit();                                            /* 363 */

    ClearMenuBackGroundLoadReq();                                       /* 366 */
    ClearMenuTopBackGroundLoadReq();                                    /* 369 */

    GameDataSaveBackGroundLoadReq(ol_loadGetHeap, ol_loadFreeHeap);     /* 372 */
    AlbumBackGroundLoadReq(ol_loadGetHeap, ol_loadFreeHeap);            /* 375 */
}

GPHASE_ENUM pre_ClearMenu(GPHASE_ENUM dummy)                            /* 383 */
{
    (void)dummy;

    ClearMenuMain();                                                    /* 385 */
    ClearMenuDisp();                                                    /* 388 */

    return GPHASE_CONTINUE;                                             /* 390 */
}

GPHASE_ENUM after_ClearMenu(GPHASE_ENUM result)                         /* 393 */
{
    u_char alpha;

    (void)result;

    if (clear_menu_ctrl.step == CLEAR_MENU_STEP_FADE_IN) {              /* 400 */
        alpha = Zero2Anim2D_FadeOutAnimCtrl(&clear_menu_disp.fade_timer,
                                            CLEAR_MENU_FADE_TIME);      /* 401 */
        ClearMenuFadeBlackBgDisp(0, 0, alpha);                          /* 403 */
    }
    else if (clear_menu_ctrl.step == CLEAR_MENU_STEP_FADE_OUT) {        /* 405 */
        alpha = Zero2Anim2D_FadeInAnimCtrl(&clear_menu_disp.fade_timer,
                                           CLEAR_MENU_FADE_TIME);       /* 406 */
        ClearMenuFadeBlackBgDisp(0, 0, alpha);                          /* 408 */
    }

    return GPHASE_CONTINUE;                                             /* 412 */
}

void end_ClearMenu(void)                                                /* 415 */
{
    ClearMenuMemFree();                                                 /* 417 */
    ClearMenuTopMemFree();                                              /* 420 */

    GameDataSaveTexMemFree();                                           /* 423 */
    AlbumEnd();                                                         /* 426 */
}

/* ==========================================================================
 *  GID_CLEARMENU_TOP -- the menu
 * ======================================================================== */

void init_ClearMenu_Top(void)                                           /* 432 */
{
    ClearMenuTopInit();                                                 /* 434 */
}

/* The menu stops taking input once the parent starts closing (step 4) but
 * keeps drawing, so it fades out with everything else rather than vanishing. */
GPHASE_ENUM one_ClearMenu_Top(GPHASE_ENUM dummy)                        /* 437 */
{
    (void)dummy;

    if (clear_menu_ctrl.step >= CLEAR_MENU_STEP_FADE_IN) {              /* 438 */
        if (clear_menu_ctrl.step < CLEAR_MENU_STEP_FADE_OUT) {          /* 439 */
            ClearMenuTopMain();                                         /* 441 */
        }

        ClearMenuTopDisp();                                             /* 445 */
    }

    return GPHASE_CONTINUE;                                             /* 448 */
}

void end_ClearMenu_Top(void)                                            /* 451 */
{
}

/* ==========================================================================
 *  GID_CLEARMENU_SAVE -- the card screen
 * ======================================================================== */

/* exe_label 1, where savepoint passes 0: this is the post-clear save, so the
 * record it writes raises ingame_wrk.clear_save_flg.  That flag is what makes
 * the difficulty row appear in the setup menu on the next playthrough. */
void init_ClearMenu_Save(void)                                          /* 457 */
{
    GameDataSaveInit(1);                                                /* 459 */
}

GPHASE_ENUM one_ClearMenu_Save(GPHASE_ENUM dummy)                       /* 462 */
{
    (void)dummy;

    if (GameDataSaveMain() != 0) {                                      /* 463 */
        SetNextGPhase(GID_CLEARMENU_TOP);                               /* 464 */
    }
    else {
        GameDataSaveDispMain();                                         /* 468 */
    }

    return GPHASE_CONTINUE;                                             /* 471 */
}

void end_ClearMenu_Save(void)                                           /* 474 */
{
    GameDataSaveEnd();                                                  /* 476 */
}

/* ==========================================================================
 *  GID_CLEARMENU_ALBUM -- the photo album
 * ======================================================================== */

void init_ClearMenu_Album(void)                                         /* 482 */
{
    AlbumInit(0);                                                       /* 484 */
}

/* Unlike the save screen, the album draws on the frame it is left as well --
 * the SetNextGPhase does not skip the draw.
 *
 * PORT HAZARD while album.o is a stub: AlbumMain() returns 0 ("still open")
 * unconditionally, so this phase never exits and the player is stuck on a
 * blank screen.  Identical to one_SavePoint_Album()'s; reconstructing album.o
 * fixes both. */
GPHASE_ENUM one_ClearMenu_Album(GPHASE_ENUM dummy)                      /* 487 */
{
    (void)dummy;

    if (AlbumMain() != 0) {                                             /* 488 */
        SetNextGPhase(GID_CLEARMENU_TOP);                               /* 489 */
    }

    AlbumDispMain();                                                    /* 493 */

    return GPHASE_CONTINUE;                                             /* 495 */
}

void end_ClearMenu_Album(void)                                          /* 498 */
{
}
