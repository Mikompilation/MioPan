// FILE: /home/zero_rom/zero2np/src/graphics/effect/effect_torch.c
//
// COMPLETE.  All 17 ZERO2.MAP .text symbols plus 22 statics, the three
// 9-entry parameter tables (.data 0x2fd5b0 / 0x2fd9e8 / 0x2fdb50) and the
// .rodata selector tables that index them.
//
// A NOTE ON THE /* NNN */ ANNOTATIONS.  Three inlines eat line notes here:
// fixed_array::operator[] (fixed_array.h 124/125), g3dxVu0CopyVector
// (g3dxVu0.h 134/135) and EffectGetRandom (effect.h 217).  A statement whose
// only work goes through one of them leaves no $LM of its own, so those
// numbers are interpolated into the measured gap; everything else is read off
// the disassembly.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), 0x00169180.

#include "effect_torch.h"

#include <stdio.h>
#include <string.h>                             /* memset */

#include "effect_oth.h"                         /* DrawFrea / draw_distortion_particles2 */
#include "effect_sub.h"                         /* Set3DPosTexure / Get2PosRot / SE */

#include "../draw_env.h"
#include "../graph2d/graph2d.h"                 /* effdat[] */
#include "../graph3d/g3dxVu0.h"                 /* g3dxVu0CopyVector */
#include "../graph3d/gra3d.h"                   /* gra3dGetCamera / gra3dcamGetPosition */
#include "../../sdk/libvu0.h"

/* --------------------------------------------------------------------------
 *  The nine authored presets.                            ROM 87 / 100 / 114
 *
 *  Types 0..8 are the placement types SetRDPFire() hands out: 6 is the moving
 *  torch, 8 the silent one, and 3 is the long-lived low-spawn variant (a
 *  1000-frame lifetime against everyone else's 50).  Type 7 is the only one
 *  with a distinctly different flare -- 303 against 140.
 *
 *  Only types 0, 4, 5, 6, 7 and 8 ever burst; 1, 2 and 3 carry
 *  IntervalMax == IntervalMin == 0, which EffOthTorch2BurstIntervalTimeGet()
 *  answers with -1 ("never").
 * ------------------------------------------------------------------------ */
TORCH2_PARAMETER TorchType00 =                              /* data 2fd5b0 */
{   50, 6, 1, 140, 1031, 974, 889, 868, 1011, 981, 284, 358, 0, 25, 0,
    181, 89, 67, 0, 2, 0, 1, 0, 140, 50, 50, 50, 89, 80 };
TORCH2_PARAMETER TorchType01 =                              /* data 2fd628 */
{   50, 6, 1, 140, 1031, 974, 889, 868, 1011, 981, 124, 358, 0, 31, 0,
    55, 29, 21, 0, 2, 0, 1, 0, 140, 50, 50, 50, 89, 80 };
TORCH2_PARAMETER TorchType02 =                              /* data 2fd6a0 */
{   50, 6, 1, 171, 1031, 974, 889, 868, 1011, 981, 84, 358, 0, 25, 0,
    181, 89, 67, 0, 2, 0, 1, 0, 140, 50, 50, 50, 89, 80 };
TORCH2_PARAMETER TorchType03 =                              /* data 2fd718 */
{   1000, 3, 0, 96, 1031, 974, 989, 701, 1011, 981, 97, 121, 1000, 38, 0,
    21, 21, 21, 0, 1, 0, 1, 0, 140, 50, 50, 50, 89, 80 };
TORCH2_PARAMETER TorchType04 =                              /* data 2fd790 */
{   50, 6, 1, 140, 1031, 974, 889, 868, 1011, 981, 100, 358, 0, 30, 0,
    55, 29, 21, 0, 2, 0, 1, 0, 140, 50, 50, 50, 89, 80 };
TORCH2_PARAMETER TorchType05 =                              /* data 2fd808 */
{   50, 6, 1, 140, 1031, 974, 889, 868, 1011, 981, 202, 358, 0, 25, 0,
    57, 63, 66, 0, 2, 0, 1, 0, 140, 50, 50, 50, 89, 80 };
TORCH2_PARAMETER TorchType06 =                              /* data 2fd880 */
{   50, 6, 1, 140, 1031, 974, 889, 868, 1011, 981, 284, 358, 0, 25, 0,
    181, 89, 67, 0, 2, 0, 1, 0, 140, 50, 50, 50, 89, 80 };
TORCH2_PARAMETER TorchType07 =                              /* data 2fd8f8 */
{   51, 7, 1, 219, 1031, 974, 889, 868, 1011, 981, 280, 496, 18, 18, 0,
    68, 72, 139, 0, 2, 0, 1, 0, 303, 75, 77, 191, 68, 80 };
TORCH2_PARAMETER TorchType08 =                              /* data 2fd970 */
{   50, 6, 1, 140, 1031, 974, 889, 868, 1011, 981, 284, 358, 0, 25, 0,
    181, 89, 67, 0, 2, 0, 1, 0, 140, 50, 50, 50, 89, 80 };

/* The burst envelope is identical for all nine; only the interval differs. */
TORCH2_BURST_PARAMETER TorchBurstType00 =                   /* data 2fd9e8 */
{   419, 149, 666, 246, 3, 33, 50, 300, 150 };
TORCH2_BURST_PARAMETER TorchBurstType01 =                   /* data 2fda10 */
{   419, 149, 666, 246, 3, 33, 50, 0, 0 };
TORCH2_BURST_PARAMETER TorchBurstType02 =                   /* data 2fda38 */
{   419, 149, 666, 246, 3, 33, 50, 0, 0 };
TORCH2_BURST_PARAMETER TorchBurstType03 =                   /* data 2fda60 */
{   419, 149, 666, 246, 3, 33, 50, 0, 0 };
TORCH2_BURST_PARAMETER TorchBurstType04 =                   /* data 2fda88 */
{   419, 149, 666, 246, 3, 33, 50, 300, 150 };
TORCH2_BURST_PARAMETER TorchBurstType05 =                   /* data 2fdab0 */
{   419, 149, 666, 246, 3, 33, 50, 141, 74 };
TORCH2_BURST_PARAMETER TorchBurstType06 =                   /* data 2fdad8 */
{   419, 149, 666, 246, 3, 33, 50, 300, 150 };
TORCH2_BURST_PARAMETER TorchBurstType07 =                   /* data 2fdb00 */
{   419, 149, 666, 246, 3, 33, 50, 300, 150 };
TORCH2_BURST_PARAMETER TorchBurstType08 =                   /* data 2fdb28 */
{   419, 149, 666, 246, 3, 33, 50, 300, 150 };

TORCH2_SPARK_PARAMETER TorchSparkType00 =                   /* data 2fdb50 */
{   87, 3, 18, 13, 1051, 951, 1056, 897, 1053, 950, 1000, 981, 848,
    23, 0, 127, 0, 185, 76, 66 };
TORCH2_SPARK_PARAMETER TorchSparkType01 =                   /* data 2fdba0 */
{   87, 3, 18, 13, 1051, 951, 1004, 897, 1053, 950, 1000, 981, 848,
    48, 0, 100, 0, 75, 35, 21 };
TORCH2_SPARK_PARAMETER TorchSparkType02 =                   /* data 2fdbf0 */
{   87, 3, 18, 13, 1051, 951, 1004, 897, 1053, 950, 1000, 981, 848,
    48, 0, 100, 0, 75, 35, 21 };
