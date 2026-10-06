/* ==========================================================================
 *  graphics/effect/effect_torch.h
 *
 *  The torch flame, its sparks and the opening light shaft.
 *
 *  A torch is a ring buffer of 200 PARTICLEs plus a "frea" (flare) sprite at
 *  its head, driven by one of nine authored parameter sets.  On top of the
 *  steady flame sit two timed disturbances: a *burst* (the flame briefly
 *  widens and lengthens) and a *wind* (a random direction added to every new
 *  particle's velocity for ~36 frames).  Sparks are a second, smaller ring of
 *  50 particles with their own parameter set, gravity and drag.
 *
 *  COMPLETE - all 17 ZERO2.MAP .text symbols plus 22 statics, the three
 *  9-entry parameter tables and their .rodata selector tables.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), 0x00169180.
 * ======================================================================== */

#ifndef _GRAPHICS_EFFECT_EFFECT_TORCH_H
#define _GRAPHICS_EFFECT_EFFECT_TORCH_H

#include "eetypes.h"
#include "effect.h"                             /* EFFECT_CONT / PARTICLE */
#include "../graph3d/ctl/fixed_array.h"

/* --------------------------------------------------------------------------
 *  One flame preset.                                          types.txt 0x74
 *
 *  Every field is an int even where it means a fraction; the divisor is at the
 *  use site.  Scale / Range / StartScale / EndScale are hundredths; the three
 *  Speed pairs are thousandths biased by 1000, so 1031 means +0.031 and 974
 *  means -0.026.  R/G/B and the two Alphas are plain 0..255-ish.
 * ------------------------------------------------------------------------ */
typedef struct                      /* 0x74 */
{
    /* 0x00 */ int LifeTime;
    /* 0x04 */ int AppearNumMax;
    /* 0x08 */ int AppearNumMin;
    /* 0x0c */ int Range;
    /* 0x10 */ int SpeedXMax;
    /* 0x14 */ int SpeedXMin;
    /* 0x18 */ int SpeedYMax;
    /* 0x1c */ int SpeedYMin;
    /* 0x20 */ int SpeedZMax;
    /* 0x24 */ int SpeedZMin;
    /* 0x28 */ int Scale;
    /* 0x2c */ int StartScale;
    /* 0x30 */ int EndScale;
    /* 0x34 */ int StartAlpha;
    /* 0x38 */ int EndAlpha;
    /* 0x3c */ int R;
    /* 0x40 */ int G;
    /* 0x44 */ int B;
    /* 0x48 */ int AlphaBlendA;
    /* 0x4c */ int AlphaBlendB;
    /* 0x50 */ int AlphaBlendC;
    /* 0x54 */ int AlphaBlendD;
    /* 0x58 */ int AlphaBlendFIX;
    /* 0x5c */ int FreaSize;
    /* 0x60 */ int FreaR;
    /* 0x64 */ int FreaG;
    /* 0x68 */ int FreaB;
    /* 0x6c */ int FreaAlpha;
    /* 0x70 */ int ParticleFollowMove;  /* % of life that trails a moved base */
} TORCH2_PARAMETER;

/* --------------------------------------------------------------------------
 *  Burst envelope.                                            types.txt 0x24
 *
 *  Three linear segments over Frame1 / Frame2 / EndFrame, interpolating the
 *  spawn Range and StartScale away from the flame's resting values and back.
 *  IntervalMax/Min of 0 means "never burst".
 * ------------------------------------------------------------------------ */
typedef struct                      /* 0x24 */
{
    /* 0x00 */ int Range1;
    /* 0x04 */ int Range2;
    /* 0x08 */ int StartScale1;
    /* 0x0c */ int StartScale2;
    /* 0x10 */ int Frame1;
    /* 0x14 */ int Frame2;
    /* 0x18 */ int EndFrame;
    /* 0x1c */ int IntervalMax;
    /* 0x20 */ int IntervalMin;
} TORCH2_BURST_PARAMETER;

/* --------------------------------------------------------------------------
 *  Spark preset.                                              types.txt 0x50
 *
 *  Same conventions as TORCH2_PARAMETER, except the Speed and AccelY pairs are
 *  biased by 1000 and divided by 100 (sparks move ten times faster than flame
 *  particles) and BrakeRate is a per-frame velocity multiplier in thousandths.
 * ------------------------------------------------------------------------ */
typedef struct                      /* 0x50 */
{
    /* 0x00 */ int LifeTimeMax;
    /* 0x04 */ int LifeTimeMin;
    /* 0x08 */ int AppearNumMax;
    /* 0x0c */ int AppearNumMin;
    /* 0x10 */ int SpeedXMax;
    /* 0x14 */ int SpeedXMin;
    /* 0x18 */ int SpeedYMax;
    /* 0x1c */ int SpeedYMin;
    /* 0x20 */ int SpeedZMax;
    /* 0x24 */ int SpeedZMin;
    /* 0x28 */ int AccelYMax;
    /* 0x2c */ int AccelYMin;
    /* 0x30 */ int BrakeRate;
    /* 0x34 */ int StartScale;
    /* 0x38 */ int EndScale;
    /* 0x3c */ int StartAlpha;
    /* 0x40 */ int EndAlpha;
    /* 0x44 */ int R;
    /* 0x48 */ int G;
    /* 0x4c */ int B;
} TORCH2_SPARK_PARAMETER;

