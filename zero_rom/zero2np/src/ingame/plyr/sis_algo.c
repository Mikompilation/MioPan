/* ==========================================================================
 *  ingame/plyr/sis_algo.c
 *
 *  Companion behaviour-script interpreter.  sister.c owns where she is; this
 *  owns what she does once a ghost is close enough to matter -- SetSisterStatus()
 *  flips sis_algo.amode to 2 and from then on SisterMain() dispatches here
 *  instead of to the follow logic.
 *
 *  ---- the machine ---------------------------------------------------------
 *  The script is SIS_ALG_OBJ, a byte stream loaded by sis_mdl.c.  It opens
 *  with a table of u16 entry offsets; SisActSet(n) reads entry n and points
 *  the cursor at it.  After that SisAlgCtrl() runs one opcode per iteration:
 *
 *      job_no    = the opcode currently executing
 *      pos_no    = sub-step within it; 0 means "fetch the next opcode"
 *      wait_time = frames to hold before the next iteration; 0xff ends the run
 *      comm_add  = the cursor           comm_add_top = the script base
 *
 *  Handlers leave pos_no = 0 and wait_time = 0 to fall straight through to
 *  the next opcode in the same frame.  Only SaWait() parks a real frame
 *  count, and only SaEndAct() sets 0xff.  Every jump operand is a u16 offset
 *  from comm_add_top, so the script is position independent.
 *
 *  Multi-byte operands are little-endian pairs assembled by hand
 *  (lo + hi * 0x100) rather than read as a u16 -- the script is not aligned.
 *
 *  ---- comparison operands -------------------------------------------------
 *  The conditional opcodes share a shape: a one-byte sense, the value(s) to
 *  test, then a u16 jump target.  Sense 0 and sense non-0 select which way
 *  round the comparison goes, and the ROM is not consistent about whether the
 *  jump is taken on true or on false -- SaEneDist/SaPlyrDist/SaEneOutTime
 *  jump when the test passes, SaGetEneNum/SaRndJump when it fails.  Preserved
 *  as found; the scripts are authored against this.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), sis_algo.o
 *  0x00259a80..0x0025aa53.
 * ======================================================================== */

#include "sis_algo.h"

#include <math.h>

#include "sis_mdl.h"
#include "sister.h"
#include "unit_ctl.h"
#include "../../common/utility.h"
#include "../../common/variable.h"
#include "../../system/os/system.h"
#include "../enemy/enemy.h"
#include "../../graphics/graph3d/g3dxVu0.h" /* g3dxVu0CopyVector */

SIS_LOCAL_ALG sis_lalg;                 /* data 3450c0 */

typedef void (*SIS_CTRL_FUNC)(SISALG_WRK *alg);

static SIS_CTRL_FUNC SisCtrlTbl[20] =   /* data 345070 */
{
    SaEndAct,       SaLoopSet,      SaLoopEnd,      SaJump,
    SaNeckPos,      SaAnimSet,      SaAnimEnd,      SaEneDist,
    SaAlgoChg,      SaAlgoTrace,    SaRotEneDir,    SaObjHit,
    SaEneOutTime,   SaPlyrDist,     SaWait,         SaGetEneNum,
    SaSetDirection, SaReqSE,        SaRndJump,      SaSetMvsta
};

/* Little-endian u16 fetched a byte at a time -- script operands are not
 * aligned, so the ROM never reads them as a halfword. */
static u_short SaGetU16(SISALG_WRK *alg)
{
    u_char lo = alg->comm_add.pu8[0];
    u_char hi = alg->comm_add.pu8[1];
    alg->comm_add.pu8 += 2;
    return (u_short)((u_int)lo + (u_int)hi * 0x100);
}

static u_char SaGetU8(SISALG_WRK *alg)
{
    u_char v = *alg->comm_add.pu8;
    alg->comm_add.pu8++;
    return v;
}

/* Jumps are u16 offsets from the script base, never absolute. */
static void SaJumpTo(SISALG_WRK *alg, u_short adj)
{
    alg->comm_add.wrk = alg->comm_add_top + (intptr_t)(int)adj;
}

