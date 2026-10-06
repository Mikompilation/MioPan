// FILE: /home/zero_rom/zero2np/src/ingame/puzzle/kaza2/kaza2_pzl.c
//
// The kazaguruma (pinwheel) puzzle, second board.
//
// The same machine as kaza_pzl.o -- see that file for how the counter-rotating
// wheels and the colour rings work -- with four differences:
//
//   * the four outer wheels are items the player has to be carrying.
//     KazaPuzzle2CondCheck() walks panel_item_tbl[] and, if any is missing,
//     opens the board in mode 5, which prints a "you need the pinwheels"
//     message and quits.  It is also what decides whether the cross-fade draws
//     one wheel or five.
//   * the wheels start turned: rot_num[] and panel_rot[] are seeded rather
//     than zeroed, so the board opens mid-puzzle.
//   * six turns instead of four.
//   * kaza2_panel_right_down's ring differs from board 1's, which is the only
//     thing that makes the answer different.
//
// Its statics carry the same names as kaza_pzl.o's -- kaza_pzl_ctrl,
// kaza_pzl_pad_func and so on -- because both files are copies of one
// original; they are separate objects, one per translation unit.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), kaza2_pzl.o.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.  Statements whose only memory access goes through
// fixed_array's inlined operator[] leave no $LM of their own -- their line is
// swallowed by fixed_array.h's 124/125 -- so a few are interpolated into a
// measured gap rather than read out of it.

#include "kaza2_pzl.h"

#include <stddef.h>                                 /* NULL                   */
#include <stdio.h>                                  /* printf                 */

#include "../kaza/puzzle_kaza_dat.h"                /* kaza2_panel_center     */
#include "../puzzle.h"                              /* GetPzlTexDataAddr      */

#include "../../../common/utility2.h"               /* PRINT_ASSERT           */
#include "../../../common/variable.h"               /* pad                    */
#include "../../../main/gphase.h"                   /* SetNextGPhase          */

#include "../../item/prg/item.h"                    /* GetPlyrItemHaveNum     */
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

static void  KazaPuzzle2CtrlInit(void);
static int   KazaPuzzle2CondCheck(void);
static void  KazaPuzzle2Mode(void);
static void  KazaPuzzle2RotAnimExe(void);
static void  KazaPuzzle2ReqNextSubStep(char next_step);
static void  KazaPuzzle2SubOutAnimCheck(void);
static int   KazaPuzzle2ClearCheck(void);
static int   GetKazaPuzzle2PinWheelWingColor(int pinwheel_label, int wing_label);
static void  KazaPuzzle2StartMsgPad(void);
static void  KazaPuzzle2PinWheelSelPad(void);
static void  KazaPuzzle2RotAnimStart(void);
static void  KazaPuzzle2ExitPad(void);
static void  KazaPuzzle2ExitReq(void);
static void  KazaPuzzle2ClearPad(void);
static void  KazaPuzzle2CondErrorPad(void);
static void  KazaPuzzle2DispInit(void);
static void  KazaPuzzle2Anim(char *anim_step, short *anim_timer, u_char *alpha);
static void  KazaPuzzle2StartMsgDisp(int off_x, int off_y, u_char alpha);
static void  KazaPuzzle2PinWheelSelDisp(int off_x, int off_y, u_char alpha);
static u_char KazaPuzzle2ModeAnimCtrl(void);
static void  KazaPuzzle2ExitSelDisp(int off_x, int off_y, u_char alpha);
static void  KazaPuzzle2ClearDisp(int off_x, int off_y, u_char alpha);
static void  KazaPuzzle2FailureDisp(int off_x, int off_y, u_char alpha);
static void  KazaPuzzle2CondErrorDisp(int off_x, int off_y, u_char alpha);
static void  KazaPuzzle2BgDisp(int off_x, int off_y, u_char alpha);
static void  KazaPuzzle2PanelAllShadowDisp(int off_x, int off_y, u_char alpha);
static void  KazaPuzzle2PanelShadowDisp(int panel_label, int off_x, int off_y, u_char alpha);
static void  KazaPuzzle2PanelAllDisp(int off_x, int off_y, u_char alpha);
static float KazaPuzzle2RotAnim(void);
static void  KazaPuzzle2PanelDisp(int panel_label, int off_x, int off_y, u_char alpha, float rot);
static void  KazaPuzzle2PanelAllEmbossShadowDisp(int off_x, int off_y, u_char alpha);
static void  KazaPuzzle2PanelEmbossShadowDisp(int panel_label, int off_x, int off_y, u_char alpha);
static void  KazaPuzzle2PanelAllEmbossHighLightDisp(int off_x, int off_y, u_char alpha);
static void  KazaPuzzle2PanelEmbossHighLightDisp(int panel_label, int off_x, int off_y, u_char alpha);
static void  KazaPuzzle2PanelFlareDisp(int off_x, int off_y, u_char alpha);
static u_char KazaPuzzle2FlareAnim(void);
static void  KazaPuzzle2CapBaseDisp(int off_x, int off_y, u_char alpha);
static void  KazaPuzzle2CapDisp(int off_x, int off_y, u_char alpha);
static void  KazaPuzzle2RemainderFrequency(int off_x, int off_y, u_char alpha);
static void  KazaPuzzle2ScreenMask(int off_x, int off_y, u_char alpha);
static void  KazaPuzzle2CmnWinDisp(int off_x, int off_y, u_char alpha);
static void  KazaPuzzle2StartMsgWinDisp(int off_x, int off_y, u_char alpha);
static void  KazaPuzzle2ExitConfWinDisp(int off_x, int off_y, u_char alpha);
static void  KazaPuzzle2ClearWinDisp(int off_x, int off_y, u_char alpha);
static void  KazaPuzzle2FailureWinDisp(int off_x, int off_y, u_char alpha);
static void  KazaPuzzle2CondErrorWinDisp(int off_x, int off_y, u_char alpha);
static void  KazaPuzzle2BlackBgDisp(int off_x, int off_y, u_char alpha);

/* Mode 5 -- the missing-pinwheels message -- is what board 1 does not have. */
static void (*kaza_pzl_pad_func[6])(void) =                 /* data 3193b0 */
{
    KazaPuzzle2StartMsgPad,
    KazaPuzzle2PinWheelSelPad,
    KazaPuzzle2ExitPad,
    KazaPuzzle2ClearPad,
    KazaPuzzle2ClearPad,
    KazaPuzzle2CondErrorPad,
};

static void (*kaza_pzl_disp_func[6])(int, int, u_char) =    /* data 3193c8 */
{
    KazaPuzzle2StartMsgDisp,
    KazaPuzzle2PinWheelSelDisp,
    KazaPuzzle2ExitSelDisp,
    KazaPuzzle2ClearDisp,
    KazaPuzzle2FailureDisp,
    KazaPuzzle2CondErrorDisp,
};

static KAZA_PZL_CTRL kaza_pzl_ctrl;                         /* bss 4b4168 */
static KAZA_PZL_DISP kaza_pzl_disp;                         /* bss 4b4190 */

