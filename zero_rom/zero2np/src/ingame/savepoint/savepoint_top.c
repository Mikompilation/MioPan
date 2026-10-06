// FILE: /home/zero_rom/zero2np/src/ingame/savepoint/savepoint_top.c
//
// The save-point menu: save / album / leave, with a yes/no confirm over the
// first two.
//
// Two windows, each with its own Zero2Anim2D in/out animation, and the pad
// handler is chosen by savepoint_top_ctrl.mode.  Both handlers are gated on
// their window having finished animating -- the menu waits for anim_step 2
// (fully open) *and* conf_anim_step 4 (confirm fully closed), the confirm
// handler for conf_anim_step 2 -- so input cannot arrive mid-transition.
//
// Choosing "leave" needs no confirmation: SavePointTopPad() and
// SavePointTopExeDecision() both send it straight to SavePointEndReq().
// That is why the csr switch appears three times in the file with the same
// shape -- once per place a decision can be taken.
//
// The text pak is language-dependent (SAVEPOINT_MOJI_PK2 + GetLanguage()) and
// so is loaded separately from savepoint_main.c's background; like title.o,
// the sum is computed fresh at each of the three use sites, with no helper
// and no clamp on the language.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), savepoint_top.o.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.  SavePointTopDisp() has no local-variable stabs at all,
// so its `alpha` is inferred from the value held in s0 across the draw calls.

#include "savepoint_top.h"

#include "savepoint_disp.h"                         /* SavePoint_MenuWinDisp  */
#include "savepoint_main.h"                         /* GetSavePointMainTexMem */
#include "tim_dat/savepoint_dat.h"                  /* savepoint_tex          */

#include "../menu/zero2_anim2d.h"                   /* Zero2Anim2D_InOut...   */
#include "../../common/utility2.h"                  /* PRINT_ASSERT           */
#include "../../graphics/graph2d/draw_cmn.h"        /* DrawCmnSelCsr          */
#include "../../graphics/graph2d/g2d_draw.h"        /* DISP_SPRT              */
#include "../../graphics/graph2d/message.h"         /* PrintMsg               */
#include "../../graphics/graph2d/tim2.h"            /* PK2SendVram            */
#include "../../main/gphase.h"                      /* SetNextGPhase          */
#include "../../system/eeiop/cddat.h"               /* SAVEPOINT_MOJI_PK2     */
#include "../../system/eeiop/fileload.h"            /* FileLoadIsEnd2         */
#include "../../system/eeiop/snd3d.h"               /* SND_3D_SET             */
#include "../../system/os/system.h"                 /* GetLanguage            */
#include "../../system/pad/pad.h"                   /* pad / paddat           */

/* The message group every save-point string comes out of. */
#define SAVEPOINT_MSG_TYPE          0x4b

/* Menu open/close animation, in frames. */
#define SAVEPOINT_TOP_ANIM_IN_TIME  10
#define SAVEPOINT_TOP_ANIM_OUT_TIME 5

/* The two title pieces in savepoint_tex[]. */
#define SAVEPOINT_TEX_TITLE         14
#define SAVEPOINT_TEX_TITLE_NUM     2

static void SavePointTopCtrlInit(void);
static int  SavePointTopTexLoadWait(void);
static void SavePointTopPad(void);
static void SavePointEndReq(void);
static void SavePointTopConfPad(void);
static void SavePointTopExeDecision(void);
static void SavePointTopDispInit(void);
static void SavePointTopTitleDisp(int off_x, int off_y, u_char alpha);
static void SavePointTopSelMsgDisp(int off_x, int off_y, u_char alpha);
static void SavePointTopCsrDisp(int off_x, int off_y, u_char alpha);

static void              *savepoint_tex_addr;               /* sdata 3f3d28 */
static char               savepoint_top_init_flg;           /* sdata 3f3d2c */
static SAVEPOINT_TOP_CTRL savepoint_top_ctrl;               /* sbss  3f4f40 */
static SAVEPOINT_TOP_DISP savepoint_top_disp;               /* sbss  3f4f48 */

/* ==========================================================================
 *  Set-up
 * ======================================================================== */

