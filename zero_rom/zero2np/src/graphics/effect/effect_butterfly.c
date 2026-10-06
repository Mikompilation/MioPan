// FILE: /home/zero_rom/zero2np/src/graphics/effect/effect_butterfly.c
//
// COMPLETE.  All 19 ZERO2.MAP .text symbols plus 15 statics, the five
// BUTTERFLY_TARGET_PARAMETER presets (.data 0x2e20e0) and the .rodata
// selector table (0x3a5db0) the static-init helper seeds.
//
// A NOTE ON THE /* NNN */ ANNOTATIONS.  Function opening lines are read off
// the $LM that precedes each PROC record; statement lines come from the
// disassembly.  Anything whose only work goes through an inline --
// fixed_array.h 124/125, g3dxVu0.h 134/135, SingleLinkList.h 54/65/76 or
// effect.h 217 (EffectGetRandom) -- leaves no $LM of its own and is
// interpolated into the measured gap.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), 0x0013fdf8.

#include "effect_butterfly.h"

#include <math.h>                               /* acosf */
#include <string.h>                             /* memset */

#include "effect.h"                             /* EffectGetRandom */
#include "effect_sub.h"                         /* Get2PosRot / Vector2Rot / Set3DPosTexure2 */

#include "../draw_env.h"
#include "../mmanage.h"                         /* mmanageReqMdl / IsReady */
#include "../graph3d/g3dxVu0.h"                 /* g3dxVu0CopyVector */
#include "../graph3d/gra3d.h"                   /* gra3dGetCamera / GRA3DLIGHTDATA */
#include "../graph3d/gra3dSGD.h"                /* _gra3dDrawSGD / _SetPREVIOUSTRI2PRIM */
#include "../motion/mdlwork.h"                  /* SendEneVram / DrawEneSubObj */
#include "../motion/mim.h"                      /* mimSetVertex */
#include "../motion/motion.h"
#include "../../common/utility.h"               /* GetTrgtRotType2 / GetDistV2 */
#include "../../sdk/libvu0.h"

/* The butterfly model and animation, by mmanage id. */
#define BUTTERFLY_MODEL_NO      5
#define BUTTERFLY_ANIM_NO       8

/* --------------------------------------------------------------------------
 *  The five guide presets.                                        ROM 120
 *
 *  Only type 2 differs from the others, and only in Speed (10.00 against
 *  5.00) -- so a "fast" guide butterfly is the sole authored variation.  The
 *  LockOnInterval triple is 0 / 4 / 8: a butterfly closer than 750 units never
 *  locks on (it wanders the last stretch), one past 3000 locks on most often.
 * ------------------------------------------------------------------------ */
BUTTERFLY_TARGET_PARAMETER ButterflyTargetType00 =              /* data 2e20e0 */
{    500, 45, 120, 120, 30, 30, 0, 4, 8, 750, 3000 };
BUTTERFLY_TARGET_PARAMETER ButterflyTargetType01 =              /* data 2e2110 */
{    500, 45, 120, 120, 30, 30, 0, 4, 8, 750, 3000 };
BUTTERFLY_TARGET_PARAMETER ButterflyTargetType02 =              /* data 2e2140 */
{   1000, 45, 120, 120, 30, 30, 0, 4, 8, 750, 3000 };
BUTTERFLY_TARGET_PARAMETER ButterflyTargetType03 =              /* data 2e2170 */
{    500, 45, 120, 120, 30, 30, 0, 4, 8, 750, 3000 };
BUTTERFLY_TARGET_PARAMETER ButterflyTargetType04 =              /* data 2e21a0 */
{    500, 45, 120, 120, 30, 30, 0, 4, 8, 750, 3000 };

/* .rodata (0x3a5db0), not listed in globals.txt -- the ROM's own name for it
 * is unrecoverable; the pointers are read out of the ELF. */
static BUTTERFLY_TARGET_PARAMETER *ButterflyTargetParamTbl[5] = /* rdata 3a5db0 */
{
    &ButterflyTargetType00, &ButterflyTargetType01, &ButterflyTargetType02,
    &ButterflyTargetType03, &ButterflyTargetType04
};

reference_fixed_array<BUTTERFLY_TARGET_PARAMETER *, 5>
    pButterflyTargetParamPtr(ButterflyTargetParamTbl);          /* sdata 3efd10 *//* 120 */

/* Butterflies carry their own light rig rather than the room's: every one of
 * the 39 lights is disabled and the ambient is 3.0, so the model draws at a
 * flat, heavily over-driven white that the vertex alpha then knocks back. */
GRA3DLIGHTDATA ButterflyLight;                                  /* data 2e21d0 */

BUTTERFLY_DISP_CTRL     ButterFlyDispCtrl;                      /* data 2e3570 */
BUTTERFLY_PARTICLE_CTRL ButterFlyParticleCtrl;                  /* data 2e3580 */

/* Defined below their first use in the ROM. */
static void  EffectButterflyMakeMatrix(ANI_CTRL *pAniCtrl, float *Pos, float *Rot);
static void  EffectDrawButterflyOne(ANI_CTRL *pAniCtrl,
                                    const GRA3DLIGHTDATA *pLight, int Alpha);
static void  EffectDrawButterflyFrea(const float *Pos, float RotX, float RotY,
                                     u_int Alpha);
static int   EffectButterflyUpdate(BUTTERFLY_DISP *pButterfly);
static int   EffectButterflyUpdateTypeDefault(BUTTERFLY_DISP *pButterfly);
static int   EffectButterflyUpdateTypeMoveToTarget(BUTTERFLY_DISP *pButterfly);
static int   EffectButterflyGetLockOnInterval(int Type, const float *Position,
                                              const float *Target);
static void  EffectButterflyGetVelocity(float *Velocity, const float *Position,
                                        const float *Target,
                                        const float *Direction, int Type,
                                        float Speed, int LockOn);
static void  ButterflyDelete(SINGLE_LINK_LIST *pSLL, SLL_CELL *pCell);
static void  EffectGetButterflyPosition(float *Position,
                                        BUTTERFLY_DISP *pButterfly);
static BUTTERFLY_DISP *EffectButterflyGetDispPtr(int Id);
static BUTTERFLY_TARGET_PARAMETER *EffectButterflyTargetParameterPtrGet(int Type);
static void  EffectButterflyParticleReq(SINGLE_LINK_LIST *pSLL,
                                        const float *Position,
                                        const float *Velocity,
                                        const float *Acceleration);
static void  EffectButterflyParticleReqAdjustParam(SINGLE_LINK_LIST *pSLL,
                                                   const float *Position,
                                                   const float *Rot);
static void  ButterflyParticleOneDraw(const BUTTERFLY_PARTICLE *pParticle,
                                      float RotX, float RotY);
