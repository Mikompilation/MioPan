// FILE: /home/zero_rom/zero2np/src/ingame/puzzle/kaza/kaza_pzl.c
//
// The kazaguruma (pinwheel) puzzle, first board.
//
// Five pinwheels: a big one in the middle and four around it.  Each has four
// coloured wings, and the puzzle wants the four places where an outer wheel
// touches the centre one to match in colour.  The cursor picks one of the
// four outer wheels and X turns it -- but the centre wheel turns with it, the
// other way, so every move changes two of the four joins at once.
//
// A wheel's state is a quarter-turn count, and its wing colours come out of
// its ring in puzzle_kaza_dat.c: GetKazaPuzzlePinWheelWingColor() is
// (wing + turns) mod 4 for the centre and (wing - turns) mod 4 for the rest,
// which is what makes them counter-rotate.  Four calls per join, four joins,
// and KazaPuzzleClearCheck() has its answer.
//
// remainder_frequency is the move budget: four turns, and running out sends
// the puzzle to mode 4 (failed) rather than ending it -- the player still has
// to acknowledge.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), kaza_pzl.o.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.  Statements whose only memory access goes through
// fixed_array's inlined operator[] leave no $LM of their own -- their line is
// swallowed by fixed_array.h's 124/125 -- so a few are interpolated into a
// measured gap rather than read out of it.

#include "kaza_pzl.h"

#include <stddef.h>                                 /* NULL                   */
#include <stdio.h>                                  /* printf                 */

#include "puzzle_kaza_dat.h"                        /* kaza_panel_center      */
#include "../puzzle.h"                              /* GetPzlTexDataAddr      */

#include "../../../common/utility2.h"               /* PRINT_ASSERT           */
#include "../../../common/variable.h"               /* pad                    */
#include "../../../main/gphase.h"                   /* SetNextGPhase          */

#include "../../menu/anim_2d.h"                     /* Anim2D_CalcNowAlpha    */
#include "../../photo/finder.h"                     /* FinderBankSetup        */

#include "../../../graphics/graph2d/draw_cmn.h"     /* DrawCmnWindow          */
#include "../../../graphics/graph2d/g2d_draw.h"     /* DISP_SPRT / DispSprD   */
#include "../../../graphics/graph2d/message.h"      /* PrintMsg               */
#include "../../../graphics/graph2d/tim2.h"         /* PK2SendVram            */
#include "../../../system/eeiop/snd_buffer.h"       /* SndBufIsPlaying        */
#include "../../../system/eeiop/sndbank.h"          /* SndBankPlay            */
#include "../../../system/os/system.h"              /* GetLanguage            */
#include "../../../system/pad/pad.h"                /* paddat/GetPadAnalogRpt */

/* --------------------------------------------------------------------------
 *  Module state
 * ------------------------------------------------------------------------ */

static void  KazaPuzzleCtrlInit(void);
static void  KazaPuzzleMode(void);
static void  KazaPuzzleRotAnimExe(void);
static void  KazaPuzzleReqNextSubStep(char next_step);
static void  KazaPuzzleSubOutAnimCheck(void);
static int   KazaPuzzleClearCheck(void);
static int   GetKazaPuzzlePinWheelWingColor(int pinwheel_label, int wing_label);
static void  KazaPuzzleStartMsgPad(void);
static void  KazaPuzzlePinWheelSelPad(void);
static void  KazaPuzzleRotAnimStart(void);
static void  KazaPuzzleExitPad(void);
static void  KazaPuzzleExitReq(void);
static void  KazaPuzzleClearPad(void);
static void  KazaPuzzleDispInit(void);
static void  KazaPuzzleAnim(char *anim_step, short *anim_timer, u_char *alpha);
static void  KazaPuzzleStartMsgDisp(int off_x, int off_y, u_char alpha);
static void  KazaPuzzlePinWheelSelDisp(int off_x, int off_y, u_char alpha);
static u_char KazaPuzzleModeAnimCtrl(void);
static void  KazaPuzzleExitSelDisp(int off_x, int off_y, u_char alpha);
static void  KazaPuzzleClearDisp(int off_x, int off_y, u_char alpha);
static void  KazaPuzzleFailureDisp(int off_x, int off_y, u_char alpha);
static void  KazaPuzzleBgDisp(int off_x, int off_y, u_char alpha);
static void  KazaPuzzlePanelAllShadowDisp(int off_x, int off_y, u_char alpha);
static void  KazaPuzzlePanelShadowDisp(int panel_label, int off_x, int off_y, u_char alpha);
static void  KazaPuzzlePanelAllDisp(int off_x, int off_y, u_char alpha);
static float KazaPuzzleRotAnim(void);
static void  KazaPuzzlePanelDisp(int panel_label, int off_x, int off_y, u_char alpha, float rot);
static void  KazaPuzzlePanelAllEmbossShadowDisp(int off_x, int off_y, u_char alpha);
static void  KazaPuzzlePanelEmbossShadowDisp(int panel_label, int off_x, int off_y, u_char alpha);
static void  KazaPuzzlePanelAllEmbossHighLightDisp(int off_x, int off_y, u_char alpha);
static void  KazaPuzzlePanelEmbossHighLightDisp(int panel_label, int off_x, int off_y, u_char alpha);
static void  KazaPuzzlePanelFlareDisp(int off_x, int off_y, u_char alpha);
static u_char KazaPuzzleFlareAnim(void);
static void  KazaPuzzleCapBaseDisp(int off_x, int off_y, u_char alpha);
static void  KazaPuzzleCapDisp(int off_x, int off_y, u_char alpha);
static void  KazaPuzzleRemainderFrequency(int off_x, int off_y, u_char alpha);
static void  KazaPuzzleScreenMask(int off_x, int off_y, u_char alpha);
static void  KazaPuzzleCmnWinDisp(int off_x, int off_y, u_char alpha);
static void  KazaPuzzleStartMsgWinDisp(int off_x, int off_y, u_char alpha);
static void  KazaPuzzleExitConfWinDisp(int off_x, int off_y, u_char alpha);
static void  KazaPuzzleClearWinDisp(int off_x, int off_y, u_char alpha);
static void  KazaPuzzleFailureWinDisp(int off_x, int off_y, u_char alpha);
static void  KazaPuzzleBlackBgDisp(int off_x, int off_y, u_char alpha);

/* mode 0 opening message, 1 the board, 2 the quit prompt, 3 solved, 4 out of
 * turns.  Modes 3 and 4 share a pad handler that only waits for a button. */
static void (*kaza_pzl_pad_func[5])(void) =                 /* data 3193e0 */
{
    KazaPuzzleStartMsgPad,
    KazaPuzzlePinWheelSelPad,
    KazaPuzzleExitPad,
    KazaPuzzleClearPad,
    KazaPuzzleClearPad,
};

