// FILE: /home/zero_rom/zero2np/src/ingame/photo/n_equip_tray.c
//
// The camera's equip tray -- the column of sub-function icons down the left of
// the viewfinder, the spirit-power accumulator under it, and the stock items
// that power buys.
//
// Three slots are fitted (mEquipFunc), one is selected (mSlctNo), and the
// selected one can be put "on the shutter" with SetUp().  Firing it costs
// stock items; stock is earned by absorbing spirit power, 1000 units per item.
// The tray is one of the two places a shot's damage is decided -- GetDmgRate()
// scales it and SetEffect() applies whatever else the fitted function does to
// the ghost (slow, paralyse, seal, reveal, trace, knock back).
//
// Everything in here is per-frame animated: the icon column slides when the
// selector moves (mMode 1 and mNowOffset), the accumulator ball spins
// (mAccumulateBollRot), the suction mouth grows while power is arriving, and
// the burst-shot mark over the capture ring pulses.  Reset() re-arms all of it
// as the finder comes up; Work() steps it; Draw() and RenzMarkDraw() are the
// two draws CNPlyrCamera makes.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), n_equip_tray.o.
// Trailing /* NNN */ comments are the original source line numbers.

#include <stdio.h>                              /* printf                      */

#include "n_equip_tray.h"
#include "n_finder_dat.h"                       /* n_finder_dat                */
#include "../enemy/enemy.h"                     /* ENE_WRK / SetEne*           */
#include "../plyr/player.h"                     /* InFinderMode                */
#include "../../graphics/effect/effect_ene.h"   /* EneHitEffectReq             */
#include "../../graphics/graph2d/g2d_draw.h"    /* DISP_SPRT / DISP_SQAR       */
#include "../../system/os/system.h"             /* SystemBankPlay              */
#include "finder.h"                             /* FinderBankPlay              */

/* ---- GS register words -------------------------------------------------
 * The same ZBUF/ALPHA pair the rest of the finder HUD draws with: ZBUF with
 * the mask bit set reads depth but never writes it, ALPHA 0x44 is source-over
 * and 0x48 additive.
 *
 * The accumulator dial is the one place that leaves that pattern.  Two black
 * quads are drawn *with* depth writes and a pass-everything TEST, laying a
 * depth wedge over the dial; the rotating needle is then drawn against it with
 * ATST GREATER (so its transparent texels are skipped) and ZTST GEQUAL, which
 * is what clips the needle to the filled part of the arc. */
#define EQT_ZBUF_NO_WRITE   0x000000010a000118ULL
#define EQT_ZBUF_WRITE      0x000000000a000118ULL
#define EQT_ALPHA_BLEND     0x44
#define EQT_ALPHA_ADD       0x48
#define EQT_TEST_MASK_WRITE 0x0000000000030003ULL
#define EQT_TEST_MASK_READ  0x000000000005000dULL

/* ---- n_finder_dat[] indices -------------------------------------------
 * Named from how each one is used; the ROM has only the numbers. */
#define FD_TRAY_RAIL        6       /* two pieces, drawn behind the icons     */
#define FD_TRAY_BASE_FRONT  8
#define FD_TRAY_BASE_BACK   10      /* drawn first of the two                 */
#define FD_SUBFUNC_FLARE    11      /* the glow over the selected icon        */
#define FD_ACCUM_NEEDLE     14      /* rides on top of the dial               */
#define FD_STOCK_FRAME      15      /* [0..grade] one per holdable item       */
#define FD_STOCK_HOLE       19      /* [0..grade] the empty socket            */
#define FD_STOCK_ITEM       23      /* [0..held]  the spinning ball in it     */
#define FD_STOCK_COST       27      /* [0..cost]  what the fitted shot spends */
#define FD_SUCK_MOUTH       0x3a
#define FD_RENZ_MARK        0x3b    /* four pieces                            */
#define FD_ACCUM_DIAL       0x9d

/* equip_func_tbl[CAMERA_SUB_FUNC_NONE].pk2_no.  Not a sprite -- it is the "no
 * icon" sentinel every draw tests for, which is why an empty slot is skipped
 * rather than drawn with a blank. */
#define NEQUIP_PK2_NONE     24

/* How much banked power one stock item costs, and the per-frame ceiling on how
 * fast the accumulator drains into stock. */
#define EQT_GAGE_PER_STOCK  1000
#define EQT_GAGE_PER_FRAME  65

/* One absorbed particle is worth this much banked power.  effect_ene.o divides
 * the shot's suction power by the same figure to get its particle count. */
#define EQT_GAGE_PER_PARTICLE 45

/* The icon column's pitch, and the dial's sweep. */
#define EQT_SLOT_PITCH      45.0f
#define EQT_DIAL_SWEEP      90.0f

/* --------------------------------------------------------------------------
 *  Static tables
 * ------------------------------------------------------------------------ */

/* Per-level parameters, indexed by GetNowSubFuncLv().  Times are in frames on
 * the sub-tables and get multiplied by 30 at the call site (the ROM stores
 * them in half-seconds).  For the four damage functions NDmgMag applies to an
 * ordinary shot and SDmgMag to a shutter-chance one.
 *
 * Every table below is byte-identical to the ROM's, checked by diffing the
 * compiled .obj against the image, with one deliberate deviation: GCC 2.96-ee
 * rounded decimal float literals toward zero, so 13 words here (1.7, 2.2, 2.4,
 * 0.2, 0.3, 0.8, 3.2, 0.6) sit one ulp below what the host compiler produces
 * from the same decimal.  The decimals are what the original source had, which
 * is what a source reconstruction wants -- same choice acs_dat.c made. */
                                                                        /* sdata 3f34b0 */
SUB_FUNC_SI_PARAM CNEquipTraySave::sub_func_si_param[4] =
{
    { 12, 0 }, { 15, 1 }, { 25, 1 }, { 50, 1 }
};
                                                                        /* data 338878 */
SUB_FUNC_MAHI_PARAM CNEquipTraySave::sub_func_mahi_param[4] =
{
    { 8, 1, 1 }, { 12, 1, 1 }, { 20, 2, 1 }, { 34, 3, 1 }
};
                                                                        /* sdata 3f34b8 */
SUB_FUNC_SUB_PARAM CNEquipTraySave::sub_func_oso_param[4] =
{
    { 12 }, { 18 }, { 24 }, { 40 }
};
                                                                        /* sdata 3f34c0 */
SUB_FUNC_SUB_PARAM CNEquipTraySave::sub_func_seal_param[4] =
{
    { 10 }, { 20 }, { 30 }, { 50 }
};
                                                                        /* sdata 3f34c8 */
SUB_FUNC_SUB_PARAM CNEquipTraySave::sub_func_trace_param[4] =
{
    { 10 }, { 18 }, { 25 }, { 50 }
};
                                                                        /* data 338888 */
SUB_FUNC_KOUGEKI_PARAM CNEquipTraySave::sub_func_rei_param[4] =
{
    { 1.5f, 1.8f, 0, 0.32f },
    { 1.7f, 2.2f, 0, 0.32f },
    { 2.0f, 2.4f, 0, 0.32f },
    { 2.0f, 2.8f, 0, 0.32f }
};
                                                                        /* data 3388c8 */
SUB_FUNC_KOUGEKI_PARAM CNEquipTraySave::sub_func_koku_param[4] =
{
    { 1.0f, 1.5f,   0, 1.0f  },
    { 1.0f, 1.7f, 120, 0.32f },
    { 1.5f, 1.8f, 120, 0.32f },
    { 1.5f, 2.0f, 120, 0.32f }
};
                                                                        /* data 338908 */
