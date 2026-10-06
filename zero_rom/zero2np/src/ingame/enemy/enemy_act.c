/* ==========================================================================
 *  ingame/enemy/enemy_act.c
 *
 *  The ghost behaviour-script interpreter.  enemy.c owns the ten ENE_WRK
 *  slots and points the cursor at an action (EneActSet / EneBlinkSet); this
 *  file is the machine that runs it, and it is where a ghost's every visible
 *  decision actually comes from -- where it walks, when it swings, how
 *  transparent it is, which sound it makes.
 *
 *  ---- the machine ---------------------------------------------------------
 *  The script is a flat byte stream (the ALG pak, loaded by alg_manage.o).
 *  ENEALG_WRK is the cursor:
 *
 *      job_no    = the opcode currently executing
 *      pos_no    = sub-step within it; 0 means "fetch the next opcode"
 *      wait_time = frames to hold before the next iteration, in ew->reso
 *                  units; 255.0 ends the run
 *      comm_add  = the cursor        comm_add_top = the script base
 *      loop[2] / loop_tr[2] / cnt[2] = two general-purpose counters
 *
 *  A handler that leaves wait_time <= 0 falls straight through to the next
 *  opcode in the same frame, so a whole chain of state changes runs in one
 *  step.  A handler that wants to be resumed next frame leaves pos_no != 0;
 *  EneAlgCtrl() then re-enters the *same* handler without re-fetching the
 *  opcode byte.  That is how the multi-frame jobs (turn towards, walk to,
 *  fade) are written -- as a little state machine keyed on pos_no.
 *
 *  Opcodes are split into four spaces by their numeric range, each with its
 *  own table:
 *
 *      0x00..0x6f  CommJmpContTbl   EJobC**   control / state
 *      0x70..0x9f  CommJmpMoveTbl   EJobM**   movement
 *      0xa0..0xdf  CommJmpBrnchTbl  EJobB**   conditional branches
 *      0xe0..0xfe  CommJmpEffTbl    EJobE**   effects
 *
 *  The blink (secondary) script is a separate, much smaller machine running
 *  off bcomm_add / bpos_no / bjob_no with a single table, BCommJmpTbl.  It
 *  runs every frame regardless of the status bits that freeze the main
 *  script, which is what keeps a held ghost's face and per-part visibility
 *  alive.
 *
 *  Multi-byte operands are little-endian pairs assembled a byte at a time
 *  (lo + hi * 0x100), never read as a u16 -- the stream is not aligned.
 *  Jump operands are u16 offsets from comm_add_top, so scripts are position
 *  independent.
 *
 *  PORT NOTE: the ROM's operand fetches are open-coded at every call site and
 *  leave no symbol behind, so they were a macro or an inlined helper in a
 *  header this build does not carry.  EaGetU8 / EaGetU16 / EaJumpTo below are
 *  the port's stand-in, the same choice sis_algo.c already made for the
 *  companion's interpreter.  Everything else is one-to-one with enemy_act.o.
 *
 *  ---- PAL ------------------------------------------------------------------
 *  Frame counts baked into scripts are 60Hz.  Every one of them is divided by
 *  1.19999993 (60/50, one ulp low, as the EE compiler rounded it) when
 *  GetPALMode() is non-zero.  Speeds go the other way and multiply.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), enemy_act.o.
 *  Trailing numeric comments are the original source line numbers.
 * ======================================================================== */

#include "enemy_act.h"

#include <stdio.h>                              /* printf                      */
#include <string.h>                             /* memset                      */
#include <math.h>                               /* sinf                        */

#include "eetypes.h"
#include "libvu0.h"
#include "enemy.h"
#include "enemy_dat.h"
#include "fly_ctrl.h"

#include "../../common/utility.h"               /* GetDistV / RotLimitChk / .. */
#include "../../common/utility2.h"              /* PRINT_ASSERT                */
#include "../../common/variable.h"              /* plyr_wrk / sis_wrk          */
#include "../../common/zero2_util.h"
#include "../../graphics/graph3d/ctl/fixed_array.h"
#include "../../graphics/graph3d/gra3d.h"
#include "../../graphics/motion/mdlwork.h"      /* ANI_CTRL                    */
#include "../../graphics/motion/motion.h"       /* ReqAnm / motGetMotReso      */
#include "../../graphics/effect/effect.h"       /* ResetEffects                */
#include "../../graphics/effect/effect_obj.h"   /* CallPartsDeform5            */
#include "../../graphics/effect/effect_sub.h"
#include "../../system/os/system.h"             /* GetPALMode                  */
#include "../../system/eeiop/sndbank.h"
#include "../../system/eeiop/stream_auto.h"
#include "../camera/map_camera.h"
#include "../ingame.h"                          /* SetIngameEneDead / ..       */
#include "../ingame_effect.h"
#include "../map/map_hit_check.h"
#include "../map/MhCtl.h"
#include "../photo/finder.h"                    /* FinderBankPlay              */
#include "../plyr/player.h"
#include "../plyr/sister.h"
#include "../plyr/sis_mdl.h"                    /* SisterDrawLock / Unlock     */
#include "../plyr/unit_ctl.h"                   /* ReqEneStop / RotRngChk      */
#include "../../graphics/graph3d/g3dxVu0.h" /* g3dxVu0CopyVector */

/* Debug switch, 0xff in the shipped image.  Defined here in the ROM and read
 * by nothing in it -- the debug menu that used it is not in this build. */
int debug_alg = 0xff;                                   /* sdata 3f0200 */

typedef void (*ENE_JOB_FUNC)(ENEALG_WRK *alg);
typedef void (*ENE_BJOB_FUNC)(ENE_WRK *ew);

/* ---- opcode handlers, in table order ------------------------------------ */

void EJobC00(ENEALG_WRK *alg); void EJobC01(ENEALG_WRK *alg);
void EJobC02(ENEALG_WRK *alg); void EJobC03(ENEALG_WRK *alg);
void EJobC04(ENEALG_WRK *alg); void EJobC05(ENEALG_WRK *alg);
void EJobC06(ENEALG_WRK *alg); void EJobC07(ENEALG_WRK *alg);
void EJobC08(ENEALG_WRK *alg); void EJobC09(ENEALG_WRK *alg);
void EJobC0A(ENEALG_WRK *alg); void EJobC0B(ENEALG_WRK *alg);
void EJobC0C(ENEALG_WRK *alg); void EJobC0D(ENEALG_WRK *alg);
void EJobC0E(ENEALG_WRK *alg); void EJobC0F(ENEALG_WRK *alg);
void EJobC10(ENEALG_WRK *alg); void EJobC11(ENEALG_WRK *alg);
void EJobC12(ENEALG_WRK *alg); void EJobC13(ENEALG_WRK *alg);
void EJobC14(ENEALG_WRK *alg); void EJobC15(ENEALG_WRK *alg);
void EJobC16(ENEALG_WRK *alg); void EJobC17(ENEALG_WRK *alg);
void EJobC18(ENEALG_WRK *alg); void EJobC19(ENEALG_WRK *alg);
void EJobC1A(ENEALG_WRK *alg); void EJobC1B(ENEALG_WRK *alg);
void EJobC1C(ENEALG_WRK *alg); void EJobC1D(ENEALG_WRK *alg);
void EJobC1E(ENEALG_WRK *alg); void EJobC1F(ENEALG_WRK *alg);
void EJobC20(ENEALG_WRK *alg); void EJobC21(ENEALG_WRK *alg);
void EJobC22(ENEALG_WRK *alg); void EJobC23(ENEALG_WRK *alg);
void EJobC24(ENEALG_WRK *alg); void EJobC25(ENEALG_WRK *alg);
void EJobC26(ENEALG_WRK *alg); void EJobC27(ENEALG_WRK *alg);
void EJobC28(ENEALG_WRK *alg); void EJobC29(ENEALG_WRK *alg);
void EJobC2A(ENEALG_WRK *alg); void EJobC2B(ENEALG_WRK *alg);
void EJobC2C(ENEALG_WRK *alg); void EJobC2D(ENEALG_WRK *alg);
void EJobC2E(ENEALG_WRK *alg); void EJobC2F(ENEALG_WRK *alg);
void EJobC30(ENEALG_WRK *alg); void EJobC31(ENEALG_WRK *alg);
void EJobC32(ENEALG_WRK *alg); void EJobC33(ENEALG_WRK *alg);
void EJobC34(ENEALG_WRK *alg); void EJobC35(ENEALG_WRK *alg);
void EJobC36(ENEALG_WRK *alg); void EJobC37(ENEALG_WRK *alg);
void EJobC38(ENEALG_WRK *alg); void EJobC39(ENEALG_WRK *alg);
void EJobC3A(ENEALG_WRK *alg); void EJobC3B(ENEALG_WRK *alg);
void EJobC3C(ENEALG_WRK *alg); void EJobC3D(ENEALG_WRK *alg);
void EJobC3E(ENEALG_WRK *alg); void EJobC3F(ENEALG_WRK *alg);
void EJobC40(ENEALG_WRK *alg); void EJobC41(ENEALG_WRK *alg);
void EJobC42(ENEALG_WRK *alg); void EJobC43(ENEALG_WRK *alg);
void EJobC44(ENEALG_WRK *alg); void EJobC45(ENEALG_WRK *alg);
void EJobC46(ENEALG_WRK *alg); void EJobC47(ENEALG_WRK *alg);
void EJobC48(ENEALG_WRK *alg); void EJobC49(ENEALG_WRK *alg);

void EJobM00(ENEALG_WRK *alg); void EJobM01(ENEALG_WRK *alg);
void EJobM02(ENEALG_WRK *alg); void EJobM03(ENEALG_WRK *alg);
void EJobM04(ENEALG_WRK *alg); void EJobM05(ENEALG_WRK *alg);
void EJobM06(ENEALG_WRK *alg); void EJobM07(ENEALG_WRK *alg);
void EJobM08(ENEALG_WRK *alg); void EJobM09(ENEALG_WRK *alg);
void EJobM0A(ENEALG_WRK *alg); void EJobM0B(ENEALG_WRK *alg);
void EJobM0C(ENEALG_WRK *alg); void EJobM0D(ENEALG_WRK *alg);
void EJobM0E(ENEALG_WRK *alg); void EJobM0F(ENEALG_WRK *alg);
void EJobM10(ENEALG_WRK *alg); void EJobM11(ENEALG_WRK *alg);
void EJobM12(ENEALG_WRK *alg); void EJobM13(ENEALG_WRK *alg);
void EJobM14(ENEALG_WRK *alg); void EJobM15(ENEALG_WRK *alg);

void EJobB00(ENEALG_WRK *alg); void EJobB01(ENEALG_WRK *alg);
void EJobB02(ENEALG_WRK *alg); void EJobB03(ENEALG_WRK *alg);
void EJobB04(ENEALG_WRK *alg); void EJobB05(ENEALG_WRK *alg);
void EJobB06(ENEALG_WRK *alg); void EJobB07(ENEALG_WRK *alg);
void EJobB08(ENEALG_WRK *alg); void EJobB09(ENEALG_WRK *alg);
void EJobB0A(ENEALG_WRK *alg); void EJobB0B(ENEALG_WRK *alg);
void EJobB0C(ENEALG_WRK *alg); void EJobB0D(ENEALG_WRK *alg);
void EJobB0E(ENEALG_WRK *alg); void EJobB0F(ENEALG_WRK *alg);
void EJobB10(ENEALG_WRK *alg); void EJobB11(ENEALG_WRK *alg);
void EJobB12(ENEALG_WRK *alg); void EJobB13(ENEALG_WRK *alg);
void EJobB14(ENEALG_WRK *alg); void EJobB15(ENEALG_WRK *alg);
void EJobB16(ENEALG_WRK *alg); void EJobB17(ENEALG_WRK *alg);
void EJobB18(ENEALG_WRK *alg); void EJobB19(ENEALG_WRK *alg);
void EJobB1A(ENEALG_WRK *alg); void EJobB1B(ENEALG_WRK *alg);
void EJobB1C(ENEALG_WRK *alg); void EJobB1D(ENEALG_WRK *alg);
void EJobB1E(ENEALG_WRK *alg); void EJobB1F(ENEALG_WRK *alg);
void EJobB20(ENEALG_WRK *alg); void EJobB21(ENEALG_WRK *alg);
void EJobB22(ENEALG_WRK *alg); void EJobB23(ENEALG_WRK *alg);
void EJobB24(ENEALG_WRK *alg); void EJobB25(ENEALG_WRK *alg);
void EJobB26(ENEALG_WRK *alg); void EJobB27(ENEALG_WRK *alg);
void EJobB28(ENEALG_WRK *alg); void EJobB29(ENEALG_WRK *alg);
void EJobB2A(ENEALG_WRK *alg);

void EJobE00(ENEALG_WRK *alg); void EJobE01(ENEALG_WRK *alg);
void EJobE02(ENEALG_WRK *alg); void EJobE03(ENEALG_WRK *alg);
void EJobE04(ENEALG_WRK *alg); void EJobE05(ENEALG_WRK *alg);

void BJobL00(ENE_WRK *ew); void BJobL01(ENE_WRK *ew);
void BJobL02(ENE_WRK *ew); void BJobL03(ENE_WRK *ew);
void BJobL04(ENE_WRK *ew); void BJobL05(ENE_WRK *ew);
void BJobL06(ENE_WRK *ew);

static void EnemyDeadPDeformCall(ENE_WRK *ew);
static void EnemyDeadPDeformReset(ENE_WRK *ew);

/* ---- dispatch tables ----------------------------------------------------- */

/* Opcodes 0x00..0x6f.  Everything that is not movement, a branch or an
 * effect: waits, jumps, status bits, animation, sound, the fly creatures. */
static ENE_JOB_FUNC CommJmpContTbl[74] =                /* data 300f50 */
{
    EJobC00, EJobC01, EJobC02, EJobC03, EJobC04, EJobC05, EJobC06, EJobC07,
    EJobC08, EJobC09, EJobC0A, EJobC0B, EJobC0C, EJobC0D, EJobC0E, EJobC0F,
    EJobC10, EJobC11, EJobC12, EJobC13, EJobC14, EJobC15, EJobC16, EJobC17,
    EJobC18, EJobC19, EJobC1A, EJobC1B, EJobC1C, EJobC1D, EJobC1E, EJobC1F,
    EJobC20, EJobC21, EJobC22, EJobC23, EJobC24, EJobC25, EJobC26, EJobC27,
    EJobC28, EJobC29, EJobC2A, EJobC2B, EJobC2C, EJobC2D, EJobC2E, EJobC2F,
    EJobC30, EJobC31, EJobC32, EJobC33, EJobC34, EJobC35, EJobC36, EJobC37,
    EJobC38, EJobC39, EJobC3A, EJobC3B, EJobC3C, EJobC3D, EJobC3E, EJobC3F,
    EJobC40, EJobC41, EJobC42, EJobC43, EJobC44, EJobC45, EJobC46, EJobC47,
    EJobC48, EJobC49
};

/* Opcodes 0x70..0x9f. */
static ENE_JOB_FUNC CommJmpMoveTbl[22] =                /* data 301078 */
{
    EJobM00, EJobM01, EJobM02, EJobM03, EJobM04, EJobM05, EJobM06, EJobM07,
    EJobM08, EJobM09, EJobM0A, EJobM0B, EJobM0C, EJobM0D, EJobM0E, EJobM0F,
    EJobM10, EJobM11, EJobM12, EJobM13, EJobM14, EJobM15
};

/* Opcodes 0xa0..0xdf. */
static ENE_JOB_FUNC CommJmpBrnchTbl[43] =               /* data 3010d0 */
{
    EJobB00, EJobB01, EJobB02, EJobB03, EJobB04, EJobB05, EJobB06, EJobB07,
    EJobB08, EJobB09, EJobB0A, EJobB0B, EJobB0C, EJobB0D, EJobB0E, EJobB0F,
    EJobB10, EJobB11, EJobB12, EJobB13, EJobB14, EJobB15, EJobB16, EJobB17,
    EJobB18, EJobB19, EJobB1A, EJobB1B, EJobB1C, EJobB1D, EJobB1E, EJobB1F,
    EJobB20, EJobB21, EJobB22, EJobB23, EJobB24, EJobB25, EJobB26, EJobB27,
    EJobB28, EJobB29, EJobB2A
};

/* Opcodes 0xe0..0xfe. */
static ENE_JOB_FUNC CommJmpEffTbl[6] =                  /* data 301180 */
{
    EJobE00, EJobE01, EJobE02, EJobE03, EJobE04, EJobE05
};

/* Blink-script opcodes.  Eight slots for seven handlers -- the ROM's table
 * really does carry a trailing null, and nothing range-checks bjob_no. */
static ENE_BJOB_FUNC BCommJmpTbl[8] =                   /* data 301198 */
{
    BJobL00, BJobL01, BJobL02, BJobL03, BJobL04, BJobL05, BJobL06, NULL
};

/* ---- operand fetch ------------------------------------------------------- */

static u_char EaGetU8(ENEALG_WRK *alg)
{
    u_char v = *alg->comm_add.pu8;
    alg->comm_add.pu8++;
    return v;
}

static u_short EaGetU16(ENEALG_WRK *alg)
{
    u_char lo = alg->comm_add.pu8[0];
    u_char hi = alg->comm_add.pu8[1];
    alg->comm_add.pu8 += 2;
    return (u_short)((u_int)lo + (u_int)hi * 0x100);
}

/* Jumps are u16 offsets from the script base, never absolute. */
static void EaJumpTo(ENEALG_WRK *alg, u_short adj)
{
    alg->comm_add.wrk = alg->comm_add_top + (intptr_t)(int)adj;
}

static u_char EaBGetU8(ENEALG_WRK *alg)
{
    u_char v = *alg->bcomm_add.pu8;
    alg->bcomm_add.pu8++;
    return v;
}

static u_short EaBGetU16(ENEALG_WRK *alg)
{
    u_char lo = alg->bcomm_add.pu8[0];
    u_char hi = alg->bcomm_add.pu8[1];
    alg->bcomm_add.pu8 += 2;
    return (u_short)((u_int)lo + (u_int)hi * 0x100);
}

/* ==========================================================================
 *  The two machines
 * ======================================================================== */

/* One step of the main action script.
 *
 * The frame's single decrement happens once, on entry; after that the loop
 * just carries each handler's parting wait_time back to the top.  That is
 * what lets a run of fall-through opcodes execute inside one frame.  Only a
 * positive wait_time returns, and only the end-of-action opcode's 255.0
 * breaks out.
 *
 * The decrement is by ew->reso, not by 1 -- a slowed ghost's script runs at
 * the same rate its animation does. */
void EneAlgCtrl(ENE_WRK *ew)                                            /* 474 */
{
    ENEALG_WRK *alg = &ew->alg;                                         /* 475 */

    if (alg->wait_time > 0.0f)                                          /* 478 */
    {
        alg->wait_time -= ew->reso;                                     /* 480 */

        if (alg->wait_time <= 0.0f)                                     /* 482 */
        {
            do
            {
                /* pos_no != 0 means the last handler asked to be resumed;
                 * do not consume another opcode byte for it. */
                if (alg->pos_no == 0)                                   /* 483 */
                {
                    alg->job_no = EaGetU8(alg);                         /* 484 */
                }

                if (alg->job_no < 0x70)                                 /* 488 */
                {
                    (*CommJmpContTbl[alg->job_no])(alg);                /* 489 */
                }
                else if ((u_char)(alg->job_no - 0x70) < 0x30)           /* 492 */
                {
                    (*CommJmpMoveTbl[alg->job_no - 0x70])(alg);         /* 493 */
                }
                else if ((u_char)(alg->job_no - 0xa0) < 0x40)           /* 496 */
                {
                    (*CommJmpBrnchTbl[alg->job_no - 0xa0])(alg);        /* 497 */
                }
                else if ((u_char)(alg->job_no - 0xe0) < 0x1f)           /* 500 */
                {
                    (*CommJmpEffTbl[alg->job_no - 0xe0])(alg);          /* 501 */
                }

                if (alg->wait_time == 255.0f)                           /* 504 */
                {
                    alg->pos_no    = 0;                                 /* 506 */
                    alg->wait_time = 0.0f;                              /* 507 */
                    break;
                }
            } while (alg->wait_time <= 0.0f);
        }
    }
}                                                                       /* 510 */

/* One step of the blink (secondary) script.  Same shape, one table, and no
 * range check on the opcode -- the blink scripts only ever use 0..6. */
void EneBlinkCtrl(ENE_WRK *ew)                                          /* 516 */
{
    ENEALG_WRK *alg = &ew->alg;                                         /* 517 */

    if (alg->bwait_time > 0.0f)                                         /* 521 */
    {
        alg->bwait_time -= ew->reso;                                    /* 523 */

        if (alg->bwait_time <= 0.0f)                                    /* 525 */
        {
            do
            {
                if (alg->bpos_no == 0)                                  /* 526 */
                {
                    alg->bjob_no = EaBGetU8(alg);                       /* 527 */
                }

                (*BCommJmpTbl[alg->bjob_no])(ew);                       /* 530 */

                if (alg->bwait_time == 255.0f)                          /* 531 */
                {
                    alg->bpos_no    = 0;                                /* 533 */
                    alg->bwait_time = 0.0f;                             /* 534 */
                    break;
                }
            } while (alg->bwait_time <= 0.0f);
        }
    }
}                                                                       /* 537 */

/* ==========================================================================
 *  0x00..0x6f -- control and state
 * ======================================================================== */

/* 0x00  wait <frames>.  Operand 0 means "one frame", not "no wait": pos_no
 * walks 0 -> 1 -> 0 for a real count and 0 -> 2 -> 0 for the degenerate one,
 * so the opcode always costs a frame.  A PAL count of zero would round to
 * zero frames and spin, hence the clamp to 1. */
void EJobC00(ENEALG_WRK *alg)                                           /* 551 */
{
    u_char time;

    switch (alg->pos_no)                                                /* 558 */
    {
    case 0:
        time = EaGetU8(alg);                                            /* 560 */
        if (time == 0)                                                  /* 562 */
        {
            alg->pos_no    = 2;                                         /* 563 */
            alg->wait_time = 1.0f;
        }
        else if (GetPALMode() != 0)                                     /* 565 */
        {
            time = (u_char)((float)time / 1.19999993f);                 /* 566 */
            if (time == 0) { time = 1; }                                /* 567 */
            alg->pos_no++;                                              /* 568 */
            alg->wait_time = (float)time;
        }
        else
        {
            alg->wait_time = (float)time;                               /* 571 */
            alg->pos_no++;                                              /* 574 */
        }
        break;

    case 1:
        alg->pos_no    = 0;                                             /* 577 */
        alg->wait_time = 0.0f;
        break;

    case 2:
        alg->wait_time = 1.0f;                                          /* 580 */
        break;

    default:
        break;
    }
}                                                                       /* 583 */

/* 0x01  jump <u16 offset>. */
void EJobC01(ENEALG_WRK *alg)                                           /* 592 */
{
    u_short adj = EaGetU16(alg);                                        /* 598 */

    alg->pos_no    = 0;
    alg->wait_time = 0.0f;
    EaJumpTo(alg, adj);                                                 /* 599 */
}

/* 0x02  end of action.  Drops every per-action status bit, raises the two the
 * rule reads back ("action finished" 0x800000 and "notice me" 0x8), marks the
 * action invalid and stops the interpreter with the 255.0 sentinel. */
void EJobC02(ENEALG_WRK *alg)                                           /* 606 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];

    ew->st.sta &= ~0x00080000L;                                         /* 613 */
    ew->st.sta &= ~0x00020000L;                                         /* 614 */
    ew->st.sta &= ~0x00040000L;                                         /* 615 */
    ew->st.sta &= ~0x04000000L;                                         /* 616 */
    ew->st.sta &= ~0x01000000L;                                         /* 617 */
    ew->st.sta &= ~0x00010000L;                                         /* 618 */
    ew->st.sta &= ~0x00001000L;                                         /* 619 */
    ew->st.sta &= ~0x00002000L;                                         /* 620 */
    ew->st.sta |=  0x00800008L;                                         /* 622 */

    ew->act_no = 0xff;                                                  /* 624 */

    alg->pos_no    = 0;                                                 /* 625 */
    alg->wait_time = 255.0f;
}