static void (*kaza_pzl_disp_func[5])(int, int, u_char) =    /* data 3193f8 */
{
    KazaPuzzleStartMsgDisp,
    KazaPuzzlePinWheelSelDisp,
    KazaPuzzleExitSelDisp,
    KazaPuzzleClearDisp,
    KazaPuzzleFailureDisp,
};

static KAZA_PZL_CTRL kaza_pzl_ctrl;                         /* bss 4b41c8 */
static KAZA_PZL_DISP kaza_pzl_disp;                         /* bss 4b41f0 */

/* Panel index -> its wing-colour ring.  Built by the file's static
 * constructor, since reference_fixed_array's storage pointer is not a
 * link-time constant. */
static int *kaza_panel_color[5] =                           /* bss 4b4210 */
{
    &kaza_panel_center[0],
    &kaza_panel_right_up[0],
    &kaza_panel_right_down[0],
    &kaza_panel_left_up[0],
    &kaza_panel_left_down[0],
};

/* Cursor (row, column) -> panel index.  The centre panel, index 0, is not
 * selectable. */
static const int kaza_panel_label_tbl[2][2] =               /* rdata 3ba128 */
{
    { 3, 1 },       /* up:   left, right */
    { 4, 2 },       /* down: left, right */
};

/* ==========================================================================
 *  Set-up
 * ======================================================================== */

void KazaPuzzleExeInit(void)                                            /* 245 */
{
    KazaPuzzleCtrlInit();                                               /* 246 */
    KazaPuzzleDispInit();                                               /* 249 */
}

static void KazaPuzzleCtrlInit(void)                                    /* 257 */
{                                                                       /* 258 */
    int i;

    kaza_pzl_ctrl.step         = 0;                                     /* 263 */
    kaza_pzl_ctrl.mode         = 0;                                     /* 265 */
    kaza_pzl_ctrl.next_mode    = 0;                                     /* 266 */
    kaza_pzl_ctrl.clear_flg    = 0;                                     /* 268 */
    kaza_pzl_ctrl.csr_yoko     = 0;                                     /* 269 */
    kaza_pzl_ctrl.csr_tate     = 0;                                     /* 270 */
    kaza_pzl_ctrl.exit_csr     = 1;                                     /* 271 */
    kaza_pzl_ctrl.rot_anim_flg = 0;                                     /* 272 */

    /* Four turns to solve it. */
    kaza_pzl_ctrl.remainder_frequency = 4;                              /* 273 */

    for (i = 0; i < 5; i++) {                                           /* 275 */
        kaza_pzl_ctrl.rot_num[i] = 0;                                   /* 277 */
    }
}

/* ==========================================================================
 *  Per-frame control
 * ======================================================================== */

int KazaPuzzleMain(void)                                                /* 290 */
{                                                                       /* 291 */
    int res;

    res = 1;                                                            /* 294 */

    if (kaza_pzl_ctrl.step == 0) {                                      /* 297 */
        KazaPuzzleDispInit();                                           /* 298 */
        kaza_pzl_ctrl.step = 1;                                         /* 300 */
    }

    if (kaza_pzl_ctrl.step == 1) {                                      /* 303 */
        KazaPuzzleMode();                                               /* 304 */
    }

    if ((kaza_pzl_ctrl.step == 2) &&                                    /* 307 */
        (kaza_pzl_disp.anim_step == 4) &&                               /* 308 */
        (SndBufIsPlaying(kaza_pzl_ctrl.snd_id) == 0)) {                 /* 310 */
        SndBankRelease(GetPzlSndBankID());                              /* 312 */
        FinderBankSetup();                                              /* 316 */
        kaza_pzl_ctrl.step = 3;                                         /* 317 */
    }

    if ((kaza_pzl_ctrl.step == 3) &&                                    /* 322 */
        (FinderBankIsReady() != 0)) {                                   /* 323 */
        SetNextGPhase(GID_STORY_NORMAL);                                /* 324 */
    }

    return res;                                                         /* 330 */
}

/* A turn in progress locks out the pad -- the wheels have to land first. */
static void KazaPuzzleMode(void)                                        /* 339 */
{                                                                       /* 340 */
    KazaPuzzleSubOutAnimCheck();                                        /* 343 */

    if (kaza_pzl_disp.sub_anim_step == 2) {                             /* 345 */
        if (kaza_pzl_ctrl.rot_anim_flg != 0) {                          /* 347 */
            KazaPuzzleRotAnimExe();                                     /* 348 */
        } else if (kaza_pzl_pad_func[kaza_pzl_ctrl.mode] != NULL) {
            kaza_pzl_pad_func[kaza_pzl_ctrl.mode]();                    /* 353 */
        }
    }
}

/* --------------------------------------------------------------------------
 *  The 20-frame turn has landed: commit it.  The selected wheel gains a
 *  quarter turn and the centre one loses one, both wrapped -- rot_num into
 *  0..3 and panel_rot into 0..360.
 * ------------------------------------------------------------------------ */
static void KazaPuzzleRotAnimExe(void)                                  /* 362 */
{                                                                       /* 363 */
    int sel_panel;

    sel_panel = kaza_panel_label_tbl[kaza_pzl_ctrl.csr_tate][kaza_pzl_ctrl.csr_yoko]; /* 367 */

    if (kaza_pzl_ctrl.rot_anim_flg != 0) {                              /* 371 */
        kaza_pzl_disp.rot_anim_timer++;                                 /* 373 */

        if (kaza_pzl_disp.rot_anim_timer > 19) {                        /* 376 */
            kaza_pzl_ctrl.rot_anim_flg   = 0;                           /* 377 */
            kaza_pzl_disp.rot_anim_step  = 0;                           /* 378 */
            kaza_pzl_disp.rot_anim_timer = 0;                           /* 379 */

            /* The centre wheel counter-rotates. */
            kaza_pzl_ctrl.rot_num[0] = (kaza_pzl_ctrl.rot_num[0] + 1) % 4;
            kaza_pzl_disp.panel_rot[0] = kaza_pzl_disp.panel_rot[0] - 90.0f;

            if (kaza_pzl_disp.panel_rot[0] < 0.0f) {
                kaza_pzl_disp.panel_rot[0] = kaza_pzl_disp.panel_rot[0] + 360.0f;
            }

            kaza_pzl_ctrl.rot_num[sel_panel] = (kaza_pzl_ctrl.rot_num[sel_panel] + 1) % 4;
            kaza_pzl_disp.panel_rot[sel_panel] = kaza_pzl_disp.panel_rot[sel_panel] + 90.0f;

            if (kaza_pzl_disp.panel_rot[sel_panel] >= 360.0f) {
                kaza_pzl_disp.panel_rot[sel_panel] =
                    kaza_pzl_disp.panel_rot[sel_panel] - 360.0f;
            }

            kaza_pzl_ctrl.remainder_frequency--;                        /* 394 */

            if (KazaPuzzleClearCheck() != 0) {                          /* 397 */
                kaza_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 2, 0, 0,
                                                   0x3200, 0x1000, 0,
                                                   (SND_3D_SET *)0);    /* 399 */
                KazaPuzzleReqNextSubStep(3);                            /* 400 */
                PuzzleClear(PZL_ID_KAZA);                               /* 403 */
            } else if (kaza_pzl_ctrl.remainder_frequency < 1) {         /* 407 */
                KazaPuzzleReqNextSubStep(4);                            /* 408 */
            }
        }
    }
}