SUB_FUNC_KOUGEKI_PARAM CNEquipTraySave::sub_func_metsu_param[4] =
{
    { 0.2f, 2.5f, 0, 0.3f },
    { 0.8f, 3.0f, 0, 0.3f },
    { 1.5f, 3.2f, 0, 0.3f },
    { 3.0f, 3.6f, 0, 0.3f }
};
                                                                        /* data 338948 */
SUB_FUNC_KOUGEKI_PARAM CNEquipTraySave::sub_func_ren_param[4] =
{
    { 2.0f, 1.0f, 0, 0.0f },
    { 2.5f, 0.8f, 0, 0.0f },
    { 3.0f, 0.6f, 0, 0.0f },
    { 4.0f, 0.5f, 0, 0.0f }
};

/* How many stock items fit, by mStockGrade. */                         /* sdata 3f34d0 */
char CNEquipTraySave::aStockMaxTbl[4] = { 1, 2, 3, 4 };

/* One row per CAMERA_SUB_FUNC_ENUM: what firing it costs, its viewfinder icon,
 * its tint, and whether it knocks the ghost back.  Row 0 is "nothing fitted"
 * and carries the NEQUIP_PK2_NONE sentinel; row 9 (Ren) is the only one that
 * does not knock back. */
                                                                        /* rdata 3c1c68 */
NEQUIP_FUNC_DAT CNEquipTrayWrk::equip_func_tbl[10] =
{
    { 0, NEQUIP_PK2_NONE, 0x82, 0x82, 0x82, 1 },    /* NONE  */
    { 1, 68,              0xb3, 0x8c, 0xba, 1 },    /* SLOW  */
    { 2, 66,              0x8e, 0xba, 0x8c, 1 },    /* STOP  */
    { 1, 67,              0xc0, 0xa3, 0x84, 1 },    /* VIEW  */
    { 2, 71,              0x9c, 0x9b, 0x8b, 1 },    /* TRACE */
    { 3, 70,              0x83, 0xb3, 0x9e, 1 },    /* SEAL  */
    { 2, 64,              0xcb, 0xc7, 0x94, 1 },    /* KOKU  */
    { 3, 63,              0x6f, 0xa2, 0xcd, 1 },    /* ZERO  */
    { 4, 65,              0xdc, 0x7b, 0x7b, 1 },    /* METSU */
    { 1, 69,              0x6c, 0x92, 0x81, 0 }     /* REN   */
};

/* The debug menu's live copy of the stock count.  Every path that changes
 * mStockNum mirrors it out here, and Work() copies it back in, which is what
 * makes the menu row editable. */
int dbg_stock_num;                                                      /* sdata 3f3508 */

/* --------------------------------------------------------------------------
 *  Saved half
 * ------------------------------------------------------------------------ */

int CNEquipTraySave::IsStockMax(void)
{
    return aStockMaxTbl[mStockGrade] <= mStockNum;
}

void CNEquipTrayWrk::GetSubFuncArray(char *equip_func)
{                                                                       /* 113 */
    for (int i = 0; i < 3; i++)                                         /* 114 */
    {
        equip_func[i] = mSave.mEquipFunc[i].Get();                      /* 115 */
    }                                                                   /* 116 */
}

/* The CAM_SUB_FUNC_GET macro opcode's landing point: it hands over all three
 * slots at once and the selector is left on the last fitted one. */
void CNEquipTrayWrk::SetSubFuncArray(char *equip_func)
{
    for (int i = 0; i < 3; i++)                                         /* 119 */
    {
        mSave.mEquipFunc[i].Set(equip_func[i]);

        if (mSave.mEquipFunc[i].Get() != 0)                             /* 120 */
        {
            mSave.mSlctNo.Set((char)i);
        }
    }
}

/* Steps the selector on to the next *fitted* slot and reports how far it
 * moved.  Empty slots are stepped over, so on a tray with one function this
 * returns 0 and the caller's slide animation is a no-op. */
int CNEquipTraySave::NextFuncSet(void)
{                                                                       /* 130 */
    int iPreNo = mSlctNo.Get();

    if (mEquipFunc[mSlctNo].Get() == 0)
    {
        PRINT_ASSERT("NextFuncSet() Illegal Arg Is Passed");             /* 136 */
    }

    do                                                                  /* 140 */
    {
        mSlctNo.LoopIncrement();
    }
    while (mEquipFunc[mSlctNo].Get() == 0);                             /* 147 */

    return mSlctNo.Get() - iPreNo;
}

int CNEquipTrayWrk::GetNowEquipFuncNum(void)
{                                                                       /* 154 */
    int iCnt = 0;

    for (int i = 0; i < 3; i++)                                         /* 155 */
    {
        if (mSave.mEquipFunc[i].Get() != 0)                             /* 156 */
        {
            iCnt++;                                                     /* 160 */
        }
    }

    return iCnt;                                                        /* 161 */
}

/* Registered by PhotoDmgChk2() for the particles the shot actually launched;
 * Work() banks them as EneDmgParticleSuctionNumGet() reports them landing. */
void CNEquipTrayWrk::SetRemainParticle(int iParticleNum)
{
    mRemainParticleNum += iParticleNum;                                 /* 165 */
}

/* Only mSave goes to the card -- the runtime half is rebuilt on load. */
void CNEquipTrayWrk::SetSave(MC_SAVE_DATA *save)
{                                                                       /* 168 */
    save->addr = (u_char *)&mSave;                                      /* 169 */
    save->size = sizeof(CNEquipTraySave);
}

void CNEquipTrayWrk::SetBattleFlg(int iFlg)
{
    mBattleFlg = (iFlg & 1);                                            /* 172 */
}

void CNEquipTraySave::Init(void)
{                                                                       /* 178 */
    mSlctNo.Set(0);                                                     /* 182 */
    mStockNum = 0;                                                      /* 187 */
    mStockGrade.Set(0);                                                 /* 189 */

    for (int i = 0; i < 3; i++)                                         /* 190 */
    {
        mEquipFunc[i].Set(0);
    }

    for (int i = 0; i < 10; i++)                                        /* 192 */
    {
        mSubFuncLv[i].Set(0);
    }

    mBankGage        = 0;                                               /* 194 */
    mAbsorbMultiRate = 1.0f;                                            /* 196 */
    mDispGage        = 0;                                               /* 197 */
}

void CNEquipTrayWrk::End(void)
{                                                                       /* 203 */
    mSave.End(mRemainParticleNum);
    mRemainParticleNum = 0;                                             /* 204 */
}

/* Banks whatever suction power never made it home as particles.  Note this is
 * silent -- no sound, no stock animation -- because the finder is already on
 * its way out by the time it runs. */
void CNEquipTraySave::End(int iRemainParticle)
{                                                                       /* 208 */
    if (mStockNum < aStockMaxTbl[mStockGrade] && mEquipFunc[mSlctNo].Get() != 0)
    {
        ConvertGage2StockNum(mBankGage +                                /* 231 */
            (int)((float)(iRemainParticle * EQT_GAGE_PER_PARTICLE) * mAbsorbMultiRate));
    }
}                                                                       /* 234 */

/* The tray never zooms the finder in this build. */
float CNEquipTrayWrk::GetTargetFOV(void)
{
    return 1.0f;                                                        /* 256 */
}

/* --------------------------------------------------------------------------
 *  Per-frame half
 * ------------------------------------------------------------------------ */

/* Re-arms the tray as the finder comes up.  mRot is seeded so the selected
 * slot is already at the front of the (unused in this build) 120-degree
 * carousel, and the accumulator ball is set spinning through a full turn. */