TORCH2_SPARK_PARAMETER TorchSparkType03 =                   /* data 2fdc40 */
{   87, 3, 18, 13, 1051, 951, 1004, 897, 1053, 950, 1000, 981, 848,
    48, 0, 100, 0, 75, 35, 21 };
TORCH2_SPARK_PARAMETER TorchSparkType04 =                   /* data 2fdc90 */
{   87, 3, 18, 13, 1051, 951, 1004, 897, 1053, 950, 1000, 981, 848,
    48, 0, 100, 0, 75, 35, 21 };
TORCH2_SPARK_PARAMETER TorchSparkType05 =                   /* data 2fdce0 */
{   87, 3, 18, 13, 1051, 951, 1056, 897, 1053, 950, 1000, 981, 848,
    23, 0, 127, 0, 51, 55, 78 };
TORCH2_SPARK_PARAMETER TorchSparkType06 =                   /* data 2fdd30 */
{   87, 3, 18, 13, 1051, 951, 1056, 897, 1053, 950, 1000, 981, 848,
    23, 0, 127, 0, 185, 76, 66 };
TORCH2_SPARK_PARAMETER TorchSparkType07 =                   /* data 2fdd80 */
{   87, 3, 18, 13, 1051, 951, 1056, 897, 1053, 950, 1000, 981, 848,
    23, 0, 127, 0, 185, 76, 66 };
TORCH2_SPARK_PARAMETER TorchSparkType08 =                   /* data 2fddd0 */
{   87, 3, 18, 13, 1051, 951, 1056, 897, 1053, 950, 1000, 981, 848,
    23, 0, 127, 0, 185, 76, 66 };

/* The selector tables the three PtrGet()s index.  These sit in .rodata
 * (0x3a8440 / 0x3a8468 / 0x3a8490) and are *not* in globals.txt, so the ROM's
 * own names for them are unrecoverable; the pointers themselves are read
 * straight out of the ELF.  In the ROM the three reference_fixed_array
 * wrappers are file-scope objects seeded by the object's static-init helper,
 * which is what leaves them zero in the image. */
static TORCH2_PARAMETER       *TorchParamTbl[9] =           /* rdata 3a8440 */
{
    &TorchType00, &TorchType01, &TorchType02, &TorchType03, &TorchType04,
    &TorchType05, &TorchType06, &TorchType07, &TorchType08
};
static TORCH2_BURST_PARAMETER *TorchBurstParamTbl[9] =      /* rdata 3a8468 */
{
    &TorchBurstType00, &TorchBurstType01, &TorchBurstType02,
    &TorchBurstType03, &TorchBurstType04, &TorchBurstType05,
    &TorchBurstType06, &TorchBurstType07, &TorchBurstType08
};
static TORCH2_SPARK_PARAMETER *TorchSparkParamTbl[9] =      /* rdata 3a8490 */
{
    &TorchSparkType00, &TorchSparkType01, &TorchSparkType02,
    &TorchSparkType03, &TorchSparkType04, &TorchSparkType05,
    &TorchSparkType06, &TorchSparkType07, &TorchSparkType08
};

reference_fixed_array<TORCH2_PARAMETER *, 9>
    pTorchParamPtr(TorchParamTbl);                          /* sdata 3f00c0 *//* 87 */
reference_fixed_array<TORCH2_BURST_PARAMETER *, 9>
    pTorchBurstParamPtr(TorchBurstParamTbl);                /* sdata 3f00c8 *//* 100 */
reference_fixed_array<TORCH2_SPARK_PARAMETER *, 9>
    pTorchSparkParamPtr(TorchSparkParamTbl);                /* sdata 3f00d0 *//* 114 */

TORCH2_BIGFREA_CTRL Torch2BigFreaCtrl;                      /* data 2fde20 */

/* Defined below their first use in the ROM, so it carried these too. */
static int  EffOthTorch2BurstIntervalTimeGet(int Type);
static int  EffOthTorch2WindIntervalTimeGet(void);
static void Torch2BasePosMoveInfluence(PARTICLE *pPartTop, float *Diff,
                                       float WholeScale, int InitLifeTime,
                                       int ParticleFollowMove);
static void Torch2AddParticle(TORCH_CTRL *pTc, float *pos, float *vel,
                              float r, float g, float b, float a, float Scale);
static void Torch2UpdateParticles(PARTICLE *pPartTop, int InitLifeTime,
                                  float StartAlpha, float EndAlpha,
                                  float StartScale, float EndScale);
static void EffOthTorch2BurstReqCtrl(TORCH_CTRL *pTc, float WholeScale,
                                     float AlphaRate);
static int  Torch2BurstRangeAndStartScaleGet(float *pRange, float *pStartScale,
                                             int Count,
                                             TORCH2_PARAMETER *pParam,
                                             TORCH2_BURST_PARAMETER *pBurstParam);
static void Torch2WindReqCtrl(TORCH_CTRL *pTc);
static int  Torch2WindVectorGet(float *NowWind, float *WindMax, int Count);
static void AddSparkParticle(SPARK_CTRL *pSc, float Scale,
                             TORCH2_SPARK_PARAMETER *pSparkParam);
static int  UpdateSparkParticles(PARTICLE *pPtop, float BrakeRate,
                                 float AlphaRate,
                                 TORCH2_SPARK_PARAMETER *pSparkParam);
static void *ContTorchSpark(SPARK_CTRL *pSc, float *pos, float size, float sr,
                            TORCH2_SPARK_PARAMETER *pSparkParam);
static void Torch2BigFreaDraw(const float *Position, float ScaleX, float ScaleY,
                              int R, int G, int B, int Alpha);

/* --------------------------------------------------------------------------
 *  Preset lookup.                                     ROM 157 / 169 / 181
 *
 *  The clamp is unsigned, so a negative Type falls to 0 as well.
 * ------------------------------------------------------------------------ */
TORCH2_PARAMETER *EffOthTorch2ParameterPtrGet(int Type)
{
    if ((u_int)Type > 8)                                            /* 158 */
    {
        Type = 0;
    }
    return pTorchParamPtr[Type];
}

TORCH2_BURST_PARAMETER *EffOthTorch2BurstParameterPtrGet(int Type)   /* 169 */
{
    if ((u_int)Type > 8)                                            /* 170 */
    {
        Type = 0;
    }
    return pTorchBurstParamPtr[Type];
}

TORCH2_SPARK_PARAMETER *EffOthTorch2SparkParameterPtrGet(int Type)   /* 181 */
{
    if ((u_int)Type > 8)                                            /* 182 */
    {
        Type = 0;
    }
    return pTorchSparkParamPtr[Type];
}

/* --------------------------------------------------------------------------
 *  Wipe a flame's particle ring and seed its counters.             ROM 196
 *
 *  Note `max` is seeded from twice the requested lifetime and then never read
 *  by anything in this object -- the ring length is the fixed 200.
 * ------------------------------------------------------------------------ */