/* ==========================================================================
 *  Machine
 * ======================================================================== */

void SisActSet(u_char alg_no)                                           /* 95 */
{
    SISALG_WRK *alg = &sis_wrk.alg;
    u_char     *top = (u_char *)sis_mdlGetAlgAdrs();

    alg->data_addr    = (intptr_t)top;
    alg->comm_add_top = alg->data_addr;
    alg->stack_p      = alg->stack_b;
    alg->wait_time    = 1;
    alg->pos_no       = 0;

    /* Entry table at the head of the script: one u16 offset per entry. */
    alg->comm_add.wrk = alg->data_addr +
                        (intptr_t)((u_int)top[alg_no * 2] +
                                   (u_int)top[alg_no * 2 + 1] * 0x100);  /* 112 */
}

void ReqSisAlgo(u_char no)                                              /* 141 */
{
    SisActSet(no);                                                      /* 142 */
}

/* Runs opcodes until one asks for a real wait, or the script ends.
 *
 * The frame's single decrement happens once, on entry -- `next` then just
 * carries each handler's parting wait_time round to the top of the loop.
 * That is what lets a chain of fall-through opcodes (all of which leave 0)
 * run to completion inside one frame: only a non-zero value returns, and only
 * SaEndAct()'s 0xff breaks out. */
void SisAlgCtrl(void)                                                   /* 116 */
{
    SISALG_WRK *alg = &sis_wrk.alg;

    if (alg->data_addr == 0 || alg->wait_time == 0)
    {
        return;
    }

    u_char next = (u_char)(alg->wait_time - 1);

    do
    {
        alg->wait_time = next;
        if (alg->wait_time != 0)
        {
            return;                     /* still waiting out a SaWait */
        }

        if (alg->pos_no == 0)
        {
            alg->job_no = *alg->comm_add.pu8;
            alg->comm_add.pu8++;
        }

        SisCtrlTbl[alg->job_no](alg);
        next = alg->wait_time;
    } while (alg->wait_time != 0xff);

    alg->pos_no    = 0;
    alg->wait_time = 0;                                                 /* 136 */
}

void SisAlgoMain(void)                                                  /* 146 */
{
    CountAlgTime();
    SisAlgCtrl();

    /* Whatever the script decided, a blocked state still has to keep the
     * player from walking through her. */
    if ((sis_wrk.cmn_wrk.st.mvsta & 0x61a8000) != 0)                    /* 149 */
    {
        SisterNoMove();                                                 /* 150 */
    }                                                                   /* 151 */
}

/* Live, hostile ghosts within `dist` of her.  Type 2 (scripted) ghosts are
 * excluded, as are ghosts that are not ENE_STATUS_ACT or already dead. */
int EneDistCount(float dist)                                            /* 155 */
{
    int num = 0;
    u_char i;

    for (i = 0; i < 10; i++)
    {
        ENE_WRK *ew = &ene_wrk[i];

        if (ew->type < 2 && ew->status == ENE_STATUS_ACT && ew->st.hp != 0 &&
            GetDistV(sis_wrk.cmn_wrk.mbox.pos, ew->mbox.pos) < dist)
        {
            num++;
        }
    }

    return num;                                                         /* 171 */
}

/* Frames since the last time a ghost was both flagged present on the player
 * and actually within 8000 units of her.  SaEneOutTime() branches on it, so a
 * script can wait out "the room has been clear for a while". */
void CountAlgTime(void)                                                 /* 175 */
{
    if ((plyr_wrk.cmn_wrk.st.sta & 0x20) == 0 || EneDistCount(8000.0f) == 0)
    {
        sis_lalg.ghost_erase_tm++;
    }
    else
    {
        sis_lalg.ghost_erase_tm = 0;
    }                                                                   /* 182 */
}

/* ==========================================================================
 *  Opcode handlers
 * ======================================================================== */

/* 0 -- end of script.  0xff is what breaks SisAlgCtrl()'s loop. */
void SaEndAct(SISALG_WRK *alg)                                          /* 194 */
{
    alg->pos_no    = 0;
    alg->wait_time = 0xff;
}