void CNEquipTrayWrk::Reset(void)
{                                                                       /* 261 */
    mMode = 0;                                                          /* 262 */
    mRot  = (float)-mSave.mSlctNo.Get() * 2.0943951f;   /* lit4 3ee61c = 2*PI/3 */

    mStockAnm.Init();
    mSubFuncAnmShot.Init();
    mGageAnmAlpha.Set(0);

    mGageUpFlg = 0;                                                     /* 267 */

    PutOut();                                                           /* 269 */

    mSuckMouthScale.Init();
    mSuckMouthAlpha.Init();

    mAccumulateBollRot.Set(-180.0f);
    mAccumulateBollRot.SetMax(180.0f);
    mAccumulateBollRot.SetMin(-180.0f);

    mAccumulateBollFlare.Init();
    mAccumulateBollFlare.BlinkOn();

    mSubFuncAnmBlink.Init();
    mSubFuncAnmBlinkNoSetup.Init();

    mNowOffset.Set(0);

    mRemainParticleNum = 0;                                             /* 285 */
    mShotAble          = 0;                                             /* 286 */
}

/* Takes the fitted function back off the shutter.  The burst-shot mark fades
 * out over 15 frames rather than snapping off. */
void CNEquipTrayWrk::PutOut(void)
{
    if (mSetupFlg != 0)                                                 /* 293 */
    {
        mSetupFlg = 0;                                                  /* 294 */
        mRenzMarkAlpha.SetAddVal((short)(-mRenzMarkAlpha.GetMax() / 15));
    }
}

/* Is there a function fitted, and enough stock to fire it? */
int CNEquipTrayWrk::IsReady(void)
{                                                                       /* 302 */
    if (GetNowEquipFuncNum() != 0 && EquipTrayFuncCostNum() <= mSave.mStockNum)  /* 303 */
    {
        return 1;
    }

    return 0;                                                           /* 307 */
}                                                                       /* 309 */

/* Puts the selected function on the shutter.  Nothing is spent here -- Use2()
 * does that when the shutter actually fires -- so this is safe to call every
 * finder frame, which is what player.c does.  The confirmation cue only plays
 * in a fight. */
int CNEquipTrayWrk::SetUp(void)
{
    if (IsReady() != 0)                                                 /* 314 */
    {
        if (mSetupFlg == 0)                                             /* 315 */
        {
            mSetupFlg = 1;                                              /* 316 */

            if (mBattleFlg != 0)                                        /* 317 */
            {
                SystemBankPlay(7, 1, 0, 0, nullptr, 0x3200, 0x1000);       /* 318 */
            }

            mRenzMarkAlpha.SetAddVal((short)(mRenzMarkAlpha.GetMax() / 8));

            return 1;                                                   /* 323 */
        }
    }

    return 0;                                                           /* 326 */
}                                                                       /* 327 */

int CNEquipTrayWrk::IsSetUp(void)
{
    return mSetupFlg;                                                   /* 331 */
}

/* Spends the fitted function's stock.  The `< 5` guard is the ROM's own -- the
 * tray never holds more than aStockMaxTbl's 4, so it only fires when the debug
 * menu has pushed the count past the real ceiling. */
int CNEquipTrayWrk::Use2(void)
{
    if (IsSetUp() == 0)                                                 /* 337 */
    {
        return 0;
    }

    if (mSave.mStockNum < 5)                                            /* 342 */
    {
        mSave.mStockNum -= EquipTrayFuncCostNum();
        dbg_stock_num = mSave.mStockNum;                                /* 346 */
    }

    if (IsReady() == 0)                                                 /* 355 */
    {
        PutOut();                                                       /* 356 */
    }

    return 1;                                                           /* 359 */
}                                                                       /* 360 */

/* The fitted function's damage multiplier.  Only the four attack functions
 * have one; everything else scores as an ordinary shot. */
float CNEquipTrayWrk::GetDmgRate(SHUTTER_CHANCE_STATE SState)
{                                                                       /* 363 */
    SUB_FUNC_KOUGEKI_PARAM *pParam;

    switch (GetNowSubFuncNo())
    {
    case CAMERA_SUB_FUNC_KOKU:
        pParam = &CNEquipTraySave::sub_func_koku_param[GetNowSubFuncLv()];
        break;                                                          /* 369 */

    case CAMERA_SUB_FUNC_ZERO:
        pParam = &CNEquipTraySave::sub_func_rei_param[GetNowSubFuncLv()];
        break;                                                          /* 372 */

    case CAMERA_SUB_FUNC_METSU:
        pParam = &CNEquipTraySave::sub_func_metsu_param[GetNowSubFuncLv()];
        break;                                                          /* 375 */

    case CAMERA_SUB_FUNC_REN:
        pParam = &CNEquipTraySave::sub_func_ren_param[GetNowSubFuncLv()];
        break;                                                          /* 378 */

    default:
        return 0.0f;                                                    /* 380 */
    }

    if (SState == SHUTTER_CHANCE_NONE)                                  /* 384 */
    {
        return pParam->NDmgMag;                                         /* 386 */
    }

    return pParam->SDmgMag;
}                                                                       /* 390 */

/* Whether a burst shot keeps its charge.  The five status functions and Ren
 * say no -- their shot is spent whatever happens -- while the three heavy
 * attacks and an empty slot let the shutter re-arm. */
int CNEquipTrayWrk::IsChargeResetOK(void)
{                                                                       /* 394 */
    switch (GetNowSubFuncNo())
    {
    case CAMERA_SUB_FUNC_SLOW:
    case CAMERA_SUB_FUNC_STOP:
    case CAMERA_SUB_FUNC_VIEW:
    case CAMERA_SUB_FUNC_TRACE:
    case CAMERA_SUB_FUNC_SEAL:
    case CAMERA_SUB_FUNC_REN:
        return 0;                                                       /* 402 */
    }

    return 1;                                                           /* 405 */
}                                                                       /* 407 */

/* Armed once per shot, before the damage sweep.  View and Trace are
 * once-per-shot effects, so SetEffect() clears the flag on the first ghost it
 * applies them to and every later ghost in the same shot goes without. */
void CNEquipTrayWrk::SetEffectsPre(void)
{
    mFirstFlg = 1;                                                      /* 412 */
}

/* Applies the fitted sub-function to one photographed ghost.  Every branch
 * ends in an EneHitEffectReq() so the hit reads differently per function; for
 * the four attack ones the label also carries the shutter-chance grade.
 *
 * Times in the parameter tables are in half-seconds, hence the * 30. */