static void KazaPuzzleReqNextSubStep(char next_step)
{
    kaza_pzl_ctrl.next_mode      = next_step;                           /* 424 */
    kaza_pzl_disp.sub_anim_step  = 3;                                   /* 425 */
    kaza_pzl_disp.sub_anim_timer = 0;                                   /* 426 */
}

static void KazaPuzzleSubOutAnimCheck(void)
{
    if (kaza_pzl_disp.sub_anim_step == 4) {                             /* 437 */
        kaza_pzl_disp.sub_anim_step = 0;                                /* 438 */
        kaza_pzl_ctrl.mode = kaza_pzl_ctrl.next_mode;                   /* 439 */
    }
}

/* --------------------------------------------------------------------------
 *  Four joins, each a pair of touching wings: centre wing 0 against the
 *  right-down wheel's wing 2, centre 1 against right-up's 3, centre 3 against
 *  left-down's 1, centre 2 against left-up's 0.
 * ------------------------------------------------------------------------ */
static int KazaPuzzleClearCheck(void)                                   /* 450 */
{                                                                       /* 451 */
    int res;

    res = 0;

    if ((GetKazaPuzzlePinWheelWingColor(0, 0) ==
         GetKazaPuzzlePinWheelWingColor(3, 2)) &&
        (GetKazaPuzzlePinWheelWingColor(0, 1) ==
         GetKazaPuzzlePinWheelWingColor(1, 3)) &&
        (GetKazaPuzzlePinWheelWingColor(0, 3) ==
         GetKazaPuzzlePinWheelWingColor(4, 1)) &&
        (GetKazaPuzzlePinWheelWingColor(0, 2) ==
         GetKazaPuzzlePinWheelWingColor(2, 0))) {                       /* 458 */
        res = 1;                                                        /* 467 */
        printf("Kaza Puzzle Clear!!\n");
    }

    return res;                                                         /* 472 */
}

/* The centre wheel adds its turn count and the outer four subtract theirs --
 * that is the whole counter-rotation, and it is why every move changes two
 * joins rather than one. */
static int GetKazaPuzzlePinWheelWingColor(int pinwheel_label, int wing_label) /* 480 */
{                                                                       /* 481 */
    int now_pos;

    if (pinwheel_label > 4) {                                           /* 488 */
        PRINT_ASSERT("Error! GetKazaPuzzlePinWheelWingColor");          /* 489 */
    }
    if (wing_label > 3) {                                               /* 491 */
        PRINT_ASSERT("Error! GetKazaPuzzlePinWheelWingColor");          /* 492 */
    }

    if (pinwheel_label == 0) {                                          /* 498 */
        now_pos = (wing_label + kaza_pzl_ctrl.rot_num[0]) % 4;
    } else {
        now_pos = (wing_label - kaza_pzl_ctrl.rot_num[pinwheel_label] + 4) % 4; /* 507 */
    }

    return kaza_panel_color[pinwheel_label][now_pos];                   /* 510 */
}

/* ==========================================================================
 *  Pad handlers, one per mode
 * ======================================================================== */

static void KazaPuzzleStartMsgPad(void)                                 /* 515 */
{                                                                       /* 516 */
    if (*paddat[0] == 1) {                                              /* 520 */
        KazaPuzzleReqNextSubStep(1);                                    /* 521 */
    } else if (*paddat[1] == 1) {                                       /* 524 */
        KazaPuzzleReqNextSubStep(1);                                    /* 525 */
    }
}

static void KazaPuzzlePinWheelSelPad(void)                              /* 534 */
{                                                                       /* 535 */
    if (((pad[0].rpt & 0x8000) != 0) || (GetPadAnalogRpt(2) != 0) ||    /* 539 */
        ((pad[0].rpt & 0x2000) != 0) || (GetPadAnalogRpt(3) != 0)) {
        kaza_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 0, 0, 0,
                                           0x3200, 0x1000, 0,
                                           (SND_3D_SET *)0);            /* 545 */
        kaza_pzl_ctrl.csr_yoko ^= 1;                                    /* 547 */
    } else if (((pad[0].rpt & 0x1000) != 0) || (GetPadAnalogRpt(0) != 0) || /* 548 */
               ((pad[0].rpt & 0x4000) != 0) || (GetPadAnalogRpt(1) != 0)) { /* 551 */
        kaza_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 0, 0, 0,
                                           0x3200, 0x1000, 0,
                                           (SND_3D_SET *)0);            /* 557 */
        kaza_pzl_ctrl.csr_tate ^= 1;                                    /* 559 */
    } else if (*paddat[0] == 1) {                                       /* 560 */
        kaza_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 1, 0, 0,
                                           0x3200, 0x1000, 0,
                                           (SND_3D_SET *)0);            /* 563 */
        KazaPuzzleRotAnimStart();                                       /* 565 */
    } else if (*paddat[1] == 1) {                                       /* 566 */
        kaza_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 3, 0, 0,
                                           0x3200, 0x1000, 0,
                                           (SND_3D_SET *)0);            /* 569 */
        KazaPuzzleReqNextSubStep(2);                                    /* 571 */
    }
}                                                                       /* 572 */

static void KazaPuzzleRotAnimStart(void)                                /* 584 */
{
    kaza_pzl_ctrl.rot_anim_flg   = 1;                                   /* 585 */
    kaza_pzl_disp.rot_anim_timer = 0;                                   /* 587 */
    kaza_pzl_disp.rot_anim_step  = 0;                                   /* 588 */
}

static void KazaPuzzleExitPad(void)                                     /* 595 */
{                                                                       /* 596 */
    if (((pad[0].rpt & 0x8000) != 0) || (GetPadAnalogRpt(2) != 0) ||    /* 600 */
        ((pad[0].rpt & 0x2000) != 0) || (GetPadAnalogRpt(3) != 0)) {
        kaza_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 0, 0, 0,
                                           0x3200, 0x1000, 0,
                                           (SND_3D_SET *)0);            /* 606 */
        kaza_pzl_ctrl.exit_csr ^= 1;                                    /* 608 */
    } else if (*paddat[0] == 1) {                                       /* 609 */
        if (kaza_pzl_ctrl.exit_csr == 0) {                              /* 612 */
            KazaPuzzleExitReq();                                        /* 614 */
        } else {
            KazaPuzzleReqNextSubStep(1);                                /* 615 */
        }
    } else if (*paddat[1] == 1) {                                       /* 619 */
        kaza_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 3, 0, 0,
                                           0x3200, 0x1000, 0,
                                           (SND_3D_SET *)0);            /* 623 */
        KazaPuzzleReqNextSubStep(1);                                    /* 625 */
    }
}                                                                       /* 626 */