static int   ButterflyParticleUpdate(BUTTERFLY_PARTICLE *pParticle);

/* --------------------------------------------------------------------------
 *  Module init.                                                   ROM 149
 * ------------------------------------------------------------------------ */
void EffectButterflyInit(void)
{
    int i;

    SingleLinkListInit(&ButterFlyDispCtrl.ButterflyList,
                       sizeof(BUTTERFLY_DISP));                 /* 152 */

    for (i = 0; i < NUM_GRA3DLIGHTID; i++)                      /* 154 */
    {
        ButterflyLight.aStatus[i].bEnable = 0;
    }

    ButterflyLight.vAmbient[0] = 3.0f;                          /* 156 */
    ButterflyLight.vAmbient[1] = 3.0f;
    ButterflyLight.vAmbient[2] = 3.0f;
    ButterflyLight.vAmbient[3] = 1.0f;                          /* 157 */
}

void EffectSetupButterflyModel(void)                            /* 164 */
{
    mmanageReqMdl(BUTTERFLY_MODEL_NO);                          /* 165 */
    mmanageReqAnm(BUTTERFLY_ANIM_NO);                           /* 166 */
}

void EffectReleaseButterflyModel(void)                          /* 172 */
{
    mmanageClearMdl(BUTTERFLY_MODEL_NO);                        /* 173 */
    mmanageClearAnm(BUTTERFLY_ANIM_NO);                         /* 174 */
}

int EffectIsReadyButterflyModel(void)                           /* 181 */
{
    void *ModelAddr;
    void *AnimAddr;
    int   IsReady;

    IsReady = 0;                                                /* 185 */

    if (mmanageIsReadyMdl(BUTTERFLY_MODEL_NO, &ModelAddr, 0) != 0 &&
        mmanageIsReadyAnm(BUTTERFLY_ANIM_NO, &AnimAddr, 0) != 0)
    {
        IsReady = 1;
    }

    return IsReady;                                             /* 191 */
}

/* --------------------------------------------------------------------------
 *  Attach a fresh ANI_CTRL to the shared model.                    ROM 200
 *
 *  The two IsReady calls are only here for their out-parameters -- the result
 *  is discarded, so requesting a butterfly before the model has landed builds
 *  an ANI_CTRL over garbage.  EffectIsReadyButterflyModel() is the caller's
 *  job.
 * ------------------------------------------------------------------------ */
void EffectInitAniCtrlButterflyOne(ANI_CTRL *pAniCtrl)
{
    void *ModelAddr;
    void *AnimAddr;

    mmanageIsReadyMdl(BUTTERFLY_MODEL_NO, &ModelAddr, 0);       /* 203 */
    mmanageIsReadyAnm(BUTTERFLY_ANIM_NO, &AnimAddr, 0);         /* 204 */

    motClearANI_CTRL(pAniCtrl);                                 /* 206 */
    motInitAniCtrlMalloc(pAniCtrl, (u_int *)AnimAddr, (u_int *)ModelAddr,
                         BUTTERFLY_MODEL_NO, BUTTERFLY_ANIM_NO); /* 208 */
}

/* --------------------------------------------------------------------------
 *  Step the animation and place the model.                        ROM 219
 *
 *  motSetCoord() answering 2 is "clip ended": the butterfly then picks one of
 *  the two wingbeat clips at random, but only if fewer than two are already
 *  queued.
 * ------------------------------------------------------------------------ */
static void EffectButterflyMakeMatrix(ANI_CTRL *pAniCtrl, float *Pos, float *Rot)
{
    int MotNo;

    if (motSetCoord(pAniCtrl, 0xff, 0) == 2 &&                  /* 223 */
        pAniCtrl->anm.playnum < 2)
    {
        MotNo = (int)EffectGetRandom(0.0f, 2.0f);
        ReqAnm(pAniCtrl, 0, BUTTERFLY_ANIM_NO, MotNo);          /* 227 */
    }

    mimSetVertex(pAniCtrl);                                     /* 244 */
    SetCoordinate(pAniCtrl, Pos, Rot);                          /* 247 */
}

/* --------------------------------------------------------------------------
 *  Draw one butterfly model.                                      ROM 258
 * ------------------------------------------------------------------------ */
static void EffectDrawButterflyOne(ANI_CTRL *pAniCtrl,
                                   const GRA3DLIGHTDATA *pLight, int Alpha)
{
    HeaderSection *hs;

    (void)pLight;

    hs = (HeaderSection *)pAniCtrl->base_p;                     /* 259 */

    SendEneVram(pAniCtrl->mdl_p, 0x2bc0);                       /* 264 */
    _gra3dDrawSGD((SGDFILEHEADER *)hs, SRT_REALTIME, NULL, -1); /* 266 */
    DrawEneSubObj(pAniCtrl->mpk_p, (u_char)Alpha, (u_char)Alpha); /* 268 */
    _SetPREVIOUSTRI2PRIM(NULL);                                 /* 271 */
}

/* --------------------------------------------------------------------------
 *  The glow around a butterfly.                                   ROM 283
 *
 *  Note the two extents differ by a tenth (170.8 x 170.9).  Both are one ulp
 *  low of their decimal, the EE compiler's usual literal truncation, so the
 *  asymmetry is the artist's rather than a rounding artefact.
 * ------------------------------------------------------------------------ */
static void EffectDrawButterflyFrea(const float *Pos, float RotX, float RotY,
                                    u_int Alpha)
{
    float    matWorldLocal[4][4];
    DRAW_ENV DrawEnv;

    DrawEnv.alpha = 0x48;                                       /* 285 */
    DrawEnv.tex1  = 0x161;
    DrawEnv.clamp = 0;
    DrawEnv.test  = 0x5000d;
    DrawEnv.zbuf  = 0x10a000118ULL;
    DrawEnv.prim  = 0x302a400000008004ULL;

    sceVu0UnitMatrix(matWorldLocal);                            /* 297 */
    sceVu0RotMatrixX(matWorldLocal, matWorldLocal, RotX);       /* 298 */
    sceVu0RotMatrixY(matWorldLocal, matWorldLocal, RotY);       /* 299 */
    sceVu0TransMatrix(matWorldLocal, matWorldLocal, (float *)Pos); /* 300 */

    Set3DPosTexure2(matWorldLocal, &DrawEnv, 0x58, 170.79999f, 170.89999f,
                    0x80, 0x80, 0x80, (u_char)Alpha, 0);        /* 309 */
}

/* --------------------------------------------------------------------------
 *  Per-frame pass over every live butterfly.                      ROM 317
 *
 *  The `pNext` is latched *before* the body runs, because the update can
 *  delete the cell out from under the walk.
 * ------------------------------------------------------------------------ */
