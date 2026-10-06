// FILE: /home/zero_rom/zero2np/src/ingame/savepoint/savepoint_main.c
//
// The save-point screen's spine, and the per-phase callbacks for it and its
// three children.
//
// GID_SAVEPOINT_MAIN is a parent phase: gphase.c runs pre_SavePoint_Main()
// before and after_SavePoint_Main() after whichever child is active, so the
// background and the two black fades belong here and the children (top menu,
// save screen, album) only draw over them.  That is also why the step machine
// has an "open" state it never leaves on its own -- SavePointEndReq() in
// savepoint_top.c is what pushes it to FADE_OUT.
//
// The fades are asymmetric on purpose.  Step 2 runs Zero2Anim2D_FadeOut over
// the black quad (black clearing off, revealing the menu); step 4 runs
// FadeIn (black closing over it).  Both share savepoint_main_disp.fade_timer,
// and SavePointMain() flips the step the same frame the counter completes --
// which is what keeps Zero2Anim2D_FadeOutAnimCtrl's return-0x80 bug from ever
// showing, since pre_ runs before after_ in the same frame.
//
// init_SavePoint_Main() also pauses the room's looping SE, unlocks the sister
// (she would otherwise keep pathing to a player who is not moving) and starts
// the menu BGM, holding the stream id for the whole visit.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), savepoint_main.o.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.

#include "savepoint_main.h"

#include "savepoint_disp.h"                         /* SavePoint_BgDisp       */
#include "savepoint_top.h"                          /* SavePointTop*          */

#include "../ingame.h"                              /* IngameLoopSEPause      */
#include "../plyr/sister.h"                         /* SisterUnlock           */
#include "../menu/zero2_anim2d.h"                   /* Zero2Anim2D_Fade*      */
#include "../../album/prg/album.h"                  /* AlbumInit / AlbumMain  */
#include "../../common/mem_util.h"                  /* mem_utilGetMem         */
#include "../../common/utility2.h"                  /* PRINT_ASSERT           */
#include "../../main/gphase.h"                      /* SetNextGPhase          */
#include "../../save_load/prg/game_data_save.h"     /* GameDataSave*          */
#include "../../system/eeiop/cddat.h"               /* GetFileSize / file ids */
#include "../../system/eeiop/fileload.h"            /* FileLoadReqEE          */
#include "../../system/eeiop/snd3d.h"               /* SND_3D_SET             */
#include "../../system/eeiop/stream_auto.h"         /* StreamAutoPlay         */

/* Frames each of the two black fades takes.  The BGM fade-out is given the
 * same figure so the music and the picture leave together. */
#define SAVEPOINT_MAIN_FADE_TIME    30

static void SavePointMainInit(void);
static void SavePointMainCtrlInit(void);
static int  SavePointMainTexLoadWait(void);
static void SavePointMain(void);
static void SavePointMainDispInit(void);
static void SavePointMainDisp(void);

static void               *savepoint_bg_tex_addr;           /* sdata 3f3ce0 */
static SAVEPOINT_MAIN_CTRL savepoint_main_ctrl;             /* sbss  3f4f38 */
static SAVEPOINT_MAIN_DISP savepoint_main_disp;             /* bss   4bbe50 */

/* ==========================================================================
 *  Set-up
 * ======================================================================== */

static void SavePointMainInit(void)                                     /* 105 */
{
    SavePointMainCtrlInit();                                            /* 109 */
    SavePointMainDispInit();                                            /* 112 */
}

static void SavePointMainCtrlInit(void)                                 /* 119 */
{
    /* stream_id is deliberately left alone -- init_SavePoint_Main() writes it
     * straight after this, from StreamAutoPlay(). */
    savepoint_main_ctrl.step = SAVEPOINT_MAIN_STEP_ENTRY;               /* 122 */
}

/* ==========================================================================
 *  Background pak
 *
 *  The Get/LoadReq/LoadWait/Liberate quad other screens use, except that the
 *  first, second and fourth take the address of the caller's pointer so
 *  savepoint_top.c can drive its own pak through the same code.
 * ======================================================================== */

void SavePointMainBackGroundLoadReq(void)                               /* 130 */
{
    if (savepoint_bg_tex_addr != nullptr) {                             /* 132 */
        LiberateSavePointMainTexMem(&savepoint_bg_tex_addr);            /* 133 */
    }

    GetSavePointMainTexMem(&savepoint_bg_tex_addr, SAVEPOINT_BG_PK2);   /* 138 */
    SavePointMainTexLoadReq(savepoint_bg_tex_addr, SAVEPOINT_BG_PK2);   /* 141 */
}

void GetSavePointMainTexMem(void **tex_addr, int data_label)            /* 151 */
{
    /* Redundant against the caller above, which already freed it; kept
     * because savepoint_top.c's loader relies on it too. */
    if (*tex_addr != nullptr) {                                         /* 154 */
        LiberateSavePointMainTexMem(tex_addr);                          /* 155 */
    }

    *tex_addr = mem_utilGetMem((int)GetFileSize(data_label));           /* 159 */
}