static void KazaPuzzleExitReq(void)
{
    kaza_pzl_ctrl.step       = 2;                                       /* 639 */
    kaza_pzl_disp.anim_step  = 3;                                       /* 640 */
    kaza_pzl_disp.anim_timer = 0;                                       /* 641 */
}

/* X leaves silently; O leaves with the cancel cue. */
static void KazaPuzzleClearPad(void)                                    /* 648 */
{                                                                       /* 649 */
    if (*paddat[0] == 1) {                                              /* 653 */
        KazaPuzzleExitReq();                                            /* 654 */
    } else if (*paddat[1] == 1) {                                       /* 657 */
        kaza_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 3, 0, 0,
                                           0x3200, 0x1000, 0,
                                           (SND_3D_SET *)0);            /* 659 */
        KazaPuzzleExitReq();                                            /* 660 */
    }
}

/* ==========================================================================
 *  Display
 * ======================================================================== */

static void KazaPuzzleDispInit(void)                                    /* 672 */
{                                                                       /* 673 */
    int i;

    kaza_pzl_disp.anim_step      = 0;                                   /* 678 */
    kaza_pzl_disp.anim_timer     = 0;                                   /* 679 */
    kaza_pzl_disp.sub_anim_step  = 0;                                   /* 680 */
    kaza_pzl_disp.sub_anim_timer = 0;                                   /* 681 */
    kaza_pzl_disp.rot_anim_step  = 0;                                   /* 682 */
    kaza_pzl_disp.rot_anim_timer = 0;                                   /* 683 */

    for (i = 0; i < 5; i++) {                                           /* 685 */
        kaza_pzl_disp.panel_rot[i] = 0.0f;                              /* 687 */
    }
}

void KazaPuzzleDispMain(void)                                           /* 697 */
{                                                                       /* 698 */
    u_char fade_alpha;

    fade_alpha = 0x80;                                                  /* 703 */

    if ((u_char)(kaza_pzl_ctrl.step - 1) < 2) {                         /* 706 */
        KazaPuzzleAnim(&kaza_pzl_disp.anim_step, &kaza_pzl_disp.anim_timer,
                       &fade_alpha);                                    /* 707 */

        if (kaza_pzl_disp.anim_step != 4) {                             /* 709 */
            KazaPuzzleBgDisp(0, 0, 0x80);                               /* 711 */

            if (kaza_pzl_disp_func[kaza_pzl_ctrl.mode] != NULL) {       /* 713 */
                kaza_pzl_disp_func[kaza_pzl_ctrl.mode](0, 0, 0x80);     /* 714 */
            }
        }

        KazaPuzzleBlackBgDisp(0, 0, fade_alpha);                        /* 719 */
    }
}

/* Unlike hina's version this one has a step-4 arm that pins alpha at full,
 * which is what keeps the screen black once the curtain has finished. */
static void KazaPuzzleAnim(char *anim_step, short *anim_timer, u_char *alpha) /* 741 */
{
    static const ALPHA_ANIM_TBL kaza_out_alpha[2] =             /* rdata 3b9e58 */
    {
        {  0, 128,  0, 30 },
        { -1,  -1, -1, -1 },
    };

    if (*anim_step == 0) {                                              /* 742 */
        *anim_timer = 0;                                                /* 743 */
        *anim_step  = 1;                                                /* 744 */
    }

    if (*anim_timer < 0) {                                              /* 748 */
        PRINT_WARNING("Warning!! KazaPuzzleAnim()");                    /* 749 */
    }

    if (*anim_step == 1) {                                              /* 754 */
        *alpha     = 0;                                                 /* 756 */
        *anim_step = 2;                                                 /* 758 */
    } else if (*anim_step == 2) {
        *alpha = 0;                                                     /* 761 */
    } else if (*anim_step == 3) {                                       /* 764 */
        *alpha = Anim2D_CalcNowAlpha((ALPHA_ANIM_TBL *)kaza_out_alpha,
                                     (int)*anim_timer);                 /* 766 */
        (*anim_timer)++;                                                /* 768 */

        if (*anim_timer >= 30) {                                        /* 769 */
            *anim_step = 4;                                             /* 771 */
        }
    } else if (*anim_step == 4) {
        *alpha = 0x80;                                                  /* 773 */
    }
}                                                                       /* 776 */

/* --------------------------------------------------------------------------
 *  The five per-mode display handlers.  They all draw the same board and
 *  differ only in the window on top of it, so the board goes out at the
 *  caller's alpha and the window at the cross-fade's.
 * ------------------------------------------------------------------------ */

static void KazaPuzzleStartMsgDisp(int off_x, int off_y, u_char alpha)  /* 787 */
{                                                                       /* 788 */
    u_char mode_alpha;

    mode_alpha = alpha;                                                 /* 792 */

    if (kaza_pzl_ctrl.step == 1) {                                      /* 794 */
        mode_alpha = KazaPuzzleModeAnimCtrl();                          /* 797 */
    }

    KazaPuzzlePanelAllShadowDisp(0, 0, alpha);                          /* 802 */
    KazaPuzzlePanelAllDisp(0, 0, alpha);                                /* 805 */
    KazaPuzzlePanelAllEmbossShadowDisp(0, 0, alpha);                    /* 808 */
    KazaPuzzlePanelAllEmbossHighLightDisp(0, 0, alpha);                 /* 811 */
    KazaPuzzleScreenMask(0, 0, alpha);                                  /* 814 */
    KazaPuzzleStartMsgWinDisp(0, 0, mode_alpha);                        /* 817 */
}

static void KazaPuzzlePinWheelSelDisp(int off_x, int off_y, u_char alpha) /* 827 */
{                                                                       /* 828 */
    u_char mode_alpha;

    mode_alpha = alpha;                                                 /* 832 */

    if (kaza_pzl_ctrl.step == 1) {                                      /* 834 */
        mode_alpha = KazaPuzzleModeAnimCtrl();                          /* 837 */
    }

    KazaPuzzlePanelAllShadowDisp(0, 0, alpha);                          /* 842 */
    KazaPuzzlePanelAllDisp(0, 0, alpha);                                /* 845 */
    KazaPuzzlePanelAllEmbossShadowDisp(0, 0, alpha);                    /* 848 */
    KazaPuzzlePanelAllEmbossHighLightDisp(0, 0, alpha);                 /* 851 */
    KazaPuzzlePanelFlareDisp(0, 0, mode_alpha);                         /* 854 */
    KazaPuzzleScreenMask(0, 0, alpha);                                  /* 857 */
    KazaPuzzleCapBaseDisp(0, 0, mode_alpha);                            /* 860 */
    KazaPuzzleCapDisp(0, 0, mode_alpha);                                /* 863 */
    KazaPuzzleRemainderFrequency(0, 0, mode_alpha);                     /* 866 */
}