void EffectButterflyMain(void)
{
    BUTTERFLY_DISP *pButterfly;
    SLL_CELL       *pCell;
    SLL_CELL       *pNext;
    GRA3DCAMERA    *pCam;
    float           Pos[4];
    float           RotX, RotY;
    float           FreaAlpha;

    pCell = SingleLinkListBeginCell(&ButterFlyDispCtrl.ButterflyList); /* 318 */

    pCam = gra3dGetCamera();                                    /* 320 */
    Get2PosRot(gra3dcamGetPosition(), pCam->vTarget, &RotX, &RotY); /* 323 */

    while (pCell != NULL)                                       /* 324 */
    {
        pNext = pCell->pNext;

        pButterfly = (BUTTERFLY_DISP *)SingleLinkListCellBodyPtr(pCell); /* 330 */

        EffectButterflyMakeMatrix(&pButterfly->AniCtrl,
                                  pButterfly->Position, pButterfly->Rot); /* 332 */
        EffectGetButterflyPosition(Pos, pButterfly);            /* 337 */

        /* The glow flickers frame to frame; the model does not. */
        FreaAlpha = pButterfly->AlphaRate * EffectGetRandom(8.0f, 18.0f);
        EffectDrawButterflyFrea(Pos, RotX, RotY, (u_int)FreaAlpha); /* 339 */
        EffectDrawButterflyOne(&pButterfly->AniCtrl, &ButterflyLight,
                               (int)(pButterfly->AlphaRate * 128.0f)); /* 340 */

        if (EffectButterflyUpdate(pButterfly) == 0)             /* 347 */
        {
            ButterflyDelete(&ButterFlyDispCtrl.ButterflyList, pCell); /* 348 */
        }
        else if (EffectGetRandom(0.0f, 20.0f) <= 1.0f)          /* 351 */
        {
            /* One trail burst every twentieth frame on average. */
            EffectButterflyParticleReqAdjustParam(
                &ButterFlyParticleCtrl.ParticleList, Pos, pButterfly->Rot); /* 357 */
        }

        pCell = pNext;                                          /* 361 */
    }
}                                                               /* 364 */

/* Dispatch by MoveType; anything else is silently "dead". */
static int EffectButterflyUpdate(BUTTERFLY_DISP *pButterfly)    /* 377 */
{
    int Update = 0;                                             /* 380 */

    if (pButterfly->MoveType == 0)                              /* 381 */
    {
        Update = EffectButterflyUpdateTypeDefault(pButterfly);  /* 383 */
    }
    else if (pButterfly->MoveType == 1)
    {
        Update = EffectButterflyUpdateTypeMoveToTarget(pButterfly); /* 384 */
    }

    return Update;                                              /* 387 */
}

/* --------------------------------------------------------------------------
 *  The ambient butterfly.                                         ROM 399
 *
 *  Every tenth frame the velocity is nudged: X/Y by a random walk clamped to
 *  +-0.7 and Z (forward speed) toward 10..12, and the whole vector is then
 *  halved -- so it converges on roughly half the clamp rather than sitting at
 *  it.  The Y nudge is biased by height: above 200 it is pushed down, below 0
 *  it is pushed up, so the butterfly stays in a band.
 *
 *  Note the whole steer is skipped when Velocity[2] is exactly 0, which is
 *  how a butterfly can be parked.
 * ------------------------------------------------------------------------ */
static int EffectButterflyUpdateTypeDefault(BUTTERFLY_DISP *pButterfly)
{
    float mtx[4][4];
    float pos[4];
    float VelWrkX, VelWrkY, VelWrkZ;
    float SpeedRate;

    if (pButterfly->LifeTime < 1)                               /* 409 */
    {
        return 0;                                               /* 410 */
    }

    if (pButterfly->MoveInterval <= 0 && pButterfly->Velocity[2] != 0.0f) /* 413 */
    {
        VelWrkX = pButterfly->Velocity[0]
                  + (EffectGetRandom(0.0f, 2.0f) - 1.0f) * 0.5f; /* 420 */

        if (pButterfly->Position[1] > 200.0f)
        {
            /* Y is down, so a positive nudge sinks it back. */
            VelWrkY = pButterfly->Velocity[1]
                      + (EffectGetRandom(0.0f, 1.0f) - 1.0f) * 0.5f;
        }
        else if (pButterfly->Position[1] >= 0.0f)
        {
            VelWrkY = pButterfly->Velocity[1]
                      + (EffectGetRandom(0.0f, 2.0f) - 1.0f) * 0.5f;
        }
        else
        {
            VelWrkY = pButterfly->Velocity[1]
                      + EffectGetRandom(0.0f, 1.0f) * 0.5f;
        }

        if (VelWrkX <= 0.7f && VelWrkX >= -0.7f)                /* 427 */
        {
            pButterfly->Velocity[0] = VelWrkX;
        }
        if (VelWrkY <= 0.7f && VelWrkY >= -0.7f)                /* 428 */
        {
            pButterfly->Velocity[1] = VelWrkY;
        }

        pButterfly->Velocity[0] *= 0.5f;                        /* 433 */
        pButterfly->Velocity[1] *= 0.5f;
        pButterfly->MoveInterval = 10;
    }

    /* Face the way it actually moved last frame. */
    GetTrgtRotType2(pButterfly->OldPosition, pButterfly->Position,
                    pButterfly->Rot, 3);                        /* 438 */

    while (pButterfly->Rot[0] < -3.1415925f)                    /* 439 */
    {
        pButterfly->Rot[0] += 6.283185f;
    }
    while (pButterfly->Rot[0] >= 3.1415925f)                    /* 440 */
    {
        pButterfly->Rot[0] -= 6.283185f;
    }
    while (pButterfly->Rot[1] < -3.1415925f)                    /* 441 */
    {
        pButterfly->Rot[1] += 6.283185f;
    }
    while (pButterfly->Rot[1] >= 3.1415925f)                    /* 442 */
    {
        pButterfly->Rot[1] -= 6.283185f;
    }

    /* Velocity is in the butterfly's own frame, so it has to go through the
     * orientation before it can move the position. */
    sceVu0UnitMatrix(mtx);                                      /* 445 */
    sceVu0RotMatrix(mtx, mtx, pButterfly->Rot);                 /* 446 */
    sceVu0TransMatrix(mtx, mtx, pButterfly->Position);          /* 447 */
    sceVu0ApplyMatrix(pos, mtx, pButterfly->Velocity);          /* 450 */

    g3dxVu0CopyVector(pButterfly->OldPosition, pButterfly->Position); /* 454 */
    g3dxVu0CopyVector(pButterfly->Position, pos);               /* 456 */

    if (pButterfly->Velocity[2] != 0.0f)                        /* 460 */
    {
        VelWrkZ = pButterfly->Velocity[2]
                  + (EffectGetRandom(0.0f, 2.0f) - 1.0f) / 5.0f;
        if (VelWrkZ < 10.0f)
        {
            VelWrkZ = 8.0f;
        }
        else if (VelWrkZ > 12.0f)
        {
            VelWrkZ = 12.0f;
        }
        pButterfly->Velocity[2] = VelWrkZ * 0.5f;
    }

    /* 32-frame fades at both ends of a 450-frame life. */
    if (pButterfly->LifeTime >= 418)                            /* 473 */
    {
        pButterfly->AlphaRate = (float)(450 - pButterfly->LifeTime) * 0.03125f;
    }
    else if (pButterfly->LifeTime < 33)                         /* 475 */
    {
        pButterfly->AlphaRate = (float)pButterfly->LifeTime * 0.03125f;
    }
    else
    {
        pButterfly->AlphaRate = 1.0f;                           /* 478 */
    }

    pButterfly->LifeTime--;                                     /* 483 */
    pButterfly->MoveInterval--;                                 /* 486 */

    return 1;                                                   /* 489 */
}