/* 0x03  release this ghost.  enemy.c's sweep picks the slot up next frame. */
void EJobC03(ENEALG_WRK *alg)                                           /* 632 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];

    ew->status = ENE_STATUS_RELEASE;                                    /* 632 */

    alg->pos_no    = 0;                                                 /* 639 */
    alg->wait_time = 0.0f;
}

/* 0x04  set / clear status 0x800000000 -- the ghost ignores the player. */
void EJobC04(ENEALG_WRK *alg)                                           /* 646 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];
    u_char   sw = EaGetU8(alg);                                         /* 650 */

    if (sw == 0)                                                        /* 656 */
    {
        ew->st.sta &= ~0x800000000L;
    }
    else
    {
        ew->st.sta |=  0x800000000L;                                    /* 657 */
    }

    alg->pos_no    = 0;                                                 /* 659 */
    alg->wait_time = 0.0f;                                              /* 661 */
}

/* 0x05  death sequence, one operand selecting the phase.
 *
 *   0  release the player / companion locks the phase-2 branch took
 *   1  hand the player back to the normal camera
 *   2  take the locks (only for attr 0x40000, the scripted "big" death)
 *   3  the kill itself: freeze the camera on the neck, start the parts
 *      deform, whiten the ghost's own light and play the death voice
 *
 * Only attr 0x40000 ghosts get the cinematic; everything else falls through
 * with just the deform and the voice line. */
void EJobC05(ENEALG_WRK *alg)                                           /* 669 */
{
    ENE_WRK *ew  = &ene_wrk[alg->idx];
    u_char   req = EaGetU8(alg);                                        /* 682 */

    switch (req)
    {
    case 0:                                                             /* 689 */
        ReqEneStop(0, 0);                                               /* 690 */
        if ((ew->attr & 0x40000) != 0)                                  /* 691 */
        {
            if (IsSisWrk() != 0) { SisterDrawUnlock(); }                /* 693 */
            PlayerUnlock();                                             /* 694 */
            PlayerDrawUnlock();
            SetIngameEneDead(0);                                        /* 696 */
        }
        break;

    case 1:                                                             /* 684 */
        PlayerChangeMode(6);                                            /* 687 */
        ReqEneStop(0, 0);
        break;

    case 2:                                                             /* 703 */
        if ((ew->attr & 0x40000) != 0)                                  /* 704 */
        {
            PlayerLock();                                               /* 705 */
            ReqEneStop(1, (u_char)(1 << alg->idx));
        }
        break;

    case 3:                                                             /* 711 */
        if ((ew->attr & 0x40000) != 0)                                  /* 712 */
        {
            float rv[4];
            float tv[4];
            float CamPos[4];

            if (IsSisWrk() != 0) { SisterDrawLock(); }                  /* 713 */

            /* Park the camera 750 units back along the line from the neck to
             * where the camera already is, and freeze it there. */
            _SetVector(tv, 0.0f, 0.0f, 750.0f, 0.0f);                   /* 717 */
            GetTrgtRot(ew->mpos.p0, gra3dGetCamera()->matCoord[3], rv, 3);
            RotFvector(rv, tv);
            sceVu0AddVector(CamPos, ew->mpos.p0, tv);                   /* 723 */
            /* The port declares the fin camera in the ROM's own mangled form,
             * float[3]; both arguments here are quadwords. */
            MapCamSetFinCamera(*(const float (*)[3])CamPos,
                               (const float (*)[3])&ew->mpos.p0);
            EffScreenEffectStatusSet(1);                                /* 727 */
        }

        EnemyDeadPDeformCall(ew);                                       /* 728 */
        SetEnemyParallelLight(ew, 1.0f, 1.0f, 1.0f, 1.0f);              /* 731 */

        if (ew->dat->dead_adpcm >= 0)                                   /* 732 */
        {
            SND_3D_SET set;

            memset(&set, 0, sizeof(set));                               /* 733 */
            set.pos = &ew->mbox.pos;                                    /* 734 */
            StreamAutoFadeOut(ew->stream_id, 10);                       /* 735 */
            ew->stream_id = StreamAutoPlay(ew->dat->dead_adpcm,
                                           ew->dat->dead_adpcm - 1,
                                           0x12, 0, 0, 0x3200, 0, &set); /* 737 */
        }

        if ((ew->attr & 0x40000) != 0)                                  /* 740 */
        {
            ReqEneStop(2, (u_char)(1 << alg->idx));                     /* 741 */
            PlayerDrawLock();
            SetIngameEneDead(1);
        }
        break;

    default:
        break;
    }

    alg->pos_no    = 0;                                                 /* 761 */
    alg->wait_time = 0.0f;                                              /* 767 */
}

/* 0x06  set / clear status 0x1000 -- the ghost is attacking. */
void EJobC06(ENEALG_WRK *alg)                                           /* 776 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];
    u_char   sw = EaGetU8(alg);                                         /* 780 */

    if (sw == 0)                                                        /* 786 */
    {
        ew->st.sta &= ~0x1000L;
    }
    else
    {
        ew->st.sta |=  0x1000L;                                         /* 787 */
    }

    alg->pos_no    = 0;                                                 /* 789 */
    alg->wait_time = 0.0f;                                              /* 791 */
}

/* 0x07  set / clear status 0x2000 -- the shutter-chance window.  Paired with
 * 0x80 (inside the ring) this is the fatal-frame shot. */
void EJobC07(ENEALG_WRK *alg)                                           /* 798 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];
    u_char   sw = EaGetU8(alg);                                         /* 802 */

    if (sw == 0)                                                        /* 809 */
    {
        ew->st.sta &= ~0x2000L;
    }
    else
    {
        ew->st.sta |=  0x2000L;                                         /* 810 */
    }

    alg->pos_no    = 0;                                                 /* 812 */
    alg->wait_time = 0.0f;                                              /* 822 */
}

/* 0x08  set / clear status 0x80000 -- the ghost cannot be knocked back. */
void EJobC08(ENEALG_WRK *alg)                                           /* 829 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];
    u_char   sw = EaGetU8(alg);                                         /* 833 */

    if (sw == 0)                                                        /* 838 */
    {
        ew->st.sta &= ~0x80000L;
    }
    else
    {
        ew->st.sta |=  0x80000L;                                        /* 839 */
    }

    alg->pos_no    = 0;                                                 /* 842 */
    alg->wait_time = 0.0f;                                              /* 844 */
}

/* 0x09  loop[0] = <u16 count>. */
void EJobC09(ENEALG_WRK *alg)                                           /* 855 */
{
    u_short tm = EaGetU16(alg);                                         /* 856 */

    if (GetPALMode() != 0)                                              /* 857 */
    {
        alg->loop[0] = (float)tm / 1.19999993f;
    }
    else
    {
        alg->loop[0] = (float)tm;                                       /* 859 */
    }

    alg->pos_no    = 0;                                                 /* 866 */
    alg->wait_time = 0.0f;
}

/* 0x0a  loop[<n>] = <u16 count>. */
void EJobC0A(ENEALG_WRK *alg)                                           /* 874 */
{
    u_char  n  = EaGetU8(alg);                                          /* 878 */
    u_short tm = EaGetU16(alg);                                         /* 879 */

    if (GetPALMode() != 0)                                              /* 880 */
    {
        alg->loop[n] = (float)tm / 1.19999993f;                         /* 881 */
    }
    else
    {
        alg->loop[n] = (float)tm;                                       /* 883 */
    }

    alg->pos_no    = 0;                                                 /* 890 */
    alg->wait_time = 0.0f;
}

/* 0x0b  loop[0] = <u16 min> + rnd(<u16 range>). */
void EJobC0B(ENEALG_WRK *alg)                                           /* 900 */
{
    u_short min = EaGetU16(alg);                                        /* 904 */
    u_short rng = EaGetU16(alg);                                        /* 905 */
    u_int   lng = 0;

    if (rng != 0)                                                       /* 910 */
    {
        lng = GetRndSP(0, rng) & 0xffff;                                /* 911 */
    }

    if (GetPALMode() != 0)                                              /* 913 */
    {
        alg->loop[0] = (float)(int)(min + lng) / 1.19999993f;           /* 914 */
    }
    else
    {
        alg->loop[0] = (float)(int)(min + lng);                         /* 916 */
    }

    alg->pos_no    = 0;                                                 /* 918 */
    alg->wait_time = 0.0f;
}

/* 0x0c  loop[<n>] = <u16 min> + rnd(<u16 range>). */
void EJobC0C(ENEALG_WRK *alg)                                           /* 929 */
{
    u_char  n   = EaGetU8(alg);                                         /* 937 */
    u_short min = EaGetU16(alg);                                        /* 938 */
    u_short rng = EaGetU16(alg);                                        /* 939 */
    u_int   lng = 0;

    if (rng != 0)                                                       /* 941 */
    {
        lng = GetRndSP(0, rng) & 0xffff;                                /* 942 */
    }

    if (GetPALMode() != 0)                                              /* 944 */
    {
        alg->loop[n] = (float)(int)(min + lng) / 1.19999993f;           /* 945 */
    }
    else
    {
        alg->loop[n] = (float)(int)(min + lng);                         /* 947 */
    }

    alg->pos_no    = 0;                                                 /* 949 */
    alg->wait_time = 0.0f;
}

/* 0x0d  set / clear status 0x40000 -- the ghost is dying. */
void EJobC0D(ENEALG_WRK *alg)                                           /* 956 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];
    u_char   sw = EaGetU8(alg);                                         /* 960 */

    if (sw == 0)                                                        /* 966 */
    {
        ew->st.sta &= ~0x40000L;
    }
    else
    {
        ew->st.sta |=  0x40000L;                                        /* 967 */
    }

    alg->pos_no    = 0;                                                 /* 970 */
    alg->wait_time = 0.0f;                                              /* 972 */
}

/* 0x0e  set / clear status 0x1000000 -- the ghost is hidden.  EneInDispChk()
 * skips the visibility test entirely while this is up. */
void EJobC0E(ENEALG_WRK *alg)                                           /* 979 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];
    u_char   sw = EaGetU8(alg);                                         /* 983 */

    if (sw == 0)                                                        /* 989 */
    {
        ew->st.sta &= ~0x1000000L;
    }
    else
    {
        ew->st.sta |=  0x1000000L;                                      /* 990 */
    }

    alg->pos_no    = 0;                                                 /* 993 */
    alg->wait_time = 0.0f;                                              /* 995 */
}

/* 0x0f  raise status 0x10000 (the ghost may be photographed for damage).
 * Clearing it also drops 0x1000, so ending the window ends the attack. */
void EJobC0F(ENEALG_WRK *alg)                                           /* 1002 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];
    u_char   sw = EaGetU8(alg);                                         /* 1006 */

    if (sw == 0)                                                        /* 1012 */
    {
        ew->st.sta &= ~0x11000L;
    }
    else
    {
        ew->st.sta |=  0x10000L;                                        /* 1013 */
    }

    alg->pos_no    = 0;                                                 /* 1015 */
    alg->wait_time = 0.0f;                                              /* 1017 */
}

/* 0x10  the attack lands.  Operand <id> selects the phase, and its top bit
 * overrides the attack pattern with an inline one:
 *
 *   bit 0x80 set -> bits 0x70 are the attack position (which reaction the
 *                   victim plays), bits 0x0f the phase; otherwise the pattern
 *                   comes from ENE_DAT::atk_ptn and the whole byte is the
 *                   phase.
 *
 *   phase 0  the grab: the victim is pinned, the ghost snapped to its front,
 *            and the reaction animation chosen from the relative bearing
 *   phase 1  nothing -- the ROM's ~95 lines here compile to no code at all
 *   phase 2  the ordinary hit; jumps to <adj> when it connects
 *
 * `flag` is the "the victim cannot be grabbed" test: dead / already reacting /
 * more than 200 units of height apart.  A phase-2 hit still lands, it just
 * does not take the player's controls away. */
void EJobC10(ENEALG_WRK *alg)                                           /* 1024 */
{
    /* Reaction animation, [bearing][attack position]; p is the player's set,
     * s the companion's. */
    u_char pani_tbl[3][2] = { { 0x32, 0x35 }, { 0x3c, 0x3e }, { 0x3d, 0x3f } }; /* 1048 */
    u_char sani_tbl[3][2] = { { 0x1c, 0x1f }, { 0x24, 0x26 }, { 0x25, 0x27 } }; /* 1053 */

    ENE_WRK   *ew  = &ene_wrk[alg->idx];
    PLCMN_WRK *pcw = ew->target;
    MOVE_BOX  *mb  = &ew->mbox;
    u_char     id;
    u_char     ani;
    u_char     flag;
    u_short    adj;
    int        n;
    float      tv[4];
    float      rv[4];
    float      rw[4];

    id = EaGetU8(alg);                                                  /* 1063 */
    alg->comm_add.pu8++;    /* one reserved operand byte, never read */ /* 1064 */
    adj = EaGetU16(alg);                                                /* 1065 */

    /* Invisibility (mercy) frames for whoever was hit -- much longer for the
     * companion, who has no way to break a grab herself. */
    if (ew->target_n == 1)                                              /* 1072 */
    {
        pcw->st.invisible_timer = 0x99;                                 /* 1073 */
    }
    else
    {
        pcw->st.invisible_timer = 0x21;                                 /* 1075 */
    }

    flag = 0;                                                           /* 1086 */

    ew->reso = 1.0f;                                                    /* 1079 */
    SetEneMahiClear(ew);                                                /* 1081 */
    SetEneSlowClear(ew);                                                /* 1083 */

    if (plyr_wrk.cmn_wrk.mode == 8 ||                                   /* 1087 */
        fabsf((pcw->mbox.pos[1] - mb->pos[1]) + ew->adjp[1]) > 200.0f ||
        (ew->target_n == 0 && plyr_wrk.cmn_wrk.mode == 3) ||
        (ew->target_n == 1 && sis_wrk.cmn_wrk.mode == 3))
    {
        flag = 1;                                                       /* 1091 */
    }

    ani = ew->dat->atk_ptn;                                             /* 1095 */
    if ((id & 0x80) != 0)                                               /* 1096 */
    {
        ani = (u_char)((id & 0x70) >> 4);                               /* 1099 */
        id  = (u_char)(id & 0x0f);                                      /* 1100 */
    }
    pcw->atk_pos = ani;

    switch (id)                                                         /* 1104 */
    {
    case 0:
        plyr_wrk.cmn_wrk.st.mvsta &= ~0xfL;                             /* 1110 */

        if (plyr_wrk.cmn_wrk.mode == 6)                                 /* 1112 */
        {
            if (ew->target_n == 0)
            {
                SetPlyrFinderQEnd();                                    /* 1113 */
                pcw->st.dmg_cam_flag = 0;                               /* 1114 */
            }
        }
        if (ew->target_n == 0)                                          /* 1116 */
        {
            SetIngameDamageMode(1);                                     /* 1117 */
            PlayerChangeMode(1);                                        /* 1118 */
            pcw->st.dmg_cam_flag = 0;
        }
        else
        {
            sis_wrk.cmn_wrk.mode = 1;                                   /* 1120 */
            pcw->st.dmg_cam_flag = 0;                                   /* 1123 */
        }

        pcw->atk_eneno = alg->idx;                                      /* 1124 */
        pcw->st.sta   &= ~0x20000L;                                     /* 1135 */
        pcw->st.sta   &= ~0x10000L;                                     /* 1136 */
        pcw->st.cond_tm = 0;                                            /* 1139 */
        pcw->st.dmg     = ew->dat->atk_h;                               /* 1142 */
        ew->atk_type    = 3;                                            /* 1143 */
        pcw->st.dmg_type = 3;
        pcw->atk_eneno  = alg->idx;                                     /* 1144 */

        /* Which way the ghost is standing relative to the victim decides the
         * reaction: 0 in front, 1 to the left, 2 to the right. */
        rv[1] = GetTrgtRotY(pcw->mbox.pos, mb->pos) - pcw->mbox.rot[1]; /* 1148 */
        RotLimitChk(&rv[1]);                                            /* 1149 */
        if (rv[1] > 1.57079625f)                                        /* 1150 */
        {
            pcw->atk_rot = 1;                                           /* 1151 */
            n = 1;
        }
        else if (rv[1] < -1.57079625f)                                  /* 1153 */
        {
            pcw->atk_rot = 2;                                           /* 1154 */
            n = 2;
        }
        else
        {
            pcw->atk_rot = 0;                                           /* 1156 */
            n = 0;
        }

        if (ew->target_n == 0)                                          /* 1158 */
        {
            SetPlyrAnime(pani_tbl[n][pcw->atk_pos], 5);                 /* 1159 */
        }
        else
        {
            SetSisterAnime(sani_tbl[n][pcw->atk_pos], 5);               /* 1161 */
            ReqSisBankPlay(0, 1, 1, 0, &sis_wrk.s3d);                   /* 1162 */
        }

        ew->atk_tm = (u_short)(ew->dat->atk_tm * 16 - ew->dat->atk_tm); /* 1166 */
        pcw->st.dwalk_tm = 45;                                          /* 1167 */

        /* Snap the ghost to hit_rng in front of the victim, then slide it
         * sideways by hit_adjx so the two models interlock. */
        sceVu0AddVector(tv, pcw->mbox.pos, ew->adjp);                   /* 1170 */
        GetTrgtRot(tv, mb->pos, rw, 3);                                 /* 1171 */
        g3dxVu0CopyVector(rv, pcw->mbox.rot);
        rv[1] = rw[1];                                                  /* 1173 */
        _SetVector(tv, 0.0f, ew->adjp[1], ew->dat->hit_rng, 0.0f);      /* 1175 */
        RotFvector(rv, tv);                                             /* 1176 */
        sceVu0AddVector(mb->pos, pcw->mbox.pos, tv);                    /* 1177 */
        GetTrgtRot(mb->pos, pcw->mbox.pos, mb->rot, 2);                 /* 1180 */
        _SetVector(tv, (float)ew->dat->hit_adjx, 0.0f, 0.0f, 0.0f);     /* 1183 */
        RotFvector(mb->rot, tv);                                        /* 1184 */
        sceVu0AddVector(mb->pos, mb->pos, tv);                          /* 1185 */
        mb->pos[1] = pcw->mbox.pos[1] + ew->adjp[1];                    /* 1188 */
        break;                                                          /* 1189 */

    case 1:
        /* Empty.  The ROM has roughly ninety-five lines here that produce no
         * instructions at all -- commented out before this build. */
        break;

    case 2:
        if (plyr_wrk.cmn_wrk.mode == 6)                                 /* 1286 */
        {
            if (ew->target_n == 0) { SetPlyrFinderQEnd(); }             /* 1287 */
        }
        pcw->st.cond_tm  = 0;                                           /* 1291 */
        pcw->st.dmg      = ew->dat->atk_p;                              /* 1294 */
        ew->atk_type     = 2;                                           /* 1302 */
        pcw->st.dmg_type = 2;
        pcw->st.dmg_cam_flag = 0;                                       /* 1303 */
        pcw->atk_eneno   = alg->idx;                                    /* 1304 */

        if (ew->target_n == 0)                                          /* 1305 */
        {
            SetIngameDamageMode(1);                                     /* 1306 */
            PlayerChangeMode(2);                                        /* 1307 */
        }
        else
        {
            sis_wrk.cmn_wrk.mode = 2;                                   /* 1309 */
        }

        ew->st.sta |= 0x8000L;                                          /* 1312 */
        GetTrgtRot(mb->pos, pcw->mbox.pos, mb->rot, 2);                 /* 1313 */

        if (flag == 0)                                                  /* 1315 */
        {
            ew->atk_type     = 1;                                       /* 1316 */
            pcw->st.dmg_type = 1;
            plyr_wrk.cmn_wrk.st.mvsta &= ~0xfL;                         /* 1317 */

            GetTrgtRot(pcw->mbox.pos, mb->pos, rv, 2);                  /* 1319 */
            rv[1] -= pcw->mbox.rot[1];                                  /* 1320 */
            RotLimitChk(&rv[1]);                                        /* 1321 */

            if (ConvertRot2Dir(rv[1], 2) == 0)                          /* 1322 */
            {
                if (ew->target_n == 0)                                  /* 1323 */
                {
                    SetPlyrAnime(0x38, 5);                              /* 1324 */
                }
                else
                {
                    SetSisterAnime(0x22, 5);                            /* 1326 */
                }
            }
            else
            {
                if (ew->target_n == 0)                                  /* 1329 */
                {
                    SetPlyrAnime(0x39, 5);                              /* 1330 */
                }
                else
                {
                    SetSisterAnime(0x23, 5);                            /* 1332 */
                }
            }
        }

        EaJumpTo(alg, adj);                                             /* 1339 */
        break;

    default:
        break;
    }

    alg->pos_no    = 0;                                                 /* 1385 */
    alg->wait_time = 0.0f;
}

/* 0x11  jump when the player is inside the hit box.  Operands: u16 range
 * (0 = ENE_DAT::hit_rng), u8 half-angle in degrees, u16 jump.  The extra
 * condition over 0x12 is that the victim must not still be invulnerable. */
void EJobC11(ENEALG_WRK *alg)                                           /* 1400 */
{
    ENE_WRK   *ew  = &ene_wrk[alg->idx];
    PLCMN_WRK *pcw = ew->target;
    MOVE_BOX  *mb  = &ew->mbox;
    u_short    id;
    u_short    adj;
    float      hit_rng;
    float      rot;
    float      hit_rot;

    id      = EaGetU16(alg);                                            /* 1408 */
    hit_rot = ((float)EaGetU8(alg) * 3.1415925f) / 180.0f;              /* 1409 */
    adj     = EaGetU16(alg);                                            /* 1411 */

    RotLimitChk(&hit_rot);                                              /* 1413 */

    hit_rng = (id == 0) ? ew->dat->hit_rng : (float)id;                 /* 1414 */

    rot = GetTrgtRotY(mb->pos, pcw->mbox.pos) - mb->rot[1];             /* 1415 */
    RotLimitChk(&rot);

    if (GetDistV(pcw->mbox.pos, mb->pos) <= hit_rng &&                  /* 1421 */
        fabsf((pcw->mbox.pos[1] - mb->pos[1]) + ew->adjp[1]) <= 300.0f &&
        fabsf(rot) <= hit_rot &&
        pcw->st.invisible_timer == 0)                                   /* 1425 */
    {
        EaJumpTo(alg, adj);                                             /* 1426 */
        ew->st.sta |= 0x40000000000L;                                   /* 1428 */
    }

    alg->pos_no    = 0;                                                 /* 1456 */
    alg->wait_time = 0.0f;                                              /* 1462 */
}

/* 0x12  the same hit-box test, but it ignores the victim's invulnerability
 * and re-arms it instead -- this is the opcode a grab uses. */
void EJobC12(ENEALG_WRK *alg)                                           /* 1472 */
{
    ENE_WRK   *ew  = &ene_wrk[alg->idx];
    PLCMN_WRK *pcw = ew->target;
    MOVE_BOX  *mb  = &ew->mbox;
    u_short    id;
    u_short    adj;
    float      hit_rng;
    float      rot;
    float      hit_rot;

    id      = EaGetU16(alg);                                            /* 1480 */
    hit_rot = ((float)EaGetU8(alg) * 3.1415925f) / 180.0f;              /* 1481 */
    adj     = EaGetU16(alg);                                            /* 1483 */

    RotLimitChk(&hit_rot);                                              /* 1485 */

    hit_rng = (id == 0) ? ew->dat->hit_rng : (float)id;                 /* 1486 */

    rot = GetTrgtRotY(mb->pos, pcw->mbox.pos) - mb->rot[1];             /* 1487 */
    RotLimitChk(&rot);

    if (GetDistV(pcw->mbox.pos, mb->pos) <= hit_rng &&                  /* 1493 */
        fabsf((pcw->mbox.pos[1] - mb->pos[1]) + ew->adjp[1]) <= 300.0f &&
        fabsf(rot) <= hit_rot)
    {
        EaJumpTo(alg, adj);                                             /* 1497 */
        pcw->st.invisible_timer = 0x21;                                 /* 1498 */
        ew->st.sta |= 0x40000000000L;                                   /* 1500 */
    }

    alg->pos_no    = 0;                                                 /* 1529 */
    alg->wait_time = 0.0f;                                              /* 1536 */
}