static u_char KazaPuzzleModeAnimCtrl(void)                              /* 874 */
{                                                                       /* 875 */
    static const ALPHA_ANIM_TBL in_alpha_tbl[2] =               /* rdata 3b9ed0 */
    {
        { 0, 128, 0, 7 },
        { -1, -1, -1, -1 },
    };
    static const ALPHA_ANIM_TBL out_alpha_tbl[2] =              /* rdata 3b9ee0 */
    {
        { 128, 0, 0, 5 },
        { -1, -1, -1, -1 },
    };
    u_char alpha;

    alpha = 0x80;                                                       /* 888 */

    if (kaza_pzl_disp.sub_anim_step == 0) {                             /* 890 */
        kaza_pzl_disp.sub_anim_timer = 0;                               /* 891 */
        kaza_pzl_disp.sub_anim_step  = 1;                               /* 892 */
    }

    if (kaza_pzl_disp.sub_anim_step == 1) {                             /* 896 */
        alpha = Anim2D_CalcNowAlpha((ALPHA_ANIM_TBL *)in_alpha_tbl,
                                    (int)kaza_pzl_disp.sub_anim_timer); /* 898 */
        kaza_pzl_disp.sub_anim_timer++;                                 /* 900 */

        if (kaza_pzl_disp.sub_anim_timer >= 7) {                        /* 902 */
            kaza_pzl_disp.sub_anim_step  = 2;                           /* 904 */
            kaza_pzl_disp.sub_anim_timer = 0;                           /* 905 */
        }                                                               /* 907 */
    } else if (kaza_pzl_disp.sub_anim_step == 2) {
        /* Settled -- unlike hina's, this arm sets nothing. */
    } else if (kaza_pzl_disp.sub_anim_step == 3) {                      /* 912 */
        alpha = Anim2D_CalcNowAlpha((ALPHA_ANIM_TBL *)out_alpha_tbl,
                                    (int)kaza_pzl_disp.sub_anim_timer); /* 914 */
        kaza_pzl_disp.sub_anim_timer++;                                 /* 916 */

        if (kaza_pzl_disp.sub_anim_timer >= 5) {                        /* 917 */
            kaza_pzl_disp.sub_anim_timer = 0;                           /* 918 */
            kaza_pzl_disp.sub_anim_step  = 4;                           /* 919 */
        }                                                               /* 921 */
    } else if (kaza_pzl_disp.sub_anim_step == 4) {
        alpha = 0;
    } else {
        PRINT_ASSERT("Error! KazaPuzzleModeAnimCtrl");                  /* 926 */
    }

    return alpha;                                                       /* 930 */
}

static void KazaPuzzleExitSelDisp(int off_x, int off_y, u_char alpha)   /* 938 */
{                                                                       /* 939 */
    u_char mode_alpha;

    mode_alpha = alpha;                                                 /* 943 */

    if (kaza_pzl_ctrl.step == 1) {                                      /* 945 */
        mode_alpha = KazaPuzzleModeAnimCtrl();                          /* 948 */
    }

    KazaPuzzlePanelAllShadowDisp(0, 0, alpha);                          /* 953 */
    KazaPuzzlePanelAllDisp(0, 0, alpha);                                /* 956 */
    KazaPuzzlePanelAllEmbossShadowDisp(0, 0, alpha);                    /* 959 */
    KazaPuzzlePanelAllEmbossHighLightDisp(0, 0, alpha);                 /* 962 */
    KazaPuzzleScreenMask(0, 0, alpha);                                  /* 965 */
    KazaPuzzleExitConfWinDisp(0, 0, mode_alpha);                        /* 968 */
}

static void KazaPuzzleClearDisp(int off_x, int off_y, u_char alpha)     /* 978 */
{                                                                       /* 979 */
    u_char mode_alpha;

    mode_alpha = alpha;                                                 /* 983 */

    if (kaza_pzl_ctrl.step == 1) {                                      /* 985 */
        mode_alpha = KazaPuzzleModeAnimCtrl();                          /* 988 */
    }

    KazaPuzzlePanelAllShadowDisp(0, 0, alpha);                          /* 993 */
    KazaPuzzlePanelAllDisp(0, 0, alpha);                                /* 996 */
    KazaPuzzlePanelAllEmbossShadowDisp(0, 0, alpha);                    /* 999 */
    KazaPuzzlePanelAllEmbossHighLightDisp(0, 0, alpha);                 /* 1002 */
    KazaPuzzleScreenMask(0, 0, alpha);                                  /* 1005 */
    KazaPuzzleClearWinDisp(0, 0, mode_alpha);                           /* 1008 */
}

static void KazaPuzzleFailureDisp(int off_x, int off_y, u_char alpha)   /* 1018 */
{                                                                       /* 1019 */
    u_char mode_alpha;

    mode_alpha = alpha;                                                 /* 1023 */

    if (kaza_pzl_ctrl.step == 1) {                                      /* 1025 */
        mode_alpha = KazaPuzzleModeAnimCtrl();                          /* 1028 */
    }

    KazaPuzzlePanelAllShadowDisp(0, 0, alpha);                          /* 1033 */
    KazaPuzzlePanelAllDisp(0, 0, alpha);                                /* 1036 */
    KazaPuzzlePanelAllEmbossShadowDisp(0, 0, alpha);                    /* 1039 */
    KazaPuzzlePanelAllEmbossHighLightDisp(0, 0, alpha);                 /* 1042 */
    KazaPuzzleScreenMask(0, 0, alpha);                                  /* 1045 */
    KazaPuzzleFailureWinDisp(0, 0, mode_alpha);                         /* 1048 */
}

/* -------------------------------------------------------------------------- */

static void KazaPuzzleBgDisp(int off_x, int off_y, u_char alpha)        /* 1058 */
{                                                                       /* 1059 */
    DISP_SPRT bg_ds;
    int       i;

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1065 */

    for (i = 0; i < 2; i++) {
        CopySprDToSpr(&bg_ds, &puzzle_kaza_tex[0x33 + i]);              /* 1068 */
        bg_ds.x     = bg_ds.x + (float)off_x;                           /* 1069 */
        bg_ds.y     = bg_ds.y + (float)off_y;                           /* 1070 */
        bg_ds.alpha = (u_char)((bg_ds.alpha * alpha) >> 7);             /* 1071 */
        DispSprD(&bg_ds);                                               /* 1072 */
    }                                                                   /* 1073 */
}

static void KazaPuzzlePanelAllShadowDisp(int off_x, int off_y, u_char alpha) /* 1083 */
{                                                                       /* 1084 */
    int i;

    for (i = 0; i < 5; i++) {                                           /* 1090 */
        KazaPuzzlePanelShadowDisp(i, off_x, off_y, alpha);              /* 1091 */
    }                                                                   /* 1092 */
}