/* Once per visit.  Steps straight past the display reset to step 1 and
 * anim_step 2, i.e. "pak already requested, window already open" -- the pak
 * was requested at room load and the menu is meant to be there the moment the
 * parent's fade clears, not to animate in behind it. */
void SavePointTopFirstInit(void)                                        /* 126 */
{
    SavePointTopCtrlInit();                                             /* 130 */
    SavePointTopDispInit();                                             /* 133 */

    savepoint_top_ctrl.step      = SAVEPOINT_TOP_STEP_LOAD_WAIT;        /* 135 */
    savepoint_top_ctrl.csr       = SAVEPOINT_TOP_CSR_SAVE;              /* 136 */

    savepoint_top_disp.anim_step = ZERO2_ANIM2D_STEP_SHOW;              /* 138 */

    savepoint_top_init_flg       = 0;                                   /* 140 */
}

/* Once per entry to the phase.  The first entry is the one FirstInit() just
 * set up, so it only claims the flag; every later entry -- coming back from
 * the save screen or the album -- resets the control block, which sends the
 * menu through step 0 and animates the window open again. */
void SavePointTopInit(void)                                             /* 148 */
{
    if (savepoint_top_init_flg != 0) {                                  /* 151 */
        SavePointTopCtrlInit();                                         /* 153 */
    }
    else {
        savepoint_top_init_flg = 1;                                     /* 156 */
    }
}

static void SavePointTopCtrlInit(void)                                  /* 165 */
{
    savepoint_top_ctrl.step     = SAVEPOINT_TOP_STEP_ENTRY;             /* 168 */
    savepoint_top_ctrl.mode     = SAVEPOINT_TOP_MODE_MENU;              /* 169 */
    savepoint_top_ctrl.conf_csr = 1;                    /* default "no" */ /* 170 */
}

/* ==========================================================================
 *  Text pak
 *
 *  Claimed and loaded through savepoint_main.c's helpers so both paks share
 *  one implementation.
 * ======================================================================== */

void SavePointTopBackGroundLoadReq(void)                                /* 178 */
{
    if (savepoint_tex_addr != nullptr) {                                /* 180 */
        LiberateSavePointMainTexMem(&savepoint_tex_addr);               /* 181 */
    }

    GetSavePointMainTexMem(&savepoint_tex_addr,
                           SAVEPOINT_MOJI_PK2 + GetLanguage());         /* 186 */
    SavePointMainTexLoadReq(savepoint_tex_addr,
                            SAVEPOINT_MOJI_PK2 + GetLanguage());        /* 189 */
}

static int SavePointTopTexLoadWait(void)                                /* 199 */
{
    if (FileLoadIsEnd2(SAVEPOINT_MOJI_PK2 + GetLanguage(),
                       savepoint_tex_addr) != 0) {                      /* 207 */
        return 1;
    }

    return 0;                                                           /* 212 */
}

/* ==========================================================================
 *  Per-frame control
 * ======================================================================== */

