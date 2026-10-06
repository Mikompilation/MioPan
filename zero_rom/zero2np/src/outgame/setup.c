// FILE: /home/zero_rom/zero2np/src/outgame/setup.c
//
// The setup mode: the shared background and texture loads for the setup menu
// and every mission screen underneath it, plus the phase callbacks for all
// six of them.
//
// The file itself is almost all glue.  Its own state is two bytes -- a load
// step and the fade counter Zero2Anim2D_InOutAnimCtrl() drives -- and every
// child phase is gated on `step` being 2 or 3, so nothing draws until the
// background is resident.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
// Trailing /* NNN */ are ROM source line numbers, measured from
// disassemble_function.

#include "setup.h"

#include "mission_sel.h"                    // MissionSel* / PlayMissionSelBGM
#include "setup_menu.h"                     // SetupMenuInit / Main / DispMain
#include "title.h"                          // SetTitleStreamID
#include "tim_dat/setup_dat.h"              // setup_tex[]
#include "../album/prg/album.h"             // AlbumInit / Main / DispMain
#include "../common/ol_load.h"              // ol_loadGetHeap / ol_loadFreeHeap
#include "../graphics/graph2d/g2d_draw.h"   // DISP_SPRT / DISP_SQAR / Copy* / Disp*
#include "../graphics/graph2d/tim2.h"       // PK2SendVram
#include "../ingame/menu/menu_cam_main.h"   // MenuCamMainInit / Main / Disp
#include "../ingame/menu/zero2_anim2d.h"    // Zero2Anim2D_InOutAnimCtrl
#include "../main/gphase.h"                 // SetNextGPhase / GID_*
#include "../main/phasefunc.h"              // GPHASE_ENUM + phase-callback prototypes
#include "../save_load/prg/game_data_save.h"// GameDataSaveInit / Main / Disp / End
#include "../system/eeiop/cddat.h"          // BGM000_TITLE_* / GetFileSize
#include "../system/eeiop/fileload.h"       // FileLoadReqEE / IsEnd2 / Cancel2
#include "../system/eeiop/stream_auto.h"    // StreamAutoPlay
#include "../system/os/system.h"            // GetLanguage

#include <stdint.h>                         // uintptr_t

/* Per-language setup paks. */
#define SETUP_BG_PK2    0x1106              /* + language */
#define SETUP_FONT_PK2  0x110b              /* + language */
#define MISSION_SEL_PK2 0x111b              /* + language */

/* SETUP_CTRL::step */
#define SETUP_DISP_INIT 0
#define SETUP_LOAD_WAIT 1
#define SETUP_MAIN      2
#define SETUP_RETURN    3

/* SETUP_DISP_CTRL::anim_step -- Zero2Anim2D_InOutAnimCtrl()'s own states;
 * 4 is "the fade-out has finished". */
#define SETUP_ANIM_OUT  3
#define SETUP_ANIM_END  4

static void *setup_bg_tex_addr;                                         /* sdata 3f4280 */
static void *setup_font_tex_addr;                                       /* sdata 3f4284 */
static void *mission_sel_tex_addr;                                      /* sdata 3f4288 */
static SETUP_CTRL setup_ctrl;                                           /* sbss  3f4f80 */
static SETUP_DISP_CTRL setup_disp_ctrl;                                 /* sbss  3f4f88 */

static void GetSetupTexMem(void **tex_addr, int data_label);
static void SetupTexLoadReq(void *tex_addr, int data_label);
static int  SetupTexLoadWait(void);
static void LiberateSetupTexMem(void **tex_addr);
static void SetupTexLoadCancel(void *tex_addr, int data_label);
static void SetupDispInit(void);
static void SetupBgDisp(int off_x, int off_y, u_char alpha, void *pk2_addr);

void SetupInit(void)
{
    setup_ctrl.step = SETUP_DISP_INIT;                                  /* 111 */
}

