/* ==========================================================================
 *  ingame/ingame_effect.h
 *
 *  The in-game effect front door: the layer that turns a game event -- a
 *  camera hit, a footstep, a room effect being drawn -- into a request on
 *  graphics/effect.  Every function here belongs to ingame_effect.o.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_INGAME_EFFECT_H
#define _INGAME_INGAME_EFFECT_H

#include "../graphics/effect/effect.h"      /* SCREEN_EFFECT_PARAMETER */
#include "../graphics/motion/motion.h"      /* ANI_CTRL */

/* ==========================================================================
 *  The parts-deform job table
 * ======================================================================== */

/* Which of the nine camera sub-functions armed a deform.  The value is stored
 * in SUBFUNC_PDEFORM_CTRL::Type and picked apart by IgEffectSubFuncPDeformMain
 * once the job's start frame arrives.  Names are the ROM's own, from the
 * SUBFUNC_PDEFORM_TYPE enum in the debug info.
 *
 * PDEFORM_TYPE_FREQ is declared but unreachable: the only writer of Type is
 * IgEffectSubFuncPDeformReqSet(), and its nine callers pass 0..8. */
enum SUBFUNC_PDEFORM_TYPE
{
    PDEFORM_TYPE_ZERO     = 0,
    PDEFORM_TYPE_SLOW     = 1,
    PDEFORM_TYPE_KOKU     = 2,
    PDEFORM_TYPE_PARALYZE = 3,
    PDEFORM_TYPE_VIEW     = 4,
    PDEFORM_TYPE_METSU    = 5,
    PDEFORM_TYPE_REN      = 6,
    PDEFORM_TYPE_TSUI     = 7,
    PDEFORM_TYPE_FUU      = 8,
    PDEFORM_TYPE_FREQ     = 9
};

/* NOT_USE the slot is free, USE it is counting down to StartFrame, WAIT the
 * deform has been handed to effect_obj.o and the slot is holding its handle
 * until EffectExecCheck() says the effect has finished. */
enum SUBFUNC_PDEFORM_STATUS
{
    SUBFUNC_PDEFORM_STATUS_NOT_USE = 0,
    SUBFUNC_PDEFORM_STATUS_USE     = 1,
    SUBFUNC_PDEFORM_STATUS_WAIT    = 2
};

typedef struct                          /* 0x18 */
{
    /* 0x00 */ int   EneWrkNo;          /* which ghost the deform is on   */
    /* 0x04 */ int   Count;             /* frames since the job was armed */
    /* 0x08 */ int   Type;              /* SUBFUNC_PDEFORM_TYPE           */
    /* 0x0c */ int   StartFrame;        /* Count the deform fires at      */
    /* 0x10 */ void *pEffRet;           /* the EFFECT_CONT it produced    */
    /* 0x14 */ int   Status;            /* SUBFUNC_PDEFORM_STATUS         */
} SUBFUNC_PDEFORM_CTRL;

/* The one-shot delay in front of the zero-shot particle burst.  Count is -1
 * when idle -- not 0, which is a live countdown's first frame. */
typedef struct                          /* 0x8 */
{
    /* 0x0 */ int EneWrkNo;
    /* 0x4 */ int Count;
} ZERO_PARTICLE_CTRL;

/* ==========================================================================
 *  Lifecycle
 * ======================================================================== */

/* Clears the ten parts-deform slots and parks the zero-particle countdown.
 * ingame.c calls it out of the room-load path. */
void IgEffectInit(void);

/* One frame: recompute the particle suction point, step the parts-deform
 * table and the zero-particle delay, draw the lens flare if it is armed, and
 * hand the player's position to the candle-flame flicker. */
void IgEffectMain(void);

/* ==========================================================================
 *  The spirit-particle suction point
 * ======================================================================== */

/* Where a photographed ghost's spirit particles fly to, in world space.  The
 * two are the same offset from the player under two different transforms --
 * the finder one is where the viewfinder sits, the other where the camera is
 * held at the hip.  IgEffectMain() recomputes whichever the player's mode
 * calls for and posts it to effect_ene.o. */
float (*IgEffectParticleEndPosFinderGet(void))[4];
float (*IgEffectParticleEndPosNoFinderGet(void))[4];
void   IgEffectParticleEndPosCalcFinder(float *EndPos);
void   IgEffectParticleEndPosCalcNoFinder(float *EndPos);

/* Non-zero once the burst has finished being drained into the tray: the tray
 * is not animating and no particle work is live.  enemy_act.c's EJobB15 waits
 * on it. */
int IgEffectIsEndParticleSuck(void);

/* ==========================================================================
 *  Damage requests
 * ======================================================================== */