void SavePointTopMain(void)                                             /* 222 */
{
    switch (savepoint_top_ctrl.step) {                                  /* 225 */
    case SAVEPOINT_TOP_STEP_ENTRY:
        SavePointTopDispInit();                                         /* 228 */
        savepoint_top_ctrl.step = SAVEPOINT_TOP_STEP_LOAD_WAIT;         /* 231 */
        break;

    case SAVEPOINT_TOP_STEP_LOAD_WAIT:
        if (SavePointTopTexLoadWait() != 0) {                           /* 233 */
            savepoint_top_ctrl.step = SAVEPOINT_TOP_STEP_MENU;          /* 236 */
        }
        break;

    case SAVEPOINT_TOP_STEP_MENU:
        /* Both handlers wait for their own window to settle. */
        if (savepoint_top_disp.anim_step == ZERO2_ANIM2D_STEP_SHOW) {   /* 238 */
            if (savepoint_top_ctrl.mode == SAVEPOINT_TOP_MODE_MENU) {   /* 239 */
                if (savepoint_top_disp.conf_anim_step == ZERO2_ANIM2D_STEP_END) { /* 241 */
                    SavePointTopPad();                                  /* 242 */
                }
            }
            else if (savepoint_top_ctrl.mode == SAVEPOINT_TOP_MODE_CONF) {
                if (savepoint_top_disp.conf_anim_step == ZERO2_ANIM2D_STEP_SHOW) { /* 246 */
                    SavePointTopConfPad();                              /* 247 */
                }
            }
            else {
                PRINT_ASSERT("Error! %s", __FUNCTION__);                /* 251 */
            }
        }
        break;

    case SAVEPOINT_TOP_STEP_DECIDED:
        /* Hold until the menu window has animated shut, then hand over. */
        if (savepoint_top_disp.anim_step == ZERO2_ANIM2D_STEP_END) {    /* 256 */
            switch (savepoint_top_ctrl.csr) {                           /* 257 */
            case SAVEPOINT_TOP_CSR_SAVE:
                SetNextGPhase(GID_SAVEPOINT_SAVE);                      /* 259 */
                break;

            case SAVEPOINT_TOP_CSR_ALBUM:
                SetNextGPhase(GID_SAVEPOINT_ALBUM);                     /* 262 */
                break;

            case SAVEPOINT_TOP_CSR_EXIT:
                SavePointEndReq();                                      /* 265 */
                break;

            default:
                PRINT_ASSERT("Error! %s", __FUNCTION__);                /* 268 */
                break;
            }
        }
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 273 */
        break;
    }
}

/* The menu window's pad.  UP/DOWN wrap through the three rows; CROSS opens
 * the confirm window for save and album but takes "leave" immediately;
 * TRIANGLE is a shortcut straight to the "leave" row -- note that it only
 * *selects* it, leaving SavePointTopMain()'s step-3 arm to act on it. */
static void SavePointTopPad(void)                                       /* 282 */
{
    if ((pad[0].rpt & 0x1000) || GetPadAnalogRpt(0)) {                  /* 286 */
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 287 */

        savepoint_top_ctrl.csr =
            (char)((savepoint_top_ctrl.csr + (SAVEPOINT_TOP_CSR_NUM - 1))
                   % SAVEPOINT_TOP_CSR_NUM);                            /* 289 */
    }
    else if ((pad[0].rpt & 0x4000) || GetPadAnalogRpt(1)) {             /* 292 */
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 293 */

        savepoint_top_ctrl.csr =
            (char)((savepoint_top_ctrl.csr + 1) % SAVEPOINT_TOP_CSR_NUM); /* 295 */
    }
    else if (*paddat[0] == 1) {                                         /* 298 */
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 299 */

        savepoint_top_ctrl.conf_csr = 1;                                /* 301 */

        switch (savepoint_top_ctrl.csr) {                               /* 303 */
        case SAVEPOINT_TOP_CSR_SAVE:
        case SAVEPOINT_TOP_CSR_ALBUM:
            savepoint_top_ctrl.mode          = SAVEPOINT_TOP_MODE_CONF; /* 306 */
            savepoint_top_ctrl.conf_csr      = 1;   /* dead; already 1 */ /* 307 */
            savepoint_top_disp.conf_anim_step  = ZERO2_ANIM2D_STEP_START; /* 308 */
            savepoint_top_disp.conf_anim_timer = 0;                     /* 309 */
            break;

        case SAVEPOINT_TOP_CSR_EXIT:
            SavePointEndReq();                                          /* 311 */
            break;

        default:
            PRINT_ASSERT("Error! %s", __FUNCTION__);                    /* 314 */
            break;
        }
    }
    else if (*paddat[1] == 1) {                                         /* 318 */
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 319 */

        savepoint_top_ctrl.csr = SAVEPOINT_TOP_CSR_EXIT;                /* 321 */
    }
}

/* Leaving is the one choice that skips the confirm window entirely: it goes
 * straight to step 3 and asks the parent phase to start closing. */
static void SavePointEndReq(void)                                       /* 330 */
{
    savepoint_top_ctrl.step = SAVEPOINT_TOP_STEP_DECIDED;               /* 333 */

    SavePointMainFadeOutReq();                                          /* 335 */
}