static int *kaza2_panel_color[5] =                          /* bss 4b41b0 */
{
    &kaza2_panel_center[0],
    &kaza2_panel_right_up[0],
    &kaza2_panel_right_down[0],
    &kaza2_panel_left_up[0],
    &kaza2_panel_left_down[0],
};

static const int kaza2_panel_label_tbl[2][2] =              /* rdata 3b9d50 */
{
    { 3, 1 },       /* up:   left, right */
    { 4, 2 },       /* down: left, right */
};

/* ==========================================================================
 *  Set-up
 * ======================================================================== */

void KazaPuzzle2ExeInit(void)                                           /* 251 */
{
    KazaPuzzle2CtrlInit();                                              /* 252 */
    KazaPuzzle2DispInit();                                              /* 255 */
}

static void KazaPuzzle2CtrlInit(void)                                   /* 263 */
{                                                                       /* 264 */
    kaza_pzl_ctrl.step = 0;                                             /* 267 */

    /* Missing a pinwheel opens the board straight into the refusal message. */
    if (KazaPuzzle2CondCheck() == 0) {                                  /* 270 */
        kaza_pzl_ctrl.mode      = 5;                                    /* 272 */
        kaza_pzl_ctrl.next_mode = 5;
    } else {
        kaza_pzl_ctrl.mode      = 0;                                    /* 276 */
        kaza_pzl_ctrl.next_mode = 0;                                    /* 277 */
    }

    kaza_pzl_ctrl.clear_flg    = 0;                                     /* 280 */
    kaza_pzl_ctrl.csr_yoko     = 0;                                     /* 281 */
    kaza_pzl_ctrl.csr_tate     = 0;                                     /* 282 */
    kaza_pzl_ctrl.exit_csr     = 1;                                     /* 283 */
    kaza_pzl_ctrl.rot_anim_flg = 0;

    /* Six turns here, four on board 1. */
    kaza_pzl_ctrl.remainder_frequency = 6;                              /* 285 */

    /* The board opens part-solved. */
    kaza_pzl_ctrl.rot_num[0] = 2;
    kaza_pzl_ctrl.rot_num[1] = 3;
    kaza_pzl_ctrl.rot_num[2] = 0;
    kaza_pzl_ctrl.rot_num[3] = 0;
    kaza_pzl_ctrl.rot_num[4] = 3;
}

/* Non-zero once the player is carrying all four outer pinwheels.  Entry 0 is
 * -1 -- the centre wheel is part of the room, not an item. */
static int KazaPuzzle2CondCheck(void)                                   /* 300 */
{                                                                       /* 301 */
    static const int panel_item_tbl[5] =                        /* rdata 3b99c0 */
    {
        -1, 49, 47, 46, 48,
    };
    int i;

    for (i = 0; i < 5; i++) {                                           /* 317 */
        if (panel_item_tbl[i] == -1) {                                  /* 318 */
            continue;
        }
        if (GetPlyrItemHaveNum(panel_item_tbl[i]) <= 0) {               /* 320 */
            break;
        }
    }                                                                   /* 327 */

    return (i >= 5);                                                    /* 338 */
}

/* ==========================================================================
 *  Per-frame control
 * ======================================================================== */

int KazaPuzzle2Main(void)                                               /* 349 */
{                                                                       /* 350 */
    int res;

    res = 1;                                                            /* 354 */

    if (kaza_pzl_ctrl.step == 0) {                                      /* 357 */
        KazaPuzzle2DispInit();                                          /* 358 */
        kaza_pzl_ctrl.step = 1;                                         /* 360 */
    }

    if (kaza_pzl_ctrl.step == 1) {                                      /* 363 */
        KazaPuzzle2Mode();                                              /* 364 */
    }

    if ((kaza_pzl_ctrl.step == 2) &&                                    /* 367 */
        (kaza_pzl_disp.anim_step == 4) &&                               /* 368 */
        (SndBufIsPlaying(kaza_pzl_ctrl.snd_id) == 0)) {                 /* 370 */
        SndBankRelease(GetPzlSndBankID());                              /* 372 */
        FinderBankSetup();                                              /* 376 */
        kaza_pzl_ctrl.step = 3;                                         /* 377 */
    }

    if ((kaza_pzl_ctrl.step == 3) &&                                    /* 382 */
        (FinderBankIsReady() != 0)) {                                   /* 383 */
        SetNextGPhase(GID_STORY_NORMAL);                                /* 384 */
    }

    return res;                                                         /* 390 */
}

static void KazaPuzzle2Mode(void)                                       /* 399 */
{                                                                       /* 400 */
    KazaPuzzle2SubOutAnimCheck();                                       /* 403 */

    if (kaza_pzl_disp.sub_anim_step == 2) {                             /* 405 */
        if (kaza_pzl_ctrl.rot_anim_flg != 0) {                          /* 407 */
            KazaPuzzle2RotAnimExe();                                    /* 408 */
        } else if (kaza_pzl_pad_func[kaza_pzl_ctrl.mode] != NULL) {
            kaza_pzl_pad_func[kaza_pzl_ctrl.mode]();                    /* 413 */
        }
    }
}

static void KazaPuzzle2RotAnimExe(void)                                 /* 422 */
{                                                                       /* 423 */
    int sel_panel;

    sel_panel = kaza2_panel_label_tbl[kaza_pzl_ctrl.csr_tate][kaza_pzl_ctrl.csr_yoko]; /* 427 */

    if (kaza_pzl_ctrl.rot_anim_flg != 0) {                              /* 431 */
        kaza_pzl_disp.rot_anim_timer++;                                 /* 433 */

        if (kaza_pzl_disp.rot_anim_timer > 19) {                        /* 436 */
            kaza_pzl_ctrl.rot_anim_flg   = 0;                           /* 437 */
            kaza_pzl_disp.rot_anim_step  = 0;                           /* 438 */
            kaza_pzl_disp.rot_anim_timer = 0;                           /* 439 */

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

            kaza_pzl_ctrl.remainder_frequency--;                        /* 454 */

            if (KazaPuzzle2ClearCheck() != 0) {                         /* 457 */
                kaza_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 2, 0, 0,
                                                   0x3200, 0x1000, 0,
                                                   (SND_3D_SET *)0);    /* 459 */
                KazaPuzzle2ReqNextSubStep(3);                           /* 460 */
                PuzzleClear(PZL_ID_KAZA2);                              /* 463 */
            } else if (kaza_pzl_ctrl.remainder_frequency < 1) {         /* 467 */
                KazaPuzzle2ReqNextSubStep(4);                           /* 468 */
            }
        }
    }
}

static void KazaPuzzle2ReqNextSubStep(char next_step)
{
    kaza_pzl_ctrl.next_mode      = next_step;                           /* 484 */
    kaza_pzl_disp.sub_anim_step  = 3;                                   /* 485 */
    kaza_pzl_disp.sub_anim_timer = 0;                                   /* 486 */
}

static void KazaPuzzle2SubOutAnimCheck(void)
{
    if (kaza_pzl_disp.sub_anim_step == 4) {                             /* 497 */
        kaza_pzl_disp.sub_anim_step = 0;                                /* 498 */
        kaza_pzl_ctrl.mode = kaza_pzl_ctrl.next_mode;                   /* 499 */
    }
}