/* 0x13  drag the held victim round to face the ghost over 31 frames, then let
 * go and re-seat the ghost in front of them.  Runs only while the ghost still
 * has attack frames left or the victim is still flagged as grabbed. */
void EJobC13(ENEALG_WRK *alg)                                           /* 1546 */
{
    static float time[10];                                              /* bss 478750 */
    static float trot[10][4];                                           /* bss 478780 */

    ENE_WRK   *ew   = &ene_wrk[alg->idx];
    MOVE_BOX  *mb   = &ew->mbox;
    PLCMN_WRK *pcmw = ew->target;
    int        no   = alg->idx;
    float      f;
    float      tv[4];

    if (ew->atk_tm == 0 && (pcmw->st.sta & 0x10000) == 0)               /* 1548 */
    {
        alg->pos_no    = 0;                                             /* 1550 */
        alg->wait_time = 0.0f;
    }
    else if (alg->pos_no == 0)                                          /* 1566 */
    {
        sceVu0AddVector(tv, pcmw->mbox.pos, ew->adjp);                  /* 1567 */
        GetTrgtRot(tv, mb->pos, trot[no], 3);                           /* 1569 */
        time[no]    = 0.0f;                                             /* 1570 */
        trot[no][3] = pcmw->mbox.rot[1];                                /* 1571 */
        alg->wait_time = 1.0f;                                          /* 1572 */
        alg->pos_no++;                                                  /* 1573 */
    }
    else if (alg->pos_no == 1)                                          /* 1574 */
    {
        if (time[no] < 31.0f)                                           /* 1577 */
        {
            f = trot[no][1] - trot[no][3];                              /* 1578 */
            RotLimitChk(&f);                                            /* 1579 */
            pcmw->mbox.rot[1] = (f * time[no]) / 31.0f + trot[no][3];   /* 1580 */
            RotLimitChk(&pcmw->mbox.rot[1]);                            /* 1581 */
            time[no] += 1.0f;                                           /* 1583 */
            alg->wait_time = 1.0f;                                      /* 1584 */
        }
        else
        {
            pcmw->mbox.rot[1] = trot[no][1];                            /* 1586 */
            _SetVector(tv, 0.0f, ew->adjp[1], ew->dat->hit_rng, 0.0f);  /* 1588 */
            RotFvector(pcmw->mbox.rot, tv);                             /* 1589 */
            sceVu0AddVector(mb->pos, pcmw->mbox.pos, tv);               /* 1590 */
            GetTrgtRot(mb->pos, pcmw->mbox.pos, mb->rot, 2);            /* 1593 */
            _SetVector(tv, (float)ew->dat->hit_adjx, 0.0f, 0.0f, 0.0f); /* 1596 */
            RotFvector(mb->rot, tv);                                    /* 1597 */
            sceVu0AddVector(mb->pos, mb->pos, tv);                      /* 1598 */
            mb->pos[1] = pcmw->mbox.pos[1] + ew->adjp[1];               /* 1601 */
            alg->wait_time = 0.0f;                                      /* 1602 */
            alg->pos_no    = 0;                                         /* 1604 */
        }
    }
}                                                                       /* 1608 */

/* 0x14  skips its two operand bytes and does nothing else.  Whatever this
 * used to do (fifty lines of it) was commented out before this build. */
void EJobC14(ENEALG_WRK *alg)                                           /* 1620 */
{
    alg->comm_add.pu8 += 2;                                             /* 1621 */

    alg->pos_no    = 0;                                                 /* 1671 */
    alg->wait_time = 0.0f;
}

/* 0x15  set / clear status 0x20000000000 -- the ghost is unphotographable. */
void EJobC15(ENEALG_WRK *alg)                                           /* 1678 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];
    u_char   sw = EaGetU8(alg);                                         /* 1682 */

    if (sw == 0)                                                        /* 1689 */
    {
        ew->st.sta &= ~0x20000000000L;
    }
    else
    {
        ew->st.sta |=  0x20000000000L;                                  /* 1690 */
    }

    alg->pos_no    = 0;                                                 /* 1692 */
    alg->wait_time = 0.0f;                                              /* 1728 */
}

/* 0x16  play the damage reaction for whatever kind of hit just landed.
 * dmg_type 1 is the ordinary shot, 2 a strong one, 3 the fatal frame, 4 the
 * zero shot; each picks its own animation, camera sound and voice bank. */
void EJobC16(ENEALG_WRK *alg)                                           /* 1735 */
{
    ENE_WRK *ew   = &ene_wrk[alg->idx];
    int      no   = 0;                                                  /* 1737 */
    int      bank;

    switch (ew->st.dmg_type)                                            /* 1745 */
    {
    case 4:
        no = 0x0b;                                                      /* 1747 */
        FinderBankPlay(0x0c, 1, 1, 0, NULL, 0x3200, 0x1000);            /* 1748 */
        SndBankPlay(ew->se_bank_no, 1, 0, 0, 0x3200, 0x1000, 0, NULL);  /* 1750 */
        break;

    case 3:
        no   = 0x0c;                                                    /* 1752 */
        FinderBankPlay(0x0c, 1, 1, 0, NULL, 0x3200, 0x1000);            /* 1753 */
        bank = 2;
        SndBankPlay(ew->se_bank_no, bank, 0, 0, 0x3200, 0x1000, 0, NULL); /* 1755 */
        break;

    case 2:
        no   = 0x0a;                                                    /* 1757 */
        FinderBankPlay(1, 1, 1, 0, NULL, 0x3200, 0x1000);               /* 1758 */
        bank = 0;
        SndBankPlay(ew->se_bank_no, bank, 0, 0, 0x3200, 0x1000, 0, NULL); /* 1760 */
        break;

    case 1:
        FinderBankPlay(0, 1, 1, 0, NULL, 0x3200, 0x1000);               /* 1761 */
        alg->wait_time = 0.0f;                                          /* 1762 */
        alg->pos_no    = 0;
        return;                                                         /* 1763 */

    default:
        break;
    }

    ew->ani_reso = motGetMotReso();                                     /* 1792 */
    ew->st.sta  &= ~0x380000000L;                                       /* 1793 */
    ReqAnm(ew->ani_ctrl_p, 4, ew->cmn_dat->anm_no, no);                 /* 1798 */

    alg->pos_no    = 0;                                                 /* 1805 */
    alg->wait_time = 0.0f;                                              /* 1806 */
}

/* 0x17  set / clear status 0x20000 -- the ghost is holding the victim. */
void EJobC17(ENEALG_WRK *alg)                                           /* 1812 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];
    u_char   sw = EaGetU8(alg);                                         /* 1816 */

    if (sw == 0)                                                        /* 1822 */
    {
        ew->st.sta &= ~0x20000L;
    }
    else
    {
        ew->st.sta |=  0x20000L;                                        /* 1823 */
    }

    alg->pos_no    = 0;                                                 /* 1826 */
    alg->wait_time = 0.0f;                                              /* 1828 */
}

/* 0x18  fade the ghost out over <u16 frames>, with the vanish sound and the
 * "fading" status bit.  The parts deform drive values ride the same ramp. */
void EJobC18(ENEALG_WRK *alg)                                           /* 1835 */
{
    static float time[10];                                              /* bss 478820 */
    static float max[10];                                               /* bss 478848 */

    ENE_WRK *ew = &ene_wrk[alg->idx];
    u_char   no = alg->idx;                                             /* 1837 */

    if (alg->pos_no == 0)                                               /* 1845 */
    {
        alg->loop[0] = (float)EaGetU16(alg);                            /* 1847 */
        time[no]     = alg->loop[0];                                    /* 1848 */
        max[no]      = (float)ew->tr_rate_alg;                          /* 1849 */
        FinderBankPlay(6, 1, 1, 0, NULL, 0x3200, 0x1000);               /* 1850 */
        ew->st.sta |= 0x4000000L;                                       /* 1851 */
        alg->wait_time = 1.0f;                                          /* 1852 */
        alg->pos_no++;
    }
    else if (alg->pos_no == 1)                                          /* 1856 */
    {
        if (alg->loop[0] > 0.0f)                                        /* 1859 */
        {
            alg->loop[0] -= 1.0f;                                       /* 1860 */
            ew->tr_rate_alg = (u_char)((max[no] * alg->loop[0]) / time[no]); /* 1861 */
            ew->d_pda  = (ew->d_mpd  * alg->loop[0]) / time[no];        /* 1862 */
            ew->d_pda2 = (ew->d_mpd2 * alg->loop[0]) / time[no];        /* 1863 */
            alg->wait_time = 1.0f;                                      /* 1864 */
        }
        else
        {
            alg->loop[0] = 0.0f;                                        /* 1866 */
            ew->tr_rate_alg = 0;                                        /* 1867 */
            ew->d_pda  = 0.0f;                                          /* 1868 */
            ew->d_pda2 = 0.0f;                                          /* 1869 */
            ew->st.sta &= ~0x4000000L;                                  /* 1870 */
            alg->wait_time = 0.0f;                                      /* 1871 */
            alg->pos_no    = 0;
        }
    }
}                                                                       /* 1914 */

/* 0x19  fade the ghost in to <u16 max> over <u16 frames>, same sound and
 * status bit as 0x18. */
void EJobC19(ENEALG_WRK *alg)                                           /* 1920 */
{
    static float time[10];                                              /* bss 478870 */
    static float max[10];                                               /* bss 478898 */

    ENE_WRK *ew = &ene_wrk[alg->idx];
    u_char   no = alg->idx;                                             /* 1922 */

    if (alg->pos_no == 0)                                               /* 1930 */
    {
        alg->loop[0] = (float)EaGetU16(alg);                            /* 1932 */
        max[no]      = (float)EaGetU16(alg);                            /* 1933 */
        time[no]     = alg->loop[0];                                    /* 1934 */
        FinderBankPlay(6, 1, 1, 0, NULL, 0x3200, 0x1000);               /* 1935 */
        ew->st.sta |= 0x4000000L;                                       /* 1936 */
        alg->wait_time = 1.0f;                                          /* 1937 */
        alg->pos_no++;
    }
    else if (alg->pos_no == 1)                                          /* 1941 */
    {
        if (alg->loop[0] > 0.0f)                                        /* 1944 */
        {
            alg->loop[0] -= 1.0f;                                       /* 1945 */
            ew->tr_rate_alg =
                (u_char)(max[no] - (max[no] * alg->loop[0]) / time[no]); /* 1946 */
            ew->d_pda  = ew->d_mpd  - (ew->d_mpd  * alg->loop[0]) / time[no]; /* 1947 */
            ew->d_pda2 = ew->d_mpd2 - (ew->d_mpd2 * alg->loop[0]) / time[no]; /* 1948 */
            alg->wait_time = 1.0f;                                      /* 1949 */
        }
        else
        {
            alg->loop[0] = 0.0f;                                        /* 1952 */
            ew->tr_rate_alg = (u_char)max[no];                          /* 1953 */
            ew->d_pda  = ew->d_mpd;                                     /* 1954 */
            ew->d_pda2 = ew->d_mpd2;                                    /* 1955 */
            ew->st.sta &= ~0x4000000L;                                  /* 1956 */
            alg->wait_time = 0.0f;
            alg->pos_no    = 0;
        }
    }
}                                                                       /* 1999 */

/* 0x1a  play animation <u8 no> with blend <u16 frames>. */
void EJobC1A(ENEALG_WRK *alg)                                           /* 2005 */
{
    ENE_WRK *ew  = &ene_wrk[alg->idx];
    u_char   no  = EaGetU8(alg);                                        /* 2010 */
    u_short  blk = EaGetU16(alg);                                       /* 2011 */

    ew->anime_no = no;                                                  /* 2017 */
    ew->ani_reso = motGetMotReso();                                     /* 2018 */
    ew->st.sta  &= ~0x780000000L;                                       /* 2020 */
    ReqAnm(ew->ani_ctrl_p, blk, ew->cmn_dat->anm_no, no);               /* 2025 */

    alg->pos_no    = 0;                                                 /* 2033 */
    alg->wait_time = 0.0f;
}

/* 0x1b  set the script's transparency term outright. */
void EJobC1B(ENEALG_WRK *alg)                                           /* 2040 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];

    ew->tr_rate_alg = EaGetU8(alg);                                     /* 2044 */

    alg->pos_no    = 0;                                                 /* 2050 */
    alg->wait_time = 0.0f;                                              /* 2052 */
}

/* 0x1c  the same for the special (aura-side) transparency term. */
void EJobC1C(ENEALG_WRK *alg)                                           /* 2059 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];

    ew->tr_rate_alg_sp = EaGetU8(alg);                                  /* 2063 */

    alg->pos_no    = 0;                                                 /* 2069 */
    alg->wait_time = 0.0f;                                              /* 2070 */
}

/* 0x1d  set / clear status 0x4000000 -- the ghost is fading. */
void EJobC1D(ENEALG_WRK *alg)                                           /* 2077 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];
    u_char   sw = EaGetU8(alg);                                         /* 2081 */

    if (sw == 0)                                                        /* 2087 */
    {
        ew->st.sta &= ~0x4000000L;
    }
    else
    {
        ew->st.sta |=  0x4000000L;                                      /* 2088 */
    }

    alg->pos_no    = 0;                                                 /* 2091 */
    alg->wait_time = 0.0f;                                              /* 2093 */
}

/* 0x1e  set / clear status 0x8000000 -- the ghost blinks in and out. */
void EJobC1E(ENEALG_WRK *alg)                                           /* 2101 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];
    u_char   sw = EaGetU8(alg);                                         /* 2105 */

    if (sw == 0)                                                        /* 2111 */
    {
        ew->st.sta &= ~0x8000000L;
    }
    else
    {
        ew->st.sta |=  0x8000000L;                                      /* 2112 */
    }

    alg->pos_no    = 0;                                                 /* 2115 */
    alg->wait_time = 0.0f;                                              /* 2117 */
}

/* 0x1f  blink on, with a period and a peak alpha, each a base plus a random
 * spread: <sw> <u8 time> <u8 time range> <u8 max> <u8 max range>.  sw 0 just
 * turns the blink off and ignores the four operands it has already eaten. */
void EJobC1F(ENEALG_WRK *alg)                                           /* 2128 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];
    u_char   sw;
    u_short  time;
    u_char   trng;
    u_short  max;
    u_char   mrng;

    sw   = EaGetU8(alg);                                                /* 2132 */
    time = EaGetU8(alg);                                                /* 2133 */
    trng = EaGetU8(alg);                                                /* 2134 */
    max  = EaGetU8(alg);                                                /* 2135 */
    mrng = EaGetU8(alg);                                                /* 2136 */

    if (sw == 0)                                                        /* 2142 */
    {
        ew->st.sta &= ~0x8000000L;                                      /* 2143 */
    }
    else
    {
        ew->st.sta |= 0x8000000L;                                       /* 2144 */

        if (trng != 0)                                                  /* 2145 */
        {
            time = (u_short)(GetRndSP(time, trng) & 0xffff);
        }
        ew->tr_time = (short)time;                                      /* 2148 */

        if (mrng != 0)                                                  /* 2149 */
        {
            max = (u_char)GetRndSP(max, mrng);
        }
        ew->tr_max = (u_char)max;                                       /* 2151 */
    }

    alg->pos_no    = 0;                                                 /* 2154 */
    alg->wait_time = 0.0f;                                              /* 2156 */
}

/* 0x20  fade out over <u16 frames>, silently -- no sound, no status bit. */
void EJobC20(ENEALG_WRK *alg)                                           /* 2164 */
{
    static float time[10];                                              /* bss 4788c0 */
    static float max[10];                                               /* bss 4788e8 */

    ENE_WRK *ew = &ene_wrk[alg->idx];
    u_char   no = alg->idx;                                             /* 2165 */

    if (alg->pos_no == 0)                                               /* 2172 */
    {
        alg->loop[0] = (float)EaGetU16(alg);                            /* 2174 */
        time[no]     = alg->loop[0];                                    /* 2175 */
        max[no]      = (float)ew->tr_rate_alg;                          /* 2176 */
        alg->pos_no++;                                                  /* 2177 */
    }
    else if (alg->pos_no != 1)
    {
        return;
    }

    if (alg->loop[0] > 0.0f)                                            /* 2186 */
    {
        alg->loop[0] -= 1.0f;                                           /* 2187 */
        ew->tr_rate_alg = (u_char)((max[no] * alg->loop[0]) / time[no]); /* 2188 */
        ew->d_pda  = (ew->d_mpd  * alg->loop[0]) / time[no];            /* 2189 */
        ew->d_pda2 = (ew->d_mpd2 * alg->loop[0]) / time[no];            /* 2190 */
        alg->wait_time = 1.0f;                                          /* 2191 */
    }
    else
    {
        ew->tr_rate_alg = 0;                                            /* 2193 */
        ew->d_pda  = 0.0f;                                              /* 2194 */
        ew->d_pda2 = 0.0f;                                              /* 2195 */
        alg->wait_time = 0.0f;                                          /* 2196 */
        alg->pos_no    = 0;                                             /* 2198 */
    }
}                                                                       /* 2201 */

/* 0x21  fade in to <u16 max> over <u16 frames>, silently. */
void EJobC21(ENEALG_WRK *alg)                                           /* 2207 */
{
    static float max[10];                                               /* bss 478910 */
    static float loop[10];                                              /* bss 478938 */

    ENE_WRK *ew = &ene_wrk[alg->idx];
    u_char   no = alg->idx;                                             /* 2208 */

    if (alg->pos_no == 0)                                               /* 2215 */
    {
        alg->loop[0] = (float)EaGetU16(alg);                            /* 2217 */
        max[no]      = (float)EaGetU16(alg);                            /* 2218 */
        loop[no]     = alg->loop[0];                                    /* 2219 */
        alg->pos_no++;                                                  /* 2220 */
    }
    else if (alg->pos_no != 1)
    {
        return;
    }

    if (alg->loop[0] > 0.0f)                                            /* 2229 */
    {
        alg->loop[0] -= 1.0f;                                           /* 2230 */
        ew->tr_rate_alg =
            (u_char)(max[no] - (max[no] * alg->loop[0]) / loop[no]);    /* 2231 */
        ew->d_pda  = ew->d_mpd  - (ew->d_mpd  * alg->loop[0]) / loop[no]; /* 2232 */
        ew->d_pda2 = ew->d_mpd2 - (ew->d_mpd2 * alg->loop[0]) / loop[no]; /* 2233 */
        alg->wait_time = 1.0f;                                          /* 2234 */
    }
    else
    {
        ew->tr_rate_alg = (u_char)max[no];                              /* 2236 */
        ew->d_pda  = ew->d_mpd;                                         /* 2237 */
        ew->d_pda2 = ew->d_mpd2;                                        /* 2238 */
        alg->pos_no    = 0;                                             /* 2239 */
        alg->wait_time = 0.0f;
    }
}                                                                       /* 2244 */

/* 0x22  slow this ghost down for <u16 frames> at 0.3x. */
void EJobC22(ENEALG_WRK *alg)                                           /* 2250 */
{
    ENE_WRK *ew  = &ene_wrk[alg->idx];
    u_short  sec = EaGetU16(alg);                                       /* 2254 */

    if (GetPALMode() != 0)                                              /* 2259 */
    {
        SetEneSlow(ew, (int)((float)sec / 1.19999993f) & 0xffff, 0.299999982f); /* 2260 */
    }
    else
    {
        SetEneSlow(ew, sec, 0.299999982f);                              /* 2262 */
    }

    alg->pos_no    = 0;
    alg->wait_time = 0.0f;                                              /* 2265 */
}

/* 0x23  call <u16 offset>: push the return cursor and jump. */
void EJobC23(ENEALG_WRK *alg)                                           /* 2275 */
{
    u_short adj = EaGetU16(alg);                                        /* 2282 */

    *(P_INT *)alg->stack_p = alg->comm_add;                             /* 2283 */
    alg->stack_p++;

    alg->pos_no    = 0;                                                 /* 2284 */
    alg->wait_time = 0.0f;
    EaJumpTo(alg, adj);
}

/* 0x24  return.  Underflow is an assert, not a fault -- the cursor is left
 * where it was and the script runs on. */
void EJobC24(ENEALG_WRK *alg)                                           /* 2296 */
{
    if (alg->stack_p > alg->stack_b)                                    /* 2297 */
    {
        alg->stack_p--;
        alg->comm_add = *(P_INT *)alg->stack_p;
    }
    else
    {
        PRINT_ASSERT("AlgoStackBuffer is Empty\n");                     /* 2299 */
    }

    alg->pos_no    = 0;                                                 /* 2301 */
    alg->wait_time = 0.0f;
}

/* 0x25  set the interpreter's user flag.  0x26..0x28 read it back. */
void EJobC25(ENEALG_WRK *alg)                                           /* 2308 */
{
    alg->flag = EaGetU8(alg);                                           /* 2312 */

    alg->pos_no    = 0;                                                 /* 2317 */
    alg->wait_time = 0.0f;                                              /* 2319 */
}

/* 0x26  restart on action 1 (idle) and mark the ghost as ignoring the player. */
void EJobC26(ENEALG_WRK *alg)                                           /* 2326 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];

    EneActSet(ew, 1);                                                   /* 2333 */
    ew->st.sta |= 0x800000000L;                                         /* 2334 */

    alg->pos_no    = 0;                                                 /* 2336 */
    alg->wait_time = 0.0f;
}

/* 0x27  the same, restarting on action 3 (the attack run). */
void EJobC27(ENEALG_WRK *alg)                                           /* 2343 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];

    EneActSet(ew, 3);                                                   /* 2350 */
    ew->st.sta |= 0x800000000L;                                         /* 2351 */

    alg->pos_no    = 0;                                                 /* 2353 */
    alg->wait_time = 0.0f;
}

/* 0x28  compare-and-set the user flag: if it equals <u16>, make it <u8>. */
void EJobC28(ENEALG_WRK *alg)                                           /* 2360 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];
    u_short  cmp = EaGetU16(alg);                                       /* 2364 */
    u_char   set = (u_char)EaGetU16(alg);                               /* 2365 */

    if (ew->alg.flag == cmp)
    {
        ew->alg.flag = set;
    }

    alg->pos_no    = 0;                                                 /* 2371 */
    alg->wait_time = 0.0f;                                              /* 2373 */
}

/* 0x29  loop[0] = this passive ghost's scripted lifetime (AENE_DAT::time).
 * A negative time means "no limit" and leaves the counter alone. */
void EJobC29(ENEALG_WRK *alg)                                           /* 2381 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];

    if (ew->aie->time >= 0)                                             /* 2388 */
    {
        if (GetPALMode() != 0)                                          /* 2390 */
        {
            alg->loop[0] = (float)ew->aie->time / 1.19999993f;          /* 2391 */
        }
        else
        {
            alg->loop[0] = (float)ew->aie->time;                        /* 2393 */
        }
    }

    alg->pos_no    = 0;
    alg->wait_time = 0.0f;                                              /* 2399 */
}

/* 0x2a  put the companion into (1) or out of (0) her scripted-motion mode. */
void EJobC2A(ENEALG_WRK *alg)                                           /* 2409 */
{
    u_char sw = EaGetU8(alg);                                           /* 2415 */

    if (sw == 0)                                                        /* 2417 */
    {
        ReqModeSisMotion(0);                                            /* 2418 */
    }
    else if (sw == 1)
    {
        ReqModeSisMotion(1);                                            /* 2421 */
    }

    alg->pos_no    = 0;
    alg->wait_time = 0.0f;                                              /* 2425 */
}

/* 0x2b  fade the in-finder transparency term out over <u16 frames>.
 * 0x2b..0x2e are one family: {in, out} x {fade down, fade up}, each with its
 * own loop_tr counter so a ghost can be running both at once. */