/* The confirm window's pad.  LEFT and RIGHT both just flip yes/no. */
static void SavePointTopConfPad(void)                                   /* 343 */
{
    if ((pad[0].rpt & 0x8000) || GetPadAnalogRpt(2) ||                  /* 347 */
        (pad[0].rpt & 0x2000) || GetPadAnalogRpt(3)) {                  /* 353 */
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 354 */

        savepoint_top_ctrl.conf_csr ^= 1;                               /* 356 */
    }
    else if (*paddat[0] == 1) {                                         /* 359 */
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 360 */

        if (savepoint_top_ctrl.conf_csr == 0) {                         /* 363 */
            SavePointTopExeDecision();                                  /* 364 */
            return;
        }

        /* "No": close the confirm window and go back to the menu. */
        savepoint_top_ctrl.mode            = SAVEPOINT_TOP_MODE_MENU;   /* 368 */
        savepoint_top_disp.conf_anim_step  = ZERO2_ANIM2D_STEP_OUT;     /* 369 */
        savepoint_top_disp.conf_anim_timer = 0;                         /* 370 */
    }
    else if (*paddat[1] == 1) {                                         /* 374 */
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 375 */

        savepoint_top_ctrl.mode            = SAVEPOINT_TOP_MODE_MENU;   /* 377 */
        savepoint_top_disp.conf_anim_step  = ZERO2_ANIM2D_STEP_OUT;     /* 378 */
        savepoint_top_disp.conf_anim_timer = 0;                         /* 379 */
    }
}

/* "Yes" on the confirm window.  Closes both windows and moves to step 3;
 * SavePointTopMain() picks up the row once the menu has animated shut. */
static void SavePointTopExeDecision(void)                               /* 388 */
{
    switch (savepoint_top_ctrl.csr) {                                   /* 391 */
    case SAVEPOINT_TOP_CSR_SAVE:
    case SAVEPOINT_TOP_CSR_ALBUM:
        savepoint_top_ctrl.step            = SAVEPOINT_TOP_STEP_DECIDED; /* 394 */

        savepoint_top_disp.anim_step       = ZERO2_ANIM2D_STEP_OUT;     /* 396 */
        savepoint_top_disp.anim_timer      = 0;                         /* 397 */
        savepoint_top_disp.conf_anim_step  = ZERO2_ANIM2D_STEP_OUT;     /* 398 */
        savepoint_top_disp.conf_anim_timer = 0;                         /* 399 */
        break;                                                          /* 400 */

    case SAVEPOINT_TOP_CSR_EXIT:
        /* Unreachable: "leave" never opens the confirm window. */
        SavePointEndReq();                                              /* 402 */
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 405 */
        break;
    }
}

void SavePointTopMemFree(void)                                          /* 418 */
{
    LiberateSavePointMainTexMem(&savepoint_tex_addr);                   /* 422 */
}

/* ==========================================================================
 *  Per-frame draw
 * ======================================================================== */

static void SavePointTopDispInit(void)                                  /* 434 */
{
    savepoint_top_disp.anim_step       = ZERO2_ANIM2D_STEP_START;       /* 437 */
    savepoint_top_disp.anim_timer      = 0;                             /* 438 */
    savepoint_top_disp.conf_anim_step  = ZERO2_ANIM2D_STEP_END;         /* 439 */
    savepoint_top_disp.conf_anim_timer = 0;                             /* 440 */
}

/* Both windows fade through Zero2Anim2D_InOutAnimCtrl, and everything drawn
 * under a window inherits that window's alpha.  Once anim_step reaches END
 * (4) the whole menu stops drawing -- which is what makes step 3 wait for it
 * before acting on the choice. */