/* --------------------------------------------------------------------------
 *  How often a guide butterfly locks onto its target.              ROM 496
 *
 *  Note the sense: a *smaller* interval means a smaller chance of locking on
 *  (the caller rolls `rand * Interval * 10 < 10`), so the Near value of 0
 *  means "never lock on when close" -- the butterfly wanders the last stretch
 *  instead of driving straight at the target.
 * ------------------------------------------------------------------------ */
static int EffectButterflyGetLockOnInterval(int Type, const float *Position,
                                            const float *Target)
{
    BUTTERFLY_TARGET_PARAMETER *pButterflyTarget;
    float                       NearDistance, FarDistance;
    float                       Distance;
    int                         Interval;

    pButterflyTarget = EffectButterflyTargetParameterPtrGetPublic(Type); /* 501 */
    NearDistance = (float)pButterflyTarget->NearDistance;       /* 502 */
    FarDistance  = (float)pButterflyTarget->FarDistance;        /* 504 */

    Distance = GetDistV2((float *)Position, (float *)Target);   /* 506 */

    if (Distance < NearDistance)                                /* 508 */
    {
        Interval = pButterflyTarget->LockOnIntervalNear;
    }
    else if (Distance < FarDistance)                            /* 511 */
    {
        Interval = pButterflyTarget->LockOnIntervalMiddle;
    }
    else
    {
        Interval = pButterflyTarget->LockOnIntervalFar;         /* 513 */
    }

    return Interval;                                            /* 520 */
}

/* --------------------------------------------------------------------------
 *  The guide butterfly.                                            ROM 532
 *
 *  Unlike the ambient one, the velocity is already in world space here, so the
 *  position is a plain add.  InTime and OutTime are one-shot fade counters:
 *  whichever is non-zero drives AlphaRate and clears itself when it expires --
 *  and OutTime expiring also zeroes LifeTime, which is what actually ends the
 *  butterfly.
 * ------------------------------------------------------------------------ */
static int EffectButterflyUpdateTypeMoveToTarget(BUTTERFLY_DISP *pButterfly)
{
    BUTTERFLY_TARGET_PARAMETER *pButterflyTarget;
    float pos[4];
    float Speed;
    int   LockOnFlg;
    int   RetVal;

    RetVal = 1;                                                 /* 534 */

    pButterflyTarget = EffectButterflyTargetParameterPtrGetPublic(pButterfly->Type); /* 537 */
    Speed = (float)pButterflyTarget->Speed / 100.0f;

    if (pButterfly->LifeTime < 1)                               /* 541 */
    {
        return 0;                                               /* 543 */
    }

    if (pButterfly->MoveInterval < 1)                           /* 545 */
    {
        LockOnFlg = (EffectGetRandom(0.0f, 1.0f)
                     * (float)EffectButterflyGetLockOnInterval(
                           pButterfly->Type, pButterfly->Position,
                           pButterfly->Target) * 10.0f) < 10.0f; /* 546 */

        EffectButterflyGetVelocity(pButterfly->Velocity, pButterfly->Position,
                                   pButterfly->Target, pButterfly->Velocity,
                                   pButterfly->Type, Speed, LockOnFlg); /* 548 */
        pButterfly->MoveInterval = 10;                          /* 549 */
    }

    GetTrgtRotType2(pButterfly->OldPosition, pButterfly->Position,
                    pButterfly->Rot, 3);                        /* 553 */

    while (pButterfly->Rot[0] < -3.1415925f)                    /* 558 */
    {
        pButterfly->Rot[0] += 6.283185f;
    }
    while (pButterfly->Rot[0] >= 3.1415925f)                    /* 562 */
    {
        pButterfly->Rot[0] -= 6.283185f;
    }
    while (pButterfly->Rot[1] < -3.1415925f)                    /* 563 */
    {
        pButterfly->Rot[1] += 6.283185f;
    }
    while (pButterfly->Rot[1] >= 3.1415925f)                    /* 564 */
    {
        pButterfly->Rot[1] -= 6.283185f;
    }

    sceVu0AddVector(pos, pButterfly->Position, pButterfly->Velocity); /* 566 */
    g3dxVu0CopyVector(pButterfly->OldPosition, pButterfly->Position); /* 569 */
    g3dxVu0CopyVector(pButterfly->Position, pos);               /* 574 */

    if (pButterfly->OutTime != 0)                               /* 575 */
    {
        pButterfly->AlphaRate = 1.0f - (float)pButterfly->Count
                                       / (float)pButterfly->OutTime; /* 576 */
        pButterfly->Count++;
        if (pButterfly->Count > pButterfly->OutTime)            /* 577 */
        {
            pButterfly->LifeTime = 0;                           /* 578 */
            pButterfly->OutTime  = 0;                           /* 579 */
            pButterfly->Count    = 0;                           /* 580 */
        }
    }
    else if (pButterfly->InTime != 0)                           /* 584 */
    {
        pButterfly->AlphaRate = (float)pButterfly->Count
                                / (float)pButterfly->InTime;    /* 585 */
        pButterfly->Count++;
        if (pButterfly->Count > pButterfly->InTime)             /* 586 */
        {
            pButterfly->InTime = 0;                             /* 587 */
            pButterfly->Count  = 0;                             /* 588 */
        }
    }
    else
    {
        pButterfly->AlphaRate = 1.0f;                           /* 589 */
    }

    /* LifeTime is never decremented here -- a guide butterfly only ends when
     * something asks it to fade out.  The ROM's, and the reason
     * EffectButterflyAllFadeOut() exists. */
    pButterfly->MoveInterval--;                                 /* 593 */

    return RetVal;                                              /* 602 */
}