void EJobC2B(ENEALG_WRK *alg)                                           /* 2433 */
{
    static float time[10];                                              /* bss 478960 */
    static float max[10];                                               /* bss 478988 */

    ENE_WRK *ew = &ene_wrk[alg->idx];
    u_char   no = alg->idx;                                             /* 2434 */

    if (alg->pos_no == 0)                                               /* 2441 */
    {
        alg->loop_tr[0] = (float)EaGetU16(alg);                         /* 2443 */
        time[no]        = alg->loop_tr[0];                              /* 2444 */
        max[no]         = (float)ew->tr_rate_in;                        /* 2445 */
        alg->pos_no++;                                                  /* 2446 */
    }
    else if (alg->pos_no != 1)
    {
        return;
    }

    if (alg->loop_tr[0] > 0.0f)                                         /* 2455 */
    {
        alg->loop_tr[0] -= 1.0f;                                        /* 2456 */
        ew->tr_rate_in = (u_char)((max[no] * alg->loop_tr[0]) / time[no]); /* 2459 */
        alg->wait_time = 1.0f;                                          /* 2460 */
    }
    else
    {
        alg->wait_time = 0.0f;                                          /* 2461 */
        alg->pos_no    = 0;
    }
}                                                                       /* 2466 */

/* 0x2c  fade the in-finder term up to <u16 max> over <u16 frames>. */
void EJobC2C(ENEALG_WRK *alg)                                           /* 2472 */
{
    static float max[10];                                               /* bss 4789b0 */
    static float loop[10];                                              /* bss 4789d8 */

    ENE_WRK *ew = &ene_wrk[alg->idx];
    u_char   no = alg->idx;                                             /* 2473 */

    if (alg->pos_no == 0)                                               /* 2480 */
    {
        alg->loop_tr[0] = (float)EaGetU16(alg);                         /* 2482 */
        max[no]         = (float)EaGetU16(alg);                         /* 2483 */
        loop[no]        = alg->loop_tr[0];                              /* 2484 */
        alg->pos_no++;                                                  /* 2485 */
    }
    else if (alg->pos_no != 1)
    {
        return;
    }

    if (alg->loop_tr[0] > 0.0f)                                         /* 2494 */
    {
        alg->loop_tr[0] -= 1.0f;                                        /* 2495 */
        ew->tr_rate_in =
            (u_char)(max[no] - (max[no] * alg->loop_tr[0]) / loop[no]); /* 2498 */
        alg->wait_time = 1.0f;                                          /* 2499 */
    }
    else
    {
        alg->wait_time = 0.0f;                                          /* 2500 */
        alg->pos_no    = 0;
    }
}                                                                       /* 2505 */

/* 0x2d  the out-of-finder term, faded down. */
void EJobC2D(ENEALG_WRK *alg)                                           /* 2510 */
{
    static float time[10];                                              /* bss 478a00 */
    static float max[10];                                               /* bss 478a28 */

    ENE_WRK *ew = &ene_wrk[alg->idx];
    u_char   no = alg->idx;                                             /* 2511 */

    if (alg->pos_no == 0)                                               /* 2518 */
    {
        alg->loop_tr[1] = (float)EaGetU16(alg);                         /* 2520 */
        time[no]        = alg->loop_tr[1];                              /* 2521 */
        max[no]         = (float)ew->tr_rate_out;                       /* 2522 */
        alg->pos_no++;                                                  /* 2523 */
    }
    else if (alg->pos_no != 1)
    {
        return;
    }

    if (alg->loop_tr[1] > 0.0f)                                         /* 2532 */
    {
        alg->loop_tr[1] -= 1.0f;                                        /* 2533 */
        ew->tr_rate_out = (u_char)((max[no] * alg->loop_tr[1]) / time[no]); /* 2536 */
        alg->wait_time  = 1.0f;                                         /* 2537 */
    }
    else
    {
        alg->wait_time = 0.0f;                                          /* 2538 */
        alg->pos_no    = 0;
    }
}                                                                       /* 2543 */

/* 0x2e  the out-of-finder term, faded up to <u16 max>. */
void EJobC2E(ENEALG_WRK *alg)                                           /* 2549 */
{
    static float max[10];                                               /* bss 478a50 */
    static float loop[10];                                              /* bss 478a78 */

    ENE_WRK *ew = &ene_wrk[alg->idx];
    u_char   no = alg->idx;                                             /* 2550 */

    if (alg->pos_no == 0)                                               /* 2557 */
    {
        alg->loop_tr[1] = (float)EaGetU16(alg);                         /* 2559 */
        max[no]         = (float)EaGetU16(alg);                         /* 2560 */
        loop[no]        = alg->loop_tr[1];                              /* 2561 */
        alg->pos_no++;                                                  /* 2562 */
    }
    else if (alg->pos_no != 1)
    {
        return;
    }

    if (alg->loop_tr[1] > 0.0f)                                         /* 2571 */
    {
        alg->loop_tr[1] -= 1.0f;                                        /* 2572 */
        ew->tr_rate_out =
            (u_char)(max[no] - (max[no] * alg->loop_tr[1]) / loop[no]); /* 2575 */
        alg->wait_time = 1.0f;                                          /* 2576 */
    }
    else
    {
        alg->wait_time = 0.0f;                                          /* 2577 */
        alg->pos_no    = 0;
    }
}                                                                       /* 2582 */

/* 0x2f  set the in-finder transparency term outright. */
void EJobC2F(ENEALG_WRK *alg)                                           /* 2588 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];

    ew->tr_rate_in = EaGetU8(alg);                                      /* 2592 */

    alg->pos_no    = 0;                                                 /* 2598 */
    alg->wait_time = 0.0f;                                              /* 2599 */
}

/* 0x30  the same for the out-of-finder term. */
void EJobC30(ENEALG_WRK *alg)                                           /* 2606 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];

    ew->tr_rate_out = EaGetU8(alg);                                     /* 2610 */

    alg->pos_no    = 0;                                                 /* 2616 */
    alg->wait_time = 0.0f;                                              /* 2617 */
}

/* 0x31  arm the aura's own blink: <sw> <base> <period> <step>. */
void EJobC31(ENEALG_WRK *alg)                                           /* 2627 */
{
    ENE_WRK *ew   = &ene_wrk[alg->idx];
    u_char   sw   = EaGetU8(alg);                                       /* 2631 */
    u_char   base = EaGetU8(alg);                                       /* 2632 */
    u_char   freq = EaGetU8(alg);                                       /* 2633 */
    u_char   add  = EaGetU8(alg);                                       /* 2634 */

    if (sw != 0)                                                        /* 2640 */
    {
        ew->tr2_base = base;                                            /* 2641 */
        ew->tr2_freq = freq;                                            /* 2642 */
        ew->tr2_add  = add;                                             /* 2643 */
        ew->tr2_cnt  = 0;                                               /* 2644 */
        ew->st.sta  |= 0x8000000L;                                      /* 2645 */
    }
    else
    {
        ew->st.sta &= ~0x8000000L;                                      /* 2647 */
    }

    alg->pos_no    = 0;                                                 /* 2649 */
    alg->wait_time = 0.0f;
}

/* 0x32  set the aura transparency term outright. */
void EJobC32(ENEALG_WRK *alg)                                           /* 2657 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];

    ew->tr2_rate_alg = EaGetU8(alg);                                    /* 2661 */

    alg->pos_no    = 0;                                                 /* 2667 */
    alg->wait_time = 0.0f;                                              /* 2668 */
}

/* 0x33  the vanish ramp used by the death sequence: <no> selects arm (0) or
 * step (1), and the step form jumps to <u16 adj> when it reaches the end.
 * Alpha, both parts-deform drive values and the ghost's own light all ride
 * the same 0..1 ramp; the light holds at full for the first half.
 *
 * Unlike the fade opcodes this one never parks itself -- the script loops
 * back to it with an explicit wait and jump, which is why the pos_no it
 * increments on the arm path is thrown away by the shared tail below. */
void EJobC33(ENEALG_WRK *alg)                                           /* 2676 */
{
    static float time[10];                                              /* bss 478aa0 */
    static float max[10];                                               /* bss 478ac8 */

    ENE_WRK *ew  = &ene_wrk[alg->idx];
    u_char   no  = alg->idx;                                            /* 2677 */
    u_char   sw;
    u_short  adj;
    float    f;

    sw = EaGetU8(alg);                                                  /* 2686 */
    alg->comm_add.pu8 += 2;  /* two reserved operand bytes */           /* 2687 */
    adj = EaGetU16(alg);                                                /* 2688 */

    if (sw == 0)                                                        /* 2694 */
    {
        time[no] = alg->loop[0];                                        /* 2696 */
        max[no]  = (float)ew->tr_rate_alg;                              /* 2698 */
        alg->pos_no++;                                                  /* 2699 */
    }                                                                   /* 2700 */
    else if (sw == 1)
    {
        if (alg->loop[0] <= 0.0f)                                       /* 2703 */
        {
            ew->tr_rate_alg = 0;                                        /* 2705 */
            ew->d_pda  = 0.0f;                                          /* 2706 */
            ew->d_pda2 = 0.0f;                                          /* 2707 */
            SetEnemyParallelLight(ew, 0.0f, 0.0f, 0.0f, 1.0f);          /* 2709 */
            EaJumpTo(alg, adj);                                         /* 2712 */
        }
        else
        {
            f = alg->loop[0] / time[no];                                /* 2715 */
            ew->tr_rate_alg = (u_char)(max[no] * f);                    /* 2717 */

            if (f < 0.5f) { f = f + f; } else { f = 1.0f; }             /* 2719 */

            ew->d_pda  = (float)(int)(f * 69.0f);                       /* 2720 */
            ew->d_pda2 = (float)(int)(f * 45.0f);                       /* 2721 */
            SetEnemyParallelLight(ew, f, f, f, 1.0f);                   /* 2725 */
            alg->loop[0] -= 1.0f;                                       /* 2727 */
        }
    }

    alg->pos_no    = 0;                                                 /* 2733 */
    alg->wait_time = 0.0f;
}

/* 0x34  play sound <u8 no> from the ghost's own bank at its position. */
void EJobC34(ENEALG_WRK *alg)                                           /* 2740 */
{
    SND_3D_SET s3d;
    ENE_WRK   *ew = &ene_wrk[alg->idx];
    u_char     no;

    memset(&s3d, 0, sizeof(s3d));                                       /* 2741 */
    no = EaGetU8(alg);                                                  /* 2744 */
    s3d.pos = &ew->mbox.pos;                                            /* 2746 */
    SndBankPlay(ew->se_bank_no, no, 0, 0, 0x3200, 0x1000, 0, &s3d);     /* 2748 */

    alg->wait_time = 0.0f;                                              /* 2749 */
    alg->pos_no    = 0;
}                                                                       /* 2755 */

/* 0x35  re-roll which unit this ghost is attacking.  Only meaningful while
 * the companion is present. */
void EJobC35(ENEALG_WRK *alg)                                           /* 2766 */
{
    if (IsSisWrk() != 0)
    {
        ChangeAtkTargetRnd(&ene_wrk[alg->idx]);
    }

    alg->pos_no    = 0;                                                 /* 2767 */
    alg->wait_time = 0.0f;
}

/* 0x36  clear ghost attribute bits <u32>. */
void EJobC36(ENEALG_WRK *alg)                                           /* 2774 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];
    u_int    attr;

    attr = (u_int)alg->comm_add.pu8[0] +
           (u_int)alg->comm_add.pu8[1] * 0x100 +
           (u_int)alg->comm_add.pu8[2] * 0x10000 +
           (u_int)alg->comm_add.pu8[3] * 0x1000000;                     /* 2778 */
    alg->comm_add.pu8 += 4;

    ew->attr &= ~attr;                                                  /* 2783 */

    alg->pos_no    = 0;                                                 /* 2785 */
    alg->wait_time = 0.0f;
}

/* 0x37  set ghost attribute bits <u32>. */
void EJobC37(ENEALG_WRK *alg)                                           /* 2792 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];
    u_int    attr;

    attr = (u_int)alg->comm_add.pu8[0] +
           (u_int)alg->comm_add.pu8[1] * 0x100 +
           (u_int)alg->comm_add.pu8[2] * 0x10000 +
           (u_int)alg->comm_add.pu8[3] * 0x1000000;                     /* 2796 */
    alg->comm_add.pu8 += 4;

    ew->attr |= attr;                                                   /* 2801 */

    alg->pos_no    = 0;                                                 /* 2803 */
    alg->wait_time = 0.0f;
}

/* 0x38  launch flying sub-creatures.  <type> is both the fly type and the
 * launch pattern: 0 and 1 send one from a single anchor, 2 and 4 send three
 * from the same anchor, 5 sends four from four different ones.  Type 3 is a
 * hole in the ROM's jump table and does nothing. */
void EJobC38(ENEALG_WRK *alg)                                           /* 2810 */
{
    ENE_WRK   *ew   = &ene_wrk[alg->idx];
    u_char     type = EaGetU8(alg);                                     /* 2815 */
    MPOS      *mp   = &ew->mpos;
    MOVE_BOX  *mb   = &ew->mbox;

    switch (type)                                                       /* 2821 */
    {
    case 0:
        EneFlyAct(ew, type, mp->p2, mb->rot, ew->target);               /* 2823 */
        break;

    case 1:
        EneFlyAct(ew, type, mp->p3, mb->rot, ew->target);               /* 2824 */
        break;

    case 2:
    case 4:
        EneFlyAct(ew, type, mp->p2, mb->rot, ew->target);               /* 2826 */
        EneFlyAct(ew, type, mp->p2, mb->rot, ew->target);               /* 2827 */
        EneFlyAct(ew, type, mp->p2, mb->rot, ew->target);
        break;

    case 5:
        EneFlyAct(ew, type, mp->p2, mb->rot, ew->target);               /* 2834 */
        EneFlyAct(ew, type, mp->p3, mb->rot, ew->target);               /* 2835 */
        EneFlyAct(ew, type, mp->p4, mb->rot, ew->target);               /* 2836 */
        EneFlyAct(ew, type, mp->p5, mb->rot, ew->target);               /* 2837 */
        break;

    default:
        break;
    }

    alg->pos_no    = 0;                                                 /* 2842 */
    alg->wait_time = 0.0f;                                              /* 2846 */
}

/* 0x39  cut this ghost's swarm loose -- every creature it owns starts fading. */
void EJobC39(ENEALG_WRK *alg)                                           /* 2853 */
{
    EraseEneFlyWork(&ene_wrk[alg->idx]);                                /* 2859 */

    alg->wait_time = 0.0f;                                              /* 2861 */
    alg->pos_no    = 0;
}

/* 0x3a  swap algorithm branch: <sw> 0 sets this ghost's own branch, non-zero
 * pushes <algno> onto each of its child ghosts instead.  A passive ghost has
 * no ENE_DAT and so no children. */
void EJobC3A(ENEALG_WRK *alg)                                           /* 2868 */
{
    ENE_WRK *ew    = &ene_wrk[alg->idx];
    u_char   sw    = EaGetU8(alg);                                      /* 2874 */
    u_char   algno = EaGetU8(alg);                                      /* 2875 */
    int      i;

    if (sw == 0)                                                        /* 2881 */
    {
        alg->branch = algno;                                            /* 2882 */
    }
    else if (ew->type != 2)                                             /* 2884 */
    {
        for (i = 0; i < 3; i++)                                         /* 2885 */
        {
            if (ew->dat->child_ene[i] >= 0)                             /* 2886 */
            {
                ChangeEneAlgorithm(0, ew->dat->child_ene[i], algno);    /* 2887 */
            }
        }                                                               /* 2890 */
    }

    alg->pos_no    = 0;                                                 /* 2895 */
    alg->wait_time = 0.0f;
}

/* 0x3b  damage every live child ghost, with damage type <u8>.  0x100000 is
 * "take damage now", 0x20000000000 the flag the shared-HP bosses read. */
void EJobC3B(ENEALG_WRK *alg)                                           /* 2902 */
{
    ENE_WRK *ew   = &ene_wrk[alg->idx];
    u_char   type = EaGetU8(alg);                                       /* 2908 */
    int      i;
    int      n;

    if (ew->type != 2)                                                  /* 2914 */
    {
        for (i = 0; i < 3; i++)                                         /* 2915 */
        {
            if (ew->dat->child_ene[i] >= 0)                             /* 2916 */
            {
                n = SearchEneWrkNo(ew->type, ew->dat->child_ene[i]);    /* 2917 */
                if (n >= 0)                                             /* 2918 */
                {
                    ENE_WRK *ewo = &ene_wrk[n];

                    if (ewo->act_no != 8 &&                             /* 2920 */
                        ewo->status == ENE_STATUS_ACT && ewo->st.hp != 0)
                    {
                        ewo->st.dmg      = 0;                           /* 2921 */
                        ewo->st.dmg_type = type;
                        ewo->st.sta     |= 0x20000100000L;              /* 2924 */
                    }
                }
            }
        }                                                               /* 2938 */
    }

    alg->pos_no    = 0;                                                 /* 2942 */
    alg->wait_time = 0.0f;                                              /* 2945 */
}

/* 0x3c  kill every child ghost outright.  The damage value is the ROM's own
 * out-of-range literal -- st.dmg is unsigned, so 0x869f is far past any real
 * HP total and the hit always reduces to zero. */
void EJobC3C(ENEALG_WRK *alg)                                           /* 2952 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];
    int      i;
    int      n;

    if (ew->type != 2)                                                  /* 2963 */
    {
        for (i = 0; i < 3; i++)                                         /* 2965 */
        {
            if (ew->dat->child_ene[i] >= 0)                             /* 2966 */
            {
                n = SearchEneWrkNo(ew->type, ew->dat->child_ene[i]);    /* 2967 */
                if (n >= 0)
                {
                    ENE_WRK *ewo = &ene_wrk[n];

                    if (ewo->status == ENE_STATUS_ACT)                  /* 2970 */
                    {
                        ewo->st.dmg  = (u_short)-0x7961;                /* 2971 */
                        ewo->st.sta |= 0x100000L;                       /* 2972 */
                    }
                }
            }
        }                                                               /* 2976 */
    }

    alg->pos_no    = 0;
    alg->wait_time = 0.0f;                                              /* 2979 */
}

/* 0x3d  push the interpreter's user flag onto every live child ghost. */
void EJobC3D(ENEALG_WRK *alg)                                           /* 2986 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];
    u_char   sw = EaGetU8(alg);                                         /* 2992 */
    int      i;
    int      n;

    if (ew->type != 2)                                                  /* 3000 */
    {
        for (i = 0; i < 3; i++)                                         /* 3001 */
        {
            if (ew->dat->child_ene[i] >= 0)                             /* 3002 */
            {
                n = SearchEneWrkNo(ew->type, ew->dat->child_ene[i]);    /* 3003 */
                if (n >= 0)                                             /* 3004 */
                {
                    ENE_WRK *ewo = &ene_wrk[n];

                    if (ewo->status == ENE_STATUS_ACT && ewo->st.hp != 0) /* 3006 */
                    {
                        ewo->alg.flag = sw;
                    }
                }
            }
        }                                                               /* 3012 */
    }

    alg->pos_no    = 0;
    alg->wait_time = 0.0f;                                              /* 3015 */
}

/* 0x3e  put the player into condition <u8> for <u16> frames. */
void EJobC3E(ENEALG_WRK *alg)                                           /* 3026 */
{
    u_char  cond = EaGetU8(alg);                                        /* 3027 */
    u_short tm   = EaGetU16(alg);                                       /* 3033 */

    plyr_wrk.cmn_wrk.st.cond    = cond;                                 /* 3034 */
    plyr_wrk.cmn_wrk.st.cond_tm = tm;

    alg->wait_time = 0.0f;                                              /* 3036 */
    alg->pos_no    = 0;
}

/* 0x3f  point the camera at this ghost. */
void EJobC3F(ENEALG_WRK *alg)                                           /* 3043 */
{
    ReqCamTraceNearEne(&ene_wrk[alg->idx]);                             /* 3050 */

    alg->wait_time = 0.0f;                                              /* 3052 */
    alg->pos_no    = 0;
}

/* 0x40  drain spirit power: <trgt> 0 the player, 1 the companion, 2 whoever
 * this ghost is attacking; <dmg> points. */
void EJobC40(ENEALG_WRK *alg)                                           /* 3059 */
{
    ENE_WRK *ew   = &ene_wrk[alg->idx];
    u_char   trgt = EaGetU8(alg);                                       /* 3063 */
    u_char   dmg  = EaGetU8(alg);                                       /* 3064 */

    switch (trgt)                                                       /* 3069 */
    {
    case 0:
        ReqPlyrSPdownP(&plyr_wrk.cmn_wrk, dmg);                         /* 3071 */
        break;
    case 1:
        ReqPlyrSPdownP(&sis_wrk.cmn_wrk, dmg);                          /* 3072 */
        break;
    case 2:
        ReqPlyrSPdownP(ew->target, dmg);                                /* 3074 */
        break;
    default:
        break;
    }

    alg->pos_no    = 0;                                                 /* 3077 */
    alg->wait_time = 0.0f;                                              /* 3081 */
}

/* 0x41  cnt[<n>] = <u8>. */
void EJobC41(ENEALG_WRK *alg)                                           /* 3091 */
{
    u_char n = EaGetU8(alg);                                            /* 3092 */
    u_char v = EaGetU8(alg);

    alg->cnt[n] = v;                                                    /* 3098 */

    alg->pos_no    = 0;                                                 /* 3100 */
    alg->wait_time = 0.0f;
}

/* 0x42  cnt[<n>] += <u8>. */
void EJobC42(ENEALG_WRK *alg)                                           /* 3110 */
{
    u_char n = EaGetU8(alg);                                            /* 3111 */
    u_char v = EaGetU8(alg);

    alg->cnt[n] = (u_char)(alg->cnt[n] + v);                            /* 3117 */

    alg->pos_no    = 0;                                                 /* 3119 */
    alg->wait_time = 0.0f;
}

/* 0x43  wake every child ghost that is not already resident. */
void EJobC43(ENEALG_WRK *alg)                                           /* 3126 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];
    int      i;

    if (ew->type != 2)                                                  /* 3135 */
    {
        for (i = 0; i < 3; i++)                                         /* 3136 */
        {
            if (ew->dat->child_ene[i] >= 0 &&                           /* 3137 */
                SearchEneWrkNo(ew->type, ew->dat->child_ene[i]) < 0)    /* 3138 */
            {
                EneActReq(0, ew->dat->child_ene[i]);                    /* 3139 */
            }
        }                                                               /* 3141 */
    }

    alg->pos_no    = 0;                                                 /* 3144 */
    alg->wait_time = 0.0f;                                              /* 3147 */
}

/* 0x44  force the scripted-stop action (8): <sw> 0 on this ghost, non-zero on
 * every live child instead. */
void EJobC44(ENEALG_WRK *alg)                                           /* 3154 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];
    u_char   sw = EaGetU8(alg);                                         /* 3160 */
    int      i;
    int      n;

    if (ew->type != 2)                                                  /* 3166 */
    {
        if (sw == 0)                                                    /* 3167 */
        {
            EneActSet(ew, 8);                                           /* 3168 */
        }
        else
        {
            for (i = 0; i < 3; i++)                                     /* 3171 */
            {
                if (ew->dat->child_ene[i] >= 0)                         /* 3172 */
                {
                    n = SearchEneWrkNo(ew->type, ew->dat->child_ene[i]); /* 3173 */
                    if (n >= 0 && ene_wrk[n].status == ENE_STATUS_ACT &&
                        ene_wrk[n].st.hp != 0)                          /* 3175 */
                    {
                        EneActSet(&ene_wrk[n], 8);                      /* 3176 */
                    }
                }
            }                                                           /* 3180 */
        }
    }

    alg->pos_no    = 0;
    alg->wait_time = 0.0f;                                              /* 3183 */
}

/* 0x45  ramp both parts-deform drive values down to zero over <u16 frames>.
 * The truncation to a byte is the ROM's, not a port artefact. */