void SavePointMainTexLoadReq(void *tex_addr, int data_label)            /* 168 */
{
    FileLoadReqEE(data_label, tex_addr, 2, nullptr, nullptr);           /* 172 */
}

static int SavePointMainTexLoadWait(void)                               /* 182 */
{
    if (FileLoadIsEnd2(SAVEPOINT_BG_PK2, savepoint_bg_tex_addr) != 0) { /* 190 */
        return 1;
    }

    return 0;                                                           /* 195 */
}

void SavePointMainMemFree(void)                                         /* 258 */
{
    LiberateSavePointMainTexMem(&savepoint_bg_tex_addr);                /* 262 */
}

void LiberateSavePointMainTexMem(void **tex_addr)                       /* 270 */
{
    if (*tex_addr != nullptr) {                                         /* 273 */
        mem_utilFreeMem(*tex_addr);                                     /* 274 */
        *tex_addr = nullptr;                                            /* 275 */
    }
}

/* ==========================================================================
 *  Per-frame control
 * ======================================================================== */

/* Step 3 is a real, empty case: while the menu is open there is nothing for
 * the parent to do, and the transition out of it comes from
 * SavePointMainFadeOutReq().  The jump table in .rodata has a slot for it
 * pointing straight at the break label, and the default arm asserts, so it is
 * an intentional case rather than a hole. */
static void SavePointMain(void)                                         /* 205 */
{
    switch (savepoint_main_ctrl.step) {                                 /* 208 */
    case SAVEPOINT_MAIN_STEP_ENTRY:
        savepoint_main_ctrl.step = SAVEPOINT_MAIN_STEP_LOAD_WAIT;       /* 210 */
        break;                                                          /* 211 */

    case SAVEPOINT_MAIN_STEP_LOAD_WAIT:
        if (SavePointMainTexLoadWait() != 0) {                          /* 213 */
            savepoint_main_ctrl.step = SAVEPOINT_MAIN_STEP_FADE_IN;     /* 216 */
        }
        break;

    case SAVEPOINT_MAIN_STEP_FADE_IN:
        if (savepoint_main_disp.fade_timer >= SAVEPOINT_MAIN_FADE_TIME) { /* 218 */
            savepoint_main_ctrl.step = SAVEPOINT_MAIN_STEP_OPEN;        /* 219 */
        }
        break;                                                          /* 221 */

    case SAVEPOINT_MAIN_STEP_OPEN:
        break;

    case SAVEPOINT_MAIN_STEP_FADE_OUT:
        if (savepoint_main_disp.fade_timer >= SAVEPOINT_MAIN_FADE_TIME) { /* 225 */
            SetNextGPhase(GID_SAVEPOINT_FADEOUT);                       /* 226 */
        }
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 230 */
        break;
    }
}

/* The player has chosen to leave.  Takes the music with it. */
void SavePointMainFadeOutReq(void)                                      /* 239 */
{
    savepoint_main_ctrl.step        = SAVEPOINT_MAIN_STEP_FADE_OUT;     /* 242 */
    savepoint_main_disp.fade_timer  = 0;                                /* 243 */

    StreamAutoFadeOut(savepoint_main_ctrl.stream_id,
                      SAVEPOINT_MAIN_FADE_TIME);                        /* 246 */
}

/* ==========================================================================
 *  Per-frame draw
 * ======================================================================== */

static void SavePointMainDispInit(void)                                 /* 288 */
{
    savepoint_main_disp.fade_timer        = 0;                          /* 291 */
    savepoint_main_disp.bg_anim_timer     = 0;                          /* 292 */
    savepoint_main_disp.moyou1_anim_timer = 0;                          /* 293 */
    savepoint_main_disp.moyou2_anim_timer = 0;                          /* 294 */
}

/* From step 2 on -- so the background starts animating underneath the
 * fade-up rather than appearing when it finishes. */
static void SavePointMainDisp(void)                                     /* 302 */
{
    if (savepoint_main_ctrl.step >= SAVEPOINT_MAIN_STEP_FADE_IN) {      /* 306 */
        SavePoint_BgDisp(&savepoint_main_disp.bg_anim_timer,
                         &savepoint_main_disp.moyou1_anim_timer,
                         &savepoint_main_disp.moyou2_anim_timer,
                         savepoint_bg_tex_addr);                        /* 308 */
    }
}

/* ==========================================================================
 *  GID_SAVEPOINT_MAIN -- the parent phase
 * ======================================================================== */

void init_SavePoint_Main(void)                                          /* 322 */
{
    IngameLoopSEPause();                                                /* 323 */

    /* She would otherwise keep walking to a player who has stopped. */
    SisterUnlock();                                                     /* 326 */

    SavePointMainInit();                                                /* 329 */
    SavePointTopFirstInit();                                            /* 332 */

    StreamAutoSetExclusiveMode(1, SAVEPOINT_MAIN_FADE_TIME);            /* 335 */

    savepoint_main_ctrl.stream_id =
        StreamAutoPlay(BGM004_MENU1_STR, BGM004_MENU1_HXD, 20, 0, 1,
                       0x3200, 0, (SND_3D_SET *)nullptr);               /* 339 */
}