/* 48 * sin(damage * PI / 211 / 2), i.e. the particle count a hit is worth --
 * saturating rather than linear, and zero-crossing again past 422 damage,
 * which no shot reaches. */
int CalcParticleNumFromDamage(int damage);

/* DEAD CODE in this build -- a jal scan over the loadable segments finds no
 * caller for any of the three.  EneHitEffectMain() and n_equip_tray.o do the
 * same jobs by their own routes. */
void IgEffectEneDmgReq(int num);
void IgEffectEneParticleDmgReq(int num, int damage);
void IgEffectEneDmgReqZero(int num, int damage);
void IgEffectZeroCamFreqReq(void);

/* ==========================================================================
 *  The nine camera sub-function hits
 * ======================================================================== */

/* One per sub-function.  effect_ene.c's EneHitEffectReq() raises exactly one
 * of these on the frame a sub-function hit lands, and each is the whole visual
 * for that function: a pair of flash presets, a parts-deform queued onto the
 * ghost, and (for the five that do not flash twice) a cue.
 *
 * The three-argument form takes the shutter-chance grade: 0 ordinary, 1
 * shutter chance, 2 fatal frame.  Grade only picks which pair of flash presets
 * plays; the deform is the same either way.
 *
 * Parameter names are the ROM's own. */
void IgEffectSubFuncZeroReq(int num, int type);
void IgEffectSubFuncSlowReq(int num);
void IgEffectSubFuncKokuReq(int num, int type);
void IgEffectSubFuncParalyzeReq(int num);
void IgEffectSubFuncViewReq(int num);
void IgEffectSubFuncMetsuReq(int num, int type);
void IgEffectSubFuncRenReq(int num, int type);
void IgEffectSubFuncTsuiReq(int num);
void IgEffectSubFuncFuuReq(int num);

/* Drops every deform job standing on one ghost, live or pending.  player.c
 * clears it as the next shot lands so two sub-functions cannot stack. */
void IgEffectSubFuncPDeformClear(int EneWrkNo);

/* ==========================================================================
 *  Screen effects
 * ======================================================================== */

/* Applies one SCREEN_EFFECT_PARAMETER set as this frame's whole-screen look. */
void IgEffectStoryMainScreenEffectReq(SCREEN_EFFECT_PARAMETER *pScreenPara);

/* The active story contrast filter -- its effect id, colour and alpha.  All
 * three read one field of effect.o's current SCREEN_EFFECT_PARAMETER. */
int IgEffectStoryMainContrastTypeGet(void);
int IgEffectStoryMainContrastColorGet(void);
int IgEffectStoryMainContrastAlphaGet(void);

/* Arms/disarms the lens flare IgEffectMain() draws off the player's own
 * spotlight.  scene.c, ev_macro.c and plyr_mdl.c all drive it. */
void IgEffectRenzFlareDispFlgSet(int Flg);

/* ==========================================================================
 *  Dust, butterflies and the burst
 * ======================================================================== */

/* A puff of dust at a bone of an animating model, Offset applied in the bone's
 * local space.  anicode.c's EFCT opcode raises the player form. */
void IgEffectDustReq(ANI_CTRL *pAniCtrl, int BoneId, float *Offset);
void IgEffectPlayerDustReq(ANI_CTRL *pAniCtrl, int BoneId);

/* The butterfly swarm ("eff_butterfly_0").  MapObjSetEffect() raises it with
 * no "already running" guard, unlike every other effect there -- the guard is
 * inside IgEffectButterflyReq() itself, which is why the request is a 1-in-400
 * dice roll against a cap of ten live butterflies. */
void IgEffectButterflyReq(float *Position);
void IgEffectButterflyAllCut(void);

/* Fires a 48-particle burst at Position with no end position and no suction,
 * so it disperses instead of flying home.  player.c's CEneTracer::Work()
 * raises one when it lets go of a traced ghost. */
void IgEffectEffectEndParticleReq(float *Position, int EffectType);

/* ==========================================================================
 *  MapPut draw callbacks for the three model-backed room effects
 * ======================================================================== */

/* Each is installed as the put-object's draw function by MapObjReg.c, so it
 * runs with the object's matrix already current; the *Sub form is the body,
 * split out so the ROM could call it with an explicit matrix. */
void IgEffectLightComeInModelDraw(void);
void IgEffectLightComeInModelDrawSub(void *pSgdTop, float (*CoordMat)[4]);
void IgEffectTourouFreaModelDraw(void);
void IgEffectTourouFreaModelDrawSub(void *pHandle, float (*CoordMat)[4]);
void IgEffectTourouBaseModelDraw(void);
void IgEffectTourouBaseModelDrawSub(void *pSgdTop, float (*CoordMat)[4]);

#endif /* _INGAME_INGAME_EFFECT_H */