void CNEquipTrayWrk::SetEffect(ENE_WRK *ew, SHUTTER_CHANCE_STATE SState)
{                                                                       /* 416 */
    switch (GetNowSubFuncNo())
    {
    case CAMERA_SUB_FUNC_SLOW:
        SetEneSlowMode(ew, CNEquipTraySave::sub_func_oso_param[GetNowSubFuncLv()].EffTime * 30,
                       0.3f);                                  /* lit4 3ee620 */
        EneHitEffectReq(ew->alg.idx, ew->mpos.p0, ENE_HIT_EFFECT_SLOW);  /* 423 */
        break;                                                          /* 424 */

    case CAMERA_SUB_FUNC_STOP:
        SetEneMahiMode(ew,                                              /* 429 */
            CNEquipTraySave::sub_func_mahi_param[GetNowSubFuncLv()].EffTime  * 30,
            CNEquipTraySave::sub_func_mahi_param[GetNowSubFuncLv()].StopTime * 30,
            CNEquipTraySave::sub_func_mahi_param[GetNowSubFuncLv()].ActTime  * 30);
        EneHitEffectReq(ew->alg.idx, ew->mpos.p0, ENE_HIT_EFFECT_PARALYZE);  /* 430 */
        break;                                                          /* 431 */

    case CAMERA_SUB_FUNC_VIEW:
        if (mFirstFlg != 0)                                             /* 433 */
        {
            SetEneView(ew,
                CNEquipTraySave::sub_func_si_param[GetNowSubFuncLv()].EffTime * 30,
                CNEquipTraySave::sub_func_si_param[GetNowSubFuncLv()].SearchFlg);
            EneHitEffectReq(ew->alg.idx, ew->mpos.p0, ENE_HIT_EFFECT_VIEW);  /* 437 */
            mFirstFlg = 0;
        }
        break;                                                          /* 440 */

    case CAMERA_SUB_FUNC_TRACE:
        if (mFirstFlg != 0)                                             /* 442 */
        {
            plyr_wrk.ene_tracer.Req(ew->alg.idx,
                CNEquipTraySave::sub_func_trace_param[GetNowSubFuncLv()].EffTime * 30);
            EneHitEffectReq(ew->alg.idx, ew->mpos.p0, ENE_HIT_EFFECT_TSUI);  /* 444 */
            mFirstFlg = 0;                                              /* 445 */
        }
        break;                                                          /* 447 */

    case CAMERA_SUB_FUNC_KOKU:
    {
        int eHitEffectLabel;

        SetEneSlowHitBack(ew, 10,                                       /* 453 */
            CNEquipTraySave::sub_func_koku_param[GetNowSubFuncLv()].SlowHTTime,
            CNEquipTraySave::sub_func_koku_param[GetNowSubFuncLv()].SlowHTRate);

        if (SState == SHUTTER_CHANCE_NONE)                              /* 456 */
        {
            eHitEffectLabel = ENE_HIT_EFFECT_KOKU;
        }
        else if (SState == SHUTTER_CHANCE_NORMAL)                       /* 458 */
        {
            eHitEffectLabel = ENE_HIT_EFFECT_KOKU_SC;
        }
        else
        {
            eHitEffectLabel = ENE_HIT_EFFECT_KOKU_SP;
        }

        EneHitEffectReq(ew->alg.idx, ew->mpos.p0, eHitEffectLabel);     /* 465 */
        break;
    }

    case CAMERA_SUB_FUNC_ZERO:
    {
        int eHitEffectLabel;

        SetEneSlowHitBack(ew, 10,                                       /* 469 */
            CNEquipTraySave::sub_func_rei_param[GetNowSubFuncLv()].SlowHTTime,
            CNEquipTraySave::sub_func_rei_param[GetNowSubFuncLv()].SlowHTRate);

        if (SState == SHUTTER_CHANCE_NONE)                              /* 472 */
        {
            eHitEffectLabel = ENE_HIT_EFFECT_ZERO;
        }
        else if (SState == SHUTTER_CHANCE_NORMAL)                       /* 474 */
        {
            eHitEffectLabel = ENE_HIT_EFFECT_ZERO_SC;
        }
        else
        {
            eHitEffectLabel = ENE_HIT_EFFECT_ZERO_SP;
        }

        EneHitEffectReq(ew->alg.idx, ew->mpos.p0, eHitEffectLabel);     /* 481 */
        break;
    }

    case CAMERA_SUB_FUNC_METSU:
    {
        int eHitEffectLabel;

        SetEneSlowHitBack(ew, 10,                                       /* 486 */
            CNEquipTraySave::sub_func_metsu_param[GetNowSubFuncLv()].SlowHTTime,
            CNEquipTraySave::sub_func_metsu_param[GetNowSubFuncLv()].SlowHTRate);

        if (SState == SHUTTER_CHANCE_NONE)                              /* 489 */
        {
            eHitEffectLabel = ENE_HIT_EFFECT_METSU;
        }
        else if (SState == SHUTTER_CHANCE_NORMAL)                       /* 491 */
        {
            eHitEffectLabel = ENE_HIT_EFFECT_METSU_SC;
        }
        else
        {
            eHitEffectLabel = ENE_HIT_EFFECT_METSU_SP;
        }

        EneHitEffectReq(ew->alg.idx, ew->mpos.p0, eHitEffectLabel);     /* 498 */
        break;
    }

    /* Ren reads the *metsu* table, not sub_func_ren_param.  That is what the
     * ROM does -- its own damage table is only ever used by GetDmgRate(). */
    case CAMERA_SUB_FUNC_REN:
    {
        int eHitEffectLabel;

        SetEneSlowHitBack(ew, 10,                                       /* 502 */
            CNEquipTraySave::sub_func_metsu_param[GetNowSubFuncLv()].SlowHTTime,
            CNEquipTraySave::sub_func_metsu_param[GetNowSubFuncLv()].SlowHTRate);

        if (SState == SHUTTER_CHANCE_NONE)                              /* 505 */
        {
            eHitEffectLabel = ENE_HIT_EFFECT_REN;
        }
        else if (SState == SHUTTER_CHANCE_NORMAL)                       /* 507 */
        {
            eHitEffectLabel = ENE_HIT_EFFECT_REN_SC;
        }
        else
        {
            eHitEffectLabel = ENE_HIT_EFFECT_REN_SP;
        }

        EneHitEffectReq(ew->alg.idx, ew->mpos.p0, eHitEffectLabel);     /* 512 */
        break;                                                          /* 514 */
    }

    case CAMERA_SUB_FUNC_SEAL:
        SetEneSealMode(ew,
            CNEquipTraySave::sub_func_seal_param[GetNowSubFuncLv()].EffTime * 30);
        EneHitEffectReq(ew->alg.idx, ew->mpos.p0, ENE_HIT_EFFECT_FUU);   /* 518 */
        break;
    }

    mSubFuncAnmShot.Blink(10);                                          /* 521 */
}

/* Adds iGage to the accumulator and converts every whole 1000 of it into a
 * stock item; non-zero when at least one came out.  Hitting the ceiling throws
 * away both the leftover and whatever was still banked -- a full tray stops
 * accumulating rather than holding a part-filled dial.
 *
 * The printf is the ROM's own and fires every frame the accumulator moves. */
int CNEquipTraySave::ConvertGage2StockNum(int iGage)
{                                                                       /* 573 */
    int ret = 0;                                                        /* 574 */

    iGage += mDispGage;                                                 /* 578 */

    int iNum    = iGage / EQT_GAGE_PER_STOCK;                           /* 580 */
    int iRemain = iGage % EQT_GAGE_PER_STOCK;                           /* 581 */

    if (iNum != 0)                                                      /* 582 */
    {
        if (mStockNum < 5)                                              /* 584 */
        {
            mStockNum += iNum;                                          /* 590 */

            if (aStockMaxTbl[mStockGrade] <= mStockNum)
            {
                mBankGage = 0;                                          /* 593 */
                mStockNum = aStockMaxTbl[mStockGrade];
                iRemain   = 0;
            }

            dbg_stock_num = mStockNum;                                  /* 597 */

            ret = 1;                                                    /* 600 */
        }
    }

    mDispGage = iRemain;                                                /* 605 */

    printf("iGage = %d iNum = %d iRemain = %d\n", iGage, iNum, iRemain); /* 606 */

    return ret;                                                         /* 608 */
}

/* CNPlyrCamera::Init() is the only caller.  The burst-shot mark is left
 * fading, so a tray that is initialised mid-fade does not freeze it lit. */
void CNEquipTrayWrk::Init(void)
{
    mFirstFlg = 1;                                                      /* 615 */
    mSetupFlg = 0;                                                      /* 616 */

    mRenzMarkAlpha.Init();

    dbg_stock_num = mSave.mStockNum;                                    /* 622 */

    mRenzMarkBlink.SetAddVal((short)(-mRenzMarkBlink.GetWidth() / 5));
}

/* Steps the selector on to the next fitted function and starts the icon column
 * sliding.  Nothing happens while a slide is already running (mMode 1), which
 * is what stops the column from being spun faster than it can animate. */