/* 1 -- <u16 count>.  Seeds the loop counter SaLoopEnd() counts down.
 * PAL runs at 5/6 the frame rate, so a frame count authored for NTSC is
 * divided by 1.2 to keep the wall-clock duration the same. */
void SaLoopSet(SISALG_WRK *alg)                                         /* 201 */
{
    u_short frm = SaGetU16(alg);

    if (GetPALMode() == 0)
    {
        alg->loop[0] = (float)frm;
    }
    else
    {
        alg->loop[0] = (float)frm / 1.2000000476837158f;
    }

    alg->pos_no    = 0;
    alg->wait_time = 0;                                                 /* 213 */
}

/* 2 -- <u16 target>.  Decrements the counter and jumps back while it lasts. */
void SaLoopEnd(SISALG_WRK *alg)                                         /* 219 */
{
    u_short adj = SaGetU16(alg);

    if (alg->loop[0] > 0.0f)
    {
        alg->loop[0] -= 1.0f;
    }
    else
    {
        /* Counter exhausted -- the ROM jumps here rather than falling out,
         * so the "target" is the loop *exit*. */
        SaJumpTo(alg, adj);
    }

    alg->pos_no    = 0;
    alg->wait_time = 0;                                                 /* 230 */
}

/* 3 -- <u16 target>.  Unconditional. */
void SaJump(SISALG_WRK *alg)                                            /* 236 */
{
    u_short adj = SaGetU16(alg);

    alg->pos_no    = 0;
    alg->wait_time = 0;
    SaJumpTo(alg, adj);                                                 /* 243 */
}

/* 4 -- no operands, no body.  The neck-target opcode was never implemented;
 * the ROM handler only clears the step state. */
void SaNeckPos(SISALG_WRK *alg)                                         /* 252 */
{
    alg->pos_no    = 0;
    alg->wait_time = 0;
}

/* 5 -- <u8 anime_no> <u8 frames>. */
void SaAnimSet(SISALG_WRK *alg)                                         /* 258 */
{
    u_char anime_no = SaGetU8(alg);
    u_char frame    = SaGetU8(alg);

    SetSisterAnime(anime_no, frame);

    alg->wait_time = 0;
    alg->pos_no    = 0;                                                 /* 266 */
}

/* 6 -- <u16 target>.  Jumps once the current animation has ended or looped. */
void SaAnimEnd(SISALG_WRK *alg)                                         /* 272 */
{
    u_short adj = SaGetU16(alg);

    if ((sis_wrk.cmn_wrk.st.sta & 0x6000) != 0)
    {
        SaJumpTo(alg, adj);
    }

    alg->pos_no    = 0;
    alg->wait_time = 0;                                                 /* 282 */
}

/* 7 -- <u8 sense> <u16 dist> <u16 target>.  Tests the nearest ghost's
 * distance, as maintained by NearEneInfo().  Jumps when the test passes. */
void SaEneDist(SISALG_WRK *alg)                                         /* 289 */
{
    u_char  sense = SaGetU8(alg);
    u_short dist  = SaGetU16(alg);
    u_short adj   = SaGetU16(alg);

    float fl = (float)dist;
    int   hit;

    if (sense == 0)
    {
        hit = sis_wrk.cmn_wrk.near_ene_dist < fl;   /* closer than    */
    }
    else
    {
        hit = fl < sis_wrk.cmn_wrk.near_ene_dist;   /* further than   */
    }

    if (hit)
    {
        SaJumpTo(alg, adj);
    }

    alg->pos_no    = 0;
    alg->wait_time = 0;                                                 /* 306 */
}

/* 8 -- <u8 amode>.  Hands her back to one of sister.c's own modes. */
void SaAlgoChg(SISALG_WRK *alg)                                         /* 312 */
{
    sis_algo.amode = SaGetU8(alg);

    alg->pos_no    = 0;
    alg->wait_time = 0;                                                 /* 319 */
}