void TorchPartInit(TORCH_CTRL *pTc, int LifeTime, float *BasePos,
                   int Type, int SeReqFlg)
{
    int i;

    for (i = 0; i < 200; i++)                                       /* 199 */
    {
        pTc->particles[i].position[0] = 0.0f;
        pTc->particles[i].position[1] = 0.0f;
        pTc->particles[i].position[2] = 0.0f;
        pTc->particles[i].position[3] = 1.0f;
        pTc->particles[i].color[3]    = 0.0f;
        pTc->particles[i].lifetime    = 0;
    }

    g3dxVu0CopyVector(pTc->BasePos, BasePos);

    pTc->head       = 0;
    pTc->BurstCount = 0;
    /* Reads pTc->Type, which is written four lines later -- so the very first
     * burst interval is drawn from whatever Type the block already held.  On a
     * fresh EFFECT_MALLOC that is uninitialised heap; on a reused block it is
     * the previous torch's type.  Faithful to the ROM. */
    pTc->BurstInterval = EffOthTorch2BurstIntervalTimeGet(pTc->Type);
    pTc->WindCount     = 0;
    pTc->WindInterval  = EffOthTorch2WindIntervalTimeGet();
    pTc->SeReqFlg   = (short)SeReqFlg;
    pTc->blife      = (short)LifeTime;
    pTc->Type       = (short)Type;
    pTc->AlphaRate  = 1.0f;
    pTc->max        = (short)(LifeTime * 2);
    pTc->disp       = 0;
}                                                                   /* 215 */

/* --------------------------------------------------------------------------
 *  Effect handler for a torch (effect id 0x1c).                    ROM 222
 *
 *  ec->pnt[0] is the caller's position vector, ec->pnt[1] the TORCH_CTRL,
 *  ec->dat.uc8[2] the torch type and uc8[4] the SE flag.
 * ------------------------------------------------------------------------ */
void SetTorch2(EFFECT_CONT *ec)
{
    TORCH2_PARAMETER       *pParam;
    TORCH2_BURST_PARAMETER *pBurstParam;
    float                   pos[4];

    /* Hard-coded rather than EffOthTorch2BurstParameterPtrGet(ec->dat.uc8[2]):
     * every torch, whatever its type, bursts on type 0's timing.  The ROM's,
     * and it is why types 1/2/3's zero intervals never take effect. */
    pBurstParam = &TorchBurstType00;                                /* 225 */

    g3dxVu0CopyVector(pos, (float *)ec->pnt[0]);

    pParam = EffOthTorch2ParameterPtrGet(ec->dat.uc8[2]);           /* 232 */

    if (ec->pnt[1] == NULL)                                         /* 234 */
    {
        ec->pnt[1] = EFFECT_MALLOC(sizeof(TORCH_CTRL));             /* 235 */
        if (ec->pnt[1] == NULL)                                     /* 236 */
        {
            ResetEffects(ec);                                       /* 247 */
            return;                                                 /* 248 */
        }
        TorchPartInit((TORCH_CTRL *)ec->pnt[1], pParam->LifeTime, pos,
                      ec->dat.uc8[2], ec->dat.uc8[4]);              /* 244 */
    }

    {
        /* One statement in the ROM: the whole struct comes out of a .rodata
         * blob (0x3a83a8) copied into the stack slot. */
        DRAW_ENV_5 env = { 0x48, 0x161, 0, 0x5000d, 0x10a000118ULL };/* 252 */

        SetDrawEnv(0, &env);                                        /* 259 */
    }

    ContTorch2((TORCH_CTRL *)ec->pnt[1], pos, ec->z, pParam, pBurstParam); /* 261 */
}                                                                   /* 262 */

/* --------------------------------------------------------------------------
 *  Run one frame of a flame.                                       ROM 271
 *
 *  Order matters: the base-position delta is measured *before* BasePos is
 *  updated, the burst and wind envelopes are sampled next, then the existing
 *  particles are drawn, then new ones are spawned, then everything ages.  So a
 *  particle spawned this frame is not drawn until the next one.
 * ------------------------------------------------------------------------ */
void ContTorch2(TORCH_CTRL *pTc, float *pos, int Depth,
                TORCH2_PARAMETER *pParam, TORCH2_BURST_PARAMETER *pBurstParam)
{
    float        work[4][4];
    float        local_clip[4][4];
    float        local_world[4][4];
    float        local_screen[4][4];
    float        ppos[4];
    float        pvel[4];
    float        wpos[4];
    float        Diff[4];
    float        Wind[4];
    GRA3DCAMERA *pCam;
    float        Range;
    float        StartScale;
    float        FreaSize;
    float        FreaAlpha;
    float        WholeScale;
    u_long       AlphaBlend;
    int          FreaR, FreaG, FreaB;
    int          ParticleNum;
    int          i;

    memset(ppos, 0, sizeof(ppos)); ppos[3] = 1.0f;                  /* 273 */
    memset(pvel, 0, sizeof(pvel));                                  /* 274 */
    memset(Wind, 0, sizeof(Wind));                                  /* 277 */

    pCam = gra3dGetCamera();                                        /* 279 */

    FreaSize   = (float)pParam->FreaSize;                           /* 308 */
    FreaAlpha  = (float)pParam->FreaAlpha;                          /* 309 */
    FreaR      = pParam->FreaR;                                     /* 310 */
    FreaG      = pParam->FreaG;                                     /* 311 */
    FreaB      = pParam->FreaB;                                     /* 312 */
    AlphaBlend = (u_long)pParam->AlphaBlendA |                      /* 313 */
                 ((u_long)pParam->AlphaBlendB << 2) |
                 ((u_long)pParam->AlphaBlendC << 4) |
                 ((u_long)pParam->AlphaBlendD << 6) |
                 ((u_long)pParam->AlphaBlendFIX << 32);
    WholeScale = (float)pParam->Scale / 100.0f;                     /* 314 */

    if (pTc == NULL)                                                /* 316 */
    {
        printf("Particle Buffer is Full : in ContTorch()\n");       /* 317 */
        return;                                                     /* 318 */
    }

    /* How far the anchor moved since last frame, and the new anchor. */
    sceVu0SubVector(Diff, pos, pTc->BasePos);                       /* 323 */
    g3dxVu0CopyVector(pTc->BasePos, pos);

    Torch2BasePosMoveInfluence(pTc->particles.data(), Diff, WholeScale,
                               pTc->blife, pParam->ParticleFollowMove); /* 338 */

    if (pBurstParam != NULL)                                        /* 341 */
    {
        EffOthTorch2BurstReqCtrl(pTc, WholeScale, pTc->AlphaRate);  /* 342 */
        if (Torch2BurstRangeAndStartScaleGet(&Range, &StartScale,
                                             pTc->BurstCount,
                                             pParam, pBurstParam) == 0) /* 347 */
        {
            pTc->BurstCount = 0;                                    /* 348 */
        }
    }
    else
    {
        Range      = (float)pParam->Range / 100.0f;                 /* 352 */
        StartScale = (float)pParam->StartScale / 100.0f;            /* 353 */
    }

    Torch2WindReqCtrl(pTc);                                         /* 357 */
    if (Torch2WindVectorGet(Wind, pTc->WindMax, pTc->WindCount) == 0) /* 360 */
    {
        pTc->WindCount = 0;
    }

    /* Particles are stored in flame-local units; the 25x here is what turns
     * a Scale of 284 (2.84) into world size. */
    sceVu0UnitMatrix(work);                                         /* 365 */
    work[0][0] = work[1][1] = work[2][2] = WholeScale * 25.0f;      /* 366 */
    sceVu0TransMatrix(local_world, work, pos);                      /* 368 */
    sceVu0MulMatrix(local_screen, pCam->matWorldScreen, local_world); /* 370 */
    sceVu0MulMatrix(local_clip, pCam->matWorldClipPolygon, local_world); /* 371 */

    /* The flare sits 20 units below the anchor, and both its size and its
     * alpha scale with how many particles were visible last frame. */
    g3dxVu0CopyVector(wpos, pos);
    wpos[1] -= 20.0f;                                               /* 375 */
    DrawFrea(wpos,
             ((FreaSize / 1000.0f) * (float)pTc->disp * 3.3f * StartScale)
                 / 200.0f,
             Depth, FreaR, FreaG, FreaB,
             (int)((FreaAlpha * (float)pTc->disp) / 200.0f));       /* 376 */

    /* 4289.9995f is one ulp below 4290.0 -- the EE compiler's usual literal
     * truncation, so the source constant was 4290. */
    pTc->disp = (short)draw_distortion_particles2(
                    local_screen, local_clip, 200, pTc->particles.data(),
                    (WholeScale * 4289.9995f) / pCam->fFov,
                    effdat[EffWrkMonochroModeGet() + 0x4e].tex0,
                    AlphaBlend);                                    /* 380 */

    ParticleNum = (int)EffectGetRandom((float)pParam->AppearNumMin,
                                       (float)pParam->AppearNumMax); /* 388 */
    for (i = 0; i < ParticleNum; i++)                               /* 389 */
    {
        /* Spawned on a flat disc of diameter Range; Y is always 0 because the
         * flame rises from its own base. */
        ppos[0] = EffectGetRandom(-0.5f, 0.5f) * Range;             /* 390 */
        ppos[1] = 0.0f;                                             /* 391 */
        ppos[2] = EffectGetRandom(-0.5f, 0.5f) * Range;             /* 392 */

        /* Speeds are thousandths biased by 1000, hence the -1000 then /1000. */
        pvel[0] = (EffectGetRandom((float)pParam->SpeedXMin,
                                   (float)pParam->SpeedXMax) - 1000.0f)
                  / 1000.0f + Wind[0];                              /* 393 */
        pvel[1] = (EffectGetRandom((float)pParam->SpeedYMin,
                                   (float)pParam->SpeedYMax) - 1000.0f)
                  / 1000.0f + Wind[1];                              /* 394 */
        pvel[2] = (EffectGetRandom((float)pParam->SpeedZMin,
                                   (float)pParam->SpeedZMax) - 1000.0f)
                  / 1000.0f + Wind[2];                              /* 395 */

        Torch2AddParticle(pTc, ppos, pvel,
                          (float)pParam->R, (float)pParam->G, (float)pParam->B,
                          (float)pParam->StartAlpha * pTc->AlphaRate,
                          StartScale);                              /* 397 */
    }                                                               /* 398 */

    Torch2UpdateParticles(pTc->particles.data(), pTc->blife,
                          (float)pParam->StartAlpha * pTc->AlphaRate,
                          (float)pParam->EndAlpha * pTc->AlphaRate,
                          StartScale,
                          (float)pParam->EndScale / 100.0f);        /* 402 */

    if (pTc->BurstCount > 0)                                        /* 403 */
    {
        pTc->BurstCount++;                                          /* 404 */
    }
    if (pTc->WindCount > 0)                                         /* 406 */
    {
        pTc->WindCount++;                                           /* 407 */
    }
}                                                                   /* 412 */