GPHASE_ENUM pre_SavePoint_Main(GPHASE_ENUM dummy)                       /* 343 */
{
    (void)dummy;

    SavePointMain();                                                    /* 345 */
    SavePointMainDisp();                                                /* 348 */

    return GPHASE_CONTINUE;                                             /* 350 */
}

GPHASE_ENUM after_SavePoint_Main(GPHASE_ENUM result)                    /* 353 */
{
    u_char alpha;

    (void)result;

    if (savepoint_main_ctrl.step == SAVEPOINT_MAIN_STEP_FADE_IN) {      /* 360 */
        alpha = Zero2Anim2D_FadeOutAnimCtrl(&savepoint_main_disp.fade_timer,
                                            SAVEPOINT_MAIN_FADE_TIME);  /* 361 */
        SavePoint_BlackBgDisp(alpha);                                   /* 363 */
    }
    else if (savepoint_main_ctrl.step == SAVEPOINT_MAIN_STEP_FADE_OUT) { /* 365 */
        alpha = Zero2Anim2D_FadeInAnimCtrl(&savepoint_main_disp.fade_timer,
                                           SAVEPOINT_MAIN_FADE_TIME);   /* 366 */
        SavePoint_BlackBgDisp(alpha);                                   /* 368 */
    }

    return GPHASE_CONTINUE;                                             /* 372 */
}

void end_SavePoint_Main(void)                                           /* 375 */
{
    StreamAutoSetExclusiveMode(0, 4);                                   /* 377 */
    IngameLoopSERestart();                                              /* 378 */
}

/* ==========================================================================
 *  GID_SAVEPOINT_TOP -- the menu
 * ======================================================================== */

void init_SavePoint_Top(void)                                           /* 384 */
{
    SavePointTopInit();                                                 /* 386 */
}

/* The menu stops taking input once the parent starts closing (step 4) but
 * keeps drawing, so it fades out with everything else rather than vanishing. */
GPHASE_ENUM one_SavePoint_Top(GPHASE_ENUM dummy)                        /* 389 */
{
    (void)dummy;

    if (savepoint_main_ctrl.step >= SAVEPOINT_MAIN_STEP_FADE_IN) {      /* 390 */
        if (savepoint_main_ctrl.step < SAVEPOINT_MAIN_STEP_FADE_OUT) {  /* 391 */
            SavePointTopMain();                                         /* 393 */
        }

        SavePointTopDisp();                                             /* 397 */
    }

    return GPHASE_CONTINUE;                                             /* 400 */
}

void end_SavePoint_Top(void)                                            /* 403 */
{
}

/* ==========================================================================
 *  GID_SAVEPOINT_SAVE -- the card screen
 * ======================================================================== */

void init_SavePoint_Save(void)                                          /* 409 */
{
    GameDataSaveInit(0);                                                /* 411 */
}

GPHASE_ENUM one_SavePoint_Save(GPHASE_ENUM dummy)                       /* 414 */
{
    (void)dummy;

    if (GameDataSaveMain() != 0) {                                      /* 415 */
        SetNextGPhase(GID_SAVEPOINT_TOP);                               /* 416 */
    }
    else {
        GameDataSaveDispMain();                                         /* 420 */
    }

    return GPHASE_CONTINUE;                                             /* 423 */
}

void end_SavePoint_Save(void)                                           /* 426 */
{
    GameDataSaveEnd();                                                  /* 428 */
}

/* ==========================================================================
 *  GID_SAVEPOINT_ALBUM -- the photo album
 * ======================================================================== */

void init_SavePoint_Album(void)                                         /* 434 */
{
    AlbumInit(0);                                                       /* 436 */
}

/* Unlike the save screen, the album draws on the frame it is left as well --
 * the SetNextGPhase does not skip the draw.
 *
 * PORT HAZARD while album.o is a stub: AlbumMain() returns 0 ("still open")
 * unconditionally, so this phase never exits and the player is stuck with a
 * blank screen.  That return was chosen for title_album.c, where either value
 * is equally inert; here it is a reachable hang, and it is the one thing
 * between this folder and a save point that works end to end.  Do not fix it
 * here -- reconstruct album.o, or flip the stub to 1 and accept that the
 * title album becomes un-enterable too.
 *
 * GID_SAVEPOINT_SAVE has no such problem -- game_data_save.o and its drawing
 * layer (save_load_disp.o) are both reconstructed, so that phase draws and
 * exits properly. */
GPHASE_ENUM one_SavePoint_Album(GPHASE_ENUM dummy)                       /* 439 */
{
    (void)dummy;

    if (AlbumMain() != 0) {                                             /* 440 */
        SetNextGPhase(GID_SAVEPOINT_TOP);                               /* 441 */
    }

    AlbumDispMain();                                                    /* 445 */

    return GPHASE_CONTINUE;                                             /* 447 */
}

void end_SavePoint_Album(void)                                          /* 450 */
{
}