/* --------------------------------------------------------------------------
 *  Spawn an ambient butterfly.                                     ROM 611
 * ------------------------------------------------------------------------ */
void EffectButterflyReq(float *Position)
{
    BUTTERFLY_DISP  Butterfly;
    SLL_CELL       *pCell;
    void           *pBody;
    float           mtx[4][4];
    float           pos[4];
    float           RotX, RotY;
    int             MotNo;

    g3dxVu0CopyVector(Butterfly.Position, Position);            /* 612 */

    Butterfly.Velocity[0] = (EffectGetRandom(0.0f, 2.0f) - 1.0f) * 0.5f; /* 628 */
    Butterfly.Velocity[1] = (EffectGetRandom(0.0f, 2.0f) - 1.0f) * 0.5f; /* 634 */
    Butterfly.Velocity[2] = EffectGetRandom(10.0f, 15.0f) * 0.5f; /* 635 */
    Butterfly.Velocity[3] = 1.0f;

    Vector2Rot(Butterfly.Velocity, &RotX, &RotY);               /* 637 */

    /* No rotation in the matrix: the first step is taken along the raw
     * velocity, and only then does the Rot follow the movement. */
    sceVu0UnitMatrix(mtx);                                      /* 638 */
    sceVu0TransMatrix(mtx, mtx, Butterfly.Position);            /* 639 */
    sceVu0ApplyMatrix(pos, mtx, Butterfly.Velocity);            /* 647 */

    g3dxVu0CopyVector(Butterfly.OldPosition, Butterfly.Position); /* 648 */
    g3dxVu0CopyVector(Butterfly.Position, pos);                 /* 650 */

    Butterfly.LifeTime     = 450;                               /* 651 */
    Butterfly.MoveInterval = 10;                                /* 653 */
    Butterfly.AlphaRate    = 0.0f;                              /* 655 */
    Butterfly.MoveType     = 0;
    Butterfly.Rot[0]       = 0.0f;                              /* 658 */
    Butterfly.Rot[1]       = RotY;
    Butterfly.Rot[2]       = 0.0f;
    Butterfly.Rot[3]       = 0.0f;

    pCell = SingleLinkListAddEnd(&ButterFlyDispCtrl.ButterflyList, &Butterfly); /* 660 */
    if (pCell != NULL)
    {
        pBody = SingleLinkListCellBodyPtr(pCell);
        EffectInitAniCtrlButterflyOne(&((BUTTERFLY_DISP *)pBody)->AniCtrl); /* 667 */
        MotNo = (int)EffectGetRandom(0.0f, 2.0f);
        ReqAnm(&((BUTTERFLY_DISP *)pBody)->AniCtrl, 0, BUTTERFLY_ANIM_NO, MotNo); /* 669 */
    }
}

/* --------------------------------------------------------------------------
 *  Spawn a guide butterfly.                                        ROM 682
 *
 *  The first velocity is taken with LockOn forced on, so it launches straight
 *  at the target and only starts wandering on the next steer.
 * ------------------------------------------------------------------------ */
void EffectButterflyReqTarget(int Id, int Type, float *Position, float *Target)
{
    BUTTERFLY_TARGET_PARAMETER *pButterflyTarget;
    BUTTERFLY_DISP  Butterfly;
    SLL_CELL       *pCell;
    void           *pBody;
    float           pos[4];
    float           DirVector[4];
    float           RotX, RotY;
    float           Speed;
    int             MotNo;

    pButterflyTarget = EffectButterflyTargetParameterPtrGet(Type); /* 683 */

    g3dxVu0CopyVector(Butterfly.Position, Position);            /* 691 */
    g3dxVu0CopyVector(Butterfly.Target, Target);                /* 698 */

    Speed = (float)pButterflyTarget->Speed / 100.0f;
    sceVu0SubVector(DirVector, Target, Position);               /* 699 */
    EffectButterflyGetVelocity(Butterfly.Velocity, Position, Target,
                               DirVector, Type, Speed, 0);      /* 700 */

    sceVu0AddVector(pos, Butterfly.Position, Butterfly.Velocity); /* 702 */
    g3dxVu0CopyVector(Butterfly.OldPosition, Butterfly.Position); /* 710 */
    g3dxVu0CopyVector(Butterfly.Position, pos);                 /* 711 */

    Butterfly.LifeTime     = 450;                               /* 713 */
    Butterfly.MoveInterval = 10;                                /* 714 */
    Butterfly.AlphaRate    = 0.0f;                              /* 715 */
    Butterfly.MoveType     = 1;                                 /* 717 */
    Butterfly.InTime       = pButterflyTarget->InTime;          /* 718 */
    Butterfly.OutTime      = 0;                                 /* 719 */
    Butterfly.Count        = 0;                                 /* 721 */
    Butterfly.Type         = Type;                              /* 722 */
    Butterfly.Id           = Id;                                /* 724 */

    Vector2Rot(Butterfly.Velocity, &RotX, &RotY);               /* 727 */
    Butterfly.Rot[0] = 0.0f;                                    /* 729 */
    Butterfly.Rot[1] = RotY;
    Butterfly.Rot[2] = 0.0f;
    Butterfly.Rot[3] = 0.0f;

    pCell = SingleLinkListAddEnd(&ButterFlyDispCtrl.ButterflyList, &Butterfly); /* 736 */
    if (pCell != NULL)
    {
        pBody = SingleLinkListCellBodyPtr(pCell);
        EffectInitAniCtrlButterflyOne(&((BUTTERFLY_DISP *)pBody)->AniCtrl);
        MotNo = (int)EffectGetRandom(0.0f, 2.0f);
        ReqAnm(&((BUTTERFLY_DISP *)pBody)->AniCtrl, 0, BUTTERFLY_ANIM_NO, MotNo); /* 738 */
    }
}

/* --------------------------------------------------------------------------
 *  Steer a guide butterfly.                                        ROM 755
 *
 *  Two stages.  First the aim: locked on, it heads straight at the target;
 *  otherwise the target vector is tilted by a random pitch and yaw drawn from
 *  the preset's Bure ("wobble") fields -- and the pitch is clamped so the
 *  wobble can never point the butterfly more than a quarter turn away from
 *  the target, with the clamp mirrored by the sign of the target's X.
 *
 *  Second the turn limit: if the new heading differs from the current
 *  direction by more than TurnMax, the heading is replaced by the current one
 *  rotated TurnMax toward it -- the sign chosen by which way round the circle
 *  is shorter.
 *
 *  The whole thing then normalises and scales by Speed.  Both normalisations
 *  are VU0 macro-mode rsqrt idioms in the ROM.
 * ------------------------------------------------------------------------ */