/* --------------------------------------------------------------------------
 *  Drag the young particles along when the anchor moves.           ROM 422
 *
 *  Only particles whose remaining life is under ParticleFollowMove% of the
 *  full span are pulled back -- so the flame's tip lags behind a moving torch
 *  while its base stays put.  The delta is converted out of world units by the
 *  same 25x flame scale ContTorch2() builds its matrix with.
 * ------------------------------------------------------------------------ */
static void Torch2BasePosMoveInfluence(PARTICLE *pPartTop, float *Diff,
                                       float WholeScale, int InitLifeTime,
                                       int ParticleFollowMove)
{
    float LocalDiff[4];
    int   i;

    sceVu0ScaleVector(LocalDiff, Diff, 1.0f / (WholeScale * 25.0f)); /* 425 */

    for (i = 0; i < 200; i++)                                       /* 428 */
    {
        if (pPartTop[i].lifetime != 0 &&                            /* 430 */
            pPartTop[i].lifetime <= (InitLifeTime * ParticleFollowMove) / 100)
        {
            sceVu0SubVector(pPartTop[i].position,
                            pPartTop[i].position, LocalDiff);       /* 434 */
        }
    }                                                               /* 438 */
}

/* --------------------------------------------------------------------------
 *  Push one particle into the ring.                                ROM 445
 *
 *  No liveness check: the oldest entry is overwritten whether or not it had
 *  expired, which is what caps a flame at 200 regardless of spawn rate.
 * ------------------------------------------------------------------------ */
static void Torch2AddParticle(TORCH_CTRL *pTc, float *pos, float *vel,
                              float r, float g, float b, float a, float Scale)
{
    PARTICLE *p;

    p = &pTc->particles[pTc->head];                                 /* 450 */

    p->lifetime    = pTc->blife;                                    /* 452 */
    p->position[0] = pos[0];                                        /* 453 */
    p->position[1] = pos[1];
    p->position[2] = pos[2];
    p->position[3] = 1.0f;
    p->velocity[0] = vel[0];                                        /* 455 */
    p->velocity[1] = vel[1];
    p->velocity[2] = vel[2];
    p->velocity[3] = 0.0f;
    p->color[0]    = r;                                             /* 456 */
    p->color[1]    = g;
    p->color[2]    = b;
    p->color[3]    = a;
    p->Scale       = Scale;                                         /* 457 */
    p->alp_step    = 0.0f;                                          /* 458 */

    pTc->head = (pTc->head + 1) % 200;                              /* 459 */
}

/* --------------------------------------------------------------------------
 *  Age every particle in a flame.                                  ROM 473
 *
 *  Progress runs 0 -> 1 over the particle's life and drives both the alpha and
 *  the scale lerp.  Note it is computed from the *shared* InitLifeTime rather
 *  than from each particle's own BaseLifeTime -- the spark version does the
 *  latter, because spark lifetimes are randomised per particle.
 * ------------------------------------------------------------------------ */
static void Torch2UpdateParticles(PARTICLE *pPartTop, int InitLifeTime,
                                  float StartAlpha, float EndAlpha,
                                  float StartScale, float EndScale)
{
    float Progress;
    int   i;

    if (pPartTop == NULL || InitLifeTime == 0)                      /* 478 */
    {
        return;
    }

    for (i = 0; i < 200; i++)                                       /* 480 */
    {
        Progress = 1.0f - (float)pPartTop[i].lifetime / (float)InitLifeTime; /* 482 */

        if (pPartTop[i].lifetime == 0)                              /* 483 */
        {
            pPartTop[i].color[3] = 0.0f;                            /* 485 */
        }
        else
        {
            sceVu0AddVector(pPartTop[i].position,
                            pPartTop[i].position, pPartTop[i].velocity); /* 488 */
            pPartTop[i].lifetime--;                                 /* 490 */
            pPartTop[i].color[3] =
                (EndAlpha - StartAlpha) * Progress + StartAlpha;     /* 492 */
            pPartTop[i].Scale =
                (EndScale - StartScale) * Progress + StartScale;     /* 493 */
        }
    }
}

