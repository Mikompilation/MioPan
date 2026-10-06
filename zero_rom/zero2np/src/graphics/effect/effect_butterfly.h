/* ==========================================================================
 *  graphics/effect/effect_butterfly.h
 *
 *  The crimson butterflies.
 *
 *  Two kinds, distinguished by BUTTERFLY_DISP::MoveType.  Type 0 is the
 *  ambient butterfly: it wanders on a randomised velocity, lives 450 frames
 *  and fades itself in and out at the ends.  Type 1 is the *guide*: it homes
 *  on a target, is addressable by Id, and is what the event macros steer.
 *
 *  Both leave a trail of BUTTERFLY_PARTICLE motes, which are a separate list
 *  with their own update and draw.
 *
 *  COMPLETE - all 19 ZERO2.MAP .text symbols plus 15 statics, the five
 *  BUTTERFLY_TARGET_PARAMETER presets and their .rodata selector table.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), 0x0013fdf8.
 * ======================================================================== */

#ifndef _GRAPHICS_EFFECT_EFFECT_BUTTERFLY_H
#define _GRAPHICS_EFFECT_EFFECT_BUTTERFLY_H

#include "eetypes.h"
#include "../../common/SingleLinkList.h"
#include "../../graphics/motion/motion.h"        /* ANI_CTRL */

/* --------------------------------------------------------------------------
 *  Guide-butterfly preset.                                    types.txt 0x2c
 *
 *  Speed is hundredths of a unit per frame; TurnMax, BureLeftRight and
 *  BureUpDown are degrees (the last two are the width of the random wobble
 *  added to the heading whenever the butterfly is *not* locked on).  The three
 *  LockOnInterval fields are the odds of locking on, chosen by distance
 *  against NearDistance / FarDistance.
 * ------------------------------------------------------------------------ */
typedef struct                      /* 0x2c */
{
    /* 0x00 */ int Speed;
    /* 0x04 */ int TurnMax;
    /* 0x08 */ int BureLeftRight;
    /* 0x0c */ int BureUpDown;
    /* 0x10 */ int InTime;
    /* 0x14 */ int OutTime;
    /* 0x18 */ int LockOnIntervalNear;
    /* 0x1c */ int LockOnIntervalMiddle;
    /* 0x20 */ int LockOnIntervalFar;
    /* 0x24 */ int NearDistance;
    /* 0x28 */ int FarDistance;
} BUTTERFLY_TARGET_PARAMETER;

/* --------------------------------------------------------------------------
 *  One live butterfly.                                       types.txt 0x2c0
 *
 *  Nearly all of it is the embedded ANI_CTRL -- the butterfly is a real
 *  animated model, not a sprite -- so the list cells are large and come out of
 *  the effect heap through SingleLinkList.
 * ------------------------------------------------------------------------ */
typedef struct _BUTTERFLY_DISP      /* 0x2c0 */
{
    /* 0x000 */ float    Position[4];
    /* 0x010 */ float    OldPosition[4];  /* last frame's, for the facing */
    /* 0x020 */ float    Target[4];       /* MoveType 1 only */
    /* 0x030 */ float    Rot[4];
    /* 0x040 */ float    Velocity[4];
    /* 0x050 */ ANI_CTRL AniCtrl;
    /* 0x290 */ int      LifeTime;
    /* 0x294 */ int      MoveInterval;    /* frames until the next steer */
    /* 0x298 */ float    AlphaRate;
    /* 0x29c */ int      MoveType;        /* 0 = ambient, 1 = guide */
    /* 0x2a0 */ int      InTime;          /* fade-in length, 0 = done */
    /* 0x2a4 */ int      OutTime;         /* fade-out length, 0 = not fading */
    /* 0x2a8 */ int      Count;
    /* 0x2ac */ int      Type;            /* preset index, MoveType 1 only */
    /* 0x2b0 */ int      Id;
} BUTTERFLY_DISP;

/* One trail mote.                                            types.txt 0x40 */
typedef struct _BUTTERFLY_PARTICLE  /* 0x40 */
{
    /* 0x00 */ float Position[4];
    /* 0x10 */ float Velocity[4];
    /* 0x20 */ float Acceleration[4];
    /* 0x30 */ int   LifeTime;
    /* 0x34 */ float AlphaRate;
} BUTTERFLY_PARTICLE;

typedef struct                      /* 0x10 */
{
    /* 0x0 */ SINGLE_LINK_LIST ButterflyList;
} BUTTERFLY_DISP_CTRL;

typedef struct                      /* 0x10 */
{
    /* 0x0 */ SINGLE_LINK_LIST ParticleList;
} BUTTERFLY_PARTICLE_CTRL;

#ifdef __cplusplus
extern "C" {
#endif

void EffectButterflyInit(void);
void EffectButterflyMain(void);
void EffectButterflyAllCut(void);

/* Model 5 / animation 8.  The model has to be resident before any butterfly
 * can be requested; EffectIsReadyButterflyModel() is the gate. */
void EffectSetupButterflyModel(void);
void EffectReleaseButterflyModel(void);
int  EffectIsReadyButterflyModel(void);
void EffectInitAniCtrlButterflyOne(ANI_CTRL *pAniCtrl);

/* Ambient butterfly: no id, no target, dies of old age after 450 frames. */
void EffectButterflyReq(float *Position);

/* Guide butterfly.  Addressable by `Id` afterwards -- but note the lookup
 * matches on MoveType 1 *and* Id, so an ambient butterfly is never found. */
void EffectButterflyReqTarget(int Id, int Type, float *Position, float *Target);
void EffectButterflyFadeOut(int Id);
void EffectButterflyAllFadeOut(void);
void EffectButterflyChangeTarget(int Id, float *Target);
void EffectButterflyChangeType(int Id, int Type);
void EffectButterflyAllChangeTarget(float *Target);
int  EffectButterflyNumGet(void);

/* Clamps an out-of-range Type to 0 rather than asserting. */
BUTTERFLY_TARGET_PARAMETER *EffectButterflyTargetParameterPtrGetPublic(int Type);

/* The trail motes.  A separate list with its own per-frame pass, so cutting
 * the butterflies does not take the trail with them. */
void EffectButterflyParticleInit(void);
void EffectButterflyParticleMain(void);
void EffectButterflyParticleAllCut(void);

#ifdef __cplusplus
}
#endif

#endif /* _GRAPHICS_EFFECT_EFFECT_BUTTERFLY_H */