static void EffectButterflyGetVelocity(float *Velocity, const float *Position,
                                       const float *Target,
                                       const float *Direction, int Type,
                                       float Speed, int LockOn)
{
    BUTTERFLY_TARGET_PARAMETER *pButterflyTarget;
    float TgtPosVector[4];
    float WrkVelocity[4];
    float NormalTgtPosVector[4];
    float NormalTgtPosVectorY0[4];
    float TmpMat[4][4];
    float VelRotX, VelRotY;
    float DirRotX, DirRotY;
    float TurnMax;
    float RotX, RotY;
    float InnerProduct;
    float Angle;
    float Sabun;

    pButterflyTarget = EffectButterflyTargetParameterPtrGet(Type); /* 761 */
    TurnMax = (float)pButterflyTarget->TurnMax * 0.017453290f;  /* 763 */

    sceVu0SubVector(TgtPosVector, (float *)Target, (float *)Position); /* 766 */

    if (LockOn == 0)                                            /* 767 */
    {
        RotX = ((float)pButterflyTarget->BureUpDown
                    * EffectGetRandom(0.0f, 1.0f)
                - (float)pButterflyTarget->BureUpDown * 0.5f) * 0.017453290f; /* 774 */
        RotY = ((float)pButterflyTarget->BureLeftRight
                    * EffectGetRandom(0.0f, 1.0f)
                - (float)pButterflyTarget->BureLeftRight * 0.5f) * 0.017453290f; /* 775 */

        sceVu0Normalize(NormalTgtPosVector, TgtPosVector);      /* 780 */

        /* The same vector flattened to the XZ plane -- the ROM builds it by
         * masking the low doubleword, i.e. zeroing X *and* Y and keeping Z/W.
         * Reproduced as found; despite the name it is not a Y-only clear. */
        NormalTgtPosVectorY0[0] = 0.0f;
        NormalTgtPosVectorY0[1] = 0.0f;
        NormalTgtPosVectorY0[2] = TgtPosVector[2];
        NormalTgtPosVectorY0[3] = TgtPosVector[3];
        sceVu0Normalize(NormalTgtPosVectorY0, NormalTgtPosVectorY0); /* 782 */

        InnerProduct = sceVu0InnerProduct(NormalTgtPosVector,
                                          NormalTgtPosVectorY0); /* 783 */
        if (InnerProduct < -1.0f || InnerProduct > 1.0f)        /* 784 */
        {
            InnerProduct = 0.0f;
        }
        Angle = acosf(InnerProduct);                            /* 786 */

        /* Clamp the wobbled pitch into the half-turn that still faces the
         * target; which half depends on the side the target is on. */
        if (NormalTgtPosVector[0] < 0.0f)                       /* 789 */
        {
            if (RotX + Angle > 1.5707963f)
            {
                RotX = 1.5707963f - Angle;
            }
            else if (RotX + Angle < -1.5707963f)
            {
                RotX = -1.5707963f - Angle;
            }
        }
        else
        {
            if (RotX + Angle > 1.5707963f)
            {
                RotX = Angle + 1.5707963f;
            }
            else if (RotX + Angle < -1.5707963f)
            {
                RotX = Angle + -1.5707963f;
            }
        }

        sceVu0UnitMatrix(TmpMat);                               /* 801 */
        sceVu0RotMatrixY(TmpMat, TmpMat, RotY);                 /* 805 */
        sceVu0RotMatrixX(TmpMat, TmpMat, RotX);                 /* 806 */
        sceVu0ApplyMatrix(WrkVelocity, TmpMat, TgtPosVector);   /* 810 */
    }
    else
    {
        g3dxVu0CopyVector(WrkVelocity, TgtPosVector);           /* 813 */
    }

    Vector2Rot(WrkVelocity, &VelRotX, &VelRotY);                /* 819 */
    Vector2Rot((float *)Direction, &DirRotX, &DirRotY);         /* 820 */

    /* The compare is done in double precision in the ROM (fptodp/dpcmp), which
     * is why fabs() shows up as a dpsub/dpcmp pair rather than an abs.s. */
    if ((double)TurnMax < fabs((double)(VelRotY - DirRotY)))    /* 823 */
    {
        if (VelRotY < 0.0f)                                     /* 828 */
        {
            VelRotY += 6.283185f;
        }
        if (DirRotY < 0.0f)                                     /* 829 */
        {
            DirRotY += 6.283185f;
        }

        Sabun = VelRotY - DirRotY;                              /* 831 */
        if (Sabun < -3.1415925f)
        {
            Sabun += 6.283185f;                                 /* 832 */
        }
        if (Sabun > 3.1415925f)
        {
            Sabun -= 6.283185f;                                 /* 833 */
        }

        if (Sabun >= 0.0f && Sabun <= 3.1415925f)               /* 834 */
        {
            RotY = (DirRotY + TurnMax) - VelRotY;
            if (RotY < 0.0f)
            {
                RotY += 6.283185f;
            }
            if (RotY > 3.1415925f)
            {
                RotY -= 6.283185f;
            }
        }
        else
        {
            RotY = (DirRotY - TurnMax) - VelRotY;               /* 837 */
            if (RotY < 0.0f)
            {
                RotY += 6.283185f;
            }
            if (RotY > 6.283185f)
            {
                RotY -= 6.283185f;
            }
        }

        sceVu0UnitMatrix(TmpMat);                               /* 842 */
        sceVu0RotMatrixY(TmpMat, TmpMat, RotY);                 /* 843 */
        sceVu0ApplyMatrix(WrkVelocity, TmpMat, WrkVelocity);    /* 844 */
    }

    sceVu0Normalize(WrkVelocity, WrkVelocity);                  /* 847 */
    sceVu0ScaleVector(Velocity, WrkVelocity, Speed);            /* 849 */
}                                                               /* 852 */

/* Free the ANI_CTRL's model allocation before the cell goes.       ROM 861 */
static void ButterflyDelete(SINGLE_LINK_LIST *pSLL, SLL_CELL *pCell)
{
    BUTTERFLY_DISP *pButterfly;

    pButterfly = (BUTTERFLY_DISP *)SingleLinkListCellBodyPtr(pCell); /* 862 */
    motInitAniCtrlFree(&pButterfly->AniCtrl);                   /* 864 */
    SingleLinkListRemove(pSLL, pCell);                          /* 865 */
}

/* The butterfly's *drawn* position is bone 1's, not BUTTERFLY_DISP::Position
 * -- the animation swings the body around the anchor, so the glow and the
 * trail have to follow the model rather than the path.            ROM 875 */