void SavePointTopDisp(void)                                             /* 448 */
{
    u_char alpha;

    /* The prompt drawn inside the confirm window: one per menu row. */
    static const int msg_id_tbl[3] =                        /* rdata 3c5000 */
    {
        3,      /* save?  */
        4,      /* album? */
        5,      /* leave? */
    };

    if ((savepoint_top_ctrl.step >= SAVEPOINT_TOP_STEP_MENU) &&         /* 462 */
        (savepoint_top_disp.anim_step != ZERO2_ANIM2D_STEP_END))        /* 463 */
    {
        alpha = Zero2Anim2D_InOutAnimCtrl(&savepoint_top_disp.anim_step,
                                          &savepoint_top_disp.anim_timer,
                                          SAVEPOINT_TOP_ANIM_IN_TIME,
                                          SAVEPOINT_TOP_ANIM_OUT_TIME); /* 465 */

        SavePoint_MenuWinDisp(0, 0, alpha);                             /* 468 */

        /* The text pak has to be resident before the title sprites are drawn. */
        PK2SendVram((uintptr_t)savepoint_tex_addr, -1, -1, 0);          /* 470 */

        SavePointTopTitleDisp(0, 0, alpha);                             /* 473 */
        SavePointTopCaptionDisp(0, 0, alpha);                           /* 476 */
        SavePointTopSelMsgDisp(0, 0, alpha);                            /* 479 */
        SavePointTopCsrDisp(0, 0, alpha);                               /* 482 */

        if (savepoint_top_disp.conf_anim_step != ZERO2_ANIM2D_STEP_END) { /* 484 */
            alpha = Zero2Anim2D_InOutAnimCtrl(&savepoint_top_disp.conf_anim_step,
                                              &savepoint_top_disp.conf_anim_timer,
                                              SAVEPOINT_TOP_ANIM_IN_TIME,
                                              SAVEPOINT_TOP_ANIM_OUT_TIME); /* 486 */

            SavePoint_MenuConfWinDisp((int)savepoint_top_ctrl.conf_csr,
                                      0, 0, alpha);                     /* 489 */

            PrintMsg(SAVEPOINT_MSG_TYPE, msg_id_tbl[savepoint_top_ctrl.csr],
                     68, 309, 1, (int)alpha, 0);                        /* 493 */
        }
    }
}

/* The two-piece screen title. */
static void SavePointTopTitleDisp(int off_x, int off_y, u_char alpha)   /* 508 */
{
    DISP_SPRT title_ds;
    int       i;

    for (i = 0; i < SAVEPOINT_TEX_TITLE_NUM; i++) {                     /* 514 */
        CopySprDToSpr(&title_ds, &savepoint_tex[SAVEPOINT_TEX_TITLE + i]); /* 515 */

        title_ds.x = title_ds.x + (float)off_x;                         /* 516 */
        title_ds.y = title_ds.y + (float)off_y;                         /* 516 */

        title_ds.alpha = (u_char)(((int)title_ds.alpha * (int)alpha) >> 7); /* 517 */

        DispSprD(&title_ds);                                            /* 518 */
    }
}

/* The three rows, centre-arranged on x = 320.  The selected one is drawn in
 * colour 7 and the rest in 1. */
static void SavePointTopSelMsgDisp(int off_x, int off_y, u_char alpha)  /* 530 */
{
    int i;

    static const int msg_y_tbl[3] =                         /* rdata 3c5010 */
    {
        146, 176, 206,
    };

    static const int msg_id_tbl[3] =                        /* rdata 3c5020 */
    {
        0,      /* save  */
        1,      /* album */
        2,      /* leave */
    };

    for (i = 0; i < SAVEPOINT_TOP_CSR_NUM; i++) {                       /* 545 */
        /* No local for this in the stabs -- the compare is line 546 and the
         * select folds into the call at 554. */
        PrintMsg_Arrange(SAVEPOINT_MSG_TYPE, msg_id_tbl[i],
                         off_x + 320, msg_y_tbl[i] + off_y,
                         (savepoint_top_ctrl.csr == i) ? 7 : 1,          /* 546 */
                         (int)alpha, 0, 0, 0, 2);                       /* 554 */
    }
}

/* The row highlight, 272 wide, stepping 30 pixels per row from y = 142. */
static void SavePointTopCsrDisp(int off_x, int off_y, u_char alpha)     /* 566 */
{
    (void)off_x;                /* both offsets are dead in the ROM */
    (void)off_y;

    DrawCmnSelCsr(0, 184.0f,
                  (float)(savepoint_top_ctrl.csr * 30 + 142),
                  alpha, 272.0f, 1);                                    /* 571 */
}