static int KazaPuzzle2ClearCheck(void)                                  /* 510 */
{                                                                       /* 511 */
    int res;

    res = 0;

    if ((GetKazaPuzzle2PinWheelWingColor(0, 0) ==
         GetKazaPuzzle2PinWheelWingColor(3, 2)) &&
        (GetKazaPuzzle2PinWheelWingColor(0, 1) ==
         GetKazaPuzzle2PinWheelWingColor(1, 3)) &&
        (GetKazaPuzzle2PinWheelWingColor(0, 3) ==
         GetKazaPuzzle2PinWheelWingColor(4, 1)) &&
        (GetKazaPuzzle2PinWheelWingColor(0, 2) ==
         GetKazaPuzzle2PinWheelWingColor(2, 0))) {                      /* 518 */
        res = 1;                                                        /* 527 */
        /* The board-1 message, verbatim. */
        printf("Kaza Puzzle Clear!!\n");
    }

    return res;                                                         /* 532 */
}

static int GetKazaPuzzle2PinWheelWingColor(int pinwheel_label, int wing_label) /* 540 */
{                                                                       /* 541 */
    int now_pos;

    if (pinwheel_label > 4) {                                           /* 548 */
        PRINT_ASSERT("Error! GetKazaPuzzle2PinWheelWingColor");          /* 549 */
    }
    if (wing_label > 3) {                                               /* 551 */
        PRINT_ASSERT("Error! GetKazaPuzzle2PinWheelWingColor");          /* 552 */
    }

    if (pinwheel_label == 0) {                                          /* 558 */
        now_pos = (wing_label + kaza_pzl_ctrl.rot_num[0]) % 4;
    } else {
        now_pos = (wing_label - kaza_pzl_ctrl.rot_num[pinwheel_label] + 4) % 4; /* 567 */
    }

    return kaza2_panel_color[pinwheel_label][now_pos];                  /* 570 */
}

/* ==========================================================================
 *  Pad handlers, one per mode
 * ======================================================================== */

static void KazaPuzzle2StartMsgPad(void)                                /* 575 */
{                                                                       /* 576 */
    if (*paddat[0] == 1) {                                              /* 580 */
        KazaPuzzle2ReqNextSubStep(1);                                   /* 581 */
    } else if (*paddat[1] == 1) {                                       /* 584 */
        KazaPuzzle2ReqNextSubStep(1);                                   /* 585 */
    }
}

static void KazaPuzzle2PinWheelSelPad(void)                             /* 594 */
{                                                                       /* 595 */
    if (((pad[0].rpt & 0x8000) != 0) || (GetPadAnalogRpt(2) != 0) ||    /* 599 */
        ((pad[0].rpt & 0x2000) != 0) || (GetPadAnalogRpt(3) != 0)) {
        kaza_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 0, 0, 0,
                                           0x3200, 0x1000, 0,
                                           (SND_3D_SET *)0);            /* 605 */
        kaza_pzl_ctrl.csr_yoko ^= 1;                                    /* 607 */
    } else if (((pad[0].rpt & 0x1000) != 0) || (GetPadAnalogRpt(0) != 0) || /* 608 */
               ((pad[0].rpt & 0x4000) != 0) || (GetPadAnalogRpt(1) != 0)) { /* 611 */
        kaza_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 0, 0, 0,
                                           0x3200, 0x1000, 0,
                                           (SND_3D_SET *)0);            /* 617 */
        kaza_pzl_ctrl.csr_tate ^= 1;                                    /* 619 */
    } else if (*paddat[0] == 1) {                                       /* 620 */
        kaza_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 1, 0, 0,
                                           0x3200, 0x1000, 0,
                                           (SND_3D_SET *)0);            /* 623 */
        KazaPuzzle2RotAnimStart();                                      /* 625 */
    } else if (*paddat[1] == 1) {                                       /* 626 */
        kaza_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 3, 0, 0,
                                           0x3200, 0x1000, 0,
                                           (SND_3D_SET *)0);            /* 629 */
        KazaPuzzle2ReqNextSubStep(2);                                   /* 631 */
    }
}                                                                       /* 632 */

static void KazaPuzzle2RotAnimStart(void)                               /* 644 */
{
    kaza_pzl_ctrl.rot_anim_flg   = 1;                                   /* 645 */
    kaza_pzl_disp.rot_anim_timer = 0;                                   /* 647 */
    kaza_pzl_disp.rot_anim_step  = 0;                                   /* 648 */
}

static void KazaPuzzle2ExitPad(void)                                    /* 655 */
{                                                                       /* 656 */
    if (((pad[0].rpt & 0x8000) != 0) || (GetPadAnalogRpt(2) != 0) ||    /* 660 */
        ((pad[0].rpt & 0x2000) != 0) || (GetPadAnalogRpt(3) != 0)) {
        kaza_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 0, 0, 0,
                                           0x3200, 0x1000, 0,
                                           (SND_3D_SET *)0);            /* 666 */
        kaza_pzl_ctrl.exit_csr ^= 1;                                    /* 668 */
    } else if (*paddat[0] == 1) {                                       /* 669 */
        if (kaza_pzl_ctrl.exit_csr == 0) {                              /* 672 */
            KazaPuzzle2ExitReq();                                       /* 674 */
        } else {
            KazaPuzzle2ReqNextSubStep(1);                               /* 675 */
        }
    } else if (*paddat[1] == 1) {                                       /* 679 */
        kaza_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 3, 0, 0,
                                           0x3200, 0x1000, 0,
                                           (SND_3D_SET *)0);            /* 683 */
        KazaPuzzle2ReqNextSubStep(1);                                   /* 685 */
    }
}                                                                       /* 686 */

static void KazaPuzzle2ExitReq(void)
{
    kaza_pzl_ctrl.step       = 2;                                       /* 699 */
    kaza_pzl_disp.anim_step  = 3;                                       /* 700 */
    kaza_pzl_disp.anim_timer = 0;                                       /* 701 */
}

static void KazaPuzzle2ClearPad(void)                                   /* 708 */
{                                                                       /* 709 */
    if (*paddat[0] == 1) {                                              /* 713 */
        KazaPuzzle2ExitReq();                                           /* 714 */
    } else if (*paddat[1] == 1) {                                       /* 717 */
        kaza_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 3, 0, 0,
                                           0x3200, 0x1000, 0,
                                           (SND_3D_SET *)0);            /* 719 */
        KazaPuzzle2ExitReq();                                           /* 720 */
    }
}

/* --------------------------------------------------------------------------
 *  The refusal message.  Both buttons page it forward, and running off the
 *  end -- either because the message system finished or because the page
 *  counter passed the last page -- rewinds to the last page and quits, so the
 *  final page stays on screen through the fade.
 * ------------------------------------------------------------------------ */