/* 9 -- no operands.  Breaks off and goes looking for the player again. */
void SaAlgoTrace(SISALG_WRK *alg)                                       /* 327 */
{
    SetSisEscape();

    alg->pos_no    = 0;
    alg->wait_time = 0;                                                 /* 328 */
}

/* 10 -- <u16 degrees> <u16 target>.  Turns her towards the nearest ghost a
 * step at a time and jumps once she is within `degrees` of facing it.  Falls
 * through (no jump) while she is still turning, so the script re-enters this
 * opcode every frame. */
void SaRotEneDir(SISALG_WRK *alg)                                       /* 333 */
{
    float vw[4];
    float psrot;

    u_short deg = SaGetU16(alg);
    u_short adj = SaGetU16(alg);

    u_int eneno = sis_wrk.cmn_wrk.near_ene_no;

    if (eneno != 0xff)
    {
        if (ene_wrk[eneno].status != ENE_STATUS_ACT)
        {
            alg->pos_no    = 0;
            alg->wait_time = 0;
            return;
        }

        float rotf = ((float)deg * 3.1415927410125732f) / 180.0f;
        float rw   = GetPALMode() == 0 ? 0.1396263986825943f     /*  8 deg */
                                       : 0.1675516068935394f;    /* 9.6 deg */

        sis_wrk.spd[2] = 0.0f;

        GetTrgtRot(sis_wrk.cmn_wrk.mbox.pos, ene_wrk[eneno].mbox.pos, vw, 2);
        psrot = vw[1] - sis_wrk.cmn_wrk.mbox.rot[1];
        RotLimitChk(&psrot);

        if (fabs((double)psrot) <= (double)rw)
        {
            /* Same magnitude-not-signed snap SetSisTurn() has -- see the note
             * there; a target to the left is turned away from on the last
             * step and picked back up next frame. */
            if (psrot < 0.0f)
            {
                sis_wrk.cmn_wrk.mbox.rot[1] -= psrot;
            }
            else
            {
                sis_wrk.cmn_wrk.mbox.rot[1] += psrot;
            }
        }
        else if (psrot < 0.0f)
        {
            sis_wrk.cmn_wrk.mbox.rot[1] -= rw;
        }
        else
        {
            sis_wrk.cmn_wrk.mbox.rot[1] += rw;
        }

        RotLimitChk(&sis_wrk.cmn_wrk.mbox.rot[1]);

        psrot = vw[1] - sis_wrk.cmn_wrk.mbox.rot[1];
        RotLimitChk(&psrot);

        sis_wrk.cmn_wrk.mbox.brot[1] = sis_wrk.cmn_wrk.mbox.rot[1];
        g3dxVu0CopyVector(sis_wrk.wpos, sis_wrk.cmn_wrk.mbox.pos);

        if (fabsf(psrot) > rotf)
        {
            alg->pos_no    = 0;
            alg->wait_time = 0;
            return;                     /* not there yet */
        }
    }

    /* Facing it -- or there is no ghost at all, which also falls through. */
    SaJumpTo(alg, adj);
    alg->pos_no    = 0;
    alg->wait_time = 0;                                                 /* 397 */
}

/* 11 -- <u16 target>.  Jumps when the last position update hit a wall. */
void SaObjHit(SISALG_WRK *alg)                                          /* 403 */
{
    u_short adj = SaGetU16(alg);

    if (sis_lalg.hit != 0)
    {
        SaJumpTo(alg, adj);
    }

    alg->pos_no    = 0;
    alg->wait_time = 0;                                                 /* 412 */
}

/* 12 -- <u8 sense> <u16 frames> <u16 target>.  Branches on how long the room
 * has been clear of ghosts.  Jumps when the test passes. */
void SaEneOutTime(SISALG_WRK *alg)                                      /* 419 */
{
    u_char  sense = SaGetU8(alg);
    u_short frm   = SaGetU16(alg);
    u_short adj   = SaGetU16(alg);

    u_long v = (u_long)(int)frm;
    int    hit;

    if (sense == 0)
    {
        hit = sis_lalg.ghost_erase_tm < v;
    }
    else
    {
        hit = v < sis_lalg.ghost_erase_tm;
    }

    if (hit)
    {
        SaJumpTo(alg, adj);
    }

    alg->pos_no    = 0;
    alg->wait_time = 0;                                                 /* 436 */
}