void SetupBackGroundLoadReq(void)
{
    if (setup_bg_tex_addr != (void *)0)                                 /* 121 */
    {
        LiberateSetupTexMem(&setup_bg_tex_addr);
    }
    if (setup_font_tex_addr != (void *)0)                               /* 122 */
    {
        LiberateSetupTexMem(&setup_font_tex_addr);
    }
    if (mission_sel_tex_addr != (void *)0)                              /* 124 */
    {
        LiberateSetupTexMem(&mission_sel_tex_addr);
    }

    GetSetupTexMem(&setup_bg_tex_addr, SETUP_BG_PK2 + GetLanguage());   /* 128 */
    GetSetupTexMem(&setup_font_tex_addr,
                   SETUP_FONT_PK2 + GetLanguage());                     /* 129 */
    GetSetupTexMem(&mission_sel_tex_addr,
                   MISSION_SEL_PK2 + GetLanguage());                    /* 134 */

    SetupTexLoadReq(setup_bg_tex_addr, SETUP_BG_PK2 + GetLanguage());   /* 137 */
    SetupTexLoadReq(setup_font_tex_addr,
                    SETUP_FONT_PK2 + GetLanguage());                    /* 140 */
    SetupTexLoadReq(mission_sel_tex_addr,
                    MISSION_SEL_PK2 + GetLanguage());                   /* 141 */
}

static void GetSetupTexMem(void **tex_addr, int data_label)
{
    if (*tex_addr != (void *)0)
    {
        LiberateSetupTexMem(tex_addr);
    }

    *tex_addr = ol_loadGetHeap(GetFileSize(data_label));
}

static void SetupTexLoadReq(void *tex_addr, int data_label)
{
    FileLoadReqEE(data_label, tex_addr, 5, (FILE_LOAD_CALLBACK)0, (void *)0);
}

/* The mission-select pak is loaded but not waited on -- the screen that needs
 * it does its own check. */
static int SetupTexLoadWait(void)
{
    int res;

    if (FileLoadIsEnd2(SETUP_BG_PK2 + GetLanguage(),
                       setup_bg_tex_addr) == 0)                         /* 192 */
    {
        res = 0;
    }
    else
    {
        res = (FileLoadIsEnd2(SETUP_FONT_PK2 + GetLanguage(),
                              setup_font_tex_addr) != 0);               /* 193 */
    }

    return res;                                                         /* 199 */
}

/* Step 3 waits for the fade-out to reach its end state and only then puts the
 * title BGM back and leaves. */
void SetupMain(void)
{
    if (setup_ctrl.step == SETUP_DISP_INIT)                             /* 211 */
    {
        SetupDispInit();                                                /* 213 */
        setup_ctrl.step = SETUP_LOAD_WAIT;                              /* 215 */
    }

    if ((setup_ctrl.step == SETUP_LOAD_WAIT) &&
        (SetupTexLoadWait() != 0))                                      /* 219 */
    {
        setup_ctrl.step = SETUP_MAIN;                                   /* 221 */
    }

    if ((setup_ctrl.step == SETUP_RETURN) &&
        (setup_disp_ctrl.anim_step == SETUP_ANIM_END))                  /* 229 */
    {
        SetTitleStreamID(StreamAutoPlay(BGM000_TITLE_STR, BGM000_TITLE_HXD,
                                        0xc, 0, 0, 0x3200, 0,
                                        (SND_3D_SET *)0));              /* 230 */
        SetNextGPhase(GID_TITLE_MENU);                                  /* 233 */
    }
}

/* Asked for by the setup menu; the actual exit happens in SetupMain(). */
void SetupReturnTitleReq(void)
{
    setup_disp_ctrl.anim_step = SETUP_ANIM_OUT;                         /* 249 */
    setup_disp_ctrl.anim_timer = '\0';                                  /* 250 */
    setup_ctrl.step = SETUP_RETURN;                                     /* 251 */
}

void *GetSetupBgPk2Addr(void)
{
    return setup_bg_tex_addr;                                           /* 268 */
}

void *GetSetupFontPk2Addr(void)
{
    return setup_font_tex_addr;                                         /* 279 */
}

void *GetSetupMsnslPk2Addr(void)
{
    return mission_sel_tex_addr;                                        /* 290 */
}