static void KazaPuzzle2CondErrorPad(void)                               /* 733 */
{                                                                       /* 734 */
    int msg_state;

    msg_state = MesStatusCheck();                                       /* 738 */

    if (msg_state == 0) {                                               /* 739 */
        SetMsgPage((char)(GetMsgPageNum(0x45, 0) - 1));                 /* 740 */
        KazaPuzzle2ExitReq();
    } else if (msg_state == 1) {                                        /* 743 */
        if (*paddat[0] == 1) {                                          /* 744 */
            MesSetNextPage();                                           /* 745 */
        } else if (*paddat[1] == 1) {                                   /* 747 */
            MesSetNextPage();                                           /* 748 */
        }

        if (GetNowMsgPageNum() >= GetMsgPageNum(0x45, 0)) {             /* 751 */
            SetMsgPage((char)(GetMsgPageNum(0x45, 0) - 1));             /* 752 */
            KazaPuzzle2ExitReq();                                       /* 753 */
        }
    }
}

/* ==========================================================================
 *  Display
 * ======================================================================== */

static void KazaPuzzle2DispInit(void)                                   /* 766 */
{                                                                       /* 767 */
    kaza_pzl_disp.anim_step      = 0;                                   /* 770 */
    kaza_pzl_disp.anim_timer     = 0;                                   /* 771 */
    kaza_pzl_disp.sub_anim_step  = 0;                                   /* 772 */
    kaza_pzl_disp.sub_anim_timer = 0;                                   /* 773 */
    kaza_pzl_disp.rot_anim_step  = 0;                                   /* 774 */
    kaza_pzl_disp.rot_anim_timer = 0;

    /* The drawn angles that go with the seeded rot_num[] above. */
    kaza_pzl_disp.panel_rot[0] = 180.0f;
    kaza_pzl_disp.panel_rot[1] = 270.0f;
    kaza_pzl_disp.panel_rot[2] = 0.0f;
    kaza_pzl_disp.panel_rot[3] = 0.0f;
    kaza_pzl_disp.panel_rot[4] = 270.0f;
}

void KazaPuzzle2DispMain(void)                                          /* 792 */
{                                                                       /* 793 */
    u_char fade_alpha;

    fade_alpha = 0x80;                                                  /* 798 */

    if ((u_char)(kaza_pzl_ctrl.step - 1) < 2) {                         /* 801 */
        KazaPuzzle2Anim(&kaza_pzl_disp.anim_step, &kaza_pzl_disp.anim_timer,
                        &fade_alpha);                                   /* 802 */

        if (kaza_pzl_disp.anim_step != 4) {                             /* 804 */
            KazaPuzzle2BgDisp(0, 0, 0x80);                              /* 806 */

            if (kaza_pzl_disp_func[kaza_pzl_ctrl.mode] != NULL) {       /* 808 */
                kaza_pzl_disp_func[kaza_pzl_ctrl.mode](0, 0, 0x80);     /* 809 */
            }
        }

        KazaPuzzle2BlackBgDisp(0, 0, fade_alpha);                       /* 814 */
    }
}

static void KazaPuzzle2Anim(char *anim_step, short *anim_timer, u_char *alpha) /* 836 */
{
    static const ALPHA_ANIM_TBL kaza_out_alpha[2] =             /* rdata 3b9a78 */
    {
        {  0, 128,  0, 30 },
        { -1,  -1, -1, -1 },
    };

    if (*anim_step == 0) {                                              /* 837 */
        *anim_timer = 0;                                                /* 838 */
        *anim_step  = 1;                                                /* 839 */
    }

    if (*anim_timer < 0) {                                              /* 843 */
        PRINT_WARNING("Warning!! KazaPuzzle2Anim()");                   /* 844 */
    }

    if (*anim_step == 1) {                                              /* 849 */
        *alpha     = 0;                                                 /* 851 */
        *anim_step = 2;                                                 /* 853 */
    } else if (*anim_step == 2) {
        *alpha = 0;                                                     /* 856 */
    } else if (*anim_step == 3) {                                       /* 859 */
        *alpha = Anim2D_CalcNowAlpha((ALPHA_ANIM_TBL *)kaza_out_alpha,
                                     (int)*anim_timer);                 /* 861 */
        (*anim_timer)++;                                                /* 863 */

        if (*anim_timer >= 30) {                                        /* 864 */
            *anim_step = 4;                                             /* 866 */
        }
    } else if (*anim_step == 4) {
        *alpha = 0x80;                                                  /* 868 */
    }
}                                                                       /* 871 */

/* -------------------------------------------------------------------------- */

static void KazaPuzzle2StartMsgDisp(int off_x, int off_y, u_char alpha) /* 882 */
{                                                                       /* 883 */
    u_char mode_alpha;

    mode_alpha = alpha;                                                 /* 887 */

    if (kaza_pzl_ctrl.step == 1) {                                      /* 889 */
        mode_alpha = KazaPuzzle2ModeAnimCtrl();                         /* 892 */
    }

    KazaPuzzle2PanelAllShadowDisp(0, 0, alpha);                         /* 897 */
    KazaPuzzle2PanelAllDisp(0, 0, alpha);                               /* 900 */
    KazaPuzzle2PanelAllEmbossShadowDisp(0, 0, alpha);                   /* 903 */
    KazaPuzzle2PanelAllEmbossHighLightDisp(0, 0, alpha);                /* 906 */
    KazaPuzzle2ScreenMask(0, 0, alpha);                                 /* 909 */
    KazaPuzzle2StartMsgWinDisp(0, 0, mode_alpha);                       /* 912 */
}

static void KazaPuzzle2PinWheelSelDisp(int off_x, int off_y, u_char alpha) /* 922 */
{                                                                       /* 923 */
    u_char mode_alpha;

    mode_alpha = alpha;                                                 /* 927 */

    if (kaza_pzl_ctrl.step == 1) {                                      /* 929 */
        mode_alpha = KazaPuzzle2ModeAnimCtrl();                         /* 932 */
    }

    KazaPuzzle2PanelAllShadowDisp(0, 0, alpha);                         /* 937 */
    KazaPuzzle2PanelAllDisp(0, 0, alpha);                               /* 940 */
    KazaPuzzle2PanelAllEmbossShadowDisp(0, 0, alpha);                   /* 943 */
    KazaPuzzle2PanelAllEmbossHighLightDisp(0, 0, alpha);                /* 946 */
    KazaPuzzle2PanelFlareDisp(0, 0, mode_alpha);                        /* 949 */
    KazaPuzzle2ScreenMask(0, 0, alpha);                                 /* 952 */
    KazaPuzzle2CapBaseDisp(0, 0, mode_alpha);                           /* 955 */
    KazaPuzzle2CapDisp(0, 0, mode_alpha);                               /* 958 */
    KazaPuzzle2RemainderFrequency(0, 0, mode_alpha);                    /* 961 */
}