void EJobC45(ENEALG_WRK *alg)                                           /* 3190 */
{
    static float time[10];                                              /* bss 478af0 */

    ENE_WRK *ew = &ene_wrk[alg->idx];
    u_char   no = alg->idx;                                             /* 3191 */

    if (alg->pos_no == 0)                                               /* 3197 */
    {
        alg->loop[0] = (float)EaGetU16(alg);                            /* 3199 */
        time[no]     = alg->loop[0];                                    /* 3200 */
        alg->wait_time = 1.0f;                                          /* 3201 */
        alg->pos_no++;
    }
    else if (alg->pos_no == 1)                                          /* 3205 */
    {
        if (alg->loop[0] > 0.0f)                                        /* 3208 */
        {
            alg->loop[0] -= 1.0f;                                       /* 3209 */
            ew->d_pda  = (float)(u_char)((ew->d_mpd  * alg->loop[0]) / time[no]); /* 3210 */
            ew->d_pda2 = (float)(u_char)((ew->d_mpd2 * alg->loop[0]) / time[no]); /* 3211 */
            alg->wait_time = 1.0f;                                      /* 3212 */
        }
        else
        {
            alg->loop[0] = 0.0f;                                        /* 3214 */
            ew->d_pda  = 0.0f;                                          /* 3215 */
            ew->d_pda2 = 0.0f;                                          /* 3216 */
            alg->wait_time = 0.0f;                                      /* 3217 */
            alg->pos_no    = 0;
        }
    }
}                                                                       /* 3225 */

/* 0x46  the counterpart: ramp both up to their configured maxima. */
void EJobC46(ENEALG_WRK *alg)                                           /* 3231 */
{
    static float time[10];                                              /* bss 478b18 */

    ENE_WRK *ew = &ene_wrk[alg->idx];
    u_char   no = alg->idx;                                             /* 3232 */

    if (alg->pos_no == 0)                                               /* 3238 */
    {
        alg->loop[0] = (float)EaGetU16(alg);                            /* 3240 */
        time[no]     = alg->loop[0];                                    /* 3241 */
        alg->wait_time = 1.0f;                                          /* 3242 */
        alg->pos_no++;
    }
    else if (alg->pos_no == 1)                                          /* 3246 */
    {
        if (alg->loop[0] > 0.0f)                                        /* 3249 */
        {
            alg->loop[0] -= 1.0f;                                       /* 3250 */
            ew->d_pda  = ew->d_mpd  - (ew->d_mpd  * alg->loop[0]) / time[no]; /* 3251 */
            ew->d_pda2 = ew->d_mpd2 - (ew->d_mpd2 * alg->loop[0]) / time[no]; /* 3252 */
            alg->wait_time = 1.0f;                                      /* 3253 */
        }
        else
        {
            alg->loop[0] = 0.0f;                                        /* 3255 */
            ew->d_pda  = ew->d_mpd;                                     /* 3256 */
            ew->d_pda2 = ew->d_mpd2;                                    /* 3257 */
            alg->wait_time = 0.0f;                                      /* 3258 */
            alg->pos_no    = 0;
        }
    }
}                                                                       /* 3266 */

/* 0x47  set both parts-deform drive values outright.  0xff means "the
 * configured maximum", 0 means off. */
void EJobC47(ENEALG_WRK *alg)                                           /* 3272 */
{
    ENE_WRK *ew  = &ene_wrk[alg->idx];                                  /* 3273 */
    u_char   flg = EaGetU8(alg);                                        /* 3277 */

    if (flg == 0)                                                       /* 3282 */
    {
        ew->d_pda  = 0.0f;                                              /* 3284 */
        ew->d_pda2 = 0.0f;                                              /* 3285 */
    }
    else if (flg == 0xff)                                               /* 3286 */
    {
        ew->d_pda  = ew->d_mpd;                                         /* 3287 */
        ew->d_pda2 = ew->d_mpd2;
    }
    else
    {
        ew->d_pda  = (float)flg;                                        /* 3289 */
        ew->d_pda2 = (float)flg;                                        /* 3290 */
    }

    alg->pos_no    = 0;                                                 /* 3293 */
    alg->wait_time = 0.0f;
}

/* 0x48  drop the player's "went through a door" latch. */
void EJobC48(ENEALG_WRK *alg)                                           /* 3304 */
{
    ClearPlyrDoorFlg();                                                 /* 3305 */

    alg->pos_no    = 0;
    alg->wait_time = 0.0f;
}

/* 0x49  loop[0] = frames left in the current animation, less <u16>.  The
 * count is read as a halfword, which is the ROM's own narrowing of
 * MOT_CTRL::all_cnt. */
void EJobC49(ENEALG_WRK *alg)                                           /* 3312 */
{
    ENE_WRK  *ew  = &ene_wrk[alg->idx];
    ANI_CTRL *anc = ew->ani_ctrl_p;                                     /* 3313 */
    u_short   frm = EaGetU16(alg);                                      /* 3316 */

    if (frm < (u_short)anc->mot.all_cnt)                                /* 3318 */
    {
        alg->loop[0] = (float)(int)((u_short)anc->mot.all_cnt - frm);   /* 3320 */
    }
    else
    {
        alg->loop[0] = 0.0f;                                            /* 3322 */
    }

    alg->pos_no    = 0;                                                 /* 3323 */
    alg->wait_time = 0.0f;                                              /* 3331 */
}

/* ==========================================================================
 *  0x70..0x9f -- movement
 * ======================================================================== */

/* 0x70  teleport: position = <s16 x> <s16 y> <s16 z>, in world space. */
void EJobM00(ENEALG_WRK *alg)                                           /* 3345 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];
    short    x  = (short)EaGetU16(alg);                                 /* 3350 */
    short    y  = (short)EaGetU16(alg);                                 /* 3351 */
    short    z  = (short)EaGetU16(alg);                                 /* 3352 */

    _SetVector(ew->mbox.pos, (float)x, (float)y, (float)z, 0.0f);       /* 3358 */

    alg->wait_time = 0.0f;                                              /* 3359 */
    alg->pos_no    = 0;
}

/* 0x71  the same offsets, but taken in the ghost's own frame and added to
 * where it already is. */
void EJobM01(ENEALG_WRK *alg)                                           /* 3366 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];
    short    x  = (short)EaGetU16(alg);                                 /* 3371 */
    short    y  = (short)EaGetU16(alg);                                 /* 3372 */
    short    z  = (short)EaGetU16(alg);                                 /* 3373 */
    float    tv[4];

    _SetVector(tv, (float)x, (float)y, (float)z, 0.0f);                 /* 3379 */
    RotFvector(ew->mbox.rot, tv);                                       /* 3380 */
    sceVu0AddVector(ew->mbox.pos, ew->mbox.pos, tv);                    /* 3381 */

    alg->wait_time = 0.0f;                                              /* 3382 */
    alg->pos_no    = 0;
}

/* 0x72  set the per-ghost spawn offset (adjp), used when a parent drags its
 * children along. */
void EJobM02(ENEALG_WRK *alg)                                           /* 3389 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];
    short    x  = (short)EaGetU16(alg);                                 /* 3393 */
    short    y  = (short)EaGetU16(alg);                                 /* 3394 */
    short    z  = (short)EaGetU16(alg);                                 /* 3395 */

    _SetVector(ew->adjp, (float)x, (float)y, (float)z, 0.0f);           /* 3401 */

    alg->wait_time = 0.0f;                                              /* 3402 */
    alg->pos_no    = 0;
}

/* 0x73  forward speed, chosen from a code that names one of the ENE_DAT
 * speeds and a multiplier.  The codes are laid out decimally: 0/1 walk and
 * run at 1x, +10 per half step up to 3x, 100.. the same set negated, and
 * 0xff a dead stop.  Everything is scaled by ew->reso so a slowed ghost
 * crawls.
 *
 * A passive ghost has no ENE_DAT, so it cannot use this at all. */
void EJobM03(ENEALG_WRK *alg)                                           /* 3419 */
{
    ENE_WRK  *ew = &ene_wrk[alg->idx];
    MOVE_BOX *mb = &ew->mbox;
    u_char    id = EaGetU8(alg);                                        /* 3425 */
    float     spd;

    if (ew->type == 2)                                                  /* 3432 */
    {
        printf("AUTO ENE CANNOT USE SPD\n");                            /* 3433 */
        return;
    }

    switch (id)                                                         /* 3438 */
    {
    case 0x00: spd =  (float)ew->dat->wspd;         break;
    case 0x01: spd =  (float)ew->dat->rspd;         break;
    case 0x0a: spd =  (float)ew->dat->wspd * 1.5f;  break;
    case 0x0b: spd =  (float)ew->dat->rspd * 1.5f;  break;
    case 0x14: spd =  (float)ew->dat->wspd * 2.0f;  break;
    case 0x15: spd =  (float)ew->dat->rspd * 2.0f;  break;
    case 0x1e: spd =  (float)ew->dat->wspd * 2.5f;  break;
    case 0x1f: spd =  (float)ew->dat->rspd * 2.5f;  break;
    case 0x28: spd =  (float)ew->dat->wspd * 3.0f;  break;
    case 0x29: spd =  (float)ew->dat->rspd * 3.0f;  break;
    case 0x32: spd =  (float)ew->dat->wspd * 0.5f;  break;
    case 0x33: spd =  (float)ew->dat->rspd * 0.5f;  break;
    case 0x64: spd = -(float)ew->dat->wspd;         break;
    case 0x65: spd = -(float)ew->dat->rspd;         break;
    case 0x6e: spd = -(float)ew->dat->wspd * 1.5f;  break;
    case 0x6f: spd = -(float)ew->dat->rspd * 1.5f;  break;
    case 0x78: spd = -(float)ew->dat->wspd * 2.0f;  break;
    case 0x79: spd = -(float)ew->dat->rspd * 2.0f;  break;
    case 0x96: spd = -(float)ew->dat->wspd * 0.5f;  break;
    case 0x97: spd = -(float)ew->dat->rspd * 0.5f;  break;
    case 0xa0: spd = -(float)ew->dat->wspd * 0.25f; break;
    case 0xa1: spd = -(float)ew->dat->rspd * 0.25f; break;
    case 0xff: spd = 0.0f;                          break;
    default:   spd =  (float)ew->dat->wspd;         break;              /* 3497 */
    }

    _SetVector(mb->spd, 0.0f, 0.0f, spd * ew->reso, 0.0f);              /* 3500 */

    alg->wait_time = 0.0f;                                              /* 3503 */
    alg->pos_no    = 0;
}

/* 0x74  set the speed vector outright: <type> <s16 x> <s16 y> <s16 z>.
 * type 0 is world space, 1 the ghost's own frame, 2 the ghost's own frame
 * scaled by its knock-back speed (ENE_DAT::hitbk, in percent). */
void EJobM04(ENEALG_WRK *alg)                                           /* 3510 */
{
    ENE_WRK  *ew   = &ene_wrk[alg->idx];
    MOVE_BOX *mb   = &ew->mbox;
    u_char    type = EaGetU8(alg);                                      /* 3517 */
    short     x    = (short)EaGetU16(alg);                              /* 3518 */
    short     y    = (short)EaGetU16(alg);                              /* 3519 */
    short     z    = (short)EaGetU16(alg);                              /* 3520 */
    float     f;

    switch (type)                                                       /* 3526 */
    {
    case 0:
        f = ew->reso;                                                   /* 3528 */
        _SetVector(mb->spd, (float)x * f, (float)y * f, (float)z * f, 0.0f); /* 3529 */
        break;

    case 1:
        f = ew->reso;                                                   /* 3531 */
        _SetVector(mb->spd, (float)x * f, (float)y * f, (float)z * f, 0.0f); /* 3532 */
        RotFvector(mb->rot, mb->spd);                                   /* 3533 */
        break;

    case 2:
        f = ((float)ew->dat->hitbk / 100.0f) * ew->reso;                /* 3535 */
        _SetVector(mb->spd, (float)x * f, (float)y * f, (float)z * f, 0.0f); /* 3536 */
        RotFvector(mb->rot, mb->spd);                                   /* 3537 */
        break;

    default:
        break;
    }

    alg->pos_no    = 0;
    alg->wait_time = 0.0f;                                              /* 3541 */
}

/* 0x75  set the three rotation speeds, in tenths of a degree per frame.
 * PAL turns are scaled up so a turn still takes the same wall-clock time. */
void EJobM05(ENEALG_WRK *alg)                                           /* 3548 */
{
    ENE_WRK  *ew = &ene_wrk[alg->idx];
    MOVE_BOX *mb = &ew->mbox;
    short     rot[3];
    u_char    i;

    for (i = 0; i < 3; i++)                                             /* 3557 */
    {
        rot[i] = (short)EaGetU16(alg);                                  /* 3559 */
        mb->rspd[i] = ((float)rot[i] * 3.1415925f) / 1800.0f;           /* 3563 */

        if (GetPALMode() != 0)                                          /* 3564 */
        {
            mb->rspd[i] *= 1.19999993f;                                 /* 3565 */
        }
        RotLimitChk(&mb->rspd[i]);                                      /* 3567 */
    }                                                                   /* 3575 */

    alg->pos_no    = 0;                                                 /* 3579 */
    alg->wait_time = 0.0f;
}

/* 0x76  circle-strafe the target.  <per> scales how far short of the target
 * the ghost aims (0 = right on top of it), <side> picks the direction, and
 * <u16 v> is the peak sideways speed; the ramp is quadratic in a static frame
 * counter that saturates at 40.
 *
 * The vertical term is the ROM's, quirk included: tv[1] is zeroed before the
 * speed is stored and the world position added back, so the height difference
 * it then computes collapses to adjp[1].  EJobM11 carries the same block. */
void EJobM06(ENEALG_WRK *alg)                                           /* 3591 */
{
    static int cnt;                                                     /* sbss 3f4c10 */

    ENE_WRK  *ew   = &ene_wrk[alg->idx];
    MOVE_BOX *pcmw = (MOVE_BOX *)&ew->target->mbox;
    MOVE_BOX *mb   = &ew->mbox;
    u_char    per;
    u_char    side;
    float     rot = 0.0f;
    float     r;
    float     m;
    float     v;
    float     f;
    float     tv[4];
    float     rv[4];

    per  = EaGetU8(alg);                                                /* 3664 */
    side = EaGetU8(alg);                                                /* 3666 */
    v    = (float)EaGetU16(alg);                                        /* 3668 */

    _ClearVector(tv);                                                   /* 3669 */
    r = GetDistV(pcmw->pos, mb->pos);                                   /* 3670 */

    if (per != 0)                                                       /* 3675 */
    {
        rot = 10.0f / (float)per;                                       /* 3676 */
    }

    m = (v / 1600.0f) * (float)(cnt * cnt);                             /* 3678 */
    tv[2] = r - (float)ew->dat->wspd * rot;                             /* 3679 */

    if (m > v) { m = v; }                                               /* 3682 */

    cnt = (cnt < 40) ? cnt + 1 : 40;                                    /* 3684 */

    m = m / tv[2];                                                      /* 3686 */

    GetTrgtRot(pcmw->pos, mb->pos, rv, 2);                              /* 3691 */
    if (side == 0)                                                      /* 3692 */
    {
        rv[1] -= m;
    }
    else
    {
        rv[1] += m;                                                     /* 3693 */
    }
    RotLimitChk(&rv[1]);                                                /* 3694 */

    RotFvector(rv, tv);                                                 /* 3696 */
    sceVu0AddVector(tv, tv, pcmw->pos);                                 /* 3697 */
    sceVu0SubVector(tv, tv, mb->pos);                                   /* 3698 */
    tv[1] = 0.0f;                                                       /* 3699 */
    g3dxVu0CopyVector(mb->spd, tv);

    sceVu0AddVector(tv, tv, mb->pos);                                   /* 3701 */
    tv[1] = (tv[1] + ew->adjp[1]) - mb->pos[1];                         /* 3704 */
    f = tv[1];                                                          /* 3705 */
    if (fabsf(f) > 1.0f)                                                /* 3706 */
    {
        tv[1] = (f >= 0.0f) ? 1.0f : -1.0f;                             /* 3707 */
    }
    mb->spd[1] = tv[1];                                                 /* 3709 */

    alg->pos_no    = 0;                                                 /* 3710 */
    alg->wait_time = 0.0f;
}

/* 0x77  the full circle-strafe: <type1> ramp shape (0 flat, 1 accelerate,
 * 2 decelerate), <type2> what to close on (1 the target, 2/3 a walk/run
 * radius short of it, anything else just resets the ramp), <side>,
 * <u16 max speed>, <u16 ramp frames>. */
void EJobM07(ENEALG_WRK *alg)                                           /* 3731 */
{
    static int cnt;                                                     /* sbss 3f4c14 */

    ENE_WRK  *ew   = &ene_wrk[alg->idx];
    MOVE_BOX *pcmw = (MOVE_BOX *)&ew->target->mbox;
    MOVE_BOX *mb   = &ew->mbox;
    u_char    type1;
    u_char    type2;
    u_char    side;
    int       time;
    int       time_m;
    float     max_v;
    float     r;
    float     m;
    float     fr;
    float     tv[4];
    float     rv[4];

    type1  = EaGetU8(alg);                                              /* 3741 */
    type2  = EaGetU8(alg);                                              /* 3743 */
    side   = EaGetU8(alg);                                              /* 3748 */
    max_v  = (float)EaGetU16(alg);                                      /* 3749 */
    time   = EaGetU16(alg);                                             /* 3750 */
    time_m = time;

    if (GetPALMode() != 0)                                              /* 3751 */
    {
        time_m = (int)((float)time / 1.19999993f);                      /* 3752 */
        fr     = 1.19999993f;
    }
    else
    {
        fr = 1.0f;                                                      /* 3754 */
    }

    _ClearVector(tv);                                                   /* 3755 */
    r = GetDistV(pcmw->pos, mb->pos);                                   /* 3756 */
    tv[2] = r;

    if (type2 == 2)                                                     /* 3758 */
    {
        tv[2] = r - (float)ew->dat->wspd;                               /* 3762 */
    }
    else if (type2 == 3)                                                /* 3763 */
    {
        tv[2] = r - (float)ew->dat->rspd;                               /* 3765 */
    }
    else if (type2 != 1)
    {
        /* Not a move at all -- just re-seat the ramp counter. */
        if (type1 == 1)      { cnt = 0; }                               /* 3816 */
        else if (type1 == 2) { cnt = time_m; }                          /* 3818 */
        else                 { cnt = 0; }                               /* 3819 */

        alg->pos_no    = 0;                                             /* 3821 */
        alg->wait_time = 0.0f;                                          /* 3822 */
        return;
    }

    if (tv[2] == 0.0f || time_m == 0)                                   /* 3766 */
    {
        alg->wait_time = 0.0f;                                          /* 3767 */
    }
    else
    {
        if (type1 != 0)                                                 /* 3771 */
        {
            max_v = (max_v * (float)(cnt * cnt)) / (float)(time_m * time_m); /* 3772 */
        }
        m = (max_v * fr) / tv[2];                                       /* 3773 */

        if (type1 == 1)                                                 /* 3779 */
        {
            cnt = (cnt < time_m) ? cnt + 1 : time_m;                    /* 3781 */
        }
        else if (type1 == 2)                                            /* 3782 */
        {
            cnt = (cnt < 1) ? 0 : cnt - 1;                              /* 3783 */
        }
        else
        {
            cnt = 0;                                                    /* 3786 */
        }

        GetTrgtRot(pcmw->pos, mb->pos, rv, 2);                          /* 3791 */
        if (side == 0)                                                  /* 3793 */
        {
            rv[1] -= m;
        }
        else
        {
            rv[1] += m;                                                 /* 3795 */
        }
        RotLimitChk(&rv[1]);                                            /* 3796 */

        RotFvector(rv, tv);                                             /* 3797 */
        sceVu0AddVector(tv, tv, pcmw->pos);                             /* 3798 */
        sceVu0SubVector(tv, tv, mb->pos);                               /* 3803 */
        tv[1] = 0.0f;                                                   /* 3804 */
        g3dxVu0CopyVector(mb->spd, tv);

        sceVu0AddVector(tv, tv, mb->pos);                               /* 3806 */
        tv[1] = (tv[1] + ew->adjp[1]) - mb->pos[1];                     /* 3808 */
        if (fabsf(tv[1]) > 1.0f)                                        /* 3809 */
        {
            tv[1] = (tv[1] >= 0.0f) ? 1.0f : -1.0f;                     /* 3810 */
        }
        mb->spd[1] = tv[1];                                             /* 3813 */
    }

    alg->pos_no = 0;                                                    /* 3825 */
}

/* 0x78  turn to face the target, allowing for its spawn offset.
 *
 *   0     snap the whole rotation now and stop turning
 *   0xff  stop turning, holding whatever heading the ghost has
 *   0xfe  aim at the player's head height at a fixed 4 deg/frame
 *   n     aim over n frames
 *
 * The PAL branch of the last case is the ROM's and computes exactly what the
 * NTSC one does -- the 60/50 factor was never applied. */
void EJobM08(ENEALG_WRK *alg)                                           /* 3832 */
{
    ENE_WRK   *ew  = &ene_wrk[alg->idx];
    PLCMN_WRK *pcw = ew->target;
    MOVE_BOX  *mb  = &ew->mbox;
    u_char     id  = EaGetU8(alg);                                      /* 3836 */
    float      tv[4];

    sceVu0AddVector(tv, pcw->mbox.pos, ew->adjp);                       /* 3838 */

    if (id == 0)                                                        /* 3840 */
    {
        GetTrgtRot(mb->pos, tv, mb->rot, 3);                            /* 3849 */
        mb->trot[3] = 0.0f;                                             /* 3850 */
    }
    else if (id == 0xff)                                                /* 3851 */
    {
        mb->trot[3] = 0.0f;                                             /* 3855 */
        mb->trot[1] = mb->rot[1];                                       /* 3856 */
    }
    else if (id == 0xfe)                                                /* 3861 */
    {
        tv[1] = plyr_wrk.cmn_wrk.headpos[1];                            /* 3862 */
        GetTrgtRot(mb->pos, tv, mb->trot, 3);                           /* 3864 */
        mb->rot[0] = mb->trot[0];                                       /* 3865 */

        if (GetPALMode() != 0)                                          /* 3866 */
        {
            mb->trot[3] = 0.0837757885f;                                /* 3867 */
        }
        else
        {
            mb->trot[3] = 0.0698131621f;                                /* 3869 */
        }
    }
    else
    {
        GetTrgtRot(mb->pos, tv, mb->trot, 3);                           /* 3875 */
        mb->rot[0] = mb->trot[0];                                       /* 3876 */

        if (GetPALMode() != 0)                                          /* 3877 */
        {
            mb->trot[3] = (1.57079625f / (float)id) * ew->reso;         /* 3878 */
        }
        else
        {
            mb->trot[3] = (1.57079625f / (float)id) * ew->reso;         /* 3880 */
        }
    }

    alg->pos_no    = 0;                                                 /* 3883 */
    alg->wait_time = 0.0f;
}

/* 0x79  skips three operand bytes and does nothing.  Forty lines of body in
 * the ROM produce no instructions -- commented out before this build. */
void EJobM09(ENEALG_WRK *alg)                                           /* 3894 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];
    float    tv[4];

    (void)ew;
    (void)tv;
    alg->comm_add.pu8 += 2;                                             /* 3904 */
    alg->comm_add.pu8 += 1;                                             /* 3905 */

    alg->pos_no    = 0;                                                 /* 3948 */
    alg->wait_time = 0.0f;
}

/* 0x7a  set the heading outright.  Values below 1000 are degrees; the four
 * codes above are relative to whoever the ghost is dealing with:
 *   1000 face the target      1500/1501 ninety degrees either side of it
 *   2000 face directly away   3000 face the player specifically
 * Only the two 1500s and the plain-degrees case are wrapped into range. */