/* 13 -- <u8 sense> <u16 dist> <u16 target>.  Her distance to the player.
 * Jumps when the test passes. */
void SaPlyrDist(SISALG_WRK *alg)                                        /* 439 */
{
    u_char  sense = SaGetU8(alg);
    u_short d     = SaGetU16(alg);
    u_short adj   = SaGetU16(alg);

    float fl   = (float)d;
    float dist = GetDistV(sis_wrk.cmn_wrk.mbox.pos, plyr_wrk.cmn_wrk.mbox.pos);
    int   hit;

    if (sense == 0)
    {
        hit = dist < fl;
    }
    else
    {
        hit = fl < dist;
    }

    if (hit)
    {
        SaJumpTo(alg, adj);
    }

    alg->pos_no    = 0;
    alg->wait_time = 0;                                                 /* 462 */
}

/* 14 -- <u8 frames>.  The only opcode that actually parks a frame count.
 *
 * pos_no walks 0 -> 1 (waiting) -> 0, or 0 -> 2 for an operand of 0, which
 * means "wait forever": step 2 re-arms wait_time every frame and never
 * advances, so only an external amode change gets the script moving again. */
void SaWait(SISALG_WRK *alg)                                            /* 465 */
{
    if (alg->pos_no == 0)
    {
        u_char time = SaGetU8(alg);

        if (time == 0)
        {
            alg->wait_time = 1;
            alg->pos_no    = 2;
            return;
        }

        if (GetPALMode() == 0)
        {
            alg->wait_time = time;
            alg->pos_no++;
            return;
        }

        /* PAL: scale the NTSC frame count down, and never round to zero --
         * a wait_time of 0 would read as "run the next opcode" back in
         * SisAlgCtrl().  (The ROM's 2^31 bias subtraction around this cast is
         * GCC's float-to-unsigned helper, not source: `time` is a u_char, so
         * the biased path is unreachable.) */
        float f = (float)time / 1.2000000476837158f;
        alg->wait_time = ((int)f & 0xff) != 0 ? (u_char)(int)f : 1;
        alg->pos_no++;
        return;
    }

    if (alg->pos_no == 1)
    {
        alg->pos_no    = 0;             /* wait served -- next opcode */
        alg->wait_time = 0;
        return;
    }

    if (alg->pos_no == 2)
    {
        alg->wait_time = 1;             /* hold here indefinitely */
    }                                                                   /* 496 */
}

/* 15 -- <u8 sense> <u16 dist> <u8 count> <u16 target>.  Counts live ghosts
 * within `dist` and branches on the total.  Note the inverted sense: this one
 * jumps when the test *fails*. */
void SaGetEneNum(SISALG_WRK *alg)                                       /* 498 */
{
    u_char  sense = SaGetU8(alg);
    u_short dist  = SaGetU16(alg);
    u_char  num   = SaGetU8(alg);
    u_short adj   = SaGetU16(alg);

    u_char n = (u_char)EneDistCount((float)dist);
    int    hit;

    if (sense == 0)
    {
        hit = num < n;                  /* more than `num` nearby */
    }
    else
    {
        hit = n < num;                  /* fewer than `num`       */
    }

    if (!hit)
    {
        SaJumpTo(alg, adj);
    }

    alg->pos_no    = 0;
    alg->wait_time = 0;                                                 /* 522 */
}

/* 16 -- <u8 id> <u8 divisor>.  Faces her at the player (id 0) or the nearest
 * ghost (id non-0).  A divisor of 0 snaps; otherwise the remaining angle is
 * divided by it and clamped to +/- 4 degrees per frame. */