static u_char KazaPuzzle2ModeAnimCtrl(void)                             /* 969 */
{                                                                       /* 970 */
    static const ALPHA_ANIM_TBL in_alpha_tbl[2] =               /* rdata 3b9af0 */
    {
        { 0, 128, 0, 7 },
        { -1, -1, -1, -1 },
    };
    static const ALPHA_ANIM_TBL out_alpha_tbl[2] =              /* rdata 3b9b00 */
    {
        { 128, 0, 0, 5 },
        { -1, -1, -1, -1 },
    };
    u_char alpha;

    alpha = 0x80;                                                       /* 983 */

    if (kaza_pzl_disp.sub_anim_step == 0) {                             /* 985 */
        kaza_pzl_disp.sub_anim_timer = 0;                               /* 986 */
        kaza_pzl_disp.sub_anim_step  = 1;                               /* 987 */
    }

    if (kaza_pzl_disp.sub_anim_step == 1) {                             /* 991 */
        alpha = Anim2D_CalcNowAlpha((ALPHA_ANIM_TBL *)in_alpha_tbl,
                                    (int)kaza_pzl_disp.sub_anim_timer); /* 993 */
        kaza_pzl_disp.sub_anim_timer++;                                 /* 995 */

        if (kaza_pzl_disp.sub_anim_timer >= 7) {                        /* 997 */
            kaza_pzl_disp.sub_anim_step  = 2;                           /* 999 */
            kaza_pzl_disp.sub_anim_timer = 0;                           /* 1000 */
        }                                                               /* 1002 */
    } else if (kaza_pzl_disp.sub_anim_step == 2) {
        /* Settled -- nothing to do. */
    } else if (kaza_pzl_disp.sub_anim_step == 3) {                      /* 1007 */
        alpha = Anim2D_CalcNowAlpha((ALPHA_ANIM_TBL *)out_alpha_tbl,
                                    (int)kaza_pzl_disp.sub_anim_timer); /* 1009 */
        kaza_pzl_disp.sub_anim_timer++;                                 /* 1011 */

        if (kaza_pzl_disp.sub_anim_timer >= 5) {                        /* 1012 */
            kaza_pzl_disp.sub_anim_timer = 0;                           /* 1013 */
            kaza_pzl_disp.sub_anim_step  = 4;                           /* 1014 */
        }                                                               /* 1016 */
    } else if (kaza_pzl_disp.sub_anim_step == 4) {
        alpha = 0;
    } else {
        PRINT_ASSERT("Error! KazaPuzzle2ModeAnimCtrl");                 /* 1021 */
    }

    return alpha;                                                       /* 1025 */
}

static void KazaPuzzle2ExitSelDisp(int off_x, int off_y, u_char alpha)  /* 1033 */
{                                                                       /* 1034 */
    u_char mode_alpha;

    mode_alpha = alpha;                                                 /* 1038 */

    if (kaza_pzl_ctrl.step == 1) {                                      /* 1040 */
        mode_alpha = KazaPuzzle2ModeAnimCtrl();                         /* 1043 */
    }

    KazaPuzzle2PanelAllShadowDisp(0, 0, alpha);                         /* 1048 */
    KazaPuzzle2PanelAllDisp(0, 0, alpha);                               /* 1051 */
    KazaPuzzle2PanelAllEmbossShadowDisp(0, 0, alpha);                   /* 1054 */
    KazaPuzzle2PanelAllEmbossHighLightDisp(0, 0, alpha);                /* 1057 */
    KazaPuzzle2ScreenMask(0, 0, alpha);                                 /* 1060 */
    KazaPuzzle2ExitConfWinDisp(0, 0, mode_alpha);                       /* 1063 */
}

static void KazaPuzzle2ClearDisp(int off_x, int off_y, u_char alpha)    /* 1073 */
{                                                                       /* 1074 */
    u_char mode_alpha;

    mode_alpha = alpha;                                                 /* 1078 */

    if (kaza_pzl_ctrl.step == 1) {                                      /* 1080 */
        mode_alpha = KazaPuzzle2ModeAnimCtrl();                         /* 1083 */
    }

    KazaPuzzle2PanelAllShadowDisp(0, 0, alpha);                         /* 1088 */
    KazaPuzzle2PanelAllDisp(0, 0, alpha);                               /* 1091 */
    KazaPuzzle2PanelAllEmbossShadowDisp(0, 0, alpha);                   /* 1094 */
    KazaPuzzle2PanelAllEmbossHighLightDisp(0, 0, alpha);                /* 1097 */
    KazaPuzzle2ScreenMask(0, 0, alpha);                                 /* 1100 */
    KazaPuzzle2ClearWinDisp(0, 0, mode_alpha);                          /* 1103 */
}

static void KazaPuzzle2FailureDisp(int off_x, int off_y, u_char alpha)  /* 1113 */
{                                                                       /* 1114 */
    u_char mode_alpha;

    mode_alpha = alpha;                                                 /* 1118 */

    if (kaza_pzl_ctrl.step == 1) {                                      /* 1120 */
        mode_alpha = KazaPuzzle2ModeAnimCtrl();                         /* 1123 */
    }

    KazaPuzzle2PanelAllShadowDisp(0, 0, alpha);                         /* 1128 */
    KazaPuzzle2PanelAllDisp(0, 0, alpha);                               /* 1131 */
    KazaPuzzle2PanelAllEmbossShadowDisp(0, 0, alpha);                   /* 1134 */
    KazaPuzzle2PanelAllEmbossHighLightDisp(0, 0, alpha);                /* 1137 */
    KazaPuzzle2ScreenMask(0, 0, alpha);                                 /* 1140 */
    KazaPuzzle2FailureWinDisp(0, 0, mode_alpha);                        /* 1143 */
}

/* Only the centre wheel -- the four the player is missing are not there. */
static void KazaPuzzle2CondErrorDisp(int off_x, int off_y, u_char alpha) /* 1153 */
{                                                                       /* 1154 */
    u_char mode_alpha;

    mode_alpha = alpha;                                                 /* 1158 */

    if (kaza_pzl_ctrl.step == 1) {                                      /* 1160 */
        mode_alpha = KazaPuzzle2ModeAnimCtrl();                         /* 1163 */
    }

    KazaPuzzle2PanelShadowDisp(0, 0, 0, alpha);                         /* 1168 */
    KazaPuzzle2PanelDisp(0, 0, 0, alpha, 0.0f);                         /* 1171 */
    KazaPuzzle2PanelEmbossShadowDisp(0, 0, 0, alpha);                   /* 1174 */
    KazaPuzzle2PanelEmbossHighLightDisp(0, 0, 0, alpha);                /* 1177 */
    KazaPuzzle2ScreenMask(0, 0, alpha);                                 /* 1180 */
    KazaPuzzle2CondErrorWinDisp(0, 0, mode_alpha);                      /* 1183 */
}

/* -------------------------------------------------------------------------- */

/* Board 2's background is sprites 0 and 1; board 1's is 0x33 and 0x34. */
static void KazaPuzzle2BgDisp(int off_x, int off_y, u_char alpha)       /* 1193 */
{                                                                       /* 1194 */
    DISP_SPRT bg_ds;
    int       i;

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1200 */

    for (i = 0; i < 2; i++) {
        CopySprDToSpr(&bg_ds, &puzzle_kaza_tex[i]);                     /* 1203 */
        bg_ds.x     = bg_ds.x + (float)off_x;                           /* 1204 */
        bg_ds.y     = bg_ds.y + (float)off_y;                           /* 1205 */
        bg_ds.alpha = (u_char)((bg_ds.alpha * alpha) >> 7);             /* 1206 */
        DispSprD(&bg_ds);                                               /* 1207 */
    }                                                                   /* 1208 */
}