/* --------------------------------------------------------------------------
 *  Light a torch.                                                  ROM 499
 *
 *  The handle handed back is the EFFECT_CONT itself, which doubles as the
 *  sound's delete key -- that is how EffectResetTorch2() can stop the loop.
 * ------------------------------------------------------------------------ */
static void *EffectSetTorch2Sub(float *pPosition, int Type, int SeReqFlg)
{
    void *pEffRet;

    pEffRet = SetEffects_TORCH2(2, Type, pPosition, SeReqFlg);     /* 502 */

    if (pEffRet != NULL && SeReqFlg != 0)                           /* 504 */
    {
        EffectSndPlayDeleteKey(0xcc9, 0, 0, 0, (float (*)[3])pPosition, (u_int)(uintptr_t)pEffRet); /* 505 */
        EffectSndFileReadyReq(0xcff);                               /* 506 */
    }

    return pEffRet;                                                 /* 509 */
}

void *EffectSetTorch2(float *pPosition, int Type)                   /* 516 */
{
    return EffectSetTorch2Sub(pPosition, Type, 1);                  /* 517 */
}

void *EffectSetTorch2NoSE(float *pPosition, int Type)               /* 524 */
{
    return EffectSetTorch2Sub(pPosition, Type, 0);                  /* 525 */
}

/* --------------------------------------------------------------------------
 *  Put a torch out.                                                ROM 534
 *
 *  The sound is only stopped if the TORCH_CTRL says one was started, so a
 *  NoSE torch does not release the shared fire sample out from under a
 *  neighbouring one.
 * ------------------------------------------------------------------------ */
void EffectResetTorch2(void *pTorch2)
{
    TORCH_CTRL *pTorchCtrl;
    int         SeReqFlg;

    SeReqFlg = 0;                                                   /* 537 */

    if (pTorch2 == NULL)                                            /* 539 */
    {
        return;
    }

    pTorchCtrl = (TORCH_CTRL *)((EFFECT_CONT *)pTorch2)->pnt[1];    /* 541 */
    if (pTorchCtrl != NULL)
    {
        SeReqFlg = pTorchCtrl->SeReqFlg;                            /* 542 */
    }

    if (SeReqFlg != 0)                                              /* 546 */
    {
        EffectSndStopDeleteKey((u_int)(uintptr_t)pTorch2, 1);       /* 547 */
        EffectSndFileRelease(0xcc9);                                /* 548 */
        EffectSndFileRelease(0xcff);                                /* 549 */
    }

    EFFECT_FREE(((EFFECT_CONT *)pTorch2)->pnt[1]);                  /* 552 */
    ResetEffects(pTorch2);                                          /* 553 */
}

/* Master fade accessors.  Both answer 0 / do nothing for a torch whose work
 * block was never allocated, which is what makes them safe to call on a
 * handle whose EFFECT_MALLOC failed.                          ROM 565 / 588 */
float EffectTorch2GetAlphaRate(void *pTorch2)
{
    TORCH_CTRL *pTc;
    float       AlphaRate;

    AlphaRate = 0.0f;                                               /* 570 */

    if (pTorch2 != NULL)                                            /* 572 */
    {
        pTc = (TORCH_CTRL *)((EFFECT_CONT *)pTorch2)->pnt[1];
        if (pTc != NULL)
        {
            AlphaRate = pTc->AlphaRate;                             /* 573 */
        }
    }

    return AlphaRate;                                               /* 578 */
}

void EffectTorch2SetAlphaRate(void *pTorch2, float AlphaRate)
{
    TORCH_CTRL *pTc;

    if (pTorch2 != NULL)                                            /* 594 */
    {
        pTc = (TORCH_CTRL *)((EFFECT_CONT *)pTorch2)->pnt[1];
        if (pTc != NULL)
        {
            pTc->AlphaRate = AlphaRate;                             /* 595 */
        }
    }
}                                                                   /* 596 */

/* --------------------------------------------------------------------------
 *  Wipe a spark ring.                                              ROM 608
 * ------------------------------------------------------------------------ */
static void TorchSparkPartInit(SPARK_CTRL *pSc, int Type, float BrakeRate,
                               float WholeScale, float AlphaRate)
{
    int i;

    if (pSc == NULL)                                                /* 611 */
    {
        return;
    }

    for (i = 0; i < 50; i++)                                        /* 613 */
    {
        pSc->particles[i].position[0] = 0.0f;
        pSc->particles[i].position[1] = 0.0f;
        pSc->particles[i].position[2] = 0.0f;
        pSc->particles[i].position[3] = 1.0f;
        pSc->particles[i].color[3]    = 0.0f;
        pSc->particles[i].lifetime    = 0;
    }

    pSc->AlphaRate  = AlphaRate;                                    /* 620 */
    pSc->Type       = (short)Type;                                  /* 621 */
    pSc->BrakeRate  = BrakeRate;                                    /* 622 */
    pSc->WholeScale = WholeScale;                                   /* 623 */
    pSc->head       = 0;                                            /* 624 */
    pSc->cnt        = 0;                                            /* 625 */
    pSc->disp       = 0;                                            /* 626 */
    pSc->blife      = 0;                                            /* 627 */
}

/* --------------------------------------------------------------------------
 *  Effect handler for a spark burst (effect id 0x1d).              ROM 633
 *
 *  Unlike a torch, the whole burst is spawned once at allocation time and then
 *  only ages: ContTorchSpark() frees the block and returns NULL the frame the
 *  last spark dies, which is what ends the effect.
 *
 *  ec->fw[0] is the overall scale and ec->fw[1] the alpha rate.
 * ------------------------------------------------------------------------ */
void SetSpark(EFFECT_CONT *ec)
{
    TORCH2_SPARK_PARAMETER *pSparkParam;
    int                     ReqNum;
    int                     i;

    pSparkParam = EffOthTorch2SparkParameterPtrGet(ec->dat.uc8[2]); /* 638 */

    ReqNum = (int)EffectGetRandom((float)pSparkParam->AppearNumMin,
                                  (float)pSparkParam->AppearNumMax); /* 645 */

    if (ec->pnt[1] == NULL)                                         /* 648 */
    {
        ec->pnt[1] = EFFECT_MALLOC(sizeof(SPARK_CTRL));             /* 649 */
        if (ec->pnt[1] == NULL)                                     /* 650 */
        {
            ResetEffects(ec);                                       /* 652 */
        }
        else
        {
            TorchSparkPartInit((SPARK_CTRL *)ec->pnt[1], ec->dat.uc8[2],
                               (float)pSparkParam->BrakeRate / 1000.0f,
                               ec->fw[0], ec->fw[1]);               /* 653 */

            for (i = 0; i < ReqNum; i++)                            /* 654 */
            {
                AddSparkParticle((SPARK_CTRL *)ec->pnt[1], 3.0f, pSparkParam);
            }
        }
    }

    ec->pnt[1] = ContTorchSpark((SPARK_CTRL *)ec->pnt[1], ec->Pos,
                                1300.0f, 3.3f, pSparkParam);        /* 657 */
    if (ec->pnt[1] == NULL)                                         /* 660 */
    {
        ResetEffects(ec);                                           /* 661 */
    }
}                                                                   /* 662 */

/* --------------------------------------------------------------------------
 *  Draw and age one spark burst.                                   ROM 670
 *
 *  Returns the block, or NULL once every spark has expired -- at which point
 *  it has already freed it.  The stop flag freezes the burst without ending
 *  it, so a photo phase does not consume the sparks it paused.
 * ------------------------------------------------------------------------ */