void SaSetDirection(SISALG_WRK *alg)                                    /* 526 */
{
    float vw[4];

    u_char id   = SaGetU8(alg);
    u_char step = SaGetU8(alg);

    MOVE_BOX *tgt;

    if (id == 0)
    {
        tgt = &plyr_wrk.cmn_wrk.mbox;
    }
    else
    {
        u_int eneno = sis_wrk.cmn_wrk.near_ene_no;
        if (eneno == 0xff)
        {
            alg->pos_no    = 0;
            alg->wait_time = 0;
            return;                     /* nothing to face */
        }
        tgt = &ene_wrk[eneno].mbox;
    }

    if (step == 0)
    {
        /* Straight into mbox.rot -- mode 3 writes all three axes. */
        GetTrgtRot(sis_wrk.cmn_wrk.mbox.pos, tgt->pos,
                   sis_wrk.cmn_wrk.mbox.rot, 3);
        alg->pos_no    = 0;
        alg->wait_time = 0;
        return;
    }

    GetTrgtRot(sis_wrk.cmn_wrk.mbox.pos, tgt->pos, vw, 3);
    vw[1] -= sis_wrk.cmn_wrk.mbox.rot[1];
    RotLimitChk(&vw[1]);

    const float lim = 0.0698131993412971f;      /* 4 degrees */
    float adjr = vw[1];
    if (vw[1] > 0.0f)
    {
        if (vw[1] > lim)  adjr = lim;
    }
    else
    {
        if (vw[1] < -lim) adjr = -lim;
    }

    sis_wrk.cmn_wrk.mbox.rot[1] += adjr / (float)step;
    RotLimitChk(&sis_wrk.cmn_wrk.mbox.rot[1]);

    alg->pos_no    = 0;
    alg->wait_time = 0;                                                 /* 569 */
}

/* 17 -- <u8 se_no>. */
void SaReqSE(SISALG_WRK *alg)                                           /* 571 */
{
    u_char no = SaGetU8(alg);

    ReqSisBankPlay(no, 1, 1, 0, &sis_wrk.s3d);

    alg->wait_time = 0;
    alg->pos_no    = 0;                                                 /* 581 */
}

/* 18 -- <u8 sense> <u8 percent> <u16 target>.  Rolls 1..99.  Like
 * SaGetEneNum, the jump is taken when the test fails. */
void SaRndJump(SISALG_WRK *alg)                                         /* 584 */
{
    u_char  sense   = SaGetU8(alg);
    u_char  percent = SaGetU8(alg);
    u_short adj     = SaGetU16(alg);

    int hit;

    if (sense == 0)
    {
        hit = (int)percent < GetRndSP(1, 99);
    }
    else
    {
        hit = GetRndSP(1, 99) < (int)percent;
    }

    if (!hit)
    {
        SaJumpTo(alg, adj);
    }

    alg->pos_no    = 0;
    alg->wait_time = 0;                                                 /* 605 */
}

/* 19 -- <u8 set> <u32 bits>.  Sets or clears mvsta bits directly.
 *
 * Alone among the handlers this one never assigns pos_no / wait_time.  It
 * does not need to: both are already 0 when a handler is entered, so leaving
 * them alone falls through to the next opcode exactly as an explicit
 * "pos_no = 0; wait_time = 0;" would. */
void SaSetMvsta(SISALG_WRK *alg)                                        /* 613 */
{
    u_char set = SaGetU8(alg);

    u_char b0 = alg->comm_add.pu8[0];
    u_char b1 = alg->comm_add.pu8[1];
    u_char b2 = alg->comm_add.pu8[2];
    u_char b3 = alg->comm_add.pu8[3];
    alg->comm_add.pu8 += 4;

    u_int attr = (u_int)b0 + (u_int)b1 * 0x100 +
                 (u_int)b2 * 0x10000 + (u_int)b3 * 0x1000000;

    if (set != 0)
    {
        sis_wrk.cmn_wrk.st.mvsta |= attr;
    }
    else
    {
        /* The clear is masked to 32 bits in the ROM, so the high half of the
         * 64-bit mvsta survives regardless of what the script asked for. */
        sis_wrk.cmn_wrk.st.mvsta &= ~(u_long)attr & 0xffffffffULL;
    }                                                                   /* 623 */
}