static void KazaPuzzlePanelShadowDisp(int panel_label, int off_x, int off_y,
                                      u_char alpha)                     /* 1103 */
{                                                                       /* 1104 */
    static const int shadow_tex_tbl[5] =                        /* rdata 3b9f30 */
    {
        16, 14, 15, 12, 13,
    };
    DISP_SPRT shadow_ds;

    if (panel_label > 4) {                                              /* 1118 */
        PRINT_ASSERT("Error! KazaPuzzlePanelShadowDisp");               /* 1119 */
    }

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1124 */

    CopySprDToSpr(&shadow_ds, &puzzle_kaza_tex[shadow_tex_tbl[panel_label]]); /* 1127 */
    shadow_ds.x      = shadow_ds.x + (float)off_x;                      /* 1128 */
    shadow_ds.y      = shadow_ds.y + (float)off_y;
    shadow_ds.alphar = 0x46;                                            /* 1129 */
    shadow_ds.alpha  = (u_char)((shadow_ds.alpha * alpha) >> 7);        /* 1130 */
    DispSprD(&shadow_ds);                                               /* 1131 */
}

/* --------------------------------------------------------------------------
 *  During a turn the centre wheel and the selected wheel are drawn at their
 *  settled angle plus (or minus) however far the 20-frame sweep has got; the
 *  other three at their settled angle.
 * ------------------------------------------------------------------------ */
static void KazaPuzzlePanelAllDisp(int off_x, int off_y, u_char alpha)  /* 1141 */
{                                                                       /* 1142 */
    int   i;
    int   sel_panel;
    float anim_rot;
    float rot;

    if (kaza_pzl_ctrl.rot_anim_flg != 0) {                              /* 1150 */
        anim_rot = KazaPuzzleRotAnim();                                 /* 1151 */
    } else {
        anim_rot = 0.0f;                                                /* 1154 */
    }

    sel_panel = kaza_panel_label_tbl[kaza_pzl_ctrl.csr_tate][kaza_pzl_ctrl.csr_yoko]; /* 1157 */

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1161 */

    for (i = 0; i < 5; i++) {                                           /* 1164 */
        if (kaza_pzl_ctrl.rot_anim_flg == 0) {                          /* 1165 */
            rot = kaza_pzl_disp.panel_rot[i];                           /* 1167 */
        } else if (i == 0) {
            rot = kaza_pzl_disp.panel_rot[0] - anim_rot;                /* 1170 */

            if (rot < 0.0f) {                                           /* 1171 */
                rot = rot + 360.0f;
            }
        } else if (i == sel_panel) {                                    /* 1175 */
            rot = kaza_pzl_disp.panel_rot[i] + anim_rot;                /* 1178 */

            if (rot >= 360.0f) {                                        /* 1179 */
                rot = rot - 360.0f;
            }
        } else {
            rot = kaza_pzl_disp.panel_rot[i];
        }

        KazaPuzzlePanelDisp(i, off_x, off_y, alpha, rot);               /* 1190 */
    }                                                                   /* 1191 */
}

/* 0 to 90 degrees over 20 frames. */
static float KazaPuzzleRotAnim(void)                                    /* 1221 */
{                                                                       /* 1222 */
    static const ROT_ANIM_TBL rot_anim_tbl[2] =                 /* rdata 3b9f70 */
    {
        {  0.0f, 90.0f,  0, 20 },
        { -1.0f, -1.0f, -1, -1 },
    };
    float rot;

    rot = 0.0f;                                                         /* 1232 */

    if (kaza_pzl_disp.rot_anim_step == 0) {                             /* 1235 */
        kaza_pzl_disp.rot_anim_timer = 0;                               /* 1236 */
        kaza_pzl_disp.rot_anim_step  = 2;                               /* 1238 */
    }

    if (kaza_pzl_disp.rot_anim_step == 2) {                             /* 1241 */
        rot = Anim2D_CalcNowRot((ROT_ANIM_TBL *)rot_anim_tbl,
                                (int)kaza_pzl_disp.rot_anim_timer);     /* 1242 */
    }

    return rot;                                                         /* 1246 */
}

static void KazaPuzzlePanelDisp(int panel_label, int off_x, int off_y,
                                u_char alpha, float rot)                /* 1256 */
{                                                                       /* 1257 */
    static const int panel_tex_tbl[5] =                         /* rdata 3b9fa0 */
    {
        22, 23, 24, 25, 26,
    };
    DISP_SPRT panel_ds;

    if (panel_label > 4) {                                              /* 1270 */
        PRINT_ASSERT("Error! KazaPuzzlePanelDisp");                     /* 1271 */
    }

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1276 */

    CopySprDToSpr(&panel_ds, &puzzle_kaza_tex[panel_tex_tbl[panel_label]]); /* 1279 */
    panel_ds.x     = panel_ds.x + (float)off_x;                         /* 1280 */
    panel_ds.y     = panel_ds.y + (float)off_y;
    panel_ds.alpha = (u_char)((panel_ds.alpha * alpha) >> 7);           /* 1281 */
    /* Rotation is about the sprite's own centre, not the screen's. */
    panel_ds.crx   = panel_ds.x + (float)panel_ds.w * 0.5f;             /* 1282 */
    panel_ds.cry   = panel_ds.y + (float)panel_ds.h * 0.5f;
    panel_ds.rot   = rot;
    DispSprD(&panel_ds);                                                /* 1283 */
}

static void KazaPuzzlePanelAllEmbossShadowDisp(int off_x, int off_y, u_char alpha) /* 1293 */
{                                                                       /* 1294 */
    int i;

    for (i = 0; i < 5; i++) {                                           /* 1300 */
        KazaPuzzlePanelEmbossShadowDisp(i, off_x, off_y, alpha);        /* 1301 */
    }                                                                   /* 1302 */
}

/* The emboss pair does not rotate -- it is the frame the wheel turns inside. */
static void KazaPuzzlePanelEmbossShadowDisp(int panel_label, int off_x, int off_y,
                                            u_char alpha)               /* 1313 */
{                                                                       /* 1314 */
    static const int emboss_tbl[5] =                            /* rdata 3b9ff8 */
    {
        11, 9, 10, 7, 8,
    };
    DISP_SPRT emboss_ds;

    if (panel_label > 4) {                                              /* 1328 */
        PRINT_ASSERT("Error! KazaPuzzlePanelEmbossShadowDisp");         /* 1329 */
    }

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1334 */

    CopySprDToSpr(&emboss_ds, &puzzle_kaza_tex[emboss_tbl[panel_label]]); /* 1337 */
    emboss_ds.x     = emboss_ds.x + (float)off_x;                       /* 1338 */
    emboss_ds.y     = emboss_ds.y + (float)off_y;
    emboss_ds.alpha = (u_char)((emboss_ds.alpha * alpha) >> 7);         /* 1339 */
    DispSprD(&emboss_ds);                                               /* 1340 */
}