static void EffectGetButterflyPosition(float *Position, BUTTERFLY_DISP *pButterfly)
{
    float LocalWorld[4][4];

    motGetLocalWorldMatrix(LocalWorld, pButterfly->AniCtrl.mpk_p, 1); /* 878 */
    g3dxVu0CopyVector(Position, LocalWorld[3]);
}

void EffectButterflyAllCut(void)                                /* 886 */
{
    SLL_CELL *pCell;
    SLL_CELL *pNext;

    pCell = SingleLinkListBeginCell(&ButterFlyDispCtrl.ButterflyList); /* 887 */

    while (pCell != NULL)                                       /* 890 */
    {
        pNext = pCell->pNext;
        ButterflyDelete(&ButterFlyDispCtrl.ButterflyList, pCell); /* 893 */
        pCell = pNext;
    }
}                                                               /* 895 */

/* --------------------------------------------------------------------------
 *  Find a guide butterfly by id.                                   ROM 903
 *
 *  Matches MoveType 1 *and* Id, so an ambient butterfly is invisible here
 *  whatever its (uninitialised) Id happens to be -- which is what makes
 *  EffectButterflyReq() safe despite never setting one.
 * ------------------------------------------------------------------------ */
static BUTTERFLY_DISP *EffectButterflyGetDispPtr(int Id)
{
    BUTTERFLY_DISP *pRetButterfly;
    SLL_CELL       *pCell;

    pCell = SingleLinkListBeginCell(&ButterFlyDispCtrl.ButterflyList); /* 904 */

    while (pCell != NULL)                                       /* 906 */
    {
        SLL_CELL *pNext = pCell->pNext;

        pRetButterfly = (BUTTERFLY_DISP *)SingleLinkListCellBodyPtr(pCell); /* 908 */
        if (pRetButterfly->MoveType == 1 && pRetButterfly->Id == Id) /* 910 */
        {
            return pRetButterfly;                               /* 912 */
        }
        pCell = pNext;                                          /* 915 */
    }

    return NULL;                                                /* 919 */
}                                                               /* 922 */

void EffectButterflyFadeOut(int Id)                             /* 929 */
{
    BUTTERFLY_DISP *pButterfly;

    pButterfly = EffectButterflyGetDispPtr(Id);                 /* 933 */
    if (pButterfly != NULL)
    {
        pButterfly->OutTime =
            EffectButterflyTargetParameterPtrGet(pButterfly->Type)->OutTime; /* 934 */
    }
}                                                               /* 936 */

void EffectButterflyAllFadeOut(void)                            /* 944 */
{
    SLL_CELL       *pCell;
    BUTTERFLY_DISP *pButterfly;

    pCell = SingleLinkListBeginCell(&ButterFlyDispCtrl.ButterflyList); /* 945 */

    while (pCell != NULL)                                       /* 949 */
    {
        SLL_CELL *pNext = pCell->pNext;

        pButterfly = (BUTTERFLY_DISP *)SingleLinkListCellBodyPtr(pCell); /* 951 */
        if (pButterfly->MoveType == 1)                          /* 953 */
        {
            pButterfly->OutTime =
                EffectButterflyTargetParameterPtrGet(pButterfly->Type)->OutTime; /* 954 */
        }
        pCell = pNext;                                          /* 956 */
    }
}                                                               /* 959 */

void EffectButterflyChangeTarget(int Id, float *Target)         /* 967 */
{
    BUTTERFLY_DISP *pButterfly;

    pButterfly = EffectButterflyGetDispPtr(Id);                 /* 968 */
    if (pButterfly != NULL)
    {
        g3dxVu0CopyVector(pButterfly->Target, Target);          /* 970 */
    }
}

void EffectButterflyChangeType(int Id, int Type)                /* 979 */
{
    BUTTERFLY_DISP *pButterfly;

    pButterfly = EffectButterflyGetDispPtr(Id);                 /* 982 */
    if (pButterfly != NULL)
    {
        pButterfly->Type = Type;                                /* 983 */
    }
}

void EffectButterflyAllChangeTarget(float *Target)              /* 991 */
{
    SLL_CELL       *pCell;
    BUTTERFLY_DISP *pButterfly;

    pCell = SingleLinkListBeginCell(&ButterFlyDispCtrl.ButterflyList); /* 992 */

    while (pCell != NULL)                                       /* 995 */
    {
        SLL_CELL *pNext = pCell->pNext;

        pButterfly = (BUTTERFLY_DISP *)SingleLinkListCellBodyPtr(pCell); /* 997 */
        if (pButterfly->MoveType == 1)                          /* 999 */
        {
            g3dxVu0CopyVector(pButterfly->Target, Target);
        }
        pCell = pNext;
    }
}                                                               /* 1003 */

int EffectButterflyNumGet(void)                                 /* 1011 */
{
    /* Every butterfly, ambient ones included. */
    return ButterFlyDispCtrl.ButterflyList.RegCount;            /* 1014 */
}

static BUTTERFLY_TARGET_PARAMETER *EffectButterflyTargetParameterPtrGet(int Type)
{                                                               /* 1021 */
    if ((u_int)Type > 4)                                        /* 1022 */
    {
        Type = 0;
    }
    return pButterflyTargetParamPtr[Type];
}

/* Identical body, exported.  The ROM keeps both -- the static one is what this
 * file calls and the public one is what player/event code calls.   ROM 1042 */
BUTTERFLY_TARGET_PARAMETER *EffectButterflyTargetParameterPtrGetPublic(int Type)
{
    if ((u_int)Type > 4)
    {
        Type = 0;
    }
    return pButterflyTargetParamPtr[Type];
}

/* --------------------------------------------------------------------------
 *  The trail motes.                                               ROM 1055
 * ------------------------------------------------------------------------ */
void EffectButterflyParticleInit(void)
{
    SingleLinkListInit(&ButterFlyParticleCtrl.ParticleList,
                       sizeof(BUTTERFLY_PARTICLE));             /* 1056 */
}

void EffectButterflyParticleMain(void)                          /* 1063 */
{
    SLL_CELL           *pCell;
    SLL_CELL           *pNext;
    BUTTERFLY_PARTICLE *pParticle;
    GRA3DCAMERA        *pCam;
    float               RotX, RotY;

    pCell = SingleLinkListBeginCell(&ButterFlyParticleCtrl.ParticleList); /* 1064 */

    pCam = gra3dGetCamera();                                    /* 1066 */
    Get2PosRot(gra3dcamGetPosition(), pCam->vTarget, &RotX, &RotY); /* 1067 */

    while (pCell != NULL)                                       /* 1070 */
    {
        pParticle = (BUTTERFLY_PARTICLE *)SingleLinkListCellBodyPtr(pCell); /* 1072 */
        pNext = pCell->pNext;

        ButterflyParticleOneDraw(pParticle, RotX, RotY);        /* 1076 */
        if (ButterflyParticleUpdate(pParticle) == 0)            /* 1077 */
        {
            SingleLinkListRemove(&ButterFlyParticleCtrl.ParticleList, pCell); /* 1078 */
        }

        pCell = pNext;
    }
}                                                               /* 1081 */