static void *ContTorchSpark(SPARK_CTRL *pSc, float *pos, float size, float sr,
                            TORCH2_SPARK_PARAMETER *pSparkParam)
{
    float        work[4][4];
    float        local_clip[4][4];
    float        local_world[4][4];
    float        local_screen[4][4];
    GRA3DCAMERA *pCam;

    pCam = gra3dGetCamera();                                        /* 673 */

    if (pSc == NULL)                                                /* 676 */
    {
        return NULL;
    }

    sceVu0UnitMatrix(work);                                         /* 683 */
    work[0][0] = work[1][1] = work[2][2] = pSc->WholeScale * 25.0f; /* 684 */
    sceVu0TransMatrix(local_world, work, pos);                      /* 686 */
    sceVu0MulMatrix(local_screen, pCam->matWorldScreen, local_world); /* 688 */
    sceVu0MulMatrix(local_clip, pCam->matWorldClipPolygon, local_world); /* 689 */

    pSc->disp = (short)draw_distortion_particles2(
                    local_screen, local_clip, 50, pSc->particles.data(),
                    (size * sr * pSc->WholeScale) / pCam->fFov,
                    effdat[EffWrkMonochroModeGet() + 0x4e].tex0,
                    0x48);                                          /* 693 */

    if (EffWrkStopFlgGet() == 0)                                    /* 695 */
    {
        if (UpdateSparkParticles(pSc->particles.data(), pSc->BrakeRate,
                                 pSc->AlphaRate, pSparkParam) == 0) /* 697 */
        {
            EFFECT_FREE(pSc);                                       /* 698 */
            return NULL;
        }
    }

    return pSc;                                                     /* 703 */
}                                                                   /* 704 */

/* --------------------------------------------------------------------------
 *  Push one spark into the ring.                                   ROM 710
 *
 *  Sparks start at the origin of the burst -- unlike flame particles there is
 *  no spawn disc -- and get a randomised lifetime, so the burst thins out
 *  rather than ending all at once.  `Scale` is accepted and ignored: the
 *  initial scale comes from the parameter set instead.
 * ------------------------------------------------------------------------ */
static void AddSparkParticle(SPARK_CTRL *pSc, float Scale,
                             TORCH2_SPARK_PARAMETER *pSparkParam)
{
    PARTICLE *pPart;

    (void)Scale;

    pPart = &pSc->particles[pSc->head];                             /* 715 */

    pPart->position[0] = 0.0f;                                      /* 717 */
    pPart->position[1] = 0.0f;
    pPart->position[2] = 0.0f;
    pPart->position[3] = 1.0f;

    /* /100 rather than the flame's /1000: sparks fly ten times as fast. */
    pPart->velocity[0] = (EffectGetRandom((float)pSparkParam->SpeedXMin,
                                          (float)pSparkParam->SpeedXMax)
                          - 1000.0f) / 100.0f;                      /* 718 */
    pPart->velocity[1] = (EffectGetRandom((float)pSparkParam->SpeedYMin,
                                          (float)pSparkParam->SpeedYMax)
                          - 1000.0f) / 100.0f;                      /* 719 */
    pPart->velocity[2] = (EffectGetRandom((float)pSparkParam->SpeedZMin,
                                          (float)pSparkParam->SpeedZMax)
                          - 1000.0f) / 100.0f;                      /* 720 */

    pPart->acceleration[0] = 0.0f;                                  /* 721 */
    pPart->acceleration[1] = (EffectGetRandom((float)pSparkParam->AccelYMin,
                                              (float)pSparkParam->AccelYMax)
                              - 1000.0f) / 1000.0f;                 /* 722 */
    pPart->acceleration[2] = 0.0f;                                  /* 723 */

    pPart->color[0] = (float)pSparkParam->R;                        /* 725 */
    pPart->color[1] = (float)pSparkParam->G;
    pPart->color[2] = (float)pSparkParam->B;
    pPart->color[3] = (float)pSparkParam->StartAlpha * pSc->AlphaRate; /* 726 */
    pPart->alp_step = 0.0f;                                         /* 727 */

    pPart->lifetime     = (int)EffectGetRandom((float)pSparkParam->LifeTimeMin,
                                               (float)pSparkParam->LifeTimeMax); /* 728 */
    pPart->BaseLifeTime = pPart->lifetime;                          /* 729 */

    /* Integer divide, so any StartScale under 100 starts at exactly 0 -- and
     * all nine presets are (23 or 48)/100.  Reproduced as found. */
    pPart->Scale = (float)(pSparkParam->StartScale / 100);

    pSc->head = (pSc->head + 1) % 50;                               /* 730 */
}

/* --------------------------------------------------------------------------
 *  Age every spark; answer how many are still alive.               ROM 739
 *
 *  Progress is per-particle here (BaseLifeTime), because spark lifetimes are
 *  randomised.  Velocity is scaled by BrakeRate every frame and gravity is
 *  added after, so drag applies to the accumulated fall as well.
 * ------------------------------------------------------------------------ */
static int UpdateSparkParticles(PARTICLE *pPtop, float BrakeRate,
                                float AlphaRate,
                                TORCH2_SPARK_PARAMETER *pSparkParam)
{
    float StartAlpha, EndAlpha;
    float StartScale, EndScale;
    float Progress;
    int   LeftNum;
    int   i;

    LeftNum    = 0;                                                 /* 741 */
    StartAlpha = (float)pSparkParam->StartAlpha * AlphaRate;        /* 742 */
    EndAlpha   = (float)pSparkParam->EndAlpha * AlphaRate;          /* 743 */
    StartScale = (float)pSparkParam->StartScale / 100.0f;           /* 745 */
    EndScale   = (float)pSparkParam->EndScale / 100.0f;             /* 747 */

    if (pPtop == NULL)                                              /* 749 */
    {
        return LeftNum;
    }

    for (i = 0; i < 50; i++)                                        /* 753 */
    {
        if (pPtop[i].BaseLifeTime == 0)                             /* 754 */
        {
            Progress = 1.0f;                                        /* 757 */
        }
        else
        {
            Progress = 1.0f - (float)pPtop[i].lifetime
                              / (float)pPtop[i].BaseLifeTime;       /* 760 */
        }

        if (pPtop[i].lifetime == 0)                                 /* 762 */
        {
            pPtop[i].color[3] = 0.0f;                               /* 763 */
        }
        else
        {
            sceVu0AddVector(pPtop[i].position,
                            pPtop[i].position, pPtop[i].velocity);  /* 765 */
            sceVu0ScaleVector(pPtop[i].velocity, pPtop[i].velocity, BrakeRate); /* 766 */
            sceVu0AddVector(pPtop[i].velocity,
                            pPtop[i].velocity, pPtop[i].acceleration); /* 768 */
            LeftNum++;                                              /* 769 */
            pPtop[i].lifetime--;                                    /* 771 */
            pPtop[i].color[3] = (EndAlpha - StartAlpha) * Progress + StartAlpha; /* 773 */
            pPtop[i].Scale    = (EndScale - StartScale) * Progress + StartScale; /* 775 */
        }
    }

    return LeftNum;                                                 /* 776 */
}