static void KazaPuzzlePanelAllEmbossHighLightDisp(int off_x, int off_y, u_char alpha) /* 1350 */
{                                                                       /* 1351 */
    int i;

    for (i = 0; i < 5; i++) {                                           /* 1357 */
        KazaPuzzlePanelEmbossHighLightDisp(i, off_x, off_y, alpha);     /* 1358 */
    }                                                                   /* 1359 */
}

/* The only one of the four per-panel draws with no range assert. */
static void KazaPuzzlePanelEmbossHighLightDisp(int panel_label, int off_x, int off_y,
                                               u_char alpha)            /* 1370 */
{
    static const int emboss_tbl[5] =                            /* rdata 3ba038 */
    {
        6, 4, 5, 2, 3,
    };
    DISP_SPRT emboss_ds;

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1384 */

    CopySprDToSpr(&emboss_ds, &puzzle_kaza_tex[emboss_tbl[panel_label]]); /* 1387 */
    emboss_ds.x     = emboss_ds.x + (float)off_x;                       /* 1388 */
    emboss_ds.y     = emboss_ds.y + (float)off_y;
    emboss_ds.alpha = (u_char)((emboss_ds.alpha * alpha) >> 7);         /* 1389 */
    DispSprD(&emboss_ds);                                               /* 1390 */
}

/* The glow around the selected wheel; one sprite per cursor cell. */
static void KazaPuzzlePanelFlareDisp(int off_x, int off_y, u_char alpha) /* 1411 */
{                                                                       /* 1412 */
    static const int flare_tex_tbl[2][2] =                      /* rdata 3ba050 */
    {
        { 17, 19 },
        { 18, 20 },
    };
    DISP_SPRT flare_ds;
    u_char    rgb;

    rgb = KazaPuzzleFlareAnim();                                        /* 1415 */

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1418 */

    CopySprDToSpr(&flare_ds,
                  &puzzle_kaza_tex[flare_tex_tbl[kaza_pzl_ctrl.csr_tate]
                                                [kaza_pzl_ctrl.csr_yoko]]); /* 1419 */
    flare_ds.x      = flare_ds.x + (float)off_x;                        /* 1420 */
    flare_ds.y      = flare_ds.y + (float)off_y;
    flare_ds.alphar = 0x48;                                             /* 1421 */
    flare_ds.alpha  = (u_char)((flare_ds.alpha * alpha) >> 7);          /* 1422 */
    flare_ds.r      = rgb;
    flare_ds.g      = rgb;
    flare_ds.b      = rgb;
    DispSprD(&flare_ds);                                                /* 1423 */
}

static u_char KazaPuzzleFlareAnim(void)                                 /* 1431 */
{                                                                       /* 1432 */
    static const RGB_ANIM_TBL flare_rgb_tbl[3] =                /* rdata 3ba060 */
    {
        {  64, 115,  0, 30 },
        { 115,  64, 30, 60 },
        {  -1,  -1, -1, -1 },
    };
    u_char rgb;

    rgb = Anim2D_CalcNowRGB((RGB_ANIM_TBL *)flare_rgb_tbl,
                            (int)kaza_pzl_disp.flare_anim_timer);       /* 1444 */

    kaza_pzl_disp.flare_anim_timer++;                                   /* 1446 */

    if (kaza_pzl_disp.flare_anim_timer > 59) {                          /* 1448 */
        kaza_pzl_disp.flare_anim_timer = 0;                             /* 1449 */
    }

    return rgb;                                                         /* 1453 */
}

static void KazaPuzzleCapBaseDisp(int off_x, int off_y, u_char alpha)   /* 1466 */
{                                                                       /* 1467 */
    DISP_SPRT base_ds;

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1469 */

    CopySprDToSpr(&base_ds, &puzzle_kaza_tex[0x1c]);                    /* 1470 */
    base_ds.x      = base_ds.x + (float)off_x;                          /* 1471 */
    base_ds.y      = base_ds.y + (float)off_y;
    base_ds.alphar = 0x46;
    base_ds.alpha  = (u_char)((base_ds.alpha * alpha) >> 7);            /* 1472 */
    DispSprD(&base_ds);                                                 /* 1473 */
}

static void KazaPuzzleCapDisp(int off_x, int off_y, u_char alpha)       /* 1488 */
{                                                                       /* 1489 */
    DISP_SPRT base_ds;

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1491 */

    CopySprDToSpr(&base_ds, &puzzle_kaza_tex[0x1b]);                    /* 1492 */
    base_ds.x     = base_ds.x + (float)off_x;                           /* 1493 */
    base_ds.y     = base_ds.y + (float)off_y;
    base_ds.alpha = (u_char)((base_ds.alpha * alpha) >> 7);
    DispSprD(&base_ds);                                                 /* 1494 */
}

/* --------------------------------------------------------------------------
 *  "N turns left".  Spanish (language 3) needs a wider word, so it gets its
 *  own digit set and its own plate; the -1 guard is dead as written -- neither
 *  table holds one -- but it is what the ROM checks.
 * ------------------------------------------------------------------------ */
static void KazaPuzzleRemainderFrequency(int off_x, int off_y, u_char alpha) /* 1504 */
{                                                                       /* 1505 */
    static const int num_tex_tbl[10] =                          /* rdata 3ba098 */
    {
        30, 30, 31, 32, 33, 34, 35, 36, 37, 38,
    };
    static const int num_tex_tbl_s[10] =                        /* rdata 3ba0c0 */
    {
        42, 42, 43, 44, 45, 46, 47, 48, 49, 50,
    };
    DISP_SPRT ds;

    if (kaza_pzl_ctrl.remainder_frequency > 9) {                        /* 1535 */
        PRINT_ASSERT("Error! KazaPuzzleRemainderFrequency");            /* 1536 */
    }

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1540 */

    CopySprDToSpr(&ds, &puzzle_kaza_tex[0x27]);                         /* 1543 */
    ds.x      = ds.x + (float)off_x;                                    /* 1544 */
    ds.y      = ds.y + (float)off_y;                                    /* 1545 */
    ds.alphar = 0x46;                                                   /* 1546 */
    ds.alpha  = (u_char)((ds.alpha * alpha) >> 7);
    DispSprD(&ds);                                                      /* 1547 */

    if (GetLanguage() == 3) {                                           /* 1551 */
        if (num_tex_tbl_s[kaza_pzl_ctrl.remainder_frequency] != -1) {   /* 1552 */
            CopySprDToSpr(&ds,
                          &puzzle_kaza_tex[num_tex_tbl_s[kaza_pzl_ctrl.remainder_frequency]]); /* 1553 */
            ds.x     = ds.x + (float)off_x;                             /* 1554 */
            ds.y     = ds.y + (float)off_y;
            ds.alpha = (u_char)((ds.alpha * alpha) >> 7);               /* 1555 */
            DispSprD(&ds);                                              /* 1556 */
        }
    } else {
        if (num_tex_tbl[kaza_pzl_ctrl.remainder_frequency] != -1) {     /* 1560 */
            CopySprDToSpr(&ds,
                          &puzzle_kaza_tex[num_tex_tbl[kaza_pzl_ctrl.remainder_frequency]]); /* 1561 */
            ds.x     = ds.x + (float)off_x;                             /* 1562 */
            ds.y     = ds.y + (float)off_y;
            ds.alpha = (u_char)((ds.alpha * alpha) >> 7);               /* 1563 */
            DispSprD(&ds);                                              /* 1564 */
        }
    }

    if (GetLanguage() == 3) {                                           /* 1570 */
        CopySprDToSpr(&ds, &puzzle_kaza_tex[0x29]);                     /* 1571 */
    } else {
        CopySprDToSpr(&ds, &puzzle_kaza_tex[0x1d]);                     /* 1574 */
    }

    ds.x     = ds.x + (float)off_x;                                     /* 1576 */
    ds.y     = ds.y + (float)off_y;
    ds.alpha = (u_char)((ds.alpha * alpha) >> 7);                       /* 1577 */
    DispSprD(&ds);                                                      /* 1578 */
}