void CNEquipTrayWrk::Rotate(void)
{                                                                       /* 627 */
    if (mMode == 0 && 1 < GetNowEquipFuncNum())                         /* 629 */
    {
        mSave.NextFuncSet();                                            /* 632 */

        if (IsReady() == 0)                                             /* 634 */
        {
            PutOut();                                                   /* 635 */
        }

        mNowOffset.Fade(45, 7);                                         /* 642 */

        /* The slot being left behind: snapped to full alpha and faded out
         * across the slide. */
        mPreSlctAlpha.Set(128);
        mPreSlctAlpha.Fade(0, 3);                                       /* 646 */
        mPreSlctYOffset = 0;                                            /* 647 */

        FinderBankPlay(16, 1, 0, 0, NULL, 0x3200, 0x1000);              /* 650 */

        mMode = 1;                                                      /* 653 */
    }
}

/* ConvertGage2StockNum() with the finder's feedback attached: a stock item
 * that lands exactly on the fitted function's cost gets the confirmation cue
 * (the shot just became available), anything else gets the ordinary one. */
void CNEquipTrayWrk::FinderConvertGage2StockNum(int iAddGage)
{                                                                       /* 659 */
    if (mSave.ConvertGage2StockNum(iAddGage) != 0)                      /* 661 */
    {
        if (mSave.mStockNum == EquipTrayFuncCostNum())
        {
            SystemBankPlay(7, 1, 0, 0, NULL, 0x3200, 0x1000);           /* 666 */
        }
        else
        {
            FinderBankPlay(14, 1, 0, 0, NULL, 0x3200, 0x1000);          /* 668 */
        }

        mStockAnm.Blink(16);                                            /* 670 */
    }
}

/* One frame.  The first half banks arriving particles and drains the
 * accumulator into stock; the second steps every animated value, and only
 * while the finder is actually up. */
void CNEquipTrayWrk::Work(void)
{                                                                       /* 675 */
    mSave.mStockNum = dbg_stock_num;                                    /* 679 */

    int absorb_num = EneDmgParticleSuctionNumGet();                     /* 682 */

    if (absorb_num != 0)                                                /* 683 */
    {
        if (mRemainParticleNum < absorb_num)                            /* 685 */
        {
            absorb_num = mRemainParticleNum;
        }

        mRemainParticleNum -= absorb_num;                               /* 688 */

        if (1 < absorb_num)                                             /* 689 */
        {
            FinderBankPlay(11, 1, 0, 0, NULL, 0x3200, 0x1000);          /* 690 */
        }

        mSave.Absorb(absorb_num * EQT_GAGE_PER_PARTICLE);               /* 693 */
    }

    if (0 < mSave.mBankGage)                                            /* 698 */
    {
        if (mGageUpFlg == 0)                                            /* 699 */
        {
            mGageAnmAlpha.Fade(-128, 1);                                /* 700 */
            mGageUpFlg = 1;                                             /* 701 */
            mSuckMouthScale.SetAddVal(20);
            mSuckMouthAlpha.SetAddVal(25);
        }

        int iAddGage = mSave.mBankGage;                                 /* 721 */

        if (EQT_GAGE_PER_FRAME < iAddGage)
        {
            iAddGage = EQT_GAGE_PER_FRAME;
        }

        mSave.mBankGage -= iAddGage;                                    /* 726 */
        FinderConvertGage2StockNum(iAddGage);                           /* 729 */
    }
    else if (mGageUpFlg != 0)                                           /* 735 */
    {
        mGageAnmAlpha.Fade(0, 1);                                       /* 736 */
        mGageUpFlg = 0;                                                 /* 737 */
        mSuckMouthScale.SetAddVal(-10);
        mSuckMouthAlpha.SetAddVal(-12);
    }

    if (InFinderMode() == 0)                                            /* 759 */
    {
        return;
    }

    mStockAnm.Work();                                                   /* 763 */
    mSubFuncAnmShot.Work();                                             /* 764 */

    if (IsSetUp() != 0)                                                 /* 765 */
    {
        mSubFuncAnmBlink.Work();                                        /* 766 */
    }
    else
    {
        mSubFuncAnmBlinkNoSetup.Work();                                 /* 768 */
    }

    mGageAnmAlpha.Work();                                               /* 770 */
    mSuckMouthScale.Work();                                             /* 772 */
    mSuckMouthAlpha.Work();                                             /* 773 */
    mRenzMarkAlpha.Work();                                              /* 775 */
    mRenzMarkBlink.Work();                                              /* 776 */

    /* 30 frames per revolution. */
    mAccumulateBollRot.LoopAdd(12.0f);

    mAccumulateBollFlare.Work();                                        /* 779 */

    if (mMode == 1)                                                     /* 796 */
    {
        mNowOffset.Work();                                              /* 797 */
        mPreSlctAlpha.Work();                                           /* 798 */

        /* The fade-out finished: hand the outgoing icon a second, slower fade
         * and park it above the column by one pitch per fitted function. */
        if (mPreSlctAlpha.IsEnd())
        {
            mPreSlctAlpha.Fade(60, 3);                                  /* 802 */
            mPreSlctYOffset = (1 - GetNowEquipFuncNum()) * 45;          /* 804 */
        }

        if (mNowOffset.IsEnd())
        {
            mMode = 0;                                                  /* 808 */
            mNowOffset.Set(0);
        }
    }
}                                                                       /* 811 */

/* The event-macro hook behind the "spirit power multiplier" upgrades. */
void CNEquipTrayWrk::SetAbsorbMultiRate(float fRate)
{
    mSave.mAbsorbMultiRate = fRate;                                     /* 815 */
}

/* Banks power arriving as particles.  The IsStockMax() call repeats the first
 * test -- that is the ROM's own redundancy, kept. */
void CNEquipTraySave::Absorb(int power)
{                                                                       /* 820 */
    if (mStockNum < aStockMaxTbl[mStockGrade]
        && mEquipFunc[mSlctNo].Get() != 0
        && IsStockMax() == 0)                                           /* 829 */
    {
        mBankGage += (int)((float)power * mAbsorbMultiRate);            /* 830 */
    }
}                                                                       /* 832 */

/* Banks power that never became particles -- PhotoDmgChk2() takes this path
 * when EneDmgParticleEffectReq() could not start the burst.  It lands as stock
 * straight away rather than filling the accumulator. */