/* --------------------------------------------------------------------------
 *  Burst scheduling.                                               ROM 789
 *
 *  The interval only runs down while no burst is in progress, so a burst's own
 *  duration does not eat into the wait for the next one.  An interval of -1
 *  ("never") is never zero and never decremented, so those torches never
 *  burst -- which is how IntervalMax/Min of 0 is expressed.
 * ------------------------------------------------------------------------ */
static void EffOthTorch2BurstReqCtrl(TORCH_CTRL *pTc, float WholeScale,
                                     float AlphaRate)
{
    if (pTc->BurstInterval == 0)                                    /* 790 */
    {
        /* Effect id 0x1d is SetSpark: a burst throws sparks. */
        SetEffects_SPARK(2, pTc->BasePos, pTc->Type,
                         WholeScale, AlphaRate);                /* 792 */
        pTc->BurstCount    = 1;                                     /* 793 */
        pTc->BurstInterval = EffOthTorch2BurstIntervalTimeGet(pTc->Type); /* 794 */

        if (pTc->SeReqFlg != 0)                                     /* 795 */
        {
            EffectSndPlay(0xcff, 0, 0, 0, (float (*)[3])pTc->BasePos); /* 796 */
        }
    }
    else if (pTc->BurstCount == 0 && pTc->BurstInterval > 0)        /* 800 */
    {
        pTc->BurstInterval--;                                       /* 801 */
    }
}

/* Frames until the next burst, or -1 when the preset says never.   ROM 809 */
static int EffOthTorch2BurstIntervalTimeGet(int Type)
{
    TORCH2_BURST_PARAMETER *pBurstParam;
    int                     RetVal;

    pBurstParam = EffOthTorch2BurstParameterPtrGet(Type);           /* 813 */

    if (pBurstParam->IntervalMax == 0 && pBurstParam->IntervalMin == 0) /* 820 */
    {
        return -1;                                                  /* 821 */
    }

    RetVal = (int)EffectGetRandom((float)pBurstParam->IntervalMin,
                                  (float)pBurstParam->IntervalMax); /* 824 */

    return RetVal;                                                  /* 827 */
}

/* --------------------------------------------------------------------------
 *  Sample the burst envelope.                                      ROM 838
 *
 *  Three linear segments: rest -> Range1 over Frame1, Range1 -> Range2 over
 *  Frame2, then back to rest by EndFrame.  Returns 0 once the burst is over
 *  (or has not started), having written the resting values -- which is the
 *  signal ContTorch2() uses to clear BurstCount.
 *
 *  Each segment guards its own divisor against a zero-length span by falling
 *  back to a Progress of 1.0, i.e. jumping straight to the segment's end.
 * ------------------------------------------------------------------------ */
static int Torch2BurstRangeAndStartScaleGet(float *pRange, float *pStartScale,
                                            int Count,
                                            TORCH2_PARAMETER *pParam,
                                            TORCH2_BURST_PARAMETER *pBurstParam)
{
    int   Frame1, Frame2, EndFrame;
    float BaseRange, Range1, Range2;
    float BaseScale, StartScale1, StartScale2;
    float Progress;

    Frame1      = pBurstParam->Frame1;                              /* 840 */
    Frame2      = pBurstParam->Frame2;                              /* 841 */
    EndFrame    = pBurstParam->EndFrame;                            /* 842 */
    BaseRange   = (float)pParam->Range / 100.0f;                    /* 843 */
    Range1      = (float)pBurstParam->Range1 / 100.0f;              /* 844 */
    Range2      = (float)pBurstParam->Range2 / 100.0f;              /* 845 */
    BaseScale   = (float)pParam->StartScale / 100.0f;               /* 846 */
    StartScale1 = (float)pBurstParam->StartScale1 / 100.0f;         /* 847 */
    StartScale2 = (float)pBurstParam->StartScale2 / 100.0f;         /* 851 */

    if (Count < 1 || Count >= EndFrame)                             /* 852 */
    {
        *pRange      = BaseRange;                                   /* 853 */
        *pStartScale = BaseScale;                                   /* 884 */
        return 0;                                                   /* 887 */
    }

    if (Count < Frame1)                                             /* 856 */
    {
        Progress = (Frame1 == 0) ? 1.0f : (float)Count / (float)Frame1; /* 858 */
        *pRange      = (Range1 - BaseRange) * Progress + BaseRange; /* 862 */
        *pStartScale = (StartScale1 - BaseScale) * Progress + BaseScale; /* 863 */
    }
    else if (Count < Frame2)                                        /* 866 */
    {
        Progress = (Frame2 == Frame1)
                       ? 1.0f
                       : (float)(Count - Frame1) / (float)(Frame2 - Frame1); /* 868 */
        *pRange      = (Range2 - Range1) * Progress + Range1;       /* 872 */
        *pStartScale = (StartScale2 - StartScale1) * Progress + StartScale1; /* 873 */
    }
    else
    {
        Progress = (EndFrame == Frame2)
                       ? 1.0f
                       : (float)(Count - Frame2) / (float)(EndFrame - Frame2); /* 875 */
        *pRange      = (BaseRange - Range2) * Progress + Range2;    /* 877 */
        *pStartScale = (BaseScale - StartScale2) * Progress + StartScale2; /* 878 */
    }

    return 1;                                                       /* 880 */
}

/* --------------------------------------------------------------------------
 *  Roll a gust direction.                                          ROM 897
 *
 *  A 0.15-long vector along local +Z, tilted by a pitch and spun to a random
 *  bearing.  The pitch expression is the ROM's own: `(rnd * 10 + 90) - 90`
 *  degrees, i.e. 0..10 degrees written the long way round.
 * ------------------------------------------------------------------------ */
static void Torch2WindVectorMake(float *Wind)
{
    float TmpMat[4][4];
    float TmpVec[4];
    float RotX, RotY;

    memset(TmpVec, 0, sizeof(TmpVec));                              /* 899 */
    TmpVec[3] = 1.0f;

    /* EffectGetRandom(15, 15) -- a zero-width range, so this is a constant
     * 0.15 with the randomiser left in place. */
    TmpVec[2] = EffectGetRandom(15.0f, 15.0f) / 100.0f;

    RotX = (EffectGetRandom(90.0f, 100.0f) - 90.0f) * 0.017453290f;
    RotY = EffectGetRandom(-3.1415925f, 3.1415925f);

    sceVu0UnitMatrix(TmpMat);                                       /* 916 */
    sceVu0RotMatrixX(TmpMat, TmpMat, RotX);                         /* 917 */
    sceVu0RotMatrixY(TmpMat, TmpMat, RotY);                         /* 918 */
    sceVu0ApplyMatrix(Wind, TmpMat, TmpVec);                        /* 919 */
}                                                                   /* 920 */

static void Torch2WindReq(TORCH_CTRL *pTc)                          /* 926 */
{
    Torch2WindVectorMake(pTc->WindMax);                             /* 927 */
    pTc->WindCount = 1;                                             /* 928 */
}

/* Genuinely a bare `return -1;` in this build -- 8 bytes.  With the interval
 * never reaching 0, Torch2WindReqCtrl() never fires and no torch ever gets a
 * gust; the whole wind path below is dead code the ROM still carries. ROM 935 */
static int EffOthTorch2WindIntervalTimeGet(void)
{
    return -1;                                                      /* 956 */
}

static void Torch2WindReqCtrl(TORCH_CTRL *pTc)                      /* 963 */
{
    if (pTc->WindInterval == 0)                                     /* 966 */
    {
        Torch2WindReq(pTc);                                         /* 967 */
        pTc->WindInterval = EffOthTorch2WindIntervalTimeGet();
    }
    else if (pTc->WindCount == 0 && pTc->WindInterval > 0)          /* 970 */
    {
        pTc->WindInterval--;                                        /* 971 */
    }
}