/* --------------------------------------------------------------------------
 *  One live flame.                                          types.txt 0x3ed0
 *
 *  16 KB, so it comes out of the effect heap rather than being embedded in the
 *  EFFECT_CONT.  `disp` is the number of particles the previous frame actually
 *  drew, fed back as the flare's brightness -- a flame whose particles are all
 *  off screen dims its own halo.
 * ------------------------------------------------------------------------ */
typedef struct                      /* 0x3ed0 */
{
    /* 0x0000 */ fixed_array<PARTICLE, 200> particles;
    /* 0x3e80 */ float BasePos[4];      /* last frame's world position */
    /* 0x3e90 */ float WindMax[4];      /* the gust currently blowing */
    /* 0x3ea0 */ int   head;            /* ring write cursor */
    /* 0x3ea4 */ int   BurstCount;      /* 0 = not bursting, else frame + 1 */
    /* 0x3ea8 */ int   BurstInterval;   /* frames until the next burst */
    /* 0x3eac */ int   WindCount;
    /* 0x3eb0 */ int   WindInterval;
    /* 0x3eb4 */ float AlphaRate;       /* master fade, 1.0 = full */
    /* 0x3eb8 */ short max;
    /* 0x3eba */ short disp;            /* particles drawn last frame */
    /* 0x3ebc */ short blife;           /* particle lifetime in frames */
    /* 0x3ebe */ short Type;
    /* 0x3ec0 */ short SeReqFlg;
} TORCH_CTRL;

/* One live spark burst.                                     types.txt 0xfc0 */
typedef struct                      /* 0xfc0 */
{
    /* 0x000 */ fixed_array<PARTICLE, 50> particles;
    /* 0xfa0 */ int   head;
    /* 0xfa4 */ int   cnt;
    /* 0xfa8 */ short max;
    /* 0xfaa */ short disp;
    /* 0xfac */ short blife;
    /* 0xfae */ short Type;
    /* 0xfb0 */ float BrakeRate;
    /* 0xfb4 */ float WholeScale;
    /* 0xfb8 */ float AlphaRate;
} SPARK_CTRL;

/* The opening light shaft.  A singleton: one position, a triangular alpha ramp
 * of period 2 * Cycle, and a flag.                          types.txt 0x20 */
typedef struct                      /* 0x20 */
{
    /* 0x00 */ float Position[4];
    /* 0x10 */ short Count;
    /* 0x12 */ short Cycle;
    /* 0x14 */ short ExecFlg;
} TORCH2_BIGFREA_CTRL;

#ifdef __cplusplus
extern "C" {
#endif

/* Preset lookup.  All three clamp an out-of-range Type to 0 rather than
 * asserting, so a bad type quietly draws torch 0. */
TORCH2_PARAMETER       *EffOthTorch2ParameterPtrGet(int Type);
TORCH2_BURST_PARAMETER *EffOthTorch2BurstParameterPtrGet(int Type);
TORCH2_SPARK_PARAMETER *EffOthTorch2SparkParameterPtrGet(int Type);

/* EFFECT_CONT handlers.  SetTorch2() is effect id 0x1c, SetSpark() 0x1d and
 * SetTorch2BigFrea() 0x1e; each allocates its work block on first call. */
void SetTorch2(EFFECT_CONT *ec);
void SetSpark(EFFECT_CONT *ec);
void SetTorch2BigFrea(EFFECT_CONT *ec);

void TorchPartInit(TORCH_CTRL *pTc, int LifeTime, float *BasePos,
                   int Type, int SeReqFlg);
void ContTorch2(TORCH_CTRL *pTc, float *pos, int Depth,
                TORCH2_PARAMETER *pParam, TORCH2_BURST_PARAMETER *pBurstParam);

/* Light a torch at `pPosition`, which is kept by address -- the effect reads
 * it every frame, so a torch follows whatever it was anchored to.  The NoSE
 * variant skips the ignition cue and the looping fire sound. */
/* PORT NOTE: the ROM declares these `sceVu0FVECTOR *` (the mangled names end
 * PA3_fi).  Taken as a plain float * here, which is what every call site in
 * the port already holds and is identical on the host. */
void *EffectSetTorch2(float *pPosition, int Type);
void *EffectSetTorch2NoSE(float *pPosition, int Type);
void  EffectResetTorch2(void *pTorch2);

float EffectTorch2GetAlphaRate(void *pTorch2);
void  EffectTorch2SetAlphaRate(void *pTorch2, float AlphaRate);

/* The opening light shaft ("eff_op_hikari").  A singleton -- the cut takes no
 * id.  Main() re-posts the effect every frame it is up. */
void EffectTorch2BigFreaInit(void);
void EffectTorch2BigFreaMain(void);
void EffectTorch2BigFreaReq(float *Position);
void EffectTorch2BigFreaCut(void);

#ifdef __cplusplus
}
#endif

#endif /* _GRAPHICS_EFFECT_EFFECT_TORCH_H */