void CNEquipTrayWrk::AbsorbImmediately(int power)
{                                                                       /* 835 */
    if (mSave.mStockNum < CNEquipTraySave::aStockMaxTbl[mSave.mStockGrade]
        && GetNowSubFuncNo() != 0)
    {
        FinderConvertGage2StockNum(power);                              /* 848 */
        FinderBankPlay(5, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 852 */
    }
}                                                                       /* 853 */

/* --------------------------------------------------------------------------
 *  Draws
 * ------------------------------------------------------------------------ */

/* The accumulator: the stock sockets, the spinning balls already in them, the
 * cost marks for the fitted shot, and the dial with its needle. */
void CNEquipTrayWrk::AccumulaterDraw(int fndr_mx, int fndr_my, int master_alp)
{                                                                       /* 859 */
    DISP_SPRT ds;

    float fAccumPercent = (float)mSave.mDispGage / 1000.0f;             /* 861 */

    /* One frame and one socket per item the tray can hold at this grade. */
    for (int i = FD_STOCK_FRAME;                                        /* 864 */
         i < CNEquipTraySave::aStockMaxTbl[mSave.mStockGrade] + FD_STOCK_FRAME; i++)
    {
        CopySprDToSpr(&ds, &n_finder_dat[i]);                           /* 865 */
        ds.zbuf   = EQT_ZBUF_NO_WRITE;                                  /* 866 */
        ds.alphar = EQT_ALPHA_BLEND;                                    /* 867 */
        ds.alpha  = (u_char)master_alp;                                 /* 868 */
        ds.x     += (float)fndr_mx;                                     /* 869 */
        ds.y     += (float)fndr_my;
        DispSprD(&ds);                                                  /* 870 */
    }                                                                   /* 871 */

    for (int i = FD_STOCK_HOLE;                                         /* 874 */
         i < CNEquipTraySave::aStockMaxTbl[mSave.mStockGrade] + FD_STOCK_HOLE; i++)
    {
        CopySprDToSpr(&ds, &n_finder_dat[i]);                           /* 875 */
        ds.zbuf   = EQT_ZBUF_NO_WRITE;                                  /* 876 */
        ds.alphar = EQT_ALPHA_BLEND;                                    /* 877 */
        ds.alpha  = (u_char)master_alp;                                 /* 878 */
        ds.x     += (float)fndr_mx;                                     /* 879 */
        ds.y     += (float)fndr_my;
        DispSprD(&ds);                                                  /* 880 */
    }                                                                   /* 881 */

    /* The items actually held.  They all share one rotation, so the row spins
     * in step. */
    for (int i = FD_STOCK_ITEM; i < mSave.mStockNum + FD_STOCK_ITEM; i++)  /* 886 */
    {
        CopySprDToSpr(&ds, &n_finder_dat[i]);                           /* 887 */
        ds.zbuf   = EQT_ZBUF_NO_WRITE;                                  /* 888 */
        ds.alphar = EQT_ALPHA_BLEND;                                    /* 889 */
        ds.rot    = mAccumulateBollRot.Get();
        ds.x     += (float)fndr_mx;                                     /* 891 */
        ds.y     += (float)fndr_my;
        ds.crx    = ds.x + (float)(ds.w >> 1);                          /* 892 */
        ds.cry    = ds.y + (float)(ds.h >> 1);
        ds.alpha  = (u_char)master_alp;
        DispSprD(&ds);                                                  /* 893 */
    }                                                                   /* 894 */

    /* What the fitted shot will spend, pulsing so it reads as a warning when
     * the stock is only just enough. */
    if (IsReady() != 0)                                                 /* 907 */
    {
        int iNeedNum = EquipTrayFuncCostNum();                          /* 910 */

        for (int i = FD_STOCK_COST; i < iNeedNum + FD_STOCK_COST; i++)
        {
            CopySprDToSpr(&ds, &n_finder_dat[i]);                       /* 911 */
            ds.zbuf   = EQT_ZBUF_NO_WRITE;                              /* 912 */
            ds.alphar = EQT_ALPHA_ADD;                                  /* 913 */
            ds.alpha  = (u_char)(master_alp * mAccumulateBollFlare.Get() / 128);
            ds.x     += (float)fndr_mx;                                 /* 915 */
            ds.y     += (float)fndr_my;
            DispSprD(&ds);                                              /* 916 */
        }                                                               /* 917 */
    }

    /* The dial.  Two black quads laid one dial-width apart write the depth
     * wedge; the needle behind them is then clipped to it. */
    SQAR_DAT sq =                                                       /* 923 */
    {
        n_finder_dat[FD_ACCUM_DIAL].w,
        n_finder_dat[FD_ACCUM_DIAL].h,
        n_finder_dat[FD_ACCUM_DIAL].x + fndr_mx - n_finder_dat[FD_ACCUM_DIAL].w,
        n_finder_dat[FD_ACCUM_DIAL].y + fndr_my,
        0x10,
        0, 0, 0, 0
    };
    DISP_SQAR dq;

    CopySqrDToSqr(&dq, &sq);                                            /* 930 */
    dq.zbuf = EQT_ZBUF_WRITE;                                           /* 931 */
    dq.test = EQT_TEST_MASK_WRITE;                                      /* 932 */
    DispSqrD(&dq);                                                      /* 934 */

    dq.x[0] += n_finder_dat[FD_ACCUM_DIAL].w;                           /* 938 */
    dq.x[1] += n_finder_dat[FD_ACCUM_DIAL].w;
    dq.x[2] += n_finder_dat[FD_ACCUM_DIAL].w;
    dq.x[3] += n_finder_dat[FD_ACCUM_DIAL].w;
    dq.pri   = 0x30;                                                    /* 939 */
    dq.z     = 0xfffcf;
    DispSqrD(&dq);                                                      /* 941 */

    /* Both dial sprites turn about the same point -- the dial's centre, in
     * absolute finder coordinates -- so the needle sweeps the plate rather
     * than spinning on its own middle. */
    CopySprDToSpr(&ds, &n_finder_dat[FD_ACCUM_DIAL]);                   /* 944 */
    ds.zbuf   = EQT_ZBUF_NO_WRITE;                                      /* 945 */
    ds.alphar = EQT_ALPHA_ADD;                                          /* 946 */
    ds.test   = EQT_TEST_MASK_READ;                                     /* 947 */
    ds.z      = 0xfffdf;                                                /* 948 */
    ds.pri    = 0x20;
    ds.alpha  = (u_char)master_alp;                                     /* 949 */
    ds.x     += (float)fndr_mx;                                         /* 950 */
    ds.y     += (float)fndr_my;
    ds.rot    = fAccumPercent * EQT_DIAL_SWEEP - EQT_DIAL_SWEEP;        /* 952 */
    ds.crx    = (float)(fndr_mx + 0x47);
    ds.cry    = (float)(fndr_my + 0x177);
    DispSprD(&ds);                                                      /* 953 */

    CopySprDToSpr(&ds, &n_finder_dat[FD_ACCUM_NEEDLE]);                 /* 956 */
    ds.zbuf   = EQT_ZBUF_NO_WRITE;                                      /* 957 */
    ds.alphar = EQT_ALPHA_BLEND;                                        /* 958 */
    ds.alpha  = (u_char)master_alp;                                     /* 959 */
    ds.x     += (float)fndr_mx;                                         /* 960 */
    ds.y     += (float)fndr_my;
    ds.rot    = fAccumPercent * EQT_DIAL_SWEEP;                         /* 962 */
    ds.crx    = (float)(fndr_mx + 0x47);
    ds.cry    = (float)(fndr_my + 0x177);
    DispSprD(&ds);                                                      /* 963 */
}

/* The tray's backing plate, drawn under everything else. */
void CNEquipTrayWrk::BaseDraw(int fndr_mx, int fndr_my, int master_alp)
{                                                                       /* 968 */
    DISP_SPRT ds;

    CopySprDToSpr(&ds, &n_finder_dat[FD_TRAY_BASE_BACK]);               /* 971 */
    ds.zbuf   = EQT_ZBUF_NO_WRITE;                                      /* 972 */
    ds.alphar = EQT_ALPHA_BLEND;                                        /* 973 */
    ds.alpha  = (u_char)master_alp;                                     /* 974 */
    ds.x     += (float)fndr_mx;                                         /* 975 */
    ds.y     += (float)fndr_my;
    DispSprD(&ds);                                                      /* 976 */

    CopySprDToSpr(&ds, &n_finder_dat[FD_TRAY_BASE_FRONT]);              /* 981 */
    ds.zbuf   = EQT_ZBUF_NO_WRITE;                                      /* 982 */
    ds.alphar = EQT_ALPHA_BLEND;                                        /* 983 */
    ds.alpha  = (u_char)master_alp;                                     /* 984 */
    ds.x     += (float)fndr_mx;                                         /* 985 */
    ds.y     += (float)fndr_my;
    DispSprD(&ds);                                                      /* 986 */
}

/* The whole tray.
 *
 * Two layouts share almost everything: at rest (mMode 0) the unselected icons
 * are stacked above the selected one, and mid-slide (mMode 1) the icon being
 * left behind is drawn separately with its own fading alpha.  Either way the
 * unselected icons are drawn at a flat 0x3c grey and only the selected one
 * carries the function's colour.
 *
 * An empty slot is skipped rather than drawn blank, which is why iYOffset only
 * steps for icons that were actually drawn. */
void CNEquipTrayWrk::Draw(int fndr_mx, int fndr_my, int master_alp)
{                                                                       /* 991 */
    DISP_SPRT ds;

    int iFuncNo = GetNowSubFuncNo();
    int pk2_no  = equip_func_tbl[iFuncNo].pk2_no;                       /* 993 */

    if (pk2_no == NEQUIP_PK2_NONE)                                      /* 996 */
    {
        return;
    }

    if (mMode == 0)                                                     /* 1000 */
    {
        /* Nothing to stack above the selected slot unless at least two
         * functions are fitted. */
        if (2 <= GetNowEquipFuncNum())                                  /* 1003 */
        {
            CVariable<char, 0, 2> iSlctNo = mSave.mSlctNo;              /* 1006 */
            float iYOffset = (float)(fndr_my + mNowOffset.Get()) - EQT_SLOT_PITCH;
            iSlctNo.LoopIncrement();

            for (int i = 0; i < 2; i++)                                 /* 1009 */
            {
                if (equip_func_tbl[mSave.mEquipFunc[iSlctNo].Get()].pk2_no
                        != NEQUIP_PK2_NONE)                             /* 1011 */
                {
                    CopySprDToSpr(&ds, &n_finder_dat[                   /* 1012 */
                        equip_func_tbl[mSave.mEquipFunc[iSlctNo].Get()].pk2_no]);
                    ds.zbuf   = EQT_ZBUF_NO_WRITE;                      /* 1013 */
                    ds.alphar = EQT_ALPHA_BLEND;                        /* 1014 */
                    ds.alpha  = (u_char)master_alp;                     /* 1015 */
                    ds.r      = 0x3c;                                   /* 1016 */
                    ds.g      = 0x3c;
                    ds.b      = 0x3c;
                    ds.x     += (float)fndr_mx;                         /* 1018 */
                    ds.y     += iYOffset;
                    DispSprD(&ds);                                      /* 1019 */

                    iYOffset -= EQT_SLOT_PITCH;                         /* 1020 */
                }

                iSlctNo.LoopIncrement();
            }                                                           /* 1023 */

            for (int i = 0; i < 2; i++)                                 /* 1025 */
            {
                CopySprDToSpr(&ds, &n_finder_dat[FD_TRAY_RAIL + i]);    /* 1026 */
                ds.zbuf   = EQT_ZBUF_NO_WRITE;                          /* 1027 */
                ds.alphar = EQT_ALPHA_BLEND;                            /* 1028 */
                ds.alpha  = (u_char)master_alp;                         /* 1029 */
                ds.x     += (float)fndr_mx;                             /* 1030 */
                ds.y     += (float)fndr_my;
                DispSprD(&ds);                                          /* 1031 */
            }                                                           /* 1032 */
        }

        BaseDraw(fndr_mx, fndr_my, master_alp);                         /* 1036 */
        AccumulaterDraw(fndr_mx, fndr_my, master_alp);                  /* 1039 */

        /* The selected icon.  It only takes the function's colour when the
         * shot is actually affordable; short of stock it greys out with the
         * rest of the column. */
        CopySprDToSpr(&ds, &n_finder_dat[pk2_no]);                      /* 1043 */
        ds.zbuf   = EQT_ZBUF_NO_WRITE;                                  /* 1044 */
        ds.alphar = EQT_ALPHA_BLEND;                                    /* 1045 */
        ds.x     += (float)fndr_mx;                                     /* 1046 */
        ds.y     += (float)fndr_my;
        ds.alpha  = (u_char)master_alp;                                 /* 1047 */

        if (IsReady() != 0)                                             /* 1048 */
        {
            ds.r = equip_func_tbl[iFuncNo].r;                           /* 1049 */
            ds.g = equip_func_tbl[iFuncNo].g;
            ds.b = equip_func_tbl[iFuncNo].b;
        }
        else
        {
            ds.r = 0x3c;                                                /* 1053 */
            ds.g = 0x3c;
            ds.b = 0x3c;
        }

        DispSprD(&ds);                                                  /* 1056 */
    }
    else
    {
        /* Mid-slide.  The slot being left behind is the previous *fitted* one,
         * so the search walks back over empty slots. */
        CVariable<char, 0, 2> iSlctNo = mSave.mSlctNo;                  /* 1061 */

        do
        {
            iSlctNo.LoopDecrement();                                    /* 1067 */
        }
        while (mSave.mEquipFunc[iSlctNo].Get() == 0);                   /* 1074 */

        CopySprDToSpr(&ds, &n_finder_dat[                               /* 1080 */
            equip_func_tbl[mSave.mEquipFunc[iSlctNo].Get()].pk2_no]);   /* 1079 */
        ds.zbuf   = EQT_ZBUF_NO_WRITE;                                  /* 1081 */
        ds.alphar = EQT_ALPHA_BLEND;                                    /* 1082 */
        ds.alpha  = (u_char)(mPreSlctAlpha.Get() * master_alp / 128);
        ds.x     += (float)fndr_mx;                                     /* 1084 */
        ds.y     += (float)(fndr_my + mPreSlctYOffset);
        DispSprD(&ds);                                                  /* 1085 */

        float iYOffset = (float)(fndr_my + mNowOffset.Get()) - EQT_SLOT_PITCH;
        iSlctNo.LoopIncrement();

        for (int i = 0; i < 2; i++)                                     /* 1095 */
        {
            if (equip_func_tbl[mSave.mEquipFunc[iSlctNo].Get()].pk2_no
                    != NEQUIP_PK2_NONE)                                 /* 1097 */
            {
                CopySprDToSpr(&ds, &n_finder_dat[                       /* 1098 */
                    equip_func_tbl[mSave.mEquipFunc[iSlctNo].Get()].pk2_no]);
                ds.zbuf   = EQT_ZBUF_NO_WRITE;                          /* 1099 */
                ds.alphar = EQT_ALPHA_BLEND;                            /* 1100 */
                ds.alpha  = (u_char)master_alp;                         /* 1101 */
                ds.r      = 0x3c;                                       /* 1102 */
                ds.g      = 0x3c;
                ds.b      = 0x3c;
                ds.x     += (float)fndr_mx;                             /* 1104 */
                ds.y     += iYOffset;
                DispSprD(&ds);                                          /* 1105 */

                iYOffset -= EQT_SLOT_PITCH;                             /* 1106 */
            }

            iSlctNo.LoopIncrement();
        }                                                               /* 1109 */

        for (int i = 0; i < 2; i++)                                     /* 1111 */
        {
            CopySprDToSpr(&ds, &n_finder_dat[FD_TRAY_RAIL + i]);        /* 1112 */
            ds.zbuf   = EQT_ZBUF_NO_WRITE;                              /* 1113 */
            ds.alphar = EQT_ALPHA_BLEND;                                /* 1114 */
            ds.alpha  = (u_char)master_alp;                             /* 1115 */
            ds.x     += (float)fndr_mx;                                 /* 1116 */
            ds.y     += (float)fndr_my;
            DispSprD(&ds);                                              /* 1117 */
        }                                                               /* 1118 */

        BaseDraw(fndr_mx, fndr_my, master_alp);                         /* 1122 */
        AccumulaterDraw(fndr_mx, fndr_my, master_alp);                  /* 1125 */
    }

    /* The suction mouth, scaled by how hard power is arriving. */
    float fScaleRate = (float)mSuckMouthScale.Get() / 100.0f;

    CopySprDToSpr(&ds, &n_finder_dat[FD_SUCK_MOUTH]);                   /* 1133 */
    ds.zbuf   = EQT_ZBUF_NO_WRITE;                                      /* 1134 */
    ds.alphar = EQT_ALPHA_ADD;                                          /* 1135 */
    ds.alpha  = (u_char)(mSuckMouthAlpha.Get() * master_alp / 128);     /* 1136 */
    ds.x     += (float)fndr_mx;                                         /* 1137 */
    ds.y     += (float)fndr_my;
    ds.scw    = fScaleRate;                                             /* 1138 */
    ds.sch    = fScaleRate;
    ds.csx    = ds.x + (float)(ds.w >> 1);
    ds.csy    = ds.y + (float)(ds.h >> 1);
    ds.r      = equip_func_tbl[iFuncNo].r;                              /* 1139 */
    ds.g      = equip_func_tbl[iFuncNo].g;
    ds.b      = equip_func_tbl[iFuncNo].b;
    DispSprD(&ds);                                                      /* 1142 */

    /* The flare over the selected icon.  A shot pulse wins outright; failing
     * that either blink switch drives it, and with both off the flare sits at
     * half brightness in whatever colour CopySprDToSpr() left. */
    int iFlareAlp;

    CopySprDToSpr(&ds, &n_finder_dat[FD_SUBFUNC_FLARE]);                /* 1149 */
    ds.zbuf   = EQT_ZBUF_NO_WRITE;                                      /* 1150 */
    ds.alphar = EQT_ALPHA_ADD;                                          /* 1151 */
    ds.x     += (float)fndr_mx;                                         /* 1152 */
    ds.y     += (float)fndr_my;

    if (mSubFuncAnmShot.IsOn())
    {
        iFlareAlp = mSubFuncAnmShot.Get();
        ds.r      = equip_func_tbl[iFuncNo].r;                          /* 1155 */
        ds.g      = equip_func_tbl[iFuncNo].g;
        ds.b      = equip_func_tbl[iFuncNo].b;
    }
    else if (mSubFuncAnmBlinkNoSetup.IsOn() || mSubFuncAnmBlink.IsOn())
    {
        /* The brighter, faster switch is only used once the shot is both set
         * up and still able to fire. */
        if (IsSetUp() == 0)                                             /* 1160 */
        {
            iFlareAlp = mSubFuncAnmBlinkNoSetup.Get();
        }
        else if (mShotAble == 0)
        {
            iFlareAlp = mSubFuncAnmBlinkNoSetup.Get();
        }
        else
        {
            iFlareAlp = mSubFuncAnmBlink.Get();
        }

        ds.r = equip_func_tbl[iFuncNo].r;                               /* 1165 */
        ds.g = equip_func_tbl[iFuncNo].g;
        ds.b = equip_func_tbl[iFuncNo].b;
    }
    else
    {
        iFlareAlp = 0x80;
    }

    ds.alpha = (u_char)(iFlareAlp * master_alp / 128);                  /* 1171 */
    DispSprD(&ds);                                                      /* 1173 */
}                                                                       /* 1179 */

/* Raised while a ghost is inside the capture ring and the fitted shot can
 * take it: the burst-shot mark fades in and both blink switches start. */
void CNEquipTrayWrk::RenzMarkOn(void)
{
    mShotAble = 1;                                                      /* 1184 */
    mRenzMarkBlink.SetAddVal((short)(mRenzMarkBlink.GetWidth() / 5));

    mSubFuncAnmBlink.BlinkOn();
    mSubFuncAnmBlinkNoSetup.BlinkOn();
}

void CNEquipTrayWrk::RenzMarkOff(void)
{
    mShotAble = 0;                                                      /* 1192 */
    mRenzMarkBlink.SetAddVal((short)(-mRenzMarkBlink.GetWidth() / 5));

    mSubFuncAnmBlink.BlinkOff();
    mSubFuncAnmBlinkNoSetup.BlinkOff();
}

/* The four-piece mark over the capture ring.  It fades in by *unwinding*: the
 * pieces start a quarter turn out and rotate home as mRenzMarkAlpha rises,
 * with the 0.9 bias meaning they are already nearly aligned at full alpha.
 * All four turn about the ring's centre, not their own. */
void CNEquipTrayWrk::RenzMarkDraw(int fndr_mx, int fndr_my, int master_alp)
{                                                                       /* 1201 */
    DISP_SPRT ds;

    float fScale = 1.0f;                                                /* 1205 */
    float fRate  = (float)mRenzMarkAlpha.Get() / (float)mRenzMarkAlpha.GetMax();
    float fRot   = (0.9f - fRate) * 90.0f;              /* lit4 3ee624 = 0.9 */  /* 1207 */

    if (fRot < 0.0f)                                                    /* 1210 */
    {
        fRot = 0.0f;                                                    /* 1211 */
    }

    for (int i = 0; i < 4; i++)                                         /* 1214 */
    {
        CopySprDToSpr(&ds, &n_finder_dat[FD_RENZ_MARK + i]);            /* 1217 */
        ds.zbuf   = EQT_ZBUF_NO_WRITE;                                  /* 1218 */
        ds.alphar = EQT_ALPHA_ADD;                                      /* 1219 */
        ds.alpha  = (u_char)(master_alp * mRenzMarkBlink.Get()          /* 1220 */
                        * mRenzMarkAlpha.Get()
                        / mRenzMarkAlpha.GetMax() / mRenzMarkBlink.GetMax());
        ds.r      = equip_func_tbl[GetNowSubFuncNo()].r;                /* 1221 */
        ds.g      = equip_func_tbl[GetNowSubFuncNo()].g;
        ds.b      = equip_func_tbl[GetNowSubFuncNo()].b;
        ds.x     += (float)fndr_mx;                                     /* 1224 */
        ds.y     += (float)fndr_my;
        ds.rot    = fRot;
        ds.scw    = fScale;                                             /* 1226 */
        ds.sch    = fScale;
        ds.csx    = (float)(fndr_mx + 0x140);
        ds.csy    = (float)(fndr_my + 0xe0);
        ds.crx    = (float)(fndr_mx + 0x140);                           /* 1227 */
        ds.cry    = (float)(fndr_my + 0xe0);
        DispSprD(&ds);                                                  /* 1228 */
    }                                                                   /* 1229 */
}

/* Non-zero while power is still draining out of the accumulator, which is what
 * holds the finder open until the tray has finished banking a shot. */
int CNEquipTrayWrk::IsMoving(void)
{
    return 0 < mSave.mBankGage;                                         /* 1236 */
}

/* Whether the fitted function knocks the ghost back. */
int CNEquipTrayWrk::IsHitBackON(void)
{                                                                       /* 1244 */
    return equip_func_tbl[GetNowSubFuncNo()].bHitBack;                  /* 1245 */
}

/* --------------------------------------------------------------------------
 *  Accumulator save/restore.  Push/Pop are how a scripted sequence borrows the
 *  gauge and hands it back untouched.
 * ------------------------------------------------------------------------ */

void CNEquipTraySave::ResetGage(void)
{
    mStockNum     = 0;                                                  /* 1251 */
    mDispGage     = 0;                                                  /* 1252 */
    mBankGage     = 0;                                                  /* 1253 */
    dbg_stock_num = 0;                                                  /* 1255 */
}

void CNEquipTraySave::PushGage(int *iGageDat)
{
    iGageDat[0] = mDispGage;                                            /* 1262 */
    iGageDat[1] = mBankGage;                                            /* 1263 */
    iGageDat[2] = mStockNum;                                            /* 1264 */
}

void CNEquipTraySave::PopGage(int *iGageDat)
{
    mDispGage = iGageDat[0];                                            /* 1270 */
    mBankGage = iGageDat[1];                                            /* 1271 */
    mStockNum = iGageDat[2];                                            /* 1272 */
}