/* --------------------------------------------------------------------------
 *  Sample the gust envelope.                                       ROM 979
 *
 *  Full strength for the first five frames, then a linear fade out to frame
 *  35; frame 36 and later answer 0, which clears WindCount.
 * ------------------------------------------------------------------------ */
static int Torch2WindVectorGet(float *NowWind, float *WindMax, int Count)
{
    NowWind[0] = 0.0f;                                              /* 980 */
    NowWind[1] = 0.0f;
    NowWind[2] = 0.0f;
    NowWind[3] = 1.0f;

    if (Count < 1)                                                  /* 990 */
    {
        return 0;
    }

    if (Count < 6)                                                  /* 996 */
    {
        g3dxVu0CopyVector(NowWind, WindMax);                        /* 998 */
        return 1;                                                   /* 1001 */
    }

    if (Count > 35)                                                 /* 1006 */
    {
        return 0;                                                   /* 1011 */
    }

    sceVu0ScaleVector(NowWind, WindMax,
                      (float)(30 - (Count - 6)) / 30.0f);           /* 1017 */
    return 1;                                                       /* 1033 */
}

/* --------------------------------------------------------------------------
 *  The opening light shaft.                                       ROM 1051
 * ------------------------------------------------------------------------ */
void EffectTorch2BigFreaInit(void)
{
    Torch2BigFreaCtrl.ExecFlg = 0;                                  /* 1052 */
    Torch2BigFreaCtrl.Count   = 0;                                  /* 1053 */
    Torch2BigFreaCtrl.Cycle   = 0;                                  /* 1054 */
}

/* --------------------------------------------------------------------------
 *  Re-post the light shaft once per frame.                        ROM 1061
 *
 *  Alpha is a triangular ramp: 18 up to 35 over Cycle frames, then back down
 *  over the next Cycle.  With Cycle == 0 it stays at 35, and Count is never
 *  reset (0 >= 0), so the pulse simply stops.
 * ------------------------------------------------------------------------ */
void EffectTorch2BigFreaMain(void)
{
    float Position[4];
    float Offset[4];
    int   Alpha;
    int   AlphaMax;

    if (Torch2BigFreaCtrl.ExecFlg == 0)                             /* 1073 */
    {
        return;
    }

    Offset[0] = 0.0f;                                               /* 1096 */
    Offset[1] = 0.0f;                                               /* 1097 */
    Offset[2] = 0.0f;                                               /* 1098 */
    Offset[3] = 0.0f;                                               /* 1099 */

    sceVu0AddVector(Position, Torch2BigFreaCtrl.Position, Offset);  /* 1102 */

    AlphaMax = 35;
    Alpha    = AlphaMax;

    if (Torch2BigFreaCtrl.Cycle != 0)                               /* 1104 */
    {
        if (Torch2BigFreaCtrl.Count < Torch2BigFreaCtrl.Cycle)      /* 1105 */
        {
            Alpha = (AlphaMax - 18) * Torch2BigFreaCtrl.Count
                    / Torch2BigFreaCtrl.Cycle + 18;                 /* 1106 */
        }
        else
        {
            Alpha = AlphaMax
                    - (AlphaMax - 18)
                      * (Torch2BigFreaCtrl.Count - Torch2BigFreaCtrl.Cycle)
                      / Torch2BigFreaCtrl.Cycle;                    /* 1109 */
        }
    }

    /* 69.5 / 77.1 are the shaft's two scales; they reach SetTorch2BigFrea()
     * through ec->fw[0] and ec->fw[1]. */
    SetEffects_TORCH_FREA(1, Position, 0x8f, 0x32, 0x26,
                          69.5f, 77.1f, Alpha);                 /* 1116 */

    Torch2BigFreaCtrl.Count++;                                      /* 1121 */
    if (Torch2BigFreaCtrl.Count >= Torch2BigFreaCtrl.Cycle * 2)     /* 1122 */
    {
        Torch2BigFreaCtrl.Count = 0;                                /* 1123 */
    }
}

void EffectTorch2BigFreaReq(float *Position)                        /* 1132 */
{
    g3dxVu0CopyVector(Torch2BigFreaCtrl.Position, Position);

    Torch2BigFreaCtrl.Cycle   = 0x37;                               /* 1137 */
    Torch2BigFreaCtrl.ExecFlg = 1;                                  /* 1138 */
    Torch2BigFreaCtrl.Count   = 0;                                  /* 1142 */
}

void EffectTorch2BigFreaCut(void)                                   /* 1150 */
{
    Torch2BigFreaCtrl.ExecFlg = 0;                                  /* 1151 */
}

/* Effect handler for the light shaft (effect id 0x1e).  One-shot: dat.uc8[1]
 * bit 0 means "last frame", so the effect releases itself.        ROM 1161 */
void SetTorch2BigFrea(EFFECT_CONT *ec)
{
    float Position[4];
    float ScaleX;

    g3dxVu0CopyVector(Position, (float *)ec->pnt[0]);

    ScaleX = ec->fw[0];

    Torch2BigFreaDraw(Position, ScaleX, ec->fw[1],
                      ec->r, ec->g, ec->b, ec->a);                  /* 1169 */

    if ((ec->dat.uc8[1] & 1) != 0)                                  /* 1171 */
    {
        ResetEffects(ec);                                           /* 1172 */
    }
}

/* --------------------------------------------------------------------------
 *  Draw the light shaft.                                          ROM 1180
 *
 *  A billboard turned to face the camera *position* rather than built from the
 *  camera matrix, so it keeps its own roll -- Get2PosRot() gives the pitch and
 *  heading from the camera to its target and the quad is built from those.
 * ------------------------------------------------------------------------ */
static void Torch2BigFreaDraw(const float *Position, float ScaleX, float ScaleY,
                              int R, int G, int B, int Alpha)
{
    float        matWorldLocal[4][4];
    DRAW_ENV     DrawEnv;
    GRA3DCAMERA *pCam;
    float        RotX, RotY;

    DrawEnv.alpha = 0x48;                                           /* 1182 */
    DrawEnv.tex1  = 0x161;
    DrawEnv.clamp = 0;
    DrawEnv.test  = 0x5000d;
    DrawEnv.zbuf  = 0x10a000118ULL;
    DrawEnv.prim  = 0x302a400000008004ULL;

    pCam = gra3dGetCamera();                                        /* 1190 */
    Get2PosRot(gra3dcamGetPosition(), pCam->vTarget, &RotX, &RotY); /* 1191 */

    sceVu0UnitMatrix(matWorldLocal);                                /* 1194 */
    sceVu0RotMatrixX(matWorldLocal, matWorldLocal, RotX);           /* 1196 */
    sceVu0RotMatrixY(matWorldLocal, matWorldLocal, RotY);           /* 1197 */
    sceVu0TransMatrix(matWorldLocal, matWorldLocal, (float *)Position); /* 1198 */

    Set3DPosTexure(matWorldLocal, &DrawEnv, 0x42,
                   ScaleX * 100.0f, ScaleY * 100.0f,
                   (u_char)R, (u_char)G, (u_char)B, (u_char)Alpha); /* 1199 */
}                                                                   /* 1203 */