static void KazaPuzzle2PanelAllShadowDisp(int off_x, int off_y, u_char alpha) /* 1218 */
{                                                                       /* 1219 */
    int i;

    for (i = 0; i < 5; i++) {                                           /* 1225 */
        KazaPuzzle2PanelShadowDisp(i, off_x, off_y, alpha);             /* 1226 */
    }                                                                   /* 1227 */
}

static void KazaPuzzle2PanelShadowDisp(int panel_label, int off_x, int off_y,
                                       u_char alpha)                    /* 1238 */
{                                                                       /* 1239 */
    static const int shadow_tex_tbl[5] =                        /* rdata 3b9b50 */
    {
        16, 14, 15, 12, 13,
    };
    DISP_SPRT shadow_ds;

    if (panel_label > 4) {                                              /* 1253 */
        PRINT_ASSERT("Error! KazaPuzzle2PanelShadowDisp");              /* 1254 */
    }

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1259 */

    CopySprDToSpr(&shadow_ds, &puzzle_kaza_tex[shadow_tex_tbl[panel_label]]); /* 1262 */
    shadow_ds.x      = shadow_ds.x + (float)off_x;                      /* 1263 */
    shadow_ds.y      = shadow_ds.y + (float)off_y;
    shadow_ds.alphar = 0x46;                                            /* 1264 */
    shadow_ds.alpha  = (u_char)((shadow_ds.alpha * alpha) >> 7);        /* 1265 */
    DispSprD(&shadow_ds);                                               /* 1266 */
}

static void KazaPuzzle2PanelAllDisp(int off_x, int off_y, u_char alpha) /* 1276 */
{                                                                       /* 1277 */
    int   i;
    int   sel_panel;
    float anim_rot;
    float rot;

    if (kaza_pzl_ctrl.rot_anim_flg != 0) {                              /* 1285 */
        anim_rot = KazaPuzzle2RotAnim();                                /* 1286 */
    } else {
        anim_rot = 0.0f;                                                /* 1289 */
    }

    sel_panel = kaza2_panel_label_tbl[kaza_pzl_ctrl.csr_tate][kaza_pzl_ctrl.csr_yoko]; /* 1292 */

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1296 */

    for (i = 0; i < 5; i++) {                                           /* 1299 */
        if (kaza_pzl_ctrl.rot_anim_flg == 0) {                          /* 1300 */
            rot = kaza_pzl_disp.panel_rot[i];                           /* 1302 */
        } else if (i == 0) {
            rot = kaza_pzl_disp.panel_rot[0] - anim_rot;                /* 1305 */

            if (rot < 0.0f) {                                           /* 1306 */
                rot = rot + 360.0f;
            }
        } else if (i == sel_panel) {                                    /* 1310 */
            rot = kaza_pzl_disp.panel_rot[i] + anim_rot;                /* 1313 */

            if (rot >= 360.0f) {                                        /* 1314 */
                rot = rot - 360.0f;
            }
        } else {
            rot = kaza_pzl_disp.panel_rot[i];
        }

        KazaPuzzle2PanelDisp(i, off_x, off_y, alpha, rot);              /* 1325 */
    }                                                                   /* 1326 */
}

static float KazaPuzzle2RotAnim(void)                                   /* 1334 */
{                                                                       /* 1335 */
    static const ROT_ANIM_TBL rot_anim_tbl[2] =                 /* rdata 3b9b90 */
    {
        {  0.0f, 90.0f,  0, 20 },
        { -1.0f, -1.0f, -1, -1 },
    };
    float rot;

    rot = 0.0f;                                                         /* 1345 */

    if (kaza_pzl_disp.rot_anim_step == 0) {                             /* 1348 */
        kaza_pzl_disp.rot_anim_timer = 0;                               /* 1349 */
        kaza_pzl_disp.rot_anim_step  = 2;                               /* 1351 */
    }

    if (kaza_pzl_disp.rot_anim_step == 2) {                             /* 1354 */
        rot = Anim2D_CalcNowRot((ROT_ANIM_TBL *)rot_anim_tbl,
                                (int)kaza_pzl_disp.rot_anim_timer);     /* 1355 */
    }

    return rot;                                                         /* 1359 */
}

/* Board 2's wheels are sprites 53..57; board 1's are 22..26. */
static void KazaPuzzle2PanelDisp(int panel_label, int off_x, int off_y,
                                 u_char alpha, float rot)               /* 1369 */
{                                                                       /* 1370 */
    static const int panel_tex_tbl[5] =                         /* rdata 3b9bc0 */
    {
        53, 54, 55, 56, 57,
    };
    DISP_SPRT panel_ds;

    if (panel_label > 4) {                                              /* 1383 */
        PRINT_ASSERT("Error! KazaPuzzle2PanelDisp");                    /* 1384 */
    }

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1389 */

    CopySprDToSpr(&panel_ds, &puzzle_kaza_tex[panel_tex_tbl[panel_label]]); /* 1392 */
    panel_ds.x     = panel_ds.x + (float)off_x;                         /* 1393 */
    panel_ds.y     = panel_ds.y + (float)off_y;
    panel_ds.alpha = (u_char)((panel_ds.alpha * alpha) >> 7);           /* 1394 */
    panel_ds.crx   = panel_ds.x + (float)panel_ds.w * 0.5f;             /* 1395 */
    panel_ds.cry   = panel_ds.y + (float)panel_ds.h * 0.5f;
    panel_ds.rot   = rot;
    DispSprD(&panel_ds);                                                /* 1396 */
}

static void KazaPuzzle2PanelAllEmbossShadowDisp(int off_x, int off_y, u_char alpha) /* 1406 */
{                                                                       /* 1407 */
    int i;

    for (i = 0; i < 5; i++) {                                           /* 1413 */
        KazaPuzzle2PanelEmbossShadowDisp(i, off_x, off_y, alpha);       /* 1414 */
    }                                                                   /* 1415 */
}

static void KazaPuzzle2PanelEmbossShadowDisp(int panel_label, int off_x, int off_y,
                                             u_char alpha)              /* 1426 */
{                                                                       /* 1427 */
    static const int emboss_tbl[5] =                            /* rdata 3b9c20 */
    {
        11, 9, 10, 7, 8,
    };
    DISP_SPRT emboss_ds;

    if (panel_label > 4) {                                              /* 1441 */
        PRINT_ASSERT("Error! KazaPuzzle2PanelEmbossShadowDisp");        /* 1442 */
    }

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1447 */

    CopySprDToSpr(&emboss_ds, &puzzle_kaza_tex[emboss_tbl[panel_label]]); /* 1450 */
    emboss_ds.x     = emboss_ds.x + (float)off_x;                       /* 1451 */
    emboss_ds.y     = emboss_ds.y + (float)off_y;
    emboss_ds.alpha = (u_char)((emboss_ds.alpha * alpha) >> 7);         /* 1452 */
    DispSprD(&emboss_ds);                                               /* 1453 */
}