void SetupMemFree(void)
{
    SetupTexLoadCancel(setup_bg_tex_addr,
                       SETUP_BG_PK2 + GetLanguage());                   /* 303 */
    SetupTexLoadCancel(setup_font_tex_addr,
                       SETUP_FONT_PK2 + GetLanguage());                 /* 304 */
    SetupTexLoadCancel(mission_sel_tex_addr,
                       MISSION_SEL_PK2 + GetLanguage());                /* 306 */

    LiberateSetupTexMem(&setup_bg_tex_addr);                            /* 310 */
    LiberateSetupTexMem(&setup_font_tex_addr);                          /* 311 */
    LiberateSetupTexMem(&mission_sel_tex_addr);                         /* 313 */
}

static void LiberateSetupTexMem(void **tex_addr)
{
    if (*tex_addr != (void *)0)
    {
        ol_loadFreeHeap(*tex_addr);
        *tex_addr = (void *)0;
    }
}

static void SetupTexLoadCancel(void *tex_addr, int data_label)
{
    if ((tex_addr != (void *)0) &&
        (FileLoadIsEnd2(data_label, tex_addr) == 0))
    {
        FileLoadCancel2(data_label, tex_addr,
                        (FILE_LOAD_CALLBACK)0, (void *)0);
    }
}

static void SetupDispInit(void)
{
    setup_disp_ctrl.anim_timer = '\0';                                  /* 360 */
    setup_disp_ctrl.anim_step = '\0';                                   /* 361 */
}

/* The background is skipped on the last frame of the fade-out -- anim_step 4
 * means the fade is over and the screen is on its way out. */
void SetupDispMain(void)
{
    u_char alpha;

    if ((setup_ctrl.step == SETUP_MAIN) ||
        (setup_ctrl.step == SETUP_RETURN))                              /* 377 */
    {
        alpha = Zero2Anim2D_InOutAnimCtrl(&setup_disp_ctrl.anim_step,
                                          &setup_disp_ctrl.anim_timer,
                                          10, 5);                       /* 378 */

        if (setup_disp_ctrl.anim_step != SETUP_ANIM_END)                /* 380 */
        {
            SetupBgDisp(0, 0, alpha, setup_bg_tex_addr);                /* 382 */
        }
    }
}

/* Four upright plates and two turned a quarter turn about a centre one
 * sprite-width down -- the same idiom DispTitleZeroLogo() uses.  The black
 * wash on top is drawn at a fixed 0x26, not at `alpha`. */
static void SetupBgDisp(int off_x, int off_y, u_char alpha, void *pk2_addr)
{
    DISP_SPRT bg_ds;
    int i;

    PK2SendVram((uintptr_t)pk2_addr, -1, -1, 0);

    for (i = 0; i < 4; i++)
    {
        CopySprDToSpr(&bg_ds, setup_tex + i);
        bg_ds.x += (float)off_x;
        bg_ds.y += (float)off_y;
        bg_ds.alpha = (u_char)(((int)bg_ds.alpha * (int)alpha) >> 7);
        DispSprD(&bg_ds);
    }

    for (i = 4; i < 6; i++)
    {
        CopySprDToSpr(&bg_ds, setup_tex + i);
        bg_ds.crx = bg_ds.x = bg_ds.x + (float)off_x;
        bg_ds.rot = 270.0f;
        bg_ds.cry = bg_ds.y = bg_ds.y + (float)off_y + (float)bg_ds.w;
        bg_ds.alpha = (u_char)(((int)bg_ds.alpha * (int)alpha) >> 7);
        DispSprD(&bg_ds);
    }

    SetupBlackBgDisp(off_x, off_y, 0x26);
}

/* The record's own alpha of 0x26 is immediately overwritten -- the same
 * build-then-substitute shape NewGameBlackBgDisp() has. */