void EJobM0A(ENEALG_WRK *alg)                                           /* 3959 */
{
    ENE_WRK   *ew   = &ene_wrk[alg->idx];
    MOVE_BOX  *mb   = &ew->mbox;
    MOVE_BOX  *pcmw = (MOVE_BOX *)&ew->target->mbox;
    int        rot  = EaGetU16(alg);                                    /* 3962 */

    switch (rot)                                                        /* 3966 */
    {
    case 1000:
        mb->rot[1] = GetTrgtRotY(mb->pos, pcmw->pos);                   /* 3971 */
        break;

    case 1500:
        mb->rot[1] = GetTrgtRotY(mb->pos, pcmw->pos) - 1.57079625f;     /* 3973 */
        RotLimitChk(&mb->rot[1]);                                       /* 3974 */
        break;

    case 1501:
        mb->rot[1] = GetTrgtRotY(mb->pos, pcmw->pos) + 1.57079625f;     /* 3976 */
        RotLimitChk(&mb->rot[1]);                                       /* 3977 */
        break;

    case 2000:
        mb->rot[1] = GetTrgtRotY(mb->pos, pcmw->pos) + 3.1415925f;      /* 3980 */
        break;

    case 3000:
        pcmw = &plyr_wrk.cmn_wrk.mbox;                                  /* 3984 */
        mb->rot[1] = GetTrgtRotY(mb->pos, pcmw->pos);                   /* 3985 */
        break;

    default:
        mb->rot[1] = ((float)rot / 180.0f) * 3.1415925f;                /* 3987 */
        RotLimitChk(&mb->rot[1]);                                       /* 3988 */
        break;
    }

    alg->pos_no    = 0;                                                 /* 3990 */
    alg->wait_time = 0.0f;                                              /* 3994 */
}

/* 0x7b  wander.  A five-step state machine: pick a heading, drift, pick
 * another, drift again, then stop and go back to walking speed.
 *
 *   turn 2 -> the first heading is straight away from the target; anything
 *             else picks a random one at least 45 degrees off centre
 *   time    -> base drift length, plus up to 30 random frames
 *
 * Below thirty frames left the speed decays by 0.95 a frame, which is what
 * makes the ghost coast to a stop instead of snapping. */
void EJobM0B(ENEALG_WRK *alg)                                           /* 4004 */
{
    static u_char turn[10];                                             /* bss 478b40 */
    static u_char time[10];                                             /* bss 478b50 */

    ENE_WRK  *ew   = &ene_wrk[alg->idx];
    MOVE_BOX *mb   = &ew->mbox;
    MOVE_BOX *pcmw = (MOVE_BOX *)&ew->target->mbox;
    int       no   = alg->idx;
    float     rot[4];

    memset(rot, 0, sizeof(rot));                                        /* 4012 */

    switch (alg->pos_no)                                                /* 4025 */
    {
    case 0:
        turn[no] = EaGetU8(alg);                                        /* 4028 */
        time[no] = EaGetU8(alg);                                        /* 4029 */
        ew->st.sta |= 0x40000L;                                         /* 4031 */
        alg->loop[0] = (float)GetRndSP(time[no], 30);                   /* 4033 */
        GetTrgtRot(mb->pos, pcmw->pos, rot, 1);                         /* 4034 */

        if (turn[no] == 2)                                              /* 4035 */
        {
            rot[1] = GetTrgtRotY(mb->pos, pcmw->pos) + 3.1415925f;      /* 4036 */
            RotLimitChk(&rot[1]);                                       /* 4037 */
        }
        else
        {
            rot[1] = ((float)GetRndSP(0, 14) - 7.0f) * 0.099999994f;    /* 4040 */
            RotLimitChk(&rot[1]);                                       /* 4041 */
            if (rot[1] > 0.0f)                                          /* 4042 */
            {
                rot[1] += 0.785398126f;                                 /* 4044 */
            }
            else
            {
                rot[1] -= 0.785398126f;                                 /* 4045 */
            }
        }

        _SetVector(mb->spd, 0.0f, 0.0f, 20.0f, 0.0f);                   /* 4052 */
        RotFvector(rot, mb->spd);                                       /* 4085 */
        alg->wait_time = 1.0f;                                          /* 4092 */
        alg->pos_no++;
        return;

    case 1:
        if (alg->loop[0] > 0.0f) { break; }                             /* 4055 */

        if (turn[no] == 2)                                              /* 4056 */
        {
            alg->pos_no    = 4;                                         /* 4057 */
            alg->wait_time = 1.0f;
        }
        else
        {
            alg->pos_no++;                                              /* 4096 */
            alg->wait_time = 1.0f;
        }
        return;

    case 2:
        if (turn[no] != 0 && GetRndSP(0, 100) < 41)                     /* 4075 */
        {
            alg->pos_no    = 4;                                         /* 4077 */
            alg->wait_time = 1.0f;
            return;
        }

        GetTrgtRot(mb->pos, pcmw->pos, rot, 1);                         /* 4079 */
        alg->loop[0] = (float)GetRndSP(time[no], 30);                   /* 4080 */
        rot[1] = (float)GetRndSP(0, 14) * 0.099999994f;                 /* 4081 */
        RotLimitChk(&rot[1]);                                           /* 4082 */
        if (rot[1] > 0.0f)                                              /* 4083 */
        {
            rot[1] += 0.785398126f;
        }
        else
        {
            rot[1] -= 0.785398126f;
        }
        mb->spd[2] = 20.0f;                                             /* 4084 */
        RotFvector(rot, mb->spd);                                       /* 4085 */
        alg->wait_time = 1.0f;                                          /* 4092 */
        alg->pos_no++;
        return;

    case 3:
        if (alg->loop[0] <= 0.0f)                                       /* 4095 */
        {
            alg->pos_no++;                                              /* 4096 */
            alg->wait_time = 1.0f;
            return;
        }
        break;

    case 4:
        ew->st.sta &= ~0x40000L;                                        /* 4107 */
        _SetVector(mb->spd, 0.0f, 0.0f, (float)ew->dat->wspd, 0.0f);    /* 4108 */
        alg->wait_time = 0.0f;                                          /* 4109 */
        alg->pos_no    = 0;
        return;

    default:
        return;
    }

    /* Drifting -- shared by the two wait states.  The last thirty frames bleed
     * the speed off so the ghost coasts to a stop. */
    alg->loop[0] -= 1.0f;                                               /* 4098 */
    if (alg->loop[0] <= 30.0f)                                          /* 4101 */
    {
        sceVu0ScaleVector(mb->spd, mb->spd, 0.949999988f);              /* 4102 */
    }
    alg->wait_time = 1.0f;                                              /* 4104 */
}                                                                       /* 4119 */

/* 0x7c, 0x7d  reserved -- both are a bare return in the ROM. */
void EJobM0C(ENEALG_WRK *alg)                                           /* 4123 */
{
    (void)alg;
}

void EJobM0D(ENEALG_WRK *alg)                                           /* 4126 */
{
    (void)alg;
}

/* 0x7e  teleport to a point around the target: <type> picks the bearing,
 * <dmin>..<wmin> the distance band in units of 50, and <u8 blank> the number
 * of frames to hold the ghost fully transparent while it moves.
 *
 *   type 0   behind the target      type 2   the target's own heading
 *   type 1   +/- 18 degrees of north
 *   type 12  the target's heading +/- 45 degrees
 *   type 13  the target's heading +/- 20 degrees
 *
 * Types 3..11 leave the bearing at whatever the previous call left in tr,
 * which is uninitialised stack -- the ROM never uses them. */
void EJobM0E(ENEALG_WRK *alg)                                           /* 4138 */
{
    static u_char tr_rate_save[10];                                     /* bss 478b60 */

    ENE_WRK   *ew   = &ene_wrk[alg->idx];
    MOVE_BOX  *mb   = &ew->mbox;
    PLCMN_WRK *pcmw = ew->target;
    u_char     type;
    u_char     dmin;
    u_char     wmin;
    u_char     blank;
    float      tv[4];
    float      tr[4];

    if (alg->pos_no != 0)                                               /* 4143 */
    {
        if (alg->pos_no != 1) { return; }

        ew->tr_rate_alg = tr_rate_save[alg->idx];                       /* 4224 */
        alg->pos_no     = 0;                                            /* 4227 */
        alg->wait_time  = 0.0f;                                         /* 4230 */
        return;
    }

    type  = EaGetU8(alg);                                               /* 4152 */
    dmin  = EaGetU8(alg);                                               /* 4155 */
    wmin  = EaGetU8(alg);                                               /* 4156 */
    blank = EaGetU8(alg);                                               /* 4157 */
    alg->comm_add.pu8++;    /* one reserved operand byte */

    if (wmin == dmin)                                                   /* 4159 */
    {
        _SetVector(tv, 0.0f, 0.0f, (float)wmin * 50.0f, 0.0f);          /* 4161 */
    }
    else
    {
        _SetVector(tv, 0.0f, 0.0f,
                   (float)GetRndSP(dmin, wmin - dmin) * 50.0f, 0.0f);   /* 4162 */
    }

    switch (type)                                                       /* 4165 */
    {
    case 0:
        _SetVector(tr, 0.0f, pcmw->mbox.rot[1] + 3.1415925f, 0.0f, 0.0f); /* 4167 */
        break;

    case 1:
        _SetVector(tr, 0.0f,
                   (float)GetRndSP(0, 36) * 0.174532905f - 3.1415925f,
                   0.0f, 0.0f);                                         /* 4171 */
        break;

    case 2:
        _SetVector(tr, 0.0f, pcmw->mbox.rot[1], 0.0f, 0.0f);            /* 4176 */
        break;

    case 12:
        _SetVector(tr, 0.0f,
                   pcmw->mbox.rot[1] +
                   ((float)(GetRndSP(0, 90) - 45) * 3.1415925f) / 180.0f,
                   0.0f, 0.0f);                                         /* 4186 */
        break;

    case 13:
        _SetVector(tr, 0.0f,
                   pcmw->mbox.rot[1] +
                   ((float)(GetRndSP(0, 40) - 20) * 3.1415925f) / 180.0f,
                   0.0f, 0.0f);                                         /* 4191 */
        break;

    default:
        break;
    }

    RotLimitChk(&tr[1]);                                                /* 4196 */
    RotFvector(tr, tv);                                                 /* 4197 */
    sceVu0AddVector(tv, pcmw->mbox.pos, tv);                            /* 4198 */

    /* Y is deliberately left alone -- the ghost keeps its own floor height. */
    {
        float y = mb->pos[1];                                           /* 4200 */
        g3dxVu0CopyVector(mb->pos, tv);
        mb->pos[1] = y;
    }

    if (blank == 0)                                                     /* 4207 */
    {
        alg->pos_no    = 0;                                             /* 4211 */
        alg->wait_time = 0.0f;
    }
    else
    {
        tr_rate_save[alg->idx] = ew->tr_rate_alg;                       /* 4212 */
        ew->tr_rate_alg = 0;
        alg->wait_time  = (float)blank;                                 /* 4220 */
        alg->pos_no++;
    }
}

/* 0x7f  ramped forward speed: <type1> ramp shape, <type2> 1 to move at all,
 * <side> to invert, <u16 max speed>, <u16 ramp frames>.  Same counter shape
 * as 0x77 but the speed goes straight down the ghost's own Z. */
void EJobM0F(ENEALG_WRK *alg)                                           /* 4243 */
{
    static int cnt;                                                     /* sbss 3f4c18 */

    ENE_WRK  *ew = &ene_wrk[alg->idx];
    MOVE_BOX *mb = &ew->mbox;
    u_char    type1;
    u_char    type2;
    u_char    side;
    int       time;
    int       time_m;
    float     max_v;
    float     m;
    float     fr;

    type1  = EaGetU8(alg);                                              /* 4250 */
    type2  = EaGetU8(alg);                                              /* 4255 */
    side   = EaGetU8(alg);                                              /* 4256 */
    max_v  = (float)EaGetU16(alg);                                      /* 4257 */
    time   = EaGetU16(alg);                                             /* 4258 */
    time_m = time;

    if (GetPALMode() != 0)                                              /* 4259 */
    {
        time_m = (int)((float)time / 1.19999993f);                      /* 4261 */
        fr     = 1.19999993f;
    }
    else
    {
        fr = 1.0f;                                                      /* 4263 */
    }

    if (type2 == 1)                                                     /* 4265 */
    {
        if (type1 == 0)                                                 /* 4269 */
        {
            m = max_v * fr;                                             /* 4271 */
        }
        else
        {
            m = (max_v * fr * (float)(cnt * cnt)) / (float)(time_m * time_m); /* 4272 */
        }

        if (type1 == 1)                                                 /* 4273 */
        {
            cnt = (cnt < time_m) ? cnt + 1 : time_m;                    /* 4278 */
        }
        else if (type1 == 2)                                            /* 4279 */
        {
            cnt = (cnt < 1) ? 0 : cnt - 1;                              /* 4282 */
        }
        else
        {
            cnt = 0;                                                    /* 4283 */
        }

        if (side != 0) { m = -m; }                                      /* 4285 */

        _SetVector(mb->spd, 0.0f, 0.0f, m, 0.0f);                       /* 4288 */
        alg->wait_time = 0.0f;                                          /* 4289 */
        alg->pos_no    = 0;
    }
    else
    {
        if (type1 == 1)      { cnt = 0; }                               /* 4290 */
        else if (type1 == 2) { cnt = time_m; }                          /* 4291 */
        else                 { cnt = 0; }

        alg->pos_no    = 0;                                             /* 4295 */
        alg->wait_time = 0.0f;                                          /* 4297 */
    }
}                                                                       /* 4298 */

/* 0x80  forward speed scaled by how much HP the ghost has left: it moves at
 * <u16 lo> percent on its last hit point and <u16 hi> percent at full, off
 * the walk (type 0) or run (type 1) speed. */
void EJobM10(ENEALG_WRK *alg)                                           /* 4304 */
{
    ENE_WRK  *ew = &ene_wrk[alg->idx];
    MOVE_BOX *mb = &ew->mbox;
    u_char    id = EaGetU8(alg);                                        /* 4308 */
    u_short   lo = EaGetU16(alg);                                       /* 4310 */
    u_short   hi = EaGetU16(alg);                                       /* 4311 */
    float     spd;
    float     r;

    spd = (id == 0) ? (float)ew->dat->wspd : (float)ew->dat->rspd;      /* 4318 */

    r = (float)((int)(((int)hi - (int)lo) * (int)ew->st.hp) /
                (int)ew->dat->hp + (int)lo) / 100.0f;                   /* 4320 */

    _SetVector(mb->spd, 0.0f, 0.0f, spd * r * ew->reso, 0.0f);          /* 4321 */

    alg->wait_time = 0.0f;                                              /* 4325 */
    alg->pos_no    = 0;                                                 /* 4326 */
}

/* 0x81  circle-strafe at a fixed rate: <per>, <side>, <u16 tenths of a
 * degree per frame>.  Carries the same vertical quirk as EJobM06. */
void EJobM11(ENEALG_WRK *alg)                                           /* 4338 */
{
    ENE_WRK  *ew   = &ene_wrk[alg->idx];
    MOVE_BOX *pcmw = (MOVE_BOX *)&ew->target->mbox;
    MOVE_BOX *mb   = &ew->mbox;
    u_char    per;
    u_char    side;
    u_short   deg;
    float     rot = 0.0f;                                               /* 4344 */
    float     r;
    float     f;
    float     tv[4];
    float     rv[4];

    per  = EaGetU8(alg);                                                /* 4346 */
    side = EaGetU8(alg);                                                /* 4348 */
    deg  = EaGetU16(alg);                                               /* 4350 */

    _ClearVector(tv);                                                   /* 4351 */
    r = GetDistV(pcmw->pos, mb->pos);                                   /* 4352 */

    if (per != 0)                                                       /* 4358 */
    {
        rot = 10.0f / (float)per;                                       /* 4359 */
    }
    tv[2] = r - (float)ew->dat->wspd * rot;                             /* 4361 */

    GetTrgtRot(pcmw->pos, mb->pos, rv, 2);                              /* 4362 */
    f = ((float)deg * 3.1415925f) / 1800.0f;                            /* 4365 */

    if (side == 0)                                                      /* 4367 */
    {
        rv[1] -= f;
    }
    else
    {
        rv[1] += f;                                                     /* 4369 */
    }
    RotLimitChk(&rv[1]);                                                /* 4370 */

    RotFvector(rv, tv);                                                 /* 4371 */
    sceVu0AddVector(tv, tv, pcmw->pos);                                 /* 4372 */
    sceVu0SubVector(tv, tv, mb->pos);                                   /* 4374 */
    tv[1] = 0.0f;                                                       /* 4375 */
    g3dxVu0CopyVector(mb->spd, tv);                                     /* 4376 */

    sceVu0AddVector(tv, tv, mb->pos);                                   /* 4379 */
    tv[1] = (tv[1] + ew->adjp[1]) - mb->pos[1];                         /* 4381 */
    if (fabsf(tv[1]) > 1.0f)                                            /* 4382 */
    {
        tv[1] = (tv[1] >= 0.0f) ? 1.0f : -1.0f;                         /* 4383 */
    }
    mb->spd[1] = tv[1];                                                 /* 4384 */

    alg->pos_no    = 0;                                                 /* 4386 */
    alg->wait_time = 0.0f;                                              /* 4387 */
}

/* 0x82  snap this ghost onto its last live child, offset by <s16 x y z> in
 * that child's frame.  Used by the bosses that ride one of their own parts. */
void EJobM12(ENEALG_WRK *alg)                                           /* 4397 */
{
    ENE_WRK  *ew = &ene_wrk[alg->idx];
    MOVE_BOX *mb = &ew->mbox;
    short     x  = (short)EaGetU16(alg);                                /* 4404 */
    short     y  = (short)EaGetU16(alg);                                /* 4405 */
    short     z  = (short)EaGetU16(alg);                                /* 4406 */
    int       i;
    int       n;
    float     mv[4];

    if (ew->type != 2)                                                  /* 4412 */
    {
        for (i = 0; i < 3; i++)                                         /* 4413 */
        {
            if (ew->dat->child_ene[i] >= 0)                             /* 4414 */
            {
                n = SearchEneWrkNo(ew->type, ew->dat->child_ene[i]);    /* 4415 */
                if (n >= 0 && ene_wrk[n].status == ENE_STATUS_ACT &&
                    ene_wrk[n].st.hp != 0)                              /* 4416 */
                {
                    g3dxVu0CopyVector(mb->pos, ene_wrk[n].mbox.pos);    /* 4418 */
                    g3dxVu0CopyVector(mb->rot, ene_wrk[n].mbox.rot);
                    _SetVector(mv, (float)x, (float)y, (float)z, 0.0f); /* 4421 */
                    RotFvector(mb->rot, mv);                            /* 4422 */
                    sceVu0AddVector(mb->pos, mb->pos, mv);              /* 4423 */
                }
            }
        }
    }

    alg->pos_no    = 0;                                                 /* 4427 */
    alg->wait_time = 0.0f;                                              /* 4429 */
}

/* 0x83  forward speed as <u16 percent> of the walk (0/anything) or run (1)
 * speed; 100 and 101 are the same two negated. */
void EJobM13(ENEALG_WRK *alg)                                           /* 4436 */
{
    ENE_WRK  *ew  = &ene_wrk[alg->idx];
    MOVE_BOX *mb  = &ew->mbox;
    u_char    id  = EaGetU8(alg);                                       /* 4441 */
    u_short   per = EaGetU16(alg);                                      /* 4443 */
    float     spd;

    switch (id)                                                         /* 4450 */
    {
    case 1:   spd =  (float)ew->dat->rspd; break;                       /* 4452 */
    case 100: spd = -(float)ew->dat->wspd; break;                       /* 4453 */
    case 101: spd = -(float)ew->dat->rspd; break;                       /* 4454 */
    default:  spd =  (float)ew->dat->wspd; break;                       /* 4455 */
    }

    _SetVector(mb->spd, 0.0f, 0.0f,
               ((spd * (float)per) / 100.0f) * ew->reso, 0.0f);         /* 4457 */

    alg->wait_time = 0.0f;                                              /* 4458 */
    alg->pos_no    = 0;
}

/* 0x84  face the heading the ghost's table entry was placed with. */
void EJobM14(ENEALG_WRK *alg)                                           /* 4465 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];
    float    rot;

    rot = ((float)ew->cmn_dat->dir * 3.1415925f) / 180.0f;              /* 4469 */
    RotLimitChk(&rot);                                                  /* 4470 */
    ew->mbox.rot[1] = rot;                                              /* 4471 */

    alg->pos_no    = 0;                                                 /* 4472 */
    alg->wait_time = 0.0f;
}

/* 0x85  turn towards the target over <u16 frames>, but only inside a
 * <s16 min>..<s16 max> degree window -- outside it the bearing is wrapped by
 * a full turn first, so the ghost takes the long way round. */
void EJobM15(ENEALG_WRK *alg)                                           /* 4479 */
{
    ENE_WRK   *ew   = &ene_wrk[alg->idx];
    MOVE_BOX  *mb   = &ew->mbox;
    PLCMN_WRK *pcw  = ew->target;
    u_short    time = EaGetU16(alg);                                    /* 4483 */
    short      lo   = (short)EaGetU16(alg);                             /* 4484 */
    short      hi   = (short)EaGetU16(alg);                             /* 4489 */
    float      fmin;
    float      fmax;
    float      tv[4];
    float      vw[4];

    fmax = ((float)hi * 3.1415925f) / 180.0f;                           /* 4490 */
    fmin = ((float)lo * 3.1415925f) / 180.0f;                           /* 4491 */

    sceVu0AddVector(tv, pcw->mbox.pos, ew->adjp);                       /* 4496 */
    GetTrgtRot(mb->pos, tv, vw, 3);                                     /* 4497 */
    vw[1] -= mb->rot[1];                                                /* 4499 */
    RotLimitChk(&vw[1]);                                                /* 4500 */

    if (vw[1] < fmin) { vw[1] += 6.28318501f; }                         /* 4501 */
    if (vw[1] > fmax) { vw[1] -= 6.28318501f; }                         /* 4502 */

    mb->rspd[1] = vw[1] / (float)time;                                  /* 4504 */
    RotLimitChk(&mb->rspd[1]);                                          /* 4505 */

    alg->pos_no    = 0;                                                 /* 4507 */
    alg->wait_time = 0.0f;                                              /* 4508 */
}                                                                       /* 4510 */

/* ==========================================================================
 *  0xa0..0xdf -- conditional branches
 *
 *  They share a shape: a one-byte sense where present, the value(s) to test,
 *  and a u16 jump offset.  The ROM is not consistent about which way round
 *  the sense goes, so each is spelled out.
 * ======================================================================== */

/* 0xa0  loop end on loop[0]: jump when the counter runs out, otherwise burn
 * one iteration and fall through. */
void EJobB00(ENEALG_WRK *alg)                                           /* 4530 */
{
    u_short adj = EaGetU16(alg);                                        /* 4536 */

    if (alg->loop[0] <= 0.0f)                                           /* 4537 */
    {
        EaJumpTo(alg, adj);
    }
    else
    {
        alg->loop[0] -= 1.0f;                                           /* 4541 */
    }

    alg->pos_no    = 0;                                                 /* 4543 */
    alg->wait_time = 0.0f;
}

/* 0xa1  the same on loop[<n>]. */
void EJobB01(ENEALG_WRK *alg)                                           /* 4556 */
{
    u_char  n   = EaGetU8(alg);                                         /* 4557 */
    u_short adj = EaGetU16(alg);                                        /* 4563 */

    if (alg->loop[n] <= 0.0f)                                           /* 4564 */
    {
        EaJumpTo(alg, adj);
    }
    else
    {
        alg->loop[n] -= 1.0f;                                           /* 4568 */
    }

    alg->pos_no    = 0;                                                 /* 4570 */
    alg->wait_time = 0.0f;
}

/* 0xa2  no operation. */
void EJobB02(ENEALG_WRK *alg)                                           /* 4578 */
{
    alg->pos_no    = 0;
    alg->wait_time = 0.0f;
}

/* 0xa3  five-way jump on the damage type that was last taken; the fifth entry
 * catches everything above 3. */
void EJobB03(ENEALG_WRK *alg)                                           /* 4585 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];
    u_short  adj[5];

    adj[0] = EaGetU16(alg);                                             /* 4592 */
    adj[1] = EaGetU16(alg);                                             /* 4593 */
    adj[2] = EaGetU16(alg);                                             /* 4594 */
    adj[3] = EaGetU16(alg);                                             /* 4595 */
    adj[4] = EaGetU16(alg);                                             /* 4596 */

    switch (ew->st.dmg_type)                                            /* 4598 */
    {
    case 0:  EaJumpTo(alg, adj[0]); break;                              /* 4601 */
    case 1:  EaJumpTo(alg, adj[1]); break;                              /* 4605 */
    case 2:  EaJumpTo(alg, adj[2]); break;                              /* 4609 */
    case 3:  EaJumpTo(alg, adj[3]); break;                              /* 4613 */
    default: EaJumpTo(alg, adj[4]); break;                              /* 4617 */
    }

    alg->pos_no    = 0;                                                 /* 4620 */
    alg->wait_time = 0.0f;
}