static void KazaPuzzle2PanelAllEmbossHighLightDisp(int off_x, int off_y, u_char alpha) /* 1463 */
{                                                                       /* 1464 */
    int i;

    for (i = 0; i < 5; i++) {                                           /* 1470 */
        KazaPuzzle2PanelEmbossHighLightDisp(i, off_x, off_y, alpha);    /* 1471 */
    }                                                                   /* 1472 */
}

static void KazaPuzzle2PanelEmbossHighLightDisp(int panel_label, int off_x, int off_y,
                                                u_char alpha)           /* 1483 */
{
    static const int emboss_tbl[5] =                            /* rdata 3b9c60 */
    {
        6, 4, 5, 2, 3,
    };
    DISP_SPRT emboss_ds;

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1497 */

    CopySprDToSpr(&emboss_ds, &puzzle_kaza_tex[emboss_tbl[panel_label]]); /* 1500 */
    emboss_ds.x     = emboss_ds.x + (float)off_x;                       /* 1501 */
    emboss_ds.y     = emboss_ds.y + (float)off_y;
    emboss_ds.alpha = (u_char)((emboss_ds.alpha * alpha) >> 7);         /* 1502 */
    DispSprD(&emboss_ds);                                               /* 1503 */
}

static void KazaPuzzle2PanelFlareDisp(int off_x, int off_y, u_char alpha) /* 1524 */
{                                                                       /* 1525 */
    static const int flare_tex_tbl[2][2] =                      /* rdata 3b9c78 */
    {
        { 17, 19 },
        { 18, 20 },
    };
    DISP_SPRT flare_ds;
    u_char    rgb;

    rgb = KazaPuzzle2FlareAnim();                                       /* 1528 */

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1531 */

    CopySprDToSpr(&flare_ds,
                  &puzzle_kaza_tex[flare_tex_tbl[kaza_pzl_ctrl.csr_tate]
                                                [kaza_pzl_ctrl.csr_yoko]]); /* 1532 */
    flare_ds.x      = flare_ds.x + (float)off_x;                        /* 1533 */
    flare_ds.y      = flare_ds.y + (float)off_y;
    flare_ds.alphar = 0x48;                                             /* 1534 */
    flare_ds.alpha  = (u_char)((flare_ds.alpha * alpha) >> 7);          /* 1535 */
    flare_ds.r      = rgb;
    flare_ds.g      = rgb;
    flare_ds.b      = rgb;
    DispSprD(&flare_ds);                                                /* 1536 */
}

static u_char KazaPuzzle2FlareAnim(void)                                /* 1544 */
{                                                                       /* 1545 */
    static const RGB_ANIM_TBL flare_rgb_tbl[3] =                /* rdata 3b9c88 */
    {
        {  64, 115,  0, 30 },
        { 115,  64, 30, 60 },
        {  -1,  -1, -1, -1 },
    };
    u_char rgb;

    rgb = Anim2D_CalcNowRGB((RGB_ANIM_TBL *)flare_rgb_tbl,
                            (int)kaza_pzl_disp.flare_anim_timer);       /* 1557 */

    kaza_pzl_disp.flare_anim_timer++;                                   /* 1559 */

    if (kaza_pzl_disp.flare_anim_timer > 59) {                          /* 1561 */
        kaza_pzl_disp.flare_anim_timer = 0;                             /* 1562 */
    }

    return rgb;                                                         /* 1566 */
}

static void KazaPuzzle2CapBaseDisp(int off_x, int off_y, u_char alpha)  /* 1579 */
{                                                                       /* 1580 */
    DISP_SPRT base_ds;

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1582 */

    CopySprDToSpr(&base_ds, &puzzle_kaza_tex[0x1c]);                    /* 1583 */
    base_ds.x      = base_ds.x + (float)off_x;                          /* 1584 */
    base_ds.y      = base_ds.y + (float)off_y;
    base_ds.alphar = 0x46;
    base_ds.alpha  = (u_char)((base_ds.alpha * alpha) >> 7);            /* 1585 */
    DispSprD(&base_ds);                                                 /* 1586 */
}

static void KazaPuzzle2CapDisp(int off_x, int off_y, u_char alpha)      /* 1601 */
{                                                                       /* 1602 */
    DISP_SPRT base_ds;

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1604 */

    CopySprDToSpr(&base_ds, &puzzle_kaza_tex[0x1b]);                    /* 1605 */
    base_ds.x     = base_ds.x + (float)off_x;                           /* 1606 */
    base_ds.y     = base_ds.y + (float)off_y;
    base_ds.alpha = (u_char)((base_ds.alpha * alpha) >> 7);
    DispSprD(&base_ds);                                                 /* 1607 */
}

static void KazaPuzzle2RemainderFrequency(int off_x, int off_y, u_char alpha) /* 1617 */
{                                                                       /* 1618 */
    static const int num_tex_tbl[10] =                          /* rdata 3b9cc0 */
    {
        30, 30, 31, 32, 33, 34, 35, 36, 37, 38,
    };
    static const int num_tex_tbl_s[10] =                        /* rdata 3b9ce8 */
    {
        42, 42, 43, 44, 45, 46, 47, 48, 49, 50,
    };
    DISP_SPRT ds;

    if (kaza_pzl_ctrl.remainder_frequency > 9) {                        /* 1648 */
        PRINT_ASSERT("Error! KazaPuzzle2RemainderFrequency");           /* 1649 */
    }

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1653 */

    CopySprDToSpr(&ds, &puzzle_kaza_tex[0x27]);                         /* 1656 */
    ds.x      = ds.x + (float)off_x;                                    /* 1657 */
    ds.y      = ds.y + (float)off_y;                                    /* 1658 */
    ds.alphar = 0x46;                                                   /* 1659 */
    ds.alpha  = (u_char)((ds.alpha * alpha) >> 7);
    DispSprD(&ds);                                                      /* 1660 */

    if (GetLanguage() == 3) {                                           /* 1663 */
        if (num_tex_tbl_s[kaza_pzl_ctrl.remainder_frequency] != -1) {   /* 1664 */
            CopySprDToSpr(&ds,
                          &puzzle_kaza_tex[num_tex_tbl_s[kaza_pzl_ctrl.remainder_frequency]]); /* 1665 */
            ds.x     = ds.x + (float)off_x;                             /* 1666 */
            ds.y     = ds.y + (float)off_y;
            ds.alpha = (u_char)((ds.alpha * alpha) >> 7);               /* 1667 */
            DispSprD(&ds);                                              /* 1668 */
        }
    } else {
        if (num_tex_tbl[kaza_pzl_ctrl.remainder_frequency] != -1) {     /* 1672 */
            CopySprDToSpr(&ds,
                          &puzzle_kaza_tex[num_tex_tbl[kaza_pzl_ctrl.remainder_frequency]]); /* 1673 */
            ds.x     = ds.x + (float)off_x;                             /* 1674 */
            ds.y     = ds.y + (float)off_y;
            ds.alpha = (u_char)((ds.alpha * alpha) >> 7);               /* 1675 */
            DispSprD(&ds);                                              /* 1676 */
        }
    }

    if (GetLanguage() == 3) {                                           /* 1682 */
        CopySprDToSpr(&ds, &puzzle_kaza_tex[0x29]);                     /* 1683 */
    } else {
        CopySprDToSpr(&ds, &puzzle_kaza_tex[0x1d]);                     /* 1686 */
    }

    ds.x     = ds.x + (float)off_x;                                     /* 1688 */
    ds.y     = ds.y + (float)off_y;
    ds.alpha = (u_char)((ds.alpha * alpha) >> 7);                       /* 1689 */
    DispSprD(&ds);                                                      /* 1690 */
}