/* One sprite stretched to the whole screen -- the vignette over the board. */
static void KazaPuzzleScreenMask(int off_x, int off_y, u_char alpha)    /* 1593 */
{                                                                       /* 1594 */
    DISP_SPRT ds;

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1597 */

    CopySprDToSpr(&ds, &puzzle_kaza_tex[0x28]);                         /* 1598 */
    ds.x      = ds.x + (float)off_x;                                    /* 1599 */
    ds.y      = ds.y + (float)off_y;
    ds.csx    = ds.x;
    ds.csy    = ds.y;
    ds.scw    = 640.0f / (float)ds.w;                                   /* 1600 */
    ds.sch    = 448.0f / (float)ds.h;
    ds.alphar = 0x46;
    ds.alpha  = (u_char)((ds.alpha * alpha) >> 7);                      /* 1601 */
    DispSprD(&ds);                                                      /* 1602 */
}

/* -------------------------------------------------------------------------- */

static void KazaPuzzleCmnWinDisp(int off_x, int off_y, u_char alpha)    /* 1611 */
{                                                                       /* 1612 */
    MSG_WIN_DAT win_dat;

    SetMsgWinDefData(&win_dat, 0x45);                                   /* 1615 */
    DrawCmnWindow(0, win_dat.x, win_dat.y, win_dat.w, win_dat.h, alpha, 0x80); /* 1618 */
}

static void KazaPuzzleStartMsgWinDisp(int off_x, int off_y, u_char alpha) /* 1628 */
{                                                                       /* 1629 */
    DISP_STR ds;

    SetMsgDefData(&ds, 0x45);                                           /* 1633 */
    KazaPuzzleCmnWinDisp(off_x, off_y, alpha);                          /* 1636 */
    PrintMsg(0x45, 9, ds.pos_x, ds.pos_y, 1, (int)alpha, 0);            /* 1638 */
}

static void KazaPuzzleExitConfWinDisp(int off_x, int off_y, u_char alpha) /* 1648 */
{                                                                       /* 1649 */
    DISP_STR ds;

    SetMsgDefData(&ds, 0x45);                                           /* 1653 */
    KazaPuzzleCmnWinDisp(off_x, off_y, alpha);                          /* 1656 */

    DrawCmnSelCsr(0, (float)(kaza_pzl_ctrl.exit_csr * 0xcf + off_x + 0x9b),
                  (float)(off_y + 0x184), alpha, 0.0f, 0);              /* 1660 */
    DrawCmnSelYes(0, (float)(off_x + 0x99), (float)(off_y + 0x186), alpha); /* 1663 */
    DrawCmnSelNo(0, (float)(off_x + 0x169), (float)(off_y + 0x186), alpha); /* 1664 */

    PrintMsg(0x45, 4, ds.pos_x, ds.pos_y, 1, (int)alpha, 0);            /* 1666 */
}

static void KazaPuzzleClearWinDisp(int off_x, int off_y, u_char alpha)  /* 1676 */
{                                                                       /* 1677 */
    DISP_STR ds;

    SetMsgDefData(&ds, 0x45);                                           /* 1681 */
    KazaPuzzleCmnWinDisp(off_x, off_y, alpha);                          /* 1684 */
    PrintMsg(0x45, 8, ds.pos_x, ds.pos_y, 1, (int)alpha, 0);            /* 1686 */
}

static void KazaPuzzleFailureWinDisp(int off_x, int off_y, u_char alpha) /* 1696 */
{                                                                       /* 1697 */
    DISP_STR ds;

    SetMsgDefData(&ds, 0x45);                                           /* 1701 */
    KazaPuzzleCmnWinDisp(off_x, off_y, alpha);                          /* 1704 */
    PrintMsg(0x45, 7, ds.pos_x, ds.pos_y, 1, (int)alpha, 0);            /* 1706 */
}

/* ==========================================================================
 *  The cross-fade in front of the puzzle phase
 *
 *  Every wheel at zero rotation, since kaza_pzl_disp does not exist yet.
 * ======================================================================== */

void KazaPuzzleCrossScreenDisp(int off_x, int off_y, u_char alpha)      /* 1716 */
{                                                                       /* 1717 */
    int i;

    KazaPuzzleBgDisp(0, 0, 0x80);                                       /* 1723 */
    KazaPuzzlePanelAllShadowDisp(0, 0, 0x80);                           /* 1726 */

    for (i = 0; i < 5; i++) {                                           /* 1730 */
        KazaPuzzlePanelDisp(i, 0, 0, 0x80, 0.0f);                       /* 1731 */
    }

    KazaPuzzlePanelAllEmbossShadowDisp(0, 0, 0x80);                     /* 1734 */
    KazaPuzzlePanelAllEmbossHighLightDisp(0, 0, 0x80);                  /* 1737 */
    KazaPuzzleScreenMask(0, 0, 0x80);                                   /* 1740 */
    KazaPuzzleBlackBgDisp(0, 0, alpha);                                 /* 1743 */
}

static void KazaPuzzleBlackBgDisp(int off_x, int off_y, u_char alpha)   /* 1753 */
{                                                                       /* 1754 */
    SQAR_DAT  black_bg = { 640, 448, 0, 0, 0, 0, 0, 0, 128 };           /* 1756 */
    DISP_SQAR dsq;

    CopySqrDToSqr(&dsq, &black_bg);                                     /* 1760 */
    dsq.alpha = (u_char)((dsq.alpha * alpha) >> 7);                     /* 1761 */
    DispSqrD(&dsq);                                                     /* 1762 */
}