/* 0xa4  random branch: <sw> <percent> <u16 adj>.  sw 0 jumps when the roll
 * comes in at or below the percentage, non-zero when it comes in above. */
void EJobB04(ENEALG_WRK *alg)                                           /* 4630 */
{
    u_char  sw      = EaGetU8(alg);                                     /* 4634 */
    u_char  percent = EaGetU8(alg);                                     /* 4635 */
    u_short adj     = EaGetU16(alg);                                    /* 4636 */

    if (sw == 0)                                                        /* 4642 */
    {
        if ((int)percent >= GetRndSP(1, 99)) { EaJumpTo(alg, adj); }    /* 4643 */
    }
    else
    {
        if ((int)percent <= GetRndSP(1, 99)) { EaJumpTo(alg, adj); }    /* 4644 */
    }

    alg->pos_no    = 0;                                                 /* 4647 */
    alg->wait_time = 0.0f;                                              /* 4651 */
}

/* 0xa5  the attack is over: jump when the ghost has run out of attack frames.
 * If it has not, and it is holding the companion, the shutter-chance window
 * is opened and the two attack flags dropped. */
void EJobB05(ENEALG_WRK *alg)                                           /* 4658 */
{
    ENE_WRK *ew  = &ene_wrk[alg->idx];
    u_short  adj = EaGetU16(alg);                                       /* 4664 */

    if (ew->atk_tm == 0)                                                /* 4671 */
    {
        EaJumpTo(alg, adj);                                             /* 4672 */
    }
    else if (ew->target_n == 1)                                         /* 4675 */
    {
        ew->st.sta = (ew->st.sta & ~0x1080000L) | 0x2000L;              /* 4678 */
    }
    else
    {
        alg->pos_no    = 0;                                             /* 4679 */
        alg->wait_time = 0.0f;                                          /* 4680 */
        return;
    }

    alg->pos_no    = 0;
    alg->wait_time = 0.0f;                                              /* 4690 */
}

/* 0xa6  jump when status 0x200000000 is up -- the ghost has been asked to
 * leave. */
void EJobB06(ENEALG_WRK *alg)                                           /* 4699 */
{
    ENE_WRK *ew  = &ene_wrk[alg->idx];
    u_short  adj = EaGetU16(alg);                                       /* 4708 */

    if ((ew->st.sta & 0x200000000L) != 0)                               /* 4710 */
    {
        EaJumpTo(alg, adj);
    }

    alg->pos_no    = 0;                                                 /* 4711 */
    alg->wait_time = 0.0f;                                              /* 4713 */
}

/* 0xa7  HP threshold: <sw> <percent of max> <u16 adj>.  sw 0 jumps when the
 * ghost is at or below the percentage, non-zero when it is at or above. */
void EJobB07(ENEALG_WRK *alg)                                           /* 4723 */
{
    ENE_WRK *ew  = &ene_wrk[alg->idx];
    u_char   sw  = EaGetU8(alg);                                        /* 4728 */
    u_short  per = EaGetU8(alg);                                        /* 4729 */
    u_short  adj = EaGetU16(alg);                                       /* 4730 */

    if (sw == 0)                                                        /* 4736 */
    {
        if (ew->st.hp <= ((u_int)ew->dat->hp * (u_int)per) / 100)       /* 4737 */
        {
            EaJumpTo(alg, adj);
        }
    }
    else
    {
        if (ew->st.hp >= ((u_int)ew->dat->hp * (u_int)per) / 100)       /* 4741 */
        {
            EaJumpTo(alg, adj);
        }
    }

    alg->pos_no    = 0;                                                 /* 4742 */
    alg->wait_time = 0.0f;                                              /* 4745 */
}

/* 0xa8  distance to the target: <sw> <u16 dist> <u16 adj>.  sw 0 jumps when
 * the ghost is closer than dist, non-zero when it is at least that far. */
void EJobB08(ENEALG_WRK *alg)                                           /* 4756 */
{
    ENE_WRK  *ew   = &ene_wrk[alg->idx];
    MOVE_BOX *pcmw = (MOVE_BOX *)&ew->target->mbox;
    MOVE_BOX *mb   = &ene_wrk[alg->idx].mbox;
    u_char    sw   = EaGetU8(alg);                                      /* 4764 */
    u_short   dist = EaGetU16(alg);                                     /* 4765 */
    u_short   adj  = EaGetU16(alg);                                     /* 4766 */
    int       hit;

    if (sw == 0)                                                        /* 4772 */
    {
        hit = (GetDistV(pcmw->pos, mb->pos) < (float)dist);             /* 4773 */
    }
    else
    {
        hit = ((float)dist <= GetDistV(pcmw->pos, mb->pos));            /* 4774 */
    }

    if (hit) { EaJumpTo(alg, adj); }                                    /* 4777 */

    alg->pos_no    = 0;                                                 /* 4778 */
    alg->wait_time = 0.0f;                                              /* 4781 */
}

/* 0xa9  which side of the ghost the target stands on: <dir> 0 left, 1 right,
 * <u16 adj>.  The bearing is left in trot so a following turn opcode can use
 * it without recomputing. */
void EJobB09(ENEALG_WRK *alg)                                           /* 4788 */
{
    ENE_WRK   *ew  = &ene_wrk[alg->idx];
    PLCMN_WRK *pcw = ew->target;
    MOVE_BOX  *mb  = &ew->mbox;
    char       dir = (char)EaGetU8(alg);                                /* 4793 */
    u_short    adj = EaGetU16(alg);                                     /* 4794 */
    float      tv[4];

    sceVu0AddVector(tv, pcw->mbox.pos, ew->adjp);                       /* 4797 */
    GetTrgtRot(mb->pos, tv, mb->trot, 3);                               /* 4798 */
    tv[1] = mb->trot[1] - mb->rot[1];                                   /* 4804 */
    RotLimitChk(&tv[1]);                                                /* 4805 */

    if (tv[1] > 0.0f)                                                   /* 4809 */
    {
        if (dir == 0) { EaJumpTo(alg, adj); }                           /* 4810 */
    }
    else
    {
        if (dir == 1) { EaJumpTo(alg, adj); }                           /* 4812 */
    }

    alg->pos_no    = 0;                                                 /* 4816 */
    alg->wait_time = 0.0f;                                              /* 4819 */
}

/* 0xaa  is the target inside a <u16 degrees> cone in front of the ghost?
 * <sw> 0 jumps when it is, non-zero when it is not. */
void EJobB0A(ENEALG_WRK *alg)                                           /* 4826 */
{
    ENE_WRK   *ew  = &ene_wrk[alg->idx];
    PLCMN_WRK *pcw = ew->target;
    MOVE_BOX  *mb  = &ew->mbox;
    u_char     sw  = EaGetU8(alg);                                      /* 4833 */
    int        rot = EaGetU16(alg);                                     /* 4837 */
    u_short    adj = EaGetU16(alg);                                     /* 4838 */
    int        w;

    w = RotRngChk(mb->pos, pcw->mbox.pos, mb->rot[1],
                  ((float)rot / 180.0f) * 3.1415925f);                  /* 4839 */

    if (sw == 0)                                                        /* 4845 */
    {
        if (w != 0) { EaJumpTo(alg, adj); }                             /* 4846 */
    }
    else
    {
        if (w == 0) { EaJumpTo(alg, adj); }                             /* 4850 */
    }

    alg->pos_no    = 0;                                                 /* 4851 */
    alg->wait_time = 0.0f;                                              /* 4855 */
}                                                                       /* 4859 */

/* 0xab  a passive ghost only: jump when the player has come inside the range
 * AENE_DAT gives it, and mark it as leaving on distance. */
void EJobB0B(ENEALG_WRK *alg)                                           /* 4866 */
{
    ENE_WRK *ew  = &ene_wrk[alg->idx];
    u_short  adj = EaGetU16(alg);                                       /* 4872 */
    float    rng;

    if (ew->type == 2)                                                  /* 4878 */
    {
        rng = ew->aie->rng;                                             /* 4880 */
        if (rng > 0.0f &&
            GetDistV(plyr_wrk.cmn_wrk.mbox.pos, ew->mbox.pos) <= rng)   /* 4890 */
        {
            EaJumpTo(alg, adj);                                         /* 4891 */
            ew->rel_type = ENE_RELEASE_DIST;                            /* 4893 */
        }
    }

    alg->pos_no    = 0;                                                 /* 4895 */
    alg->wait_time = 0.0f;                                              /* 4896 */
}                                                                       /* 4900 */

/* 0xac  jump when the ghost is on action 3, the attack run. */
void EJobB0C(ENEALG_WRK *alg)                                           /* 4907 */
{
    ENE_WRK *ew  = &ene_wrk[alg->idx];
    u_short  adj = EaGetU16(alg);                                       /* 4911 */

    if (ew->act_no == 3)                                                /* 4917 */
    {
        EaJumpTo(alg, adj);
    }

    alg->pos_no    = 0;                                                 /* 4918 */
    alg->wait_time = 0.0f;                                              /* 4920 */
}

/* 0xad  jump when the interpreter's user flag is set. */
void EJobB0D(ENEALG_WRK *alg)                                           /* 4930 */
{
    u_short adj = EaGetU16(alg);                                        /* 4936 */

    if (alg->flag != 0)                                                 /* 4937 */
    {
        EaJumpTo(alg, adj);
    }

    alg->pos_no    = 0;                                                 /* 4940 */
    alg->wait_time = 0.0f;
}

/* 0xae  user-flag compare: <sw> <u16 value> <u16 adj>.  sw 0 tests this
 * ghost's flag, non-zero every live child's -- any match jumps. */
void EJobB0E(ENEALG_WRK *alg)                                           /* 4947 */
{
    ENE_WRK *ew  = &ene_wrk[alg->idx];
    u_char   sw  = EaGetU8(alg);                                        /* 4953 */
    u_short  num = EaGetU16(alg);                                       /* 4954 */
    u_short  adj = EaGetU16(alg);                                       /* 4955 */
    int      i;
    int      n;
    int      datno;

    if (sw == 0)                                                        /* 4963 */
    {
        if (alg->flag == num) { EaJumpTo(alg, adj); }                   /* 4964 */
    }
    else if (ew->type != 2)                                             /* 4968 */
    {
        for (i = 0; i < 3; i++)                                         /* 4969 */
        {
            datno = ew->dat->child_ene[i];                              /* 4970 */
            if (datno >= 0)
            {
                n = SearchEneWrkNo(ew->type, datno);                    /* 4971 */
                if (n >= 0 && ene_wrk[n].status == ENE_STATUS_ACT &&
                    ene_wrk[n].st.hp != 0 && ene_wrk[n].alg.flag == num) /* 4974 */
                {
                    EaJumpTo(alg, adj);                                 /* 4976 */
                }
            }
        }                                                               /* 4982 */
    }

    alg->pos_no    = 0;                                                 /* 4987 */
    alg->wait_time = 0.0f;
}

/* 0xaf  how many ghosts are on the field: <sw> <num> <u16 adj>.  sw 0 jumps
 * at or below num, non-zero at or above. */
void EJobB0F(ENEALG_WRK *alg)                                           /* 4994 */
{
    u_char  fl  = EaGetU8(alg);                                         /* 4999 */
    u_char  num = EaGetU8(alg);                                         /* 5000 */
    u_short adj = EaGetU16(alg);                                        /* 5001 */
    int     i;
    int     n = 0;

    for (i = 0; i < ENE_WRK_MAX; i++)                                   /* 5007 */
    {
        if (IsActEnemy(i) != 0) { n++; }                                /* 5009 */
    }

    if (fl == 0)                                                        /* 5012 */
    {
        if (n <= (int)num) { EaJumpTo(alg, adj); }                      /* 5014 */
    }
    else
    {
        if (n >= (int)num) { EaJumpTo(alg, adj); }                      /* 5019 */
    }

    alg->pos_no    = 0;                                                 /* 5020 */
    alg->wait_time = 0.0f;                                              /* 5023 */
}

/* 0xb0  a passive ghost's lifetime is up.  A negative AENE_DAT::time means
 * "no limit" and only the leave request ends it; otherwise loop[0] counts
 * down at the ghost's own animation rate. */
void EJobB10(ENEALG_WRK *alg)                                           /* 5030 */
{
    ENE_WRK *ew  = &ene_wrk[alg->idx];
    u_short  adj = EaGetU16(alg);                                       /* 5034 */

    if (ew->aie->time < 0)                                              /* 5040 */
    {
        if ((ew->st.sta & 0x200000000L) != 0)                           /* 5042 */
        {
            EaJumpTo(alg, adj);
            ew->rel_type = ENE_RELEASE_TIME_OUT;
        }
        else
        {
            alg->pos_no    = 0;
            alg->wait_time = 0.0f;
            return;
        }
    }
    else if (alg->loop[0] <= 0.0f)                                      /* 5046 */
    {
        EaJumpTo(alg, adj);                                             /* 5051 */
        ew->rel_type = ENE_RELEASE_TIME_OUT;
    }
    else
    {
        alg->loop[0] -= ew->reso;                                       /* 5052 */
    }

    alg->pos_no    = 0;                                                 /* 5053 */
    alg->wait_time = 0.0f;                                              /* 5057 */
}

/* 0xb1  two-way branch on the algorithm branch request ChangeEneAlgorithm()
 * left: 1 and 0xf0 take the first target, 2 the second.  The request is
 * consumed either way. */
void EJobB11(ENEALG_WRK *alg)                                           /* 5067 */
{
    u_short adj1 = EaGetU16(alg);                                       /* 5068 */
    u_short adj2 = EaGetU16(alg);

    switch (alg->branch)                                                /* 5074 */
    {
    case 1:
    case 0xf0:
        EaJumpTo(alg, adj1);                                            /* 5082 */
        alg->pos_no = 0;
        break;
    case 2:
        EaJumpTo(alg, adj2);                                            /* 5084 */
        alg->pos_no = 0;
        break;
    default:
        alg->pos_no = 0;
        break;
    }

    alg->branch    = 0;                                                 /* 5087 */
    alg->wait_time = 0.0f;                                              /* 5089 */
}

/* 0xb2  jump when the ghost has been photographed away, and record it. */
void EJobB12(ENEALG_WRK *alg)                                           /* 5096 */
{
    ENE_WRK *ew  = &ene_wrk[alg->idx];
    u_short  adj = EaGetU16(alg);                                       /* 5100 */

    if ((ew->st.sta & 0x10000000000L) != 0)                             /* 5107 */
    {
        EaJumpTo(alg, adj);                                             /* 5108 */
        ew->rel_type = ENE_RELEASE_TAKE_PICT;                           /* 5109 */
    }

    alg->pos_no    = 0;                                                 /* 5112 */
    alg->wait_time = 0.0f;
}

/* 0xb3  jump when the ghost is playing animation <u16>.  Leaves pos_no and
 * wait_time alone -- both are already zero when a branch opcode runs. */
void EJobB13(ENEALG_WRK *alg)                                           /* 5119 */
{
    ENE_WRK *ew  = &ene_wrk[alg->idx];
    u_short  no  = EaGetU16(alg);                                       /* 5123 */
    u_short  adj = EaGetU16(alg);                                       /* 5124 */

    if (ew->anime_no == no)                                             /* 5130 */
    {
        EaJumpTo(alg, adj);
    }
}                                                                       /* 5131 */

/* 0xb4  end the death cinematic: drop the screen effect, tear the parts
 * deform down, hand the camera back, and jump. */
void EJobB14(ENEALG_WRK *alg)                                           /* 5139 */
{
    ENE_WRK *ew  = &ene_wrk[alg->idx];
    u_short  adj = EaGetU16(alg);                                       /* 5143 */

    EffScreenEffectStatusSet(0);                                        /* 5151 */
    EnemyDeadPDeformReset(ew);                                          /* 5153 */
    MapCamCutFinCamera();                                               /* 5154 */

    alg->pos_no    = 0;                                                 /* 5156 */
    alg->wait_time = 0.0f;
    EaJumpTo(alg, adj);                                                 /* 5158 */
}

/* 0xb5  jump once the soul-suck particle effect has finished. */
void EJobB15(ENEALG_WRK *alg)                                           /* 5168 */
{
    u_short adj = EaGetU16(alg);                                        /* 5173 */

    if (IgEffectIsEndParticleSuck() != 0)                               /* 5174 */
    {
        EaJumpTo(alg, adj);
    }

    alg->pos_no    = 0;                                                 /* 5176 */
    alg->wait_time = 0.0f;
}

/* 0xb6  is the grab still good?  <u16 adj1> is taken when the hold has been
 * broken (and the attack re-flagged), <u16 adj2> when the victim is still
 * held and still taking damage. */
void EJobB16(ENEALG_WRK *alg)                                           /* 5183 */
{
    ENE_WRK   *ew   = &ene_wrk[alg->idx];
    PLCMN_WRK *pcw  = ew->target;
    u_short    adj1 = EaGetU16(alg);                                    /* 5187 */
    u_short    adj2 = EaGetU16(alg);                                    /* 5188 */

    if ((pcw->st.sta & 0x20000) != 0)                                   /* 5194 */
    {
        if ((pcw->st.sta & 0x10000) != 0)                               /* 5195 */
        {
            EaJumpTo(alg, adj2);                                        /* 5197 */
            alg->pos_no    = 0;
            alg->wait_time = 0.0f;
            return;
        }
    }
    else if (ew->atk_tm != 0)                                           /* 5200 */
    {
        /* Somebody else grabbed the victim out from under this ghost. */
        if (pcw->atk_eneno == alg->idx)                                 /* 5203 */
        {
            alg->pos_no    = 0;                                         /* 5204 */
            alg->wait_time = 0.0f;                                      /* 5205 */
            return;
        }
        ew->atk_tm = 0;                                                 /* 5209 */
    }

    ew->st.sta |= 0x8000L;                                              /* 5210 */
    EaJumpTo(alg, adj1);                                                /* 5211 */

    alg->pos_no    = 0;                                                 /* 5212 */
    alg->wait_time = 0.0f;                                              /* 5214 */
}

/* 0xb7  jump while the ghost is inside the camera's ring. */
void EJobB17(ENEALG_WRK *alg)                                           /* 5221 */
{
    ENE_WRK *ew  = &ene_wrk[alg->idx];
    u_short  adj = EaGetU16(alg);                                       /* 5225 */

    if ((ew->st.sta & 0x80L) != 0)                                      /* 5230 */
    {
        EaJumpTo(alg, adj);
    }

    alg->pos_no    = 0;                                                 /* 5231 */
    alg->wait_time = 0.0f;                                              /* 5233 */
}

/* 0xb8  jump while the player is looking through the camera. */
void EJobB18(ENEALG_WRK *alg)                                           /* 5243 */
{
    u_short adj = EaGetU16(alg);                                        /* 5248 */

    if (plyr_wrk.cmn_wrk.mode == 6)                                     /* 5249 */
    {
        EaJumpTo(alg, adj);
    }

    alg->pos_no    = 0;                                                 /* 5251 */
    alg->wait_time = 0.0f;
}

/* 0xb9  walk to a fixed world point: <s16 speed percent> <s16 turn frames>
 * <s16 x y z> <u16 adj>.  The ghost only actually steps once it is pointed
 * within two degrees of the destination, and jumps on arrival.
 *
 * Whether the height is taken into account is decided by comparing the
 * ghost's own Y against the destination's -- equal means "same floor", and
 * the bearing is then computed in two dimensions. */
void EJobB19(ENEALG_WRK *alg)                                           /* 5259 */
{
    ENE_WRK  *ew  = &ene_wrk[alg->idx];
    MOVE_BOX *mb  = &ew->mbox;
    short     spd;
    short     rt;
    short     x;
    short     y;
    short     z;
    u_short   adj;
    float     l1;
    float     l2;
    float     tv[4];
    float     vw[4];

    spd = (short)EaGetU16(alg);                                         /* 5267 */
    rt  = (short)EaGetU16(alg);                                         /* 5268 */
    x   = (short)EaGetU16(alg);                                         /* 5269 */
    y   = (short)EaGetU16(alg);                                         /* 5270 */
    z   = (short)EaGetU16(alg);                                         /* 5271 */
    adj = EaGetU16(alg);                                                /* 5272 */

    _SetVector(vw, (float)x, (float)y, (float)z, 0.0f);                 /* 5278 */
    l1 = GetDistV2(mb->pos, vw);                                        /* 5280 */
    l2 = ((float)ew->dat->wspd * (float)spd) / 100.0f;                  /* 5281 */

    if (l1 > l2)                                                        /* 5283 */
    {
        _SetVector(tv, 0.0f, 0.0f, l2, 0.0f);                           /* 5284 */

        if (mb->pos[1] == (float)y)                                     /* 5285 */
        {
            GetTrgtRot(mb->pos, vw, mb->trot, 2);                       /* 5286 */
        }
        else
        {
            GetTrgtRot(mb->pos, vw, mb->trot, 3);                       /* 5288 */
        }

        mb->rot[0]  = mb->trot[0];                                      /* 5290 */
        mb->trot[3] = (3.1415925f / (float)rt) * ew->reso;              /* 5291 */

        if (fabsf((mb->trot[1] * 180.0f) / 3.1415925f -
                  (mb->rot[1]  * 180.0f) / 3.1415925f) < 2.0f)          /* 5292 */
        {
            RotFvector(mb->rot, tv);                                    /* 5293 */
            sceVu0AddVector(mb->pos, mb->pos, tv);                      /* 5294 */
        }
    }
    else
    {
        g3dxVu0CopyVector(mb->pos, vw);                                 /* 5303 */
    }

    if (l1 < l2) { EaJumpTo(alg, adj); }                                /* 5304 */

    alg->pos_no    = 0;                                                 /* 5307 */
    alg->wait_time = 0.0f;
}

/* 0xba  jump when the player is inside the ghost's <u16 dist>-deep,
 * <u16 degrees>-wide view cone. */
void EJobB1A(ENEALG_WRK *alg)                                           /* 5314 */
{
    u_short dist = EaGetU16(alg);                                       /* 5318 */
    u_short rot  = EaGetU16(alg);                                       /* 5319 */
    u_short adj  = EaGetU16(alg);                                       /* 5320 */

    if (CheckEneView(&ene_wrk[alg->idx], (float)dist, (float)rot) != 0) /* 5326 */
    {
        EaJumpTo(alg, adj);
    }

    alg->pos_no    = 0;                                                 /* 5327 */
    alg->wait_time = 0.0f;                                              /* 5329 */
}

/* 0xbb  how much film the player is carrying: <sw> <num> <u16 adj>.  sw 0
 * jumps at or below num, non-zero at or above. */
void EJobB1B(ENEALG_WRK *alg)                                           /* 5340 */
{
    u_char  sw  = EaGetU8(alg);                                         /* 5341 */
    u_char  num = EaGetU8(alg);                                         /* 5342 */
    u_short adj = EaGetU16(alg);

    if (sw == 0)                                                        /* 5348 */
    {
        if (plyr_wrk.charge_num <= num) { EaJumpTo(alg, adj); }         /* 5349 */
    }
    else
    {
        if (plyr_wrk.charge_num >= num) { EaJumpTo(alg, adj); }         /* 5351 */
    }

    alg->pos_no    = 0;                                                 /* 5353 */
    alg->wait_time = 0.0f;
}

/* 0xbc  a child ghost's HP threshold: <sw> <percent> <u16 adj>.  The first
 * child that satisfies it jumps and ends the opcode. */