static void KazaPuzzle2ScreenMask(int off_x, int off_y, u_char alpha)   /* 1705 */
{                                                                       /* 1706 */
    DISP_SPRT ds;

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1709 */

    CopySprDToSpr(&ds, &puzzle_kaza_tex[0x28]);                         /* 1710 */
    ds.x      = ds.x + (float)off_x;                                    /* 1711 */
    ds.y      = ds.y + (float)off_y;
    ds.csx    = ds.x;
    ds.csy    = ds.y;
    ds.scw    = 640.0f / (float)ds.w;                                   /* 1712 */
    ds.sch    = 448.0f / (float)ds.h;
    ds.alphar = 0x46;
    ds.alpha  = (u_char)((ds.alpha * alpha) >> 7);                      /* 1713 */
    DispSprD(&ds);                                                      /* 1714 */
}

/* -------------------------------------------------------------------------- */

static void KazaPuzzle2CmnWinDisp(int off_x, int off_y, u_char alpha)   /* 1723 */
{                                                                       /* 1724 */
    MSG_WIN_DAT win_dat;

    SetMsgWinDefData(&win_dat, 0x45);                                   /* 1727 */
    DrawCmnWindow(0, win_dat.x, win_dat.y, win_dat.w, win_dat.h, alpha, 0x80); /* 1730 */
}

static void KazaPuzzle2StartMsgWinDisp(int off_x, int off_y, u_char alpha) /* 1740 */
{                                                                       /* 1741 */
    DISP_STR ds;

    SetMsgDefData(&ds, 0x45);                                           /* 1745 */
    KazaPuzzle2CmnWinDisp(off_x, off_y, alpha);                         /* 1748 */
    PrintMsg(0x45, 9, ds.pos_x, ds.pos_y, 1, (int)alpha, 0);            /* 1750 */
}

static void KazaPuzzle2ExitConfWinDisp(int off_x, int off_y, u_char alpha) /* 1760 */
{                                                                       /* 1761 */
    DISP_STR ds;

    SetMsgDefData(&ds, 0x45);                                           /* 1765 */
    KazaPuzzle2CmnWinDisp(off_x, off_y, alpha);                         /* 1768 */

    DrawCmnSelCsr(0, (float)(kaza_pzl_ctrl.exit_csr * 0xcf + off_x + 0x9b),
                  (float)(off_y + 0x184), alpha, 0.0f, 0);              /* 1772 */
    DrawCmnSelYes(0, (float)(off_x + 0x99), (float)(off_y + 0x186), alpha); /* 1775 */
    DrawCmnSelNo(0, (float)(off_x + 0x169), (float)(off_y + 0x186), alpha); /* 1776 */

    PrintMsg(0x45, 4, ds.pos_x, ds.pos_y, 1, (int)alpha, 0);            /* 1778 */
}

static void KazaPuzzle2ClearWinDisp(int off_x, int off_y, u_char alpha) /* 1788 */
{                                                                       /* 1789 */
    DISP_STR ds;

    SetMsgDefData(&ds, 0x45);                                           /* 1793 */
    KazaPuzzle2CmnWinDisp(off_x, off_y, alpha);                         /* 1796 */
    PrintMsg(0x45, 8, ds.pos_x, ds.pos_y, 1, (int)alpha, 0);            /* 1798 */
}

static void KazaPuzzle2FailureWinDisp(int off_x, int off_y, u_char alpha) /* 1808 */
{                                                                       /* 1809 */
    DISP_STR ds;

    SetMsgDefData(&ds, 0x45);                                           /* 1813 */
    KazaPuzzle2CmnWinDisp(off_x, off_y, alpha);                         /* 1816 */
    PrintMsg(0x45, 7, ds.pos_x, ds.pos_y, 1, (int)alpha, 0);            /* 1818 */
}

/* The only paged message in the file, hence PrintMsg_P rather than PrintMsg. */
static void KazaPuzzle2CondErrorWinDisp(int off_x, int off_y, u_char alpha) /* 1828 */
{                                                                       /* 1829 */
    DISP_STR ds;

    SetMsgDefData(&ds, 0x45);                                           /* 1833 */
    KazaPuzzle2CmnWinDisp(off_x, off_y, alpha);                         /* 1836 */
    PrintMsg_P(0x45, 0, ds.pos_x, ds.pos_y, 1, (int)alpha, 0, 0, 0);    /* 1839 */
}

/* ==========================================================================
 *  The cross-fade in front of the puzzle phase
 * ======================================================================== */

void KazaPuzzle2CrossScreenDisp(int off_x, int off_y, u_char alpha)     /* 1849 */
{                                                                       /* 1850 */
    KazaPuzzle2BgDisp(0, 0, 0x80);                                      /* 1854 */

    /* Without the four pinwheels there is only the centre one to draw. */
    if (KazaPuzzle2CondCheck() == 0) {                                  /* 1857 */
        KazaPuzzle2PanelShadowDisp(0, 0, 0, 0x80);                      /* 1859 */
        KazaPuzzle2PanelDisp(0, 0, 0, 0x80, 0.0f);                      /* 1862 */
        KazaPuzzle2PanelEmbossShadowDisp(0, 0, 0, 0x80);                /* 1865 */
        KazaPuzzle2PanelEmbossHighLightDisp(0, 0, 0, 0x80);             /* 1868 */
    } else {
        KazaPuzzle2PanelAllShadowDisp(0, 0, 0x80);                      /* 1872 */
        KazaPuzzle2PanelAllDisp(0, 0, 0x80);                            /* 1875 */
        KazaPuzzle2PanelAllEmbossShadowDisp(0, 0, 0x80);                /* 1878 */
        KazaPuzzle2PanelAllEmbossHighLightDisp(0, 0, 0x80);             /* 1881 */
    }

    KazaPuzzle2ScreenMask(0, 0, 0x80);                                  /* 1885 */
    KazaPuzzle2BlackBgDisp(0, 0, alpha);                                /* 1888 */
}

static void KazaPuzzle2BlackBgDisp(int off_x, int off_y, u_char alpha)  /* 1898 */
{                                                                       /* 1899 */
    SQAR_DAT  black_bg = { 640, 448, 0, 0, 0, 0, 0, 0, 128 };           /* 1901 */
    DISP_SQAR dsq;

    CopySqrDToSqr(&dsq, &black_bg);                                     /* 1905 */
    dsq.alpha = (u_char)((dsq.alpha * alpha) >> 7);                     /* 1906 */
    DispSqrD(&dsq);                                                     /* 1907 */
}