void SetupBlackBgDisp(int off_x, int off_y, u_char alpha)
{
    DISP_SQAR dsq;
    SQAR_DAT black_bg = { 640, 448, 0, 0, 0, 0, 0, 0, 0x26 };           /* 433 */

    (void)off_x;
    (void)off_y;

    CopySqrDToSqr(&dsq, &black_bg);                                     /* 437 */
    dsq.alpha = alpha;                                                  /* 439 */
    DispSqrD(&dsq);                                                     /* 439 */
}

// ──────────────────────────────────────────────────────────────────────
// GPhase per-phase callbacks, invoked via the main/gphase.c dispatch tables.
// GID_TITLE_SETUPMENU and GID_TITLE_MISSION are the two modes this file
// hosts; the four Mission_* phases live under the second of them.

void init_Title_SetupMenu(void)
{
    SetupMenuInit();                                                    /* 453 */
}

GPHASE_ENUM one_Title_SetupMenu(GPHASE_ENUM dummy)
{
    (void)dummy;

    if ((setup_ctrl.step == SETUP_MAIN) ||
        (setup_ctrl.step == SETUP_RETURN))                              /* 459 */
    {
        SetupMenuMain();                                                /* 461 */
        SetupMenuDispMain();                                            /* 464 */
    }

    return GPHASE_CONTINUE;                                             /* 468 */
}

void end_Title_SetupMenu(void)
{
}

void init_Title_Mission(void)
{
    PlayMissionSelBGM();                                                /* 479 */
}

GPHASE_ENUM pre_Title_Mission(GPHASE_ENUM dummy)
{
    (void)dummy;
    return GPHASE_CONTINUE;
}

GPHASE_ENUM after_Title_Mission(GPHASE_ENUM result)
{
    (void)result;
    return GPHASE_CONTINUE;
}

void end_Title_Mission(void)
{
}

void init_Mission_Sel(void)
{
    MissionSelInit();                                                   /* 498 */
}

GPHASE_ENUM one_Mission_Sel(GPHASE_ENUM dummy)
{
    (void)dummy;

    if ((setup_ctrl.step == SETUP_MAIN) ||
        (setup_ctrl.step == SETUP_RETURN))                              /* 503 */
    {
        MissionSelMain();                                               /* 505 */
        MissionSelDisp();                                               /* 507 */
    }

    return GPHASE_CONTINUE;                                             /* 510 */
}

void end_Mission_Sel(void)
{
    MissionSelEnd();                                                    /* 515 */
}

void init_Mission_Cam(void)
{
    MenuCamMainInit('\x01');                                            /* 523 */
}

GPHASE_ENUM one_Mission_Cam(GPHASE_ENUM dummy)
{
    (void)dummy;

    if (MenuCamMain() != 0)                                             /* 528 */
    {
        SetNextGPhase(GID_MISSION_SEL);                                 /* 535 */
    }
    else
    {
        MenuCamMainDisp();                                              /* 532 */
    }

    return GPHASE_CONTINUE;
}

void end_Mission_Cam(void)
{
}

void init_Mission_Album(void)
{
    AlbumInit(0);                                                       /* 546 */
}

/* Unlike the camera and save phases, the album is drawn on the frame it
 * finishes as well -- AlbumDispMain() is outside the test. */
GPHASE_ENUM one_Mission_Album(GPHASE_ENUM dummy)
{
    (void)dummy;

    if (AlbumMain() != 0)                                               /* 551 */
    {
        SetNextGPhase(GID_MISSION_SEL);                                 /* 555 */
    }

    AlbumDispMain();                                                    /* 557 */

    return GPHASE_CONTINUE;
}

void end_Mission_Album(void)
{
}

void init_Mission_Save(void)
{
    GameDataSaveInit('\x02');                                           /* 567 */
}

GPHASE_ENUM one_Mission_Save(GPHASE_ENUM dummy)
{
    (void)dummy;

    if (GameDataSaveMain() != 0)                                        /* 572 */
    {
        SetNextGPhase(GID_MISSION_SEL);                                 /* 578 */
    }
    else
    {
        GameDataSaveDispMain();                                         /* 575 */
    }

    return GPHASE_CONTINUE;
}

void end_Mission_Save(void)
{
    GameDataSaveEnd();                                                  /* 582 */
}