void EJobB1C(ENEALG_WRK *alg)                                           /* 5363 */
{
    ENE_WRK *ew  = &ene_wrk[alg->idx];
    u_char   fl  = EaGetU8(alg);                                        /* 5370 */
    u_short  per = EaGetU8(alg);                                        /* 5371 */
    u_short  adj = EaGetU16(alg);                                       /* 5372 */
    int      i;
    int      n;

    if (ew->type != 2)                                                  /* 5378 */
    {
        for (i = 0; i < 3; i++)                                         /* 5379 */
        {
            if (ew->dat->child_ene[i] >= 0)                             /* 5380 */
            {
                n = SearchEneWrkNo(ew->type, ew->dat->child_ene[i]);    /* 5381 */
                if (n >= 0)                                             /* 5382 */
                {
                    ENE_WRK *ewo = &ene_wrk[n];

                    if (fl == 0)                                        /* 5384 */
                    {
                        if (ewo->st.hp <= ((u_int)ewo->dat->hp * (u_int)per) / 100) /* 5385 */
                        {
                            alg->pos_no    = 0;
                            alg->wait_time = 0.0f;
                            EaJumpTo(alg, adj);                         /* 5387 */
                            return;
                        }
                    }
                    else if (ewo->st.hp >= ((u_int)ewo->dat->hp * (u_int)per) / 100) /* 5390 */
                    {
                        alg->pos_no    = 0;
                        alg->wait_time = 0.0f;
                        EaJumpTo(alg, adj);                             /* 5391 */
                        return;
                    }
                }
            }
        }                                                               /* 5392 */
    }

    alg->pos_no    = 0;                                                 /* 5399 */
    alg->wait_time = 0.0f;                                              /* 5400 */
}

/* 0xbd  jump when the player is in condition <u8>. */
void EJobB1D(ENEALG_WRK *alg)                                           /* 5410 */
{
    u_char  cond = EaGetU8(alg);                                        /* 5411 */
    u_short adj  = EaGetU16(alg);                                       /* 5417 */

    if (plyr_wrk.cmn_wrk.st.cond == cond)                               /* 5418 */
    {
        EaJumpTo(alg, adj);
    }

    alg->pos_no    = 0;                                                 /* 5420 */
    alg->wait_time = 0.0f;
}

/* 0xbe  walk to a random point near the target.  <sw> 0 picks the point --
 * rnd(min..max) * 50 units away on a random bearing, keeping the ghost's own
 * floor height -- and non-zero steps towards it at <spd> * the walk speed,
 * jumping to <u16 adj> once it arrives. */
void EJobB1E(ENEALG_WRK *alg)                                           /* 5427 */
{
    static float pos[10][4];                                            /* bss 478b70 */

    ENE_WRK   *ew   = &ene_wrk[alg->idx];
    MOVE_BOX  *mb   = &ew->mbox;
    PLCMN_WRK *pcmw = ew->target;
    u_char     sw;
    u_char     min;
    u_char     max;
    u_char     spd;
    u_short    adj;
    float      l1;
    float      l2;
    float      tv[4];
    float      tr[4];

    sw  = EaGetU8(alg);                                                 /* 5429 */
    min = EaGetU8(alg);                                                 /* 5434 */
    max = EaGetU8(alg);                                                 /* 5437 */
    spd = EaGetU8(alg);                                                 /* 5438 */
    adj = EaGetU16(alg);                                                /* 5439 */

    if (sw == 0)                                                        /* 5441 */
    {
        _SetVector(tv, 0.0f, 0.0f,
                   (float)GetRndSP(min, max - min) * 50.0f, 0.0f);      /* 5447 */
        _SetVector(tr, 0.0f,
                   (float)GetRndSP(0, 36) * 0.174532905f - 3.1415925f,
                   0.0f, 0.0f);                                         /* 5448 */
        RotLimitChk(&tr[1]);                                            /* 5449 */
        RotFvector(tr, tv);                                             /* 5450 */
        sceVu0AddVector(tv, pcmw->mbox.pos, tv);                        /* 5451 */

        /* Keep the ghost's own height -- the destination is a floor point. */
        {
            float y = mb->pos[1];                                       /* 5452 */
            g3dxVu0CopyVector(pos[alg->idx], tv);                       /* 5453 */
            pos[alg->idx][1] = y;
        }
        alg->pos_no = 0;
    }
    else
    {
        l1 = GetDistV(mb->pos, pos[alg->idx]);                          /* 5456 */
        l2 = (float)((u_int)ew->dat->wspd * (u_int)spd);                /* 5457 */

        if (l1 > l2)                                                    /* 5459 */
        {
            _SetVector(tv, 0.0f, 0.0f, l2 * ew->reso, 0.0f);            /* 5460 */
            GetTrgtRot(mb->pos, pos[alg->idx], tr, 2);                  /* 5461 */
            RotFvector(tr, tv);                                         /* 5462 */
            sceVu0AddVector(mb->pos, mb->pos, tv);                      /* 5463 */
        }
        else
        {
            g3dxVu0CopyVector(mb->pos, pos[alg->idx]);
        }

        if (l1 < l2) { EaJumpTo(alg, adj); }                            /* 5468 */
        alg->pos_no = 0;                                                /* 5469 */
    }

    alg->wait_time = 0.0f;                                              /* 5472 */
}

/* 0xbf  counter compare: <sw> <n> <value> <u16 adj>.  sw 0 jumps at or below,
 * non-zero at or above. */
void EJobB1F(ENEALG_WRK *alg)                                           /* 5483 */
{
    u_char  sw  = EaGetU8(alg);                                         /* 5484 */
    u_char  no  = EaGetU8(alg);                                         /* 5485 */
    u_char  num = EaGetU8(alg);                                         /* 5486 */
    u_short adr = EaGetU16(alg);

    if (sw == 0)                                                        /* 5492 */
    {
        if (alg->cnt[no] <= num) { EaJumpTo(alg, adr); }                /* 5493 */
    }
    else
    {
        if (alg->cnt[no] >= num) { EaJumpTo(alg, adr); }                /* 5495 */
    }

    alg->pos_no    = 0;                                                 /* 5497 */
    alg->wait_time = 0.0f;
}

/* 0xc0  jump while any child ghost is still on the field. */
void EJobB20(ENEALG_WRK *alg)                                           /* 5504 */
{
    ENE_WRK *ew  = &ene_wrk[alg->idx];
    u_short  adr = EaGetU16(alg);                                       /* 5510 */
    int      i;
    int      act = 0;

    if (ew->type != 2)                                                  /* 5516 */
    {
        for (i = 0; i < 3; i++)                                         /* 5517 */
        {
            if (ew->dat->child_ene[i] >= 0 &&                           /* 5518 */
                SearchEneWrkNo(ew->type, ew->dat->child_ene[i]) >= 0)   /* 5519 */
            {
                act++;                                                  /* 5520 */
            }
        }

        if (act == 0)                                                   /* 5524 */
        {
            alg->pos_no    = 0;
            alg->wait_time = 0.0f;
            return;
        }
        EaJumpTo(alg, adr);                                             /* 5525 */
    }

    alg->pos_no    = 0;                                                 /* 5526 */
    alg->wait_time = 0.0f;                                              /* 5530 */
}

/* 0xc1  jump when every resident child ghost is on action <u8>. */
void EJobB21(ENEALG_WRK *alg)                                           /* 5538 */
{
    ENE_WRK *ew  = &ene_wrk[alg->idx];
    u_char   act = EaGetU8(alg);                                        /* 5545 */
    u_short  adr = EaGetU16(alg);                                       /* 5546 */
    int      i;
    int      n;
    int      cldnum = 0;
    int      oknum  = 0;

    if (ew->type != 2)                                                  /* 5552 */
    {
        for (i = 0; i < 3; i++)                                         /* 5553 */
        {
            if (ew->dat->child_ene[i] >= 0)                             /* 5554 */
            {
                n = SearchEneWrkNo(ew->type, ew->dat->child_ene[i]);    /* 5555 */
                if (n >= 0)                                             /* 5556 */
                {
                    cldnum++;
                    if (ene_wrk[n].act_no == act) { oknum++; }          /* 5558 */
                }
            }
        }                                                               /* 5564 */

        if (oknum != cldnum)
        {
            alg->pos_no    = 0;
            alg->wait_time = 0.0f;
            return;
        }
        EaJumpTo(alg, adr);                                             /* 5565 */
    }

    alg->pos_no    = 0;                                                 /* 5566 */
    alg->wait_time = 0.0f;                                              /* 5569 */
}

/* 0xc2  line of sight from the ghost's neck to the target's head: <sw> 0
 * jumps when nothing is in the way, non-zero when something is. */
void EJobB22(ENEALG_WRK *alg)                                           /* 5576 */
{
    ENE_WRK   *ew  = &ene_wrk[alg->idx];
    PLCMN_WRK *pcw = ew->target;
    u_char     sw  = EaGetU8(alg);                                      /* 5579 */
    u_short    adr = EaGetU16(alg);                                     /* 5582 */
    u_char     ret;

    ret = (u_char)(MhCtlHitLineCheck(ew->mpos.p0, pcw->headpos,
                                     pcw->pr_info.area_no) != 0);       /* 5583 */
    ret |= (u_char)(MapHitLineCheck(ew->mpos.p0, pcw->floor,
                                    pcw->headpos, pcw->floor, 0.0f) != 0);

    if (sw == 0)                                                        /* 5589 */
    {
        if (ret == 0) { EaJumpTo(alg, adr); }                           /* 5590 */
    }
    else
    {
        if (ret != 0) { EaJumpTo(alg, adr); }                           /* 5592 */
    }

    alg->pos_no    = 0;                                                 /* 5593 */
    alg->wait_time = 0.0f;                                              /* 5598 */
}

/* 0xc3  four-way branch on the algorithm branch request: 0 and 1 (and 0xf0)
 * take adj1, 2 adj2, 3 adj3, 4 adj4. */
void EJobB23(ENEALG_WRK *alg)                                           /* 5608 */
{
    u_short adj1 = EaGetU16(alg);                                       /* 5609 */
    u_short adj2 = EaGetU16(alg);                                       /* 5610 */
    u_short adj3 = EaGetU16(alg);                                       /* 5611 */
    u_short adj4 = EaGetU16(alg);

    switch (alg->branch)                                                /* 5617 */
    {
    case 1:
    case 0xf0:
        EaJumpTo(alg, adj1);                                            /* 5625 */
        break;
    case 2:
        EaJumpTo(alg, adj2);                                            /* 5628 */
        break;
    case 3:
        EaJumpTo(alg, adj3);                                            /* 5631 */
        break;
    case 4:
        EaJumpTo(alg, adj4);                                            /* 5633 */
        break;
    default:                                                            /* 5624 */
        break;
    }

    alg->pos_no    = 0;
    alg->branch    = 0;                                                 /* 5636 */
    alg->wait_time = 0.0f;                                              /* 5638 */
}

/* 0xc4  as 0xa8, but a call rather than a jump: the return cursor is pushed
 * so the target can come back with 0x24. */
void EJobB24(ENEALG_WRK *alg)                                           /* 5645 */
{
    ENE_WRK  *ew   = &ene_wrk[alg->idx];
    MOVE_BOX *pcmw = (MOVE_BOX *)&ew->target->mbox;
    MOVE_BOX *mb   = &ene_wrk[alg->idx].mbox;
    u_char    sw   = EaGetU8(alg);                                      /* 5653 */
    u_short   dist = EaGetU16(alg);                                     /* 5654 */
    u_short   adj  = EaGetU16(alg);                                     /* 5655 */
    int       hit;

    if (sw == 0)                                                        /* 5661 */
    {
        hit = (GetDistV(pcmw->pos, mb->pos) < (float)dist);             /* 5662 */
    }
    else
    {
        hit = ((float)dist <= GetDistV(pcmw->pos, mb->pos));            /* 5664 */
    }

    if (hit)                                                            /* 5667 */
    {
        *(P_INT *)alg->stack_p = alg->comm_add;                         /* 5668 */
        alg->stack_p++;
        EaJumpTo(alg, adj);                                             /* 5669 */
    }

    alg->pos_no    = 0;
    alg->wait_time = 0.0f;                                              /* 5672 */
}

/* 0xc5  jump when the player has just gone through a door. */
void EJobB25(ENEALG_WRK *alg)                                           /* 5682 */
{
    u_short adj = EaGetU16(alg);                                        /* 5687 */

    if (GetPlyrDoorFlg() != 0)                                          /* 5688 */
    {
        EaJumpTo(alg, adj);
    }

    alg->pos_no    = 0;                                                 /* 5690 */
    alg->wait_time = 0.0f;
}

/* 0xc6  the same, but only for door <u16 no>. */
void EJobB26(ENEALG_WRK *alg)                                           /* 5700 */
{
    u_short no  = EaGetU16(alg);                                        /* 5701 */
    u_short adj = EaGetU16(alg);

    if (GetPlyrDoorFlg() != 0 && plyr_wrk.door_no == no)                /* 5706 */
    {
        EaJumpTo(alg, adj);                                             /* 5707 */
    }

    alg->pos_no    = 0;                                                 /* 5709 */
    alg->wait_time = 0.0f;
}

/* 0xc7  jump when status 0x400000000 is up. */
void EJobB27(ENEALG_WRK *alg)                                           /* 5716 */
{
    ENE_WRK *ew  = &ene_wrk[alg->idx];
    u_short  adj = EaGetU16(alg);                                       /* 5720 */

    if ((ew->st.sta & 0x400000000L) != 0)                               /* 5726 */
    {
        EaJumpTo(alg, adj);
    }

    alg->pos_no    = 0;                                                 /* 5727 */
    alg->wait_time = 0.0f;                                              /* 5729 */
}

/* 0xc8  jump while the player is moving. */
void EJobB28(ENEALG_WRK *alg)                                           /* 5739 */
{
    u_short adj = EaGetU16(alg);                                        /* 5744 */

    if ((plyr_wrk.cmn_wrk.st.mvsta & 0xeL) != 0)                        /* 5745 */
    {
        EaJumpTo(alg, adj);
    }

    alg->pos_no    = 0;                                                 /* 5747 */
    alg->wait_time = 0.0f;
}

/* 0xc9  jump while the player is standing still. */
void EJobB29(ENEALG_WRK *alg)                                           /* 5757 */
{
    u_short adj = EaGetU16(alg);                                        /* 5762 */

    if ((plyr_wrk.cmn_wrk.st.mvsta & 1L) != 0)                          /* 5763 */
    {
        EaJumpTo(alg, adj);
    }

    alg->pos_no    = 0;                                                 /* 5765 */
    alg->wait_time = 0.0f;
}

/* 0xca  jump while the shutter is still open. */
void EJobB2A(ENEALG_WRK *alg)                                           /* 5775 */
{
    u_short adj = EaGetU16(alg);                                        /* 5781 */

    if (plyr_wrk.shutter_tm != 0)                                       /* 5782 */
    {
        EaJumpTo(alg, adj);
    }

    alg->pos_no    = 0;                                                 /* 5784 */
    alg->wait_time = 0.0f;
}

/* ==========================================================================
 *  0xe0..0xfe -- effects
 * ======================================================================== */

/* 0xe0, 0xe1  four operand bytes each, no code.  Whatever effect these two
 * used to raise was commented out before this build; the scripts still carry
 * the operands, so the cursor still has to step over them. */
void EJobE00(ENEALG_WRK *alg)                                           /* 5803 */
{
    alg->comm_add.pu8 += 4;                                             /* 5806 */

    alg->pos_no    = 0;                                                 /* 5835 */
    alg->wait_time = 0.0f;
}

void EJobE01(ENEALG_WRK *alg)                                           /* 5846 */
{
    alg->comm_add.pu8 += 4;                                             /* 5849 */

    alg->pos_no    = 0;                                                 /* 5881 */
    alg->wait_time = 0.0f;
}

/* 0xe2  sixteen operand bytes, likewise dead. */
void EJobE02(ENEALG_WRK *alg)                                           /* 5894 */
{
    alg->pos_no    = 0;                                                 /* 5901 */
    alg->wait_time = 0.0f;
    alg->comm_add.pu8 += 16;                                            /* 5909 */
}

/* 0xe3  drop status 0x400000 (the ghost's extra effect is running) and hold
 * for a frame. */
void EJobE03(ENEALG_WRK *alg)                                           /* 5916 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];

    if (alg->pos_no == 0)                                               /* 5926 */
    {
        alg->comm_add.pu8 += 1;                                         /* 5929 */
        ew->st.sta &= ~0x400000L;                                       /* 5931 */
        alg->wait_time = 1.0f;                                          /* 5933 */
        alg->pos_no++;
    }
    else if (alg->pos_no == 1)                                          /* 5940 */
    {
        alg->pos_no    = 0;                                             /* 5945 */
        alg->wait_time = 0.0f;
    }
}

/* 0xe4  fade the extra effect's weight up to <u8 percent> at 12.5% a frame. */
void EJobE04(ENEALG_WRK *alg)                                           /* 5951 */
{
    static float fper[10];                                              /* bss 478c10 */

    ENE_WRK *ew = &ene_wrk[alg->idx];
    u_char   no = alg->idx;                                             /* 5952 */
    float    add;

    if (alg->pos_no == 0)                                               /* 5961 */
    {
        fper[no] = (float)EaGetU8(alg) / 100.0f;                        /* 5963 */
        alg->wait_time = 1.0f;                                          /* 5964 */
        alg->pos_no++;
    }
    else if (alg->pos_no == 1)                                          /* 5965 */
    {
        add = ew->effw + 0.125f;                                        /* 5966 */

        if (add >= fper[no])                                            /* 5968 */
        {
            ew->effw = fper[no];                                        /* 5970 */
            alg->pos_no    = 0;
            alg->wait_time = 0.0f;
        }
        else
        {
            ew->effw = add;                                             /* 5973 */
            alg->wait_time = 1.0f;                                      /* 5975 */
        }
    }
}                                                                       /* 5977 */

/* 0xe5  fade it back out to nothing at 6.25% a frame. */
void EJobE05(ENEALG_WRK *alg)                                           /* 5983 */
{
    ENE_WRK *ew = &ene_wrk[alg->idx];                                   /* 5984 */
    float    sub;

    if (alg->pos_no == 0)                                               /* 5991 */
    {
        alg->pos_no = 1;                                                /* 5993 */
    }
    else if (alg->pos_no == 1)                                          /* 5994 */
    {
        sub = ew->effw - 0.0625f;                                       /* 5996 */

        if (sub <= 0.0f)                                                /* 6000 */
        {
            ew->effw = 0.0f;                                            /* 6001 */
            alg->wait_time = 0.0f;                                      /* 6003 */
            alg->pos_no    = 0;
            return;
        }
        ew->effw = sub;                                                 /* 5998 */
    }
    else
    {
        return;
    }

    alg->wait_time = 1.0f;                                              /* 6005 */
}

/* ==========================================================================
 *  Blink script
 *
 *  Seven opcodes, on their own cursor.  It runs every frame even while the
 *  status bits that freeze the main script are up, which is what keeps a held
 *  ghost's face and per-part visibility alive.
 * ======================================================================== */

/* 0x00  wait <frames>, with the same "0 means one frame" shape as the main
 * script's own wait opcode. */
void BJobL00(ENE_WRK *ew)                                               /* 6027 */
{
    ENEALG_WRK *alg = &ew->alg;
    u_char      time;

    switch (alg->bpos_no)                                               /* 6030 */
    {
    case 0:
        time = EaBGetU8(alg);                                           /* 6032 */
        if (time == 0)                                                  /* 6033 */
        {
            alg->bpos_no    = 2;                                        /* 6035 */
            alg->bwait_time = 1.0f;
        }
        else
        {
            alg->bpos_no++;
            alg->bwait_time = (float)time;
        }
        break;

    case 1:
        alg->bpos_no    = 0;                                            /* 6040 */
        alg->bwait_time = 0.0f;
        break;

    case 2:
        alg->bwait_time = 1.0f;                                         /* 6043 */
        break;

    default:
        break;
    }
}                                                                       /* 6046 */

/* 0x01  jump <u16 offset>. */
void BJobL01(ENE_WRK *ew)                                               /* 6054 */
{
    ENEALG_WRK *alg = &ew->alg;
    u_short     adj = EaBGetU16(alg);                                   /* 6060 */

    alg->bcomm_add.wrk = alg->bcomm_add_top + (intptr_t)(int)adj;       /* 6062 */
    alg->bpos_no    = 0;                                                /* 6063 */
    alg->bwait_time = 0.0f;
}

/* 0x02  four operand bytes, no code -- the same dead effect slot the main
 * script's 0xe0 has. */
void BJobL02(ENE_WRK *ew)                                               /* 6073 */
{
    ENEALG_WRK *alg = &ew->alg;                                         /* 6075 */

    alg->bcomm_add.pu8 += 4;                                            /* 6078 */

    alg->bpos_no    = 0;                                                /* 6091 */
    alg->bwait_time = 0.0f;
}

/* 0x03  jump while the ghost is fading. */
void BJobL03(ENE_WRK *ew)                                               /* 6100 */
{
    ENEALG_WRK *alg = &ew->alg;
    u_short     adj = EaBGetU16(alg);                                   /* 6102 */

    if ((ew->st.sta & 0x4000000L) != 0)                                 /* 6106 */
    {
        alg->bcomm_add.wrk = alg->bcomm_add_top + (intptr_t)(int)adj;
    }

    alg->bpos_no    = 0;                                                /* 6107 */
    alg->bwait_time = 0.0f;                                             /* 6109 */
}

/* 0x04  jump while the ghost is blinking. */
void BJobL04(ENE_WRK *ew)                                               /* 6118 */
{
    ENEALG_WRK *alg = &ew->alg;
    u_short     adj = EaBGetU16(alg);                                   /* 6120 */

    if ((ew->st.sta & 0x8000000L) != 0)                                 /* 6122 */
    {
        alg->bcomm_add.wrk = alg->bcomm_add_top + (intptr_t)(int)adj;
    }

    alg->bpos_no    = 0;                                                /* 6123 */
    alg->bwait_time = 0.0f;                                             /* 6125 */
}

/* 0x05  one step of the aura's sine blink.  tr2_cnt is a degree counter that
 * wraps at 360; freq is the amplitude and base the floor. */
void BJobL05(ENE_WRK *ew)                                               /* 6132 */
{
    ENEALG_WRK *alg = &ew->alg;

    ew->tr2_rate_alg = (u_char)
        ((char)(int)(sinf(((float)ew->tr2_cnt * 3.1415925f) / 180.0f) *
                     (float)ew->tr2_freq) + ew->tr2_base);              /* 6135 */
    ew->tr2_cnt = (u_short)(((u_int)ew->tr2_cnt + (u_int)ew->tr2_add) % 360); /* 6136 */

    alg->bpos_no    = 0;                                                /* 6138 */
    alg->bwait_time = 0.0f;
}

/* 0x06  no operation. */
void BJobL06(ENE_WRK *ew)                                               /* 6146 */
{
    ENEALG_WRK *alg = &ew->alg;

    alg->bpos_no    = 0;                                                /* 6159 */
    alg->bwait_time = 0.0f;
}

/* ==========================================================================
 *  Death dissolve
 * ======================================================================== */

/* Start the two parts-deform effects that dissolve a dying ghost.  They read
 * d_pd / d_pd2 (and the two wave constants) every frame, so the script drives
 * them by writing those rather than by calling back. */
static void EnemyDeadPDeformCall(ENE_WRK *ew)                           /* 6178 */
{
    static float WaveSpeed = 1.79999995f;                               /* sdata 3f0258 */
    static float WaveRate  = 0.910000026f;                              /* sdata 3f025c */

    float sclx = 0.399999976f;                                          /* 6179 */
    float scly = 0.899999976f;

    ResetEffects(ew->pdf);                                              /* 6188 */
    ResetEffects(ew->pdf2);

    ew->pdf  = CallPartsDeform5(0x13, sclx, scly, ew, &ew->d_pd);       /* 6198 */
    ew->pdf2 = CallPartsDeform5_2(0x18, sclx, scly, ew, &ew->d_pd2,
                                  &WaveSpeed, &WaveRate);
}

static void EnemyDeadPDeformReset(ENE_WRK *ew)                          /* 6208 */
{
    ResetEffects(ew->pdf);                                              /* 6209 */
    ResetEffects(ew->pdf2);
}

/* The ghost's own directional light colour.  `amb` is taken and ignored --
 * the ROM never writes an ambient term here. */
void SetEnemyParallelLight(ENE_WRK *ew, float r, float g, float b, float amb) /* 6214 */
{
    (void)amb;
    _SetVector(ew->directionaldiffuse, r, g, b, 0.0f);                  /* 6218 */
}