static void EffectButterflyParticleReq(SINGLE_LINK_LIST *pSLL,  /* 1094 */
                                       const float *Position,
                                       const float *Velocity,
                                       const float *Acceleration)
{
    BUTTERFLY_PARTICLE Particle;

    g3dxVu0CopyVector(Particle.Position, (float *)Position);
    g3dxVu0CopyVector(Particle.Velocity, (float *)Velocity);
    g3dxVu0CopyVector(Particle.Acceleration, (float *)Acceleration);

    Particle.LifeTime  = 30;                                    /* 1103 */
    Particle.AlphaRate = 1.0f;                                  /* 1105 */

    SingleLinkListAddEnd(pSLL, &Particle);                      /* 1107 */
}

/* --------------------------------------------------------------------------
 *  Throw a small burst of motes off a butterfly.                  ROM 1118
 *
 *  Two to four of them, each along a direction jittered off the butterfly's
 *  own facing.  Note the two jitter expressions are not symmetric: pitch is
 *  `rand*180 + 350 - 90` degrees and yaw `rand*180 + 180 - 90`, so the motes
 *  are thrown mostly *behind* the butterfly in pitch.  Written out as found,
 *  constant folding included.
 * ------------------------------------------------------------------------ */
static void EffectButterflyParticleReqAdjustParam(SINGLE_LINK_LIST *pSLL,
                                                  const float *Position,
                                                  const float *Rot)
{
    float TmpMat[4][4];
    float Accel[4];
    float Velocity[4];
    float TmpVec[4];
    float ButterflyRotX, ButterflyRotY;
    float RotX, RotY;
    int   ReqNum;
    int   i;

    memset(Accel, 0, sizeof(Accel));                            /* 1120 */
    Accel[1] = 0.1f;                             /* Y is down: they sink */

    ButterflyRotX = Rot[0];
    ButterflyRotY = Rot[1];

    ReqNum = (int)EffectGetRandom(0.5f, 3.5f) + 2;              /* 1125 */

    for (i = 0; i < ReqNum; i++)                                /* 1126 */
    {
        TmpVec[0] = 0.0f;
        TmpVec[1] = 0.0f;
        TmpVec[2] = 1.0f;
        TmpVec[3] = 1.0f;
        TmpVec[2] *= EffectGetRandom(0.0f, 40.0f) / 10.0f + 1.0f; /* 1134 */

        RotX = ButterflyRotX
               + (EffectGetRandom(350.0f, 530.0f) - 90.0f) * 0.017453290f; /* 1139 */
        RotY = ButterflyRotY
               + (EffectGetRandom(180.0f, 360.0f) - 90.0f) * 0.017453290f; /* 1140 */

        if (RotX < -3.1415925f)                                 /* 1151 */
        {
            RotX += 6.283185f;
        }
        else if (RotX > 3.1415925f)
        {
            RotX -= 6.283185f;
        }
        if (RotY < -3.1415925f)                                 /* 1152 */
        {
            RotY += 6.283185f;
        }
        else if (RotY > 3.1415925f)
        {
            RotY -= 6.283185f;
        }

        sceVu0UnitMatrix(TmpMat);                               /* 1157 */
        sceVu0RotMatrixX(TmpMat, TmpMat, RotX);                 /* 1158 */
        sceVu0RotMatrixY(TmpMat, TmpMat, RotY);                 /* 1160 */
        sceVu0ApplyMatrix(Velocity, TmpMat, TmpVec);            /* 1163 */

        EffectButterflyParticleReq(pSLL, Position, Velocity, Accel); /* 1165 */
    }                                                           /* 1168 */
}                                                               /* 1169 */

static void ButterflyParticleOneDraw(const BUTTERFLY_PARTICLE *pParticle,
                                     float RotX, float RotY)    /* 1180 */
{
    float    matWorldLocal[4][4];
    DRAW_ENV DrawEnv;
    u_char   a;

    /* ALPHA 0x44 rather than the butterfly glow's 0x48: the motes lerp against
     * the frame instead of adding to it. */
    DrawEnv.alpha = 0x44;                                       /* 1182 */
    DrawEnv.tex1  = 0x161;
    DrawEnv.clamp = 0;
    DrawEnv.test  = 0x5000d;
    DrawEnv.zbuf  = 0x10a000118ULL;
    DrawEnv.prim  = 0x302a400000008004ULL;

    sceVu0UnitMatrix(matWorldLocal);                            /* 1194 */
    sceVu0RotMatrixX(matWorldLocal, matWorldLocal, RotX);       /* 1195 */
    sceVu0RotMatrixY(matWorldLocal, matWorldLocal, RotY);       /* 1196 */
    sceVu0TransMatrix(matWorldLocal, matWorldLocal,
                      (float *)pParticle->Position);            /* 1197 */

    a = (u_char)(int)(pParticle->AlphaRate * 128.0f);
    Set3DPosTexure2(matWorldLocal, &DrawEnv, 0x58, 3.1999998f, 3.1999998f,
                    0x80, 0x80, 0x80, a, 0);                    /* 1206 */
}

static int ButterflyParticleUpdate(BUTTERFLY_PARTICLE *pParticle) /* 1219 */
{
    int RetVal = 0;

    if (pParticle->LifeTime > 0)                                /* 1220 */
    {
        sceVu0AddVector(pParticle->Position, pParticle->Position,
                        pParticle->Velocity);                   /* 1222 */
        sceVu0AddVector(pParticle->Velocity, pParticle->Velocity,
                        pParticle->Acceleration);               /* 1223 */
        pParticle->Position[3] = 1.0f;                          /* 1224 */
        pParticle->LifeTime--;                                  /* 1225 */
        pParticle->AlphaRate = (float)pParticle->LifeTime / 30.0f; /* 1226 */
        RetVal = 1;                                             /* 1230 */
    }

    return RetVal;                                              /* 1234 */
}                                                               /* 1237 */

void EffectButterflyParticleAllCut(void)                        /* 1244 */
{
    SLL_CELL *pCell;
    SLL_CELL *pNext;

    pCell = SingleLinkListBeginCell(&ButterFlyParticleCtrl.ParticleList); /* 1245 */

    while (pCell != NULL)                                       /* 1248 */
    {
        pNext = pCell->pNext;
        SingleLinkListRemove(&ButterFlyParticleCtrl.ParticleList, pCell); /* 1251 */
        pCell = pNext;
    }
}                                                               /* 1253 */
