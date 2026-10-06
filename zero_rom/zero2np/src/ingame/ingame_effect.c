// FILE: /home/zero_rom/zero2np/src/ingame/ingame_effect.c
//
// ingame_effect.o -- the in-game side of the effect system: the layer that
// turns a game event into a request on graphics/effect.  Five jobs live here.
//
//   - The spirit-particle suction point.  Two world-space positions, one for
//     the finder and one for the hip-held camera, recomputed every frame from
//     the player's transform and posted to effect_ene.o.  It is what makes a
//     photographed ghost's particles fly to the camera instead of dispersing.
//   - The nine camera sub-function hits.  Each is a pair of flash presets, a
//     parts-deform queued onto the ghost, and a cue.
//   - The parts-deform job table: ten slots, each a (ghost, type, start frame)
//     request that fires a few frames after the hit and then holds the effect
//     handle until effect.o says the deform has run out.
//   - The zero-shot delay, the lens flare, dust puffs and the butterfly swarm.
//   - The three model-backed room effects -- the shaft of light, and the
//     lantern's base and flame -- whose MapPut draw callbacks are here rather
//     than in effect_obj.o because they need the map's draw path.
//
// A NOTE ON THE /* NNN */ ANNOTATIONS.  Function opening lines are the $LM
// that precedes each PROC record in symbols.txt; statement lines come from the
// same table rather than Ghidra's `; Line` comments.  Anything whose only work
// goes through an inline leaves no $LM of its own and is interpolated into the
// measured gap -- fixed_array.h 124/125 (operator[]), g3dxVu0.h 134/135 (the
// quadword copy) and effect.h 217 (EffectGetRandom) all do that here.
//
// A NOTE ON MISSING LOCALS.  Four pointer locals in this object have no stab:
// the matrix pointer in each of the three *ModelDraw entry points and the
// camera direction in IgEffectTourouFreaModelDrawSub.  Each is provably a
// source variable -- its producing call sits on its own measured line, several
// lines above the statement that consumes it, and GCC 2.96 does not move calls
// -- so the names for those four are the port's, not the ROM's.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "ingame_effect.h"

#include <string.h>                             /* memset */

#include "../common/variable.h"                 /* plyr_wrk */
#include "../sdk/libvu0.h"

#include "../graphics/draw_env.h"               /* SetAlphaRegister / DRAW_ENV */
#include "../graphics/effect/effect.h"          /* SetEffects_* / EffectGetRandom */
#include "../graphics/effect/effect_butterfly.h"
#include "../graphics/effect/effect_ene.h"      /* EneDmg* / ENE_DMG_PARTICLE_REQ */
#include "../graphics/effect/effect_obj.h"      /* CallPartsDeform6 / TOUROU_* */
#include "../graphics/effect/effect_oth.h"      /* EffectCandleFlameDraw */
#include "../graphics/effect/effect_sub.h"      /* Vector2Rot / Set3DPosTexure */
#include "../graphics/graph3d/g3dMath.h"        /* g3dSinf / VECTOR3 */
#include "../graphics/graph3d/g3dxVu0.h"        /* g3dxVu0CopyVector */
#include "../graphics/graph3d/gra3d.h"          /* fog / light */
#include "../graphics/graph3d/gra3dSGD.h"       /* gra3dSetVertexColorPreset */
#include "../graphics/motion/mdlwork.h"         /* ManmdlSetAlpha */

#include "enemy/enemy.h"                        /* ene_wrk */
#include "map/MapDraw.h"                        /* MapDrawObj[NoShadow] */
#include "map/MapPut.h"                         /* MapPutGet* */
#include "photo/finder.h"                       /* FinderBankPlay */
#include "photo/freq_camera.h"                  /* ReqFreqCamera */
#include "photo/m_plyr_camera.h"                /* m_plyr_camera */

#include "../system/pad/vib_manage.h"           /* SetVibrate */

/* The effect id EffectExecCheck() is asked about below: SetEffects_PDEFORM's,
 * i.e. "is this still one of my parts-deforms". */
#define IG_EFFECT_ID_PDEFORM    0x18

/* Player mode 6 -- the viewfinder is up.  Spelled as the raw constant here
 * because the ROM's debug info carries no enum for it, the same way
 * effect_ene.c does. */
#define IG_PLMODE_FINDER        6

/* ---- file statics -------------------------------------------------------- */

/* bss 4b4040 */ static fixed_array<SUBFUNC_PDEFORM_CTRL, 10> SubFuncPDeformCtrl;
/* bss 4b4130 */ static sceVu0FVECTOR ParticleEndPosFiner;    /* ROM's spelling */
/* bss 4b4140 */ static sceVu0FVECTOR ParticleEndPosNoFiner;
/* sbss 3f4d28 */ static ZERO_PARTICLE_CTRL ZeroParticleCtrl;
/* sbss 3f4d30 */ static int RenzFlareDispFlg;

/* ---- file-local forward declarations -------------------------------------
 * The ROM's own: every one of these is defined below its first caller, which
 * C++ does not allow without a declaration in front. */
static void  IgEffectSubFuncPDeformReqSet(int EneWrkNo, int Type, int StartFrame);
static void  IgEffectSubFuncPDeformMain(void);
static void  IgEffectZeroParticleReqSet(int num);
static void  IgEffectZeroParticleMain(void);
static void  IgEffectZeroParticleReq(int num);

/* --------------------------------------------------------------------------
 *  Room-load init.  Both the Status and the Count of every deform slot are
 *  cleared, and the zero-particle countdown is parked at -1 rather than 0 --
 *  0 is a live countdown's first frame.
 * ------------------------------------------------------------------------ */
void IgEffectInit(void)                                         /* ROM 144 */
{
    int i;

    for (i = 0; i < 10; i++)                                    /* 147, 150 */
    {
        SubFuncPDeformCtrl[i].Status = SUBFUNC_PDEFORM_STATUS_NOT_USE; /* 148 */
        SubFuncPDeformCtrl[i].Count  = 0;                       /* 149 */
    }

    ZeroParticleCtrl.Count = -1;                                /* 151 */
    RenzFlareDispFlg       = 0;                                 /* 152 */
}

/* --------------------------------------------------------------------------
 *  One frame.  The suction point is recomputed from whichever transform the
 *  player's mode calls for and handed to effect_ene.o unconditionally, so a
 *  burst launched in the finder keeps flying to the right place after the
 *  finder comes down.
 * ------------------------------------------------------------------------ */
void IgEffectMain(void)                                         /* ROM 159 */
{
    if (plyr_wrk.cmn_wrk.mode != IG_PLMODE_FINDER)              /* 162 */
    {
        IgEffectParticleEndPosCalcNoFinder(ParticleEndPosNoFiner);      /* 163 */
        EneDmgParticleEndPosSet(IgEffectParticleEndPosNoFinderGet());   /* 164 */
    }
    else
    {
        IgEffectParticleEndPosCalcFinder(ParticleEndPosFiner);          /* 167 */
        EneDmgParticleEndPosSet(IgEffectParticleEndPosFinderGet());     /* 168 */
    }

    IgEffectSubFuncPDeformMain();                               /* 171 */
    IgEffectZeroParticleMain();                                 /* 172 */

    if (RenzFlareDispFlg != 0)                                  /* 175 */
    {
        /* Type 4 off the player's own spotlight -- the same (position,
         * rotation) pair scene_effect.c feeds it from the room's authored
         * light.  SetRenzFlare() reads the second one as trot. */
        SetEffects_RENZFLARE(1, 4, plyr_wrk.spot_pos,           /* 176 */
                             plyr_wrk.cmn_wrk.mbox.rot);
    }

    EffectCandleFlameYuramekiCtrl(plyr_wrk.cmn_wrk.mbox.pos,    /* 180 */
                                  plyr_wrk.cmn_wrk.mbox.bpos);
}

float (*IgEffectParticleEndPosFinderGet(void))[4]               /* ROM 189 */
{
    return &ParticleEndPosFiner;                                /* 190 */
}

float (*IgEffectParticleEndPosNoFinderGet(void))[4]             /* ROM 198 */
{
    return &ParticleEndPosNoFiner;                              /* 199 */
}

/* --------------------------------------------------------------------------
 *  Where the particles are pulled to with the viewfinder up: a fixed offset
 *  in the player's own space, rotated by pitch then yaw and translated onto
 *  the player.  Only X and Y are rolled in -- the roll axis is not, so the
 *  point does not swing when the camera tilts.
 *
 *  The four components are four separate assignments here and a .rodata blob
 *  copy in the NoFinder twin below, which is what tells a run of stores from a
 *  declaration initialiser: only the second one is one statement.
 * ------------------------------------------------------------------------ */
void IgEffectParticleEndPosCalcFinder(float *EndPos)            /* ROM 208 */
{
    float wlm[4][4];
    float ppp2[4];

    ppp2[0] = -220.299988f;                                     /* 220 */
    ppp2[1] = -534.6f;                                          /* 221 */
    ppp2[2] = 319.8f;                                           /* 222 */
    ppp2[3] = 1.0f;                                             /* 223 */

    sceVu0UnitMatrix(wlm);                                      /* 225 */
    sceVu0RotMatrixX(wlm, wlm, plyr_wrk.cmn_wrk.mbox.rot[0]);   /* 226 */
    sceVu0RotMatrixY(wlm, wlm, plyr_wrk.cmn_wrk.mbox.rot[1]);   /* 227 */
    sceVu0TransMatrix(wlm, wlm, plyr_wrk.cmn_wrk.mbox.pos);     /* 228 */
    sceVu0ApplyMatrix(EndPos, wlm, ppp2);                       /* 229 */
}

/* The same thing with the camera at the hip: much closer in and level with the
 * player rather than up at the viewfinder. */
void IgEffectParticleEndPosCalcNoFinder(float *EndPos)          /* ROM 238 */
{
    float wlm[4][4];
    float ppp[4] = { -22.0f, -690.0f, 80.0f, 1.0f };            /* 240 */

    sceVu0UnitMatrix(wlm);                                      /* 242 */
    sceVu0RotMatrixX(wlm, wlm, plyr_wrk.cmn_wrk.mbox.rot[0]);   /* 243 */
    sceVu0RotMatrixY(wlm, wlm, plyr_wrk.cmn_wrk.mbox.rot[1]);   /* 244 */
    sceVu0TransMatrix(wlm, wlm, plyr_wrk.cmn_wrk.mbox.pos);     /* 245 */
    sceVu0ApplyMatrix(EndPos, wlm, ppp);                        /* 246 */
}

/* --------------------------------------------------------------------------
 *  How many spirit particles a hit is worth.  A quarter sine over 0..422
 *  damage, so it saturates at 48 around 211 and would fall away again past
 *  that -- no shot in the game reaches it.
 * ------------------------------------------------------------------------ */
int CalcParticleNumFromDamage(int damage)                       /* ROM 252 */
{
    return (int)(g3dSinf((float)damage * 3.1415925f / 211.0f * 0.5f) /* 253, 254 */
                 * 48.0f);
}

/* --------------------------------------------------------------------------
 *  DEAD CODE.  A jal scan over the loadable segments finds no caller for this
 *  or the two below it: EneHitEffectMain() raises the damage flash itself and
 *  player.c fills its own ENE_DMG_PARTICLE_REQ.  Kept because ZERO2.MAP
 *  exports them.
 * ------------------------------------------------------------------------ */
void IgEffectEneDmgReq(int num)                                 /* ROM 261 */
{
    EneDmgScreenEffectReq(ene_wrk[num].mpos.p0,                 /* 262 */
                          ene_wrk[num].st.dmg_type,
                          (int)ene_wrk[num].st.sta,
                          ene_wrk[num].dist_p_e[0]);
}

/* DEAD CODE.  EffectType (0x20) is deliberately not written -- the ROM leaves
 * it holding whatever was on the stack.  Harmless: EneDmgParticleEffectReq()
 * copies StartPos, pEndPos, ParticleNum, DistPE and SuctionFlg and never looks
 * at EffectType, which only EffectEndParticleEffectReq() reads. */
void IgEffectEneParticleDmgReq(int num, int damage)             /* ROM 270 */
{
    int                   particle_num;
    ENE_DMG_PARTICLE_REQ  EneDmgParticleReq;

    particle_num = CalcParticleNumFromDamage(damage);           /* 274 */

    g3dxVu0CopyVector(EneDmgParticleReq.StartPos, ene_wrk[num].mpos.p0); /* 277 */
    EneDmgParticleReq.pEndPos     = IgEffectParticleEndPosFinderGet();   /* 278 */
    EneDmgParticleReq.ParticleNum = particle_num;               /* 279 */
    EneDmgParticleReq.DistPE      = ene_wrk[num].dist_p_e[0];   /* 280 */
    EneDmgParticleReq.SuctionFlg  = 1;                          /* 281 */

    EneDmgParticleEffectReq(&EneDmgParticleReq);                /* 282 */

    m_plyr_camera.eq_tray.SetRemainParticle(particle_num);      /* 285 */
}

/* DEAD CODE.  The zero-shot's whole reaction: the damage flash, a camera
 * frequency wobble, the delayed particle burst and two pad rumbles -- a short
 * sharp one and a long weak one. */
void IgEffectEneDmgReqZero(int num, int damage)                 /* ROM 292 */
{
    /* `damage` is dead on arrival -- a1 is overwritten by the first inlined
     * bounds check two instructions in and never read as the parameter.  Kept
     * because ZERO2.MAP types the export with it. */
    (void)damage;

    EneDmgScreenEffectReq(ene_wrk[num].mpos.p0,                 /* 292 */
                          ene_wrk[num].st.dmg_type,
                          (int)ene_wrk[num].st.sta,
                          ene_wrk[num].dist_p_e[0]);

    ReqFreqCamera();                                            /* 295 */
    IgEffectZeroParticleReqSet(num);                            /* 298 */

    SetVibrate(0, 30, 1);                                       /* 301 */
    SetVibrate(1, 40, 255);                                     /* 302 */
}

/* ==========================================================================
 *  The nine camera sub-function hits
 *
 *  Every one has the same shape: pick a pair of flash presets, play both, then
 *  queue the parts-deform.  The four graded functions choose their pair off
 *  `type` -- 0 ordinary, 1 shutter chance, 2 fatal frame -- and the five
 *  ungraded ones have a single pair and follow with a cue instead, because
 *  their deform helpers below play a different one.
 *
 *  The third argument to IgEffectSubFuncPDeformReqSet() is the delay in frames
 *  before the deform fires, and it is what separates them: Ren is immediate,
 *  Zero and Koku land almost at once, View waits 46 frames.
 * ======================================================================== */

void IgEffectSubFuncZeroReq(int num, int type)                  /* ROM 312 */
{
    int EffectTypeA;
    int EffectTypeB;

    if (type == 0)                                              /* 319 */
    {
        EffectTypeA = 6;                                        /* 320 */
        EffectTypeB = 7;                                        /* 321 */
    }
    else if (type == 1)                                         /* 324 */
    {
        EffectTypeA = 8;                                        /* 325 */
        EffectTypeB = 9;                                        /* 326 */
    }
    else                                                        /* 328 */
    {
        EffectTypeA = 10;                                       /* 329 */
        EffectTypeB = 11;                                       /* 330 */
    }

    EneDmgLargeHitReq(EffectTypeA);                             /* 333 */
    EneDmgLargeHitReq(EffectTypeB);                             /* 334 */

    IgEffectSubFuncPDeformReqSet(num, PDEFORM_TYPE_ZERO, 3);    /* 338 */
}

void IgEffectSubFuncSlowReq(int num)                            /* ROM 353 */
{
    EneDmgLargeHitReq(4);                                       /* 358 */
    EneDmgLargeHitReq(5);                                       /* 359 */

    IgEffectSubFuncPDeformReqSet(num, PDEFORM_TYPE_SLOW, 32);   /* 363 */

    FinderBankPlay(0x11, 1, 1, 0, nullptr, 0x3200, 0x1000);     /* 366 */
}

void IgEffectSubFuncKokuReq(int num, int type)                  /* ROM 376 */
{
    int EffectTypeA;
    int EffectTypeB;

    if (type == 0)                                              /* 383 */
    {
        EffectTypeA = 12;                                       /* 384 */
        EffectTypeB = 13;                                       /* 385 */
    }
    else if (type == 1)                                         /* 388 */
    {
        EffectTypeA = 14;                                       /* 389 */
        EffectTypeB = 15;                                       /* 390 */
    }
    else                                                        /* 392 */
    {
        EffectTypeA = 16;                                       /* 393 */
        EffectTypeB = 17;                                       /* 394 */
    }

    EneDmgLargeHitReq(EffectTypeA);                             /* 397 */
    EneDmgLargeHitReq(EffectTypeB);                             /* 398 */

    IgEffectSubFuncPDeformReqSet(num, PDEFORM_TYPE_KOKU, 5);    /* 402 */
}

void IgEffectSubFuncParalyzeReq(int num)                        /* ROM 414 */
{
    EneDmgLargeHitReq(18);                                      /* 418 */
    EneDmgLargeHitReq(19);                                      /* 419 */

    IgEffectSubFuncPDeformReqSet(num, PDEFORM_TYPE_PARALYZE, 22); /* 423 */

    FinderBankPlay(0x11, 1, 1, 0, nullptr, 0x3200, 0x1000);     /* 426 */
}

void IgEffectSubFuncViewReq(int num)                            /* ROM 436 */
{
    EneDmgLargeHitReq(20);                                      /* 440 */
    EneDmgLargeHitReq(21);                                      /* 441 */

    IgEffectSubFuncPDeformReqSet(num, PDEFORM_TYPE_VIEW, 46);   /* 445 */

    FinderBankPlay(0x11, 1, 1, 0, nullptr, 0x3200, 0x1000);     /* 448 */
}

void IgEffectSubFuncMetsuReq(int num, int type)                 /* ROM 459 */
{
    int EffectTypeA;
    int EffectTypeB;

    if (type == 0)                                              /* 466 */
    {
        EffectTypeA = 22;                                       /* 467 */
        EffectTypeB = 23;                                       /* 468 */
    }
    else if (type == 1)                                         /* 471 */
    {
        EffectTypeA = 24;                                       /* 472 */
        EffectTypeB = 25;                                       /* 473 */
    }
    else                                                        /* 475 */
    {
        EffectTypeA = 26;                                       /* 476 */
        EffectTypeB = 27;                                       /* 477 */
    }

    EneDmgLargeHitReq(EffectTypeA);                             /* 480 */
    EneDmgLargeHitReq(EffectTypeB);                             /* 481 */

    IgEffectSubFuncPDeformReqSet(num, PDEFORM_TYPE_METSU, 5);   /* 485 */
}

void IgEffectSubFuncRenReq(int num, int type)                   /* ROM 498 */
{
    int EffectTypeA;
    int EffectTypeB;

    if (type == 0)                                              /* 505 */
    {
        EffectTypeA = 28;                                       /* 506 */
        EffectTypeB = 29;                                       /* 507 */
    }
    else if (type == 1)                                         /* 510 */
    {
        EffectTypeA = 30;                                       /* 511 */
        EffectTypeB = 31;                                       /* 512 */
    }
    else                                                        /* 514 */
    {
        EffectTypeA = 32;                                       /* 515 */
        EffectTypeB = 33;                                       /* 516 */
    }

    EneDmgLargeHitReq(EffectTypeA);                             /* 519 */
    EneDmgLargeHitReq(EffectTypeB);                             /* 520 */

    IgEffectSubFuncPDeformReqSet(num, PDEFORM_TYPE_REN, 0);     /* 524 */
}

void IgEffectSubFuncTsuiReq(int num)                            /* ROM 536 */
{
    EneDmgLargeHitReq(34);                                      /* 540 */
    EneDmgLargeHitReq(35);                                      /* 541 */

    IgEffectSubFuncPDeformReqSet(num, PDEFORM_TYPE_TSUI, 25);   /* 545 */

    FinderBankPlay(0x11, 1, 1, 0, nullptr, 0x3200, 0x1000);     /* 548 */
}

void IgEffectSubFuncFuuReq(int num)                             /* ROM 558 */
{
    EneDmgLargeHitReq(36);                                      /* 562 */
    EneDmgLargeHitReq(37);                                      /* 563 */

    IgEffectSubFuncPDeformReqSet(num, PDEFORM_TYPE_FUU, 25);    /* 567 */

    FinderBankPlay(0x11, 1, 1, 0, nullptr, 0x3200, 0x1000);     /* 570 */
}

/* --------------------------------------------------------------------------
 *  Claim the first free deform slot.  WrkNo stays -1 if all ten are busy and
 *  the request is silently dropped -- no assert, no warning.
 * ------------------------------------------------------------------------ */
static void IgEffectSubFuncPDeformReqSet(int EneWrkNo, int Type,
                                         int StartFrame)        /* ROM 582 */
{
    int i;
    int WrkNo;

    WrkNo = -1;                                                 /* 584 */

    for (i = 0; i < 10; i++)                                    /* 586, 591 */
    {
        if (SubFuncPDeformCtrl[i].Status == SUBFUNC_PDEFORM_STATUS_NOT_USE) /* 587 */
        {
            WrkNo = i;                                          /* 588 */
            break;                                              /* 589 */
        }
    }

    if (WrkNo != -1)                                            /* 593 */
    {
        SubFuncPDeformCtrl[WrkNo].EneWrkNo   = EneWrkNo;        /* 594 */
        SubFuncPDeformCtrl[WrkNo].Count      = 0;               /* 595 */
        SubFuncPDeformCtrl[WrkNo].Type       = Type;            /* 596 */
        SubFuncPDeformCtrl[WrkNo].StartFrame = StartFrame;      /* 597 */
        SubFuncPDeformCtrl[WrkNo].pEffRet    = nullptr;         /* 598 */
        SubFuncPDeformCtrl[WrkNo].Status     = SUBFUNC_PDEFORM_STATUS_USE; /* 599 */
    }
}

/* ==========================================================================
 *  The nine deform helpers
 *
 *  One per sub-function, all the same two statements plus a call: park this
 *  function's drive speed and rate in a pair of file statics, hand their
 *  addresses to CallPartsDeform6(), then play the cue.
 *
 *  The statics have to be static -- effect_obj.o keeps the two pointers for
 *  the life of the deform and reads them every frame, so a stack pair would
 *  dangle.  All eighteen are 1.0f in .sdata and every one is overwritten
 *  before its first use, so the initialiser never shows.
 *
 *  CallPartsDeform6's arguments are (type, scale, pos, in, keep, out, alp,
 *  pSpd, pRate, r, g, b): `in`/`keep`/`out` are the flow envelope in frames
 *  and r/g/b tint the deform grid.
 * ======================================================================== */

static void *IgEffectZeroPDeformReq(int EneWrkNo)               /* ROM 607 */
{
    /* sdata 3f15a8 */ static float spd  = 1.0f;
    /* sdata 3f15ac */ static float rate = 1.0f;
    void *pRet;

    spd  = 0.309999973f;                                        /* 625 */
    rate = 0.25f;                                               /* 626 */

    pRet = CallPartsDeform6(0x0f, 1.0f, &ene_wrk[EneWrkNo],     /* 628 */
                            9, 18, 59, 50, &spd, &rate,
                            0x6d, 0x71, 0x7a);

    FinderBankPlay(0x14, 1, 1, 0, nullptr, 0x3200, 0x1000);     /* 635 */

    return pRet;                                                /* 639 */
}

static void *IgEffectSlowPDeformReq(int EneWrkNo)               /* ROM 646 */
{
    /* sdata 3f15b0 */ static float spd  = 1.0f;
    /* sdata 3f15b4 */ static float rate = 1.0f;
    void *pRet;

    spd  = 0.04f;                                               /* 664 */
    rate = 0.539999962f;                                        /* 665 */

    pRet = CallPartsDeform6(0x0d, 1.0f, &ene_wrk[EneWrkNo],     /* 667 */
                            7, 235, 69, 12, &spd, &rate,
                            0xc5, 0xad, 0xbe);

    FinderBankPlay(0x12, 1, 1, 0, nullptr, 0x3200, 0x1000);     /* 674 */

    return pRet;                                                /* 676 */
}

static void *IgEffectKokuPDeformReq(int EneWrkNo)               /* ROM 683 */
{
    /* sdata 3f15b8 */ static float spd  = 1.0f;
    /* sdata 3f15bc */ static float rate = 1.0f;
    void *pRet;

    spd  = 2.12999988f;                                         /* 701 */
    rate = 0.71f;                                               /* 702 */

    pRet = CallPartsDeform6(0x10, 1.0f, &ene_wrk[EneWrkNo],     /* 704 */
                            0, 0, 33, 27, &spd, &rate,
                            0xec, 0xe6, 0xb0);

    FinderBankPlay(0x14, 1, 1, 0, nullptr, 0x3200, 0x1000);     /* 711 */

    return pRet;                                                /* 714 */
}

static void *IgEffectParalyzePDeformReq(int EneWrkNo)           /* ROM 721 */
{
    /* sdata 3f15c0 */ static float spd  = 1.0f;
    /* sdata 3f15c4 */ static float rate = 1.0f;
    void *pRet;

    spd  = 0.08f;                                               /* 739 */
    rate = 0.539999962f;                                        /* 740 */

    pRet = CallPartsDeform6(0x0b, 1.0f, &ene_wrk[EneWrkNo],     /* 742 */
                            46, 156, 43, 24, &spd, &rate,
                            0xbc, 0xbf, 0xb8);

    FinderBankPlay(0x12, 1, 1, 0, nullptr, 0x3200, 0x1000);     /* 749 */

    return pRet;                                                /* 752 */
}

static void *IgEffectViewPDeformReq(int EneWrkNo)               /* ROM 759 */
{
    /* sdata 3f15c8 */ static float spd  = 1.0f;
    /* sdata 3f15cc */ static float rate = 1.0f;
    void *pRet;

    spd  = 0.149999991f;                                        /* 777 */
    rate = 0.229999989f;                                        /* 778 */

    pRet = CallPartsDeform6(0x0b, 1.0f, &ene_wrk[EneWrkNo],     /* 780 */
                            7, 94, 46, 15, &spd, &rate,
                            0xff, 0xed, 0xda);

    FinderBankPlay(0x12, 1, 1, 0, nullptr, 0x3200, 0x1000);     /* 787 */

    return pRet;                                                /* 790 */
}

static void *IgEffectMetsuPDeformReq(int EneWrkNo)              /* ROM 797 */
{
    /* sdata 3f15d0 */ static float spd  = 1.0f;
    /* sdata 3f15d4 */ static float rate = 1.0f;
    void *pRet;

    spd  = 2.12999988f;                                         /* 815 */
    rate = 0.71f;                                               /* 816 */

    pRet = CallPartsDeform6(0x10, 1.0f, &ene_wrk[EneWrkNo],     /* 818 */
                            0, 50, 33, 58, &spd, &rate,
                            0x9c, 0x92, 0x92);

    FinderBankPlay(0x14, 1, 1, 0, nullptr, 0x3200, 0x1000);     /* 825 */

    return pRet;                                                /* 828 */
}

static void *IgEffectRenPDeformReq(int EneWrkNo)                /* ROM 835 */
{
    /* sdata 3f15d8 */ static float spd  = 1.0f;
    /* sdata 3f15dc */ static float rate = 1.0f;
    void *pRet;

    spd  = 0.849999964f;                                        /* 853 */
    rate = 0.0699999928f;                                       /* 854 */

    pRet = CallPartsDeform6(0x10, 1.0f, &ene_wrk[EneWrkNo],     /* 856 */
                            0, 0, 13, 38, &spd, &rate,
                            0xdb, 0xeb, 0xcd);

    FinderBankPlay(0x14, 1, 1, 0, nullptr, 0x3200, 0x1000);     /* 863 */

    return pRet;                                                /* 866 */
}

static void *IgEffectTsuiPDeformReq(int EneWrkNo)               /* ROM 873 */
{
    /* sdata 3f15e0 */ static float spd  = 1.0f;
    /* sdata 3f15e4 */ static float rate = 1.0f;
    void *pRet;

    spd  = 0.149999991f;                                        /* 891 */
    rate = 0.229999989f;                                        /* 892 */

    pRet = CallPartsDeform6(0x0b, 1.0f, &ene_wrk[EneWrkNo],     /* 894 */
                            7, 169, 46, 21, &spd, &rate,
                            0xff, 0xf7, 0xda);

    FinderBankPlay(0x12, 1, 1, 0, nullptr, 0x3200, 0x1000);     /* 901 */

    return pRet;                                                /* 904 */
}

static void *IgEffectFuuPDeformReq(int EneWrkNo)                /* ROM 911 */
{
    /* sdata 3f15e8 */ static float spd  = 1.0f;
    /* sdata 3f15ec */ static float rate = 1.0f;
    void *pRet;

    spd  = 0.149999991f;                                        /* 929 */
    rate = 0.229999989f;                                        /* 930 */

    pRet = CallPartsDeform6(0x0d, 1.0f, &ene_wrk[EneWrkNo],     /* 932 */
                            7, 349, 97, 26, &spd, &rate,
                            0xd5, 0xf7, 0xec);

    FinderBankPlay(0x12, 1, 1, 0, nullptr, 0x3200, 0x1000);     /* 939 */

    return pRet;                                                /* 942 */
}

/* --------------------------------------------------------------------------
 *  Drop every deform standing on one ghost, pending or live.  ResetEffects()
 *  is called on the handle whether or not there is one -- for a slot still in
 *  the USE state pEffRet is NULL, which ResetEffects() takes as a no-op.
 * ------------------------------------------------------------------------ */
void IgEffectSubFuncPDeformClear(int EneWrkNo)                  /* ROM 949 */
{
    int i;

    for (i = 0; i < 10; i++)                                    /* 952, 962 */
    {
        if (SubFuncPDeformCtrl[i].Status != SUBFUNC_PDEFORM_STATUS_NOT_USE &&
            SubFuncPDeformCtrl[i].EneWrkNo == EneWrkNo)         /* 955 */
        {
            ResetEffects(SubFuncPDeformCtrl[i].pEffRet);        /* 958 */
            SubFuncPDeformCtrl[i].pEffRet = nullptr;            /* 959 */
            SubFuncPDeformCtrl[i].Status  = SUBFUNC_PDEFORM_STATUS_NOT_USE; /* 960 */
        }
    }
}

/* --------------------------------------------------------------------------
 *  The deform table's per-frame pass, in two loops over the same ten slots.
 *
 *  The first retires finished jobs: a WAIT slot whose effect is no longer one
 *  of effect.o's live parts-deforms drops its handle and frees the slot.  The
 *  second counts USE slots up to their start frame and then fires them.
 *
 *  Firing clears any deform already on that ghost first, so the newest
 *  sub-function always wins.  PDEFORM_TYPE_FREQ is the odd one out: it raises
 *  a camera wobble instead of a deform and leaves the slot in USE with Count
 *  already at StartFrame, which would fire it again every frame -- unreachable
 *  in this build, since nothing ever asks for type 9.
 * ------------------------------------------------------------------------ */
static void IgEffectSubFuncPDeformMain(void)                    /* ROM 969 */
{
    SUBFUNC_PDEFORM_CTRL *pPDeformCtrl;
    int                   i;

    for (i = 0; i < 10; i++)                                    /* 973, 987 */
    {
        pPDeformCtrl = &SubFuncPDeformCtrl[i];                  /* 974 */

        if (pPDeformCtrl->Status == SUBFUNC_PDEFORM_STATUS_WAIT) /* 976 */
        {
            if (EffectExecCheck(pPDeformCtrl->pEffRet, IG_EFFECT_ID_PDEFORM) == 0) /* 978 */
            {
                pPDeformCtrl->pEffRet = nullptr;                /* 980 */
            }

            if (pPDeformCtrl->pEffRet == nullptr)               /* 983 */
            {
                pPDeformCtrl->Status = SUBFUNC_PDEFORM_STATUS_NOT_USE; /* 984 */
            }
        }
    }

    for (i = 0; i < 10; i++)                                    /* 989, 1056 */
    {
        pPDeformCtrl = &SubFuncPDeformCtrl[i];                  /* 990 */

        if (pPDeformCtrl->Status == SUBFUNC_PDEFORM_STATUS_USE)  /* 992 */
        {
            if (pPDeformCtrl->Count == pPDeformCtrl->StartFrame) /* 993 */
            {
                if (pPDeformCtrl->Type == PDEFORM_TYPE_ZERO)     /* 994 */
                {
                    IgEffectSubFuncPDeformClear(pPDeformCtrl->EneWrkNo);         /* 995 */
                    pPDeformCtrl->pEffRet = IgEffectZeroPDeformReq(pPDeformCtrl->EneWrkNo); /* 996 */
                    pPDeformCtrl->Status  = SUBFUNC_PDEFORM_STATUS_WAIT;         /* 997 */
                }
                else if (pPDeformCtrl->Type == PDEFORM_TYPE_SLOW)                /* 1000 */
                {
                    IgEffectSubFuncPDeformClear(pPDeformCtrl->EneWrkNo);         /* 1001 */
                    pPDeformCtrl->pEffRet = IgEffectSlowPDeformReq(pPDeformCtrl->EneWrkNo); /* 1002 */
                    pPDeformCtrl->Status  = SUBFUNC_PDEFORM_STATUS_WAIT;         /* 1003 */
                }
                else if (pPDeformCtrl->Type == PDEFORM_TYPE_KOKU)                /* 1006 */
                {
                    IgEffectSubFuncPDeformClear(pPDeformCtrl->EneWrkNo);         /* 1007 */
                    pPDeformCtrl->pEffRet = IgEffectKokuPDeformReq(pPDeformCtrl->EneWrkNo); /* 1008 */
                    pPDeformCtrl->Status  = SUBFUNC_PDEFORM_STATUS_WAIT;         /* 1009 */
                }
                else if (pPDeformCtrl->Type == PDEFORM_TYPE_PARALYZE)            /* 1012 */
                {
                    IgEffectSubFuncPDeformClear(pPDeformCtrl->EneWrkNo);         /* 1013 */
                    pPDeformCtrl->pEffRet = IgEffectParalyzePDeformReq(pPDeformCtrl->EneWrkNo); /* 1014 */
                    pPDeformCtrl->Status  = SUBFUNC_PDEFORM_STATUS_WAIT;         /* 1015 */
                }
                else if (pPDeformCtrl->Type == PDEFORM_TYPE_VIEW)                /* 1018 */
                {
                    IgEffectSubFuncPDeformClear(pPDeformCtrl->EneWrkNo);         /* 1019 */
                    pPDeformCtrl->pEffRet = IgEffectViewPDeformReq(pPDeformCtrl->EneWrkNo); /* 1020 */
                    pPDeformCtrl->Status  = SUBFUNC_PDEFORM_STATUS_WAIT;         /* 1021 */
                }
                else if (pPDeformCtrl->Type == PDEFORM_TYPE_METSU)               /* 1024 */
                {
                    IgEffectSubFuncPDeformClear(pPDeformCtrl->EneWrkNo);         /* 1025 */
                    pPDeformCtrl->pEffRet = IgEffectMetsuPDeformReq(pPDeformCtrl->EneWrkNo); /* 1026 */
                    pPDeformCtrl->Status  = SUBFUNC_PDEFORM_STATUS_WAIT;         /* 1027 */
                }
                else if (pPDeformCtrl->Type == PDEFORM_TYPE_REN)                 /* 1030 */
                {
                    IgEffectSubFuncPDeformClear(pPDeformCtrl->EneWrkNo);         /* 1031 */
                    pPDeformCtrl->pEffRet = IgEffectRenPDeformReq(pPDeformCtrl->EneWrkNo); /* 1032 */
                    pPDeformCtrl->Status  = SUBFUNC_PDEFORM_STATUS_WAIT;         /* 1033 */
                }
                else if (pPDeformCtrl->Type == PDEFORM_TYPE_TSUI)                /* 1036 */
                {
                    IgEffectSubFuncPDeformClear(pPDeformCtrl->EneWrkNo);         /* 1037 */
                    pPDeformCtrl->pEffRet = IgEffectTsuiPDeformReq(pPDeformCtrl->EneWrkNo); /* 1038 */
                    pPDeformCtrl->Status  = SUBFUNC_PDEFORM_STATUS_WAIT;         /* 1039 */
                }
                else if (pPDeformCtrl->Type == PDEFORM_TYPE_FUU)                 /* 1042 */
                {
                    IgEffectSubFuncPDeformClear(pPDeformCtrl->EneWrkNo);         /* 1043 */
                    pPDeformCtrl->pEffRet = IgEffectFuuPDeformReq(pPDeformCtrl->EneWrkNo); /* 1044 */
                    pPDeformCtrl->Status  = SUBFUNC_PDEFORM_STATUS_WAIT;         /* 1045 */
                }
                else if (pPDeformCtrl->Type == PDEFORM_TYPE_FREQ)                /* 1048 */
                {
                    ReqFreqCamera();                                             /* 1049 */
                }
            }
            else
            {
                pPDeformCtrl->Count++;                                           /* 1053 */
            }
        }
    }
}

/* --------------------------------------------------------------------------
 *  The zero-shot's particle burst, delayed eight frames so it lands with the
 *  flash rather than with the shutter.
 * ------------------------------------------------------------------------ */
static void IgEffectZeroParticleReqSet(int num)                 /* ROM 1063 */
{
    ZeroParticleCtrl.EneWrkNo = num;                            /* 1064 */
    ZeroParticleCtrl.Count    = 0;                              /* 1065 */
}

static void IgEffectZeroParticleMain(void)                      /* ROM 1072 */
{
    if (ZeroParticleCtrl.Count != -1)                           /* 1088 */
    {
        if (ZeroParticleCtrl.Count == 8)                        /* 1089 */
        {
            IgEffectZeroParticleReq(ZeroParticleCtrl.EneWrkNo); /* 1091 */
            ZeroParticleCtrl.Count = -1;                        /* 1093 */
        }
        else
        {
            ZeroParticleCtrl.Count++;                           /* 1096 */
        }
    }
}

/* A full 48-particle burst off the ghost, aimed at the finder's suction point
 * but with SuctionFlg clear -- so it drifts towards the camera without being
 * banked, which is what makes a zero shot look like it missed. */
static void IgEffectZeroParticleReq(int num)                    /* ROM 1106 */
{
    ENE_DMG_PARTICLE_REQ EneDmgParticleReq;

    g3dxVu0CopyVector(EneDmgParticleReq.StartPos, ene_wrk[num].mpos.p0); /* 1110 */
    EneDmgParticleReq.pEndPos     = IgEffectParticleEndPosFinderGet();   /* 1111 */
    EneDmgParticleReq.ParticleNum = 48;                         /* 1112 */
    EneDmgParticleReq.DistPE      = ene_wrk[num].dist_p_e[0];   /* 1113 */
    EneDmgParticleReq.SuctionFlg  = 0;                          /* 1114 */
    EneDmgParticleReq.EffectType  = 0;                          /* 1115 */

    EneDmgParticleEffectReq(&EneDmgParticleReq);                /* 1116 */
}

/* DEAD CODE -- no caller anywhere in the loadable segments.  Everything that
 * wants the wobble calls ReqFreqCamera() directly. */
void IgEffectZeroCamFreqReq(void)                               /* ROM 1123 */
{
    ReqFreqCamera();                                            /* 1124 */
}

/* --------------------------------------------------------------------------
 *  "Has the suck finished?"  Both halves have to be quiet: the equip tray is
 *  still animating while it banks what arrived, and effect_ene.o still has
 *  particle work live while any are in flight.
 * ------------------------------------------------------------------------ */
int IgEffectIsEndParticleSuck(void)                             /* ROM 1132 */
{
    if (m_plyr_camera.eq_tray.IsMoving() == 0 &&                /* 1133 */
        IsActiveEneDmgParticle() == 0)
    {
        return 1;                                               /* 1135 */
    }

    return 0;                                                   /* 1138 */
}

void IgEffectRenzFlareDispFlgSet(int Flg)                       /* ROM 1145 */
{
    if (Flg != 0)                                               /* 1146 */
    {
        RenzFlareDispFlg = 1;                                   /* 1147 */
    }
    else                                                        /* 1149 */
    {
        RenzFlareDispFlg = 0;                                   /* 1150 */
    }
}

/* --------------------------------------------------------------------------
 *  Issue this frame's whole-screen look from one of effect.o's six canned
 *  SCREEN_EFFECT_PARAMETER blocks.  effect.c's InitEffectsEF() calls this
 *  every frame the player is not in the finder, so ScreenEffectParam00's
 *  Dither 3 is the ordinary walking-around film grain.
 *
 *  Each test reloads its own field rather than caching it: functions.txt
 *  lists no local for any of them.  Only Contrast and the three (tx, ty)
 *  pairs are variables, and the pairs are three separate block scopes -- the
 *  local list carries tx and ty three times, at two different stack offsets.
 * ------------------------------------------------------------------------ */
void IgEffectStoryMainScreenEffectReq(SCREEN_EFFECT_PARAMETER *pScreenPara)
{                                                               /* ROM 1158 */
    if (GetDebugMenuSwitch() == 0)                              /* 1161 */
    {
        return;
    }

    if (pScreenPara->Z_Dep == 1)                                /* 1164 */
    {
        SetEffects_Z_DEP(1);                                    /* 1165 */
    }

    /* 1..8 is the whole dither type range -- SubDither3() takes 1..7 and
     * SubDither4() type 8.  EffectDitherIsSet() keeps a dither the event
     * script already armed from being replaced by the room default. */
    if ((u_int)(pScreenPara->Dither - 1) < 8)                   /* 1173 */
    {
        if (EffectDitherIsSet() == 0)                           /* 1174 */
        {
            SetEffects_DITHER(1, pScreenPara->Dither,           /* 1175 */
                              (float)pScreenPara->DitherAlpha,
                              (float)pScreenPara->DitherSpeed,
                              pScreenPara->DitherAlphaMax,
                              pScreenPara->DitherColorMax,
                              0, 0, 0);
        }
    }

    /* The three blur variants differ only in the id they hand SetEffects and
     * in which stack slot each of tx / ty lands in. */
    if (pScreenPara->Blur == 1)                                 /* 1179 */
    {
        float tx;
        float ty;

        GetCamI2DPos(plyr_wrk.cmn_wrk.mbox.pos, &tx, &ty);      /* 1181 */
        SetEffects_BLUR(0, 1, &pScreenPara->BlurAlpha,          /* 1182 */
                        pScreenPara->BlurScale, pScreenPara->BlrrRot,
                        tx, ty);
    }
    if (pScreenPara->Blur == 2)                                 /* 1184 */
    {
        float tx;
        float ty;

        GetCamI2DPos(plyr_wrk.cmn_wrk.mbox.pos, &tx, &ty);      /* 1186 */
        SetEffects_BLUR(1, 1, &pScreenPara->BlurAlpha,          /* 1187 */
                        pScreenPara->BlurScale, pScreenPara->BlrrRot,
                        tx, ty);
    }
    if (pScreenPara->Blur == 3)                                 /* 1189 */
    {
        float tx;
        float ty;

        GetCamI2DPos(plyr_wrk.cmn_wrk.mbox.pos, &tx, &ty);      /* 1191 */
        SetEffects_BLUR(2, 1, &pScreenPara->BlurAlpha,          /* 1192 */
                        pScreenPara->BlurScale, pScreenPara->BlrrRot,
                        tx, ty);
    }

    if (pScreenPara->Deform != 0)                               /* 1195 */
    {
        /* Both narrow to u_char at the call -- lbu off the low byte of each
         * int, so a value over 255 wraps rather than clamping. */
        SetEffects_DEFORM(1, (u_char)pScreenPara->Deform,        /* 1196 */
                          (u_char)pScreenPara->DeformRate, 0, 0, 0);
    }

    if (pScreenPara->Focus != 0)                                /* 1199 */
    {
        SetEffects_FOCUS(1, (u_char)pScreenPara->Focus);        /* 1200 */
    }

    if (pScreenPara->Overlap == 1)                              /* 1203 */
    {
        SetEffects_OVERLAP(1, 0x10);                            /* 1204 */
    }

    if (pScreenPara->FadeFrame != 0)                            /* 1207 */
    {
        SetEffects_FADEFRAME(1, (u_char)pScreenPara->FadeFrameAlpha, 0); /* 1208 */
    }

    {
        int Contrast = IgEffectStoryMainContrastTypeGet();      /* 1211 */

        if (Contrast != 0)                                      /* 1213 */
        {
            if (Contrast == 0xc)                                /* 1214 */
            {
                SetEffects_NEGA(1, (u_char)pScreenPara->NegaColor, /* 1218 */
                                (u_char)pScreenPara->NegaAlpha,
                                0, 0, 0, &pScreenPara->NegaAlpha2);
            }
            else
            {
                SetEffects_NCONTRAST(Contrast, 1,               /* 1215 */
                                     IgEffectStoryMainContrastColorGet(),
                                     IgEffectStoryMainContrastAlphaGet());
            }
        }
    }

    if (pScreenPara->BlackFilter != 0)                          /* 1222 */
    {
        /* The ROM writes SetEffects(0xb, 1) and pushes no alpha, but its
         * handler reads one anyway -- `lbu 0x0(a2)` off the va_list, i.e. the
         * spill of whatever the caller left in a2.  At this call site that is
         * BlackFilter itself, loaded there for the test above, so the filter
         * runs at (u_char)BlackFilter.  Reproduced rather than passed 0: a 0
         * would make the whole branch a no-op.  Moot in practice -- all six
         * ScreenEffectParam blocks have BlackFilter 0, so it never runs. */
        SetEffects_BLACKFILTER(1, (u_char)pScreenPara->BlackFilter); /* 1223 */
    }
}                                                               /* 1257 */

/* --------------------------------------------------------------------------
 *  The story screen-effect contrast filter: which of the four contrast
 *  handlers the current SCREEN_EFFECT_PARAMETER asks for, and the colour and
 *  alpha to run it at.  Contrast 2..5 map onto effect ids 0xd/0xe/0xf/0xc --
 *  three NCONTRAST variants and the nega -- and anything else is 0, meaning
 *  no filter.  puzzle.c's SetEffectsStoryContrast(type, 1, colour, alpha) is
 *  written to be handed that 0.
 * ------------------------------------------------------------------------ */
int IgEffectStoryMainContrastTypeGet(void)                      /* ROM 1266 */
{
    /* GCC folds the declaration's 0 into the last arm's conditional move,
     * which is why there is no `li 0` of its own. */
    int RetVal = 0;                                             /* 1267 */

    if (EffectGetNowScreenEffectParamPtr()->Contrast == 2)      /* 1270 */
    {
        RetVal = 0xd;
    }
    else if (EffectGetNowScreenEffectParamPtr()->Contrast == 3) /* 1274 */
    {
        RetVal = 0xe;
    }
    else if (EffectGetNowScreenEffectParamPtr()->Contrast == 4) /* 1278 */
    {
        RetVal = 0xf;
    }
    else if (EffectGetNowScreenEffectParamPtr()->Contrast == 5) /* 1282 */
    {
        RetVal = 0xc;
    }

    return RetVal;                                              /* 1286 */
}

int IgEffectStoryMainContrastColorGet(void)                     /* ROM 1293 */
{
    return EffectGetNowScreenEffectParamPtr()->ContrastColor;   /* 1294, 1296 */
}

int IgEffectStoryMainContrastAlphaGet(void)                     /* ROM 1303 */
{
    return EffectGetNowScreenEffectParamPtr()->ContrastAlpha;   /* 1304, 1306 */
}

/* --------------------------------------------------------------------------
 *  A dust puff at a bone.  Offset is in the bone's local space, so the caller
 *  does not have to know where the foot ended up.
 * ------------------------------------------------------------------------ */
void IgEffectDustReq(ANI_CTRL *pAniCtrl, int BoneId, float *Offset)
{                                                               /* ROM 1313 */
    float LocalWorld[4][4];
    float Position[4];

    if (pAniCtrl != nullptr)                                    /* 1317 */
    {
        motGetLocalWorldMatrix(LocalWorld, pAniCtrl->mpk_p, BoneId); /* 1318 */
        sceVu0ApplyMatrix(Position, LocalWorld, Offset);        /* 1319 */

        SetEffects_DUST(2, Position);                           /* 1320 */
    }
}

/* Four separate stores, not an initialiser: the ROM writes them on 1338..1341
 * and only a .rodata blob copy would say otherwise. */
void IgEffectPlayerDustReq(ANI_CTRL *pAniCtrl, int BoneId)      /* ROM 1328 */
{
    float Offset[4];

    Offset[0] = -4.0f;                                          /* 1338 */
    Offset[1] = 0.0f;                                           /* 1339 */
    Offset[2] = 0.0f;                                           /* 1340 */
    Offset[3] = 1.0f;                                           /* 1341 */

    IgEffectDustReq(pAniCtrl, BoneId, Offset);                  /* 1344 */
}

/* --------------------------------------------------------------------------
 *  One butterfly, maybe.  Ten live at once is the cap, and past that the
 *  request is a 1-in-BirthRate dice roll per call -- MapObjSetEffect() raises
 *  it every frame for every butterfly placement in the room, so the swarm
 *  fills in gradually rather than all at once.
 *
 *  BirthRate is an int the ROM converts at run time (`li 400` then `cvt.s.w`),
 *  which is what says it was a variable rather than a float literal folded in.
 *  The `* 10 < 10` pair is the ROM's own: it reduces to random(0, 400) < 1.
 * ------------------------------------------------------------------------ */
void IgEffectButterflyReq(float *Position)                      /* ROM 1367 */
{
    int BirthRate = 400;                                        /* 1369 */

    if (EffectButterflyNumGet() < 10 &&                         /* 1378 */
        EffectGetRandom(0, BirthRate) * 10 < 10)                /* 1379 */
    {
        EffectButterflyReq(Position);                           /* 1380 */
    }
}

void IgEffectButterflyAllCut(void)                              /* ROM 1389 */
{
    EffectButterflyAllCut();                                    /* 1390 */
    EffectButterflyParticleAllCut();                            /* 1391 */
}

/* ==========================================================================
 *  The three model-backed room effects
 *
 *  Each is a MapPut draw callback, so MapPutGetNowHdl() is the object being
 *  drawn.  `work` carries the model the registration stashed there and the
 *  matrix comes back by pointer.
 * ======================================================================== */

void IgEffectLightComeInModelDraw(void)                         /* ROM 1398 */
{
    void          *hdl;
    void          *pSgdTop;
    MAPPUT_MATRIX *CoordMat;

    hdl      = MapPutGetNowHdl();                               /* 1400 */
    pSgdTop  = (void *)MapPutGetWork(hdl);                      /* 1401 */
    CoordMat = MapPutGetMatrixPtr(hdl);                         /* 1402 */

    IgEffectLightComeInModelDrawSub(pSgdTop, *CoordMat);        /* 1403 */
}

/* --------------------------------------------------------------------------
 *  The shaft of light through a window or a doorway.  It is drawn additively
 *  with clamping off and no lighting at all -- a black ambient with every
 *  light disabled -- and with the fog pushed out to 1000..2000 at full white,
 *  so distance does not eat it.  All four pieces of GS and g3d state are
 *  restored afterwards.
 *
 *  EffectLightComeInExecOne()'s second argument is "is the finder up": the
 *  shaft brightens when the player is looking through the camera, which is
 *  what makes it a landmark while hunting.  The ROM computes that test on its
 *  own line, six above the call that consumes it.
 * ------------------------------------------------------------------------ */
void IgEffectLightComeInModelDrawSub(void *pSgdTop, float (*CoordMat)[4])
{                                                               /* ROM 1410 */
    long   BackupAlpha;
    long   BackupClamp;
    float  Ambient[4];
    G3DFOG FogBak;

    memset(Ambient, 0, sizeof(Ambient));                        /* 1414 */

    if (pSgdTop != nullptr)                                     /* 1417 */
    {
        BackupAlpha = GET_ALPHA_REGISTER(0);                    /* 1419 */
        BackupClamp = GET_CLAMP_REGISTER(0);                    /* 1420 */

        EffectLightComeInExecOne(EffectLightComeInGetDataPtr(pSgdTop), /* 1423, 1429 */
                                 plyr_wrk.cmn_wrk.mode == IG_PLMODE_FINDER);

        SetAlphaRegister(0, 0x48);                              /* 1431 */
        SetClampRegister(0, 0);                                 /* 1432 */

        FogBak = gra3dGetFogRef();                              /* 1434 */
        gra3dSetFog(255.0f, 255.0f, 1000.0f, 2000.0f);          /* 1435 */
        gra3dApplyFog();                                        /* 1436 */

        gra3dLightEnableAll(0);                                 /* 1438 */
        gra3dSetAmbient(Ambient);                               /* 1439 */
        gra3dApplyLight();                                      /* 1440 */

        MapDrawObjNoShadow(pSgdTop, CoordMat);                  /* 1442 */

        SetAlphaRegister(0, BackupAlpha);                       /* 1444 */
        SetClampRegister(0, BackupClamp);                       /* 1445 */

        gra3dSetFog(&FogBak);                                   /* 1447 */
        gra3dApplyFog();                                        /* 1448 */
    }
}                                                               /* 1449 */

void IgEffectTourouFreaModelDraw(void)                          /* ROM 1455 */
{
    void          *hdl;
    MAPPUT_MATRIX *CoordMat;

    hdl      = MapPutGetNowHdl();                               /* 1457 */
    CoordMat = MapPutGetMatrixPtr(hdl);                         /* 1458 */

    IgEffectTourouFreaModelDrawSub(hdl, *CoordMat);             /* 1459 */
}

/* --------------------------------------------------------------------------
 *  The lantern's flame: a billboarded texture turned to face the camera, plus
 *  the shared candle-flame particle 25 units above the lantern's own origin.
 *
 *  The billboard takes only the camera's yaw -- Vector2Rot() gives both angles
 *  and RotX is thrown away -- so the sprite stays upright however the camera
 *  is pitched.  Alpha 0x48 is additive and ZBUF 0x10a000118 has ZMSK set, so
 *  the flame reads depth but does not write it.
 * ------------------------------------------------------------------------ */
void IgEffectTourouFreaModelDrawSub(void *pHandle, float (*CoordMat)[4])
{                                                               /* ROM 1466 */
    /* data 318730 */
    static DRAW_ENV DrawEnv =
    {
        0x161,                      /* tex1  */
        0x44,                       /* alpha */
        0x10a000118ULL,             /* zbuf  */
        0x5000dULL,                 /* test  */
        0,                          /* clamp */
        0x302a400000008004ULL,      /* prim  */
    };

    float             NewCoord[4][4];
    float             FlamePos[4];
    TOUROU_FREA_DATA *pTourouFrea;
    long              BackupAlpha;
    long              BackupZBuf;
    float             RotX;
    float             RotY;

    /* No stab for this one -- see the file header.  The ROM calls it here,
     * before the pHandle test, and holds the result until line 1503. */
    float (&dir)[4] = gra3dcamGetDirection();                   /* 1471 */

    sceVu0IVECTOR     FlameColor = { 128, 128, 128, 18 };       /* 1481 */

    if (pHandle != nullptr)                                     /* 1487 */
    {
        g3dxVu0CopyVector(FlamePos, CoordMat[3]);               /* 1491 */
        FlamePos[1] += 25.0f;                                   /* 1493 */

        BackupAlpha = GET_ALPHA_REGISTER(0);                    /* 1495 */
        BackupZBuf  = GET_ZBUF_REGISTER(0);                     /* 1496 */

        pTourouFrea = EffectTourouFreaGetDataPtr(pHandle);      /* 1498 */

        if (pTourouFrea != nullptr)                             /* 1500 */
        {
            Vector2Rot(dir, &RotX, &RotY);                      /* 1503 */

            EffectTourouFreaExecOne(pTourouFrea);               /* 1505 */

            sceVu0UnitMatrix(NewCoord);                         /* 1507 */
            sceVu0RotMatrixY(NewCoord, NewCoord, RotY);         /* 1508 */
            sceVu0TransMatrix(NewCoord, NewCoord, CoordMat[3]); /* 1509 */

            SetAlphaRegister(0, 0x48);                          /* 1511 */
            SetZbufRegister(0, 0x10a000118ULL);                 /* 1512 */

            Set3DPosTexure(NewCoord, &DrawEnv, 0x4c,            /* 1523 */
                           pTourouFrea->ScaleX * 100.0f,
                           pTourouFrea->ScaleY * 100.0f,
                           0x89, 100, 90, (u_char)pTourouFrea->Alpha);

            EffectCandleFlameDraw(FlamePos, FlameColor, 1.0f,   /* 1525 */
                                  pTourouFrea->FlameCounter);

            SetAlphaRegister(0, BackupAlpha);                   /* 1526 */
            SetZbufRegister(0, BackupZBuf);                     /* 1527 */
        }
    }
}                                                               /* 1528 */

void IgEffectTourouBaseModelDraw(void)                          /* ROM 1534 */
{
    void          *hdl;
    void          *pSgdTop;
    MAPPUT_MATRIX *CoordMat;

    hdl      = MapPutGetNowHdl();                               /* 1536 */
    pSgdTop  = (void *)MapPutGetWork(hdl);                      /* 1537 */
    CoordMat = MapPutGetMatrixPtr(hdl);                         /* 1538 */

    IgEffectTourouBaseModelDrawSub(pSgdTop, *CoordMat);         /* 1539 */
}

/* --------------------------------------------------------------------------
 *  The lantern's paper shade.  Unlike the flame this is a real model, so it
 *  goes through MapDrawObj(); what makes it glow is the vertex colour preset
 *  (-1 = every vertex) and the additive blend, both driven by the colour and
 *  alpha effect_obj.o's flicker keeps in TOUROU_BASE_DATA::Color.
 * ------------------------------------------------------------------------ */
void IgEffectTourouBaseModelDrawSub(void *pSgdTop, float (*CoordMat)[4])
{                                                               /* ROM 1546 */
    TOUROU_BASE_DATA *pTourouBase;
    VECTOR3           Color;
    long              BackupAlpha;
    long              BackupZBuf;

    if (pSgdTop != nullptr)                                     /* 1552 */
    {
        BackupAlpha = GET_ALPHA_REGISTER(0);                    /* 1554 */
        BackupZBuf  = GET_ZBUF_REGISTER(0);                     /* 1555 */

        pTourouBase = EffectTourouBaseGetDataPtr(pSgdTop);      /* 1557 */

        if (pTourouBase != nullptr)                             /* 1559 */
        {
            EffectTourouBaseExecOne(pTourouBase);               /* 1561 */

            ManmdlSetAlpha(pSgdTop, (u_char)pTourouBase->Color[3]); /* 1563 */

            Color[0] = (float)pTourouBase->Color[0];            /* 1564 */
            Color[1] = (float)pTourouBase->Color[1];            /* 1565 */
            Color[2] = (float)pTourouBase->Color[2];            /* 1566 */

            gra3dSetVertexColorPreset((SGDFILEHEADER *)pSgdTop, -1, Color); /* 1567 */

            SetAlphaRegister(0, 0x48);                          /* 1569 */
            SetZbufRegister(0, 0x10a000118ULL);                 /* 1570 */

            MapDrawObj(pSgdTop, CoordMat);                      /* 1572 */

            SetAlphaRegister(0, BackupAlpha);                   /* 1574 */
            SetZbufRegister(0, BackupZBuf);                     /* 1575 */
        }
    }
}                                                               /* 1576 */

/* --------------------------------------------------------------------------
 *  A free-standing 48-particle burst with no destination and no suction, so
 *  it disperses.  EffectType picks the sprite; player.c's CEneTracer raises
 *  one when it lets go of a traced ghost.
 * ------------------------------------------------------------------------ */
void IgEffectEffectEndParticleReq(float *Position, int EffectType)
{                                                               /* ROM 1582 */
    ENE_DMG_PARTICLE_REQ EneDmgParticleReq;
    int                  ParticleNum;

    ParticleNum = 48;                                           /* 1588 */

    g3dxVu0CopyVector(EneDmgParticleReq.StartPos, Position);    /* 1591 */

    EneDmgParticleReq.pEndPos     = nullptr;                    /* 1593 */
    EneDmgParticleReq.ParticleNum = ParticleNum;                /* 1594 */
    EneDmgParticleReq.DistPE      = 0.0f;                       /* 1595 */
    EneDmgParticleReq.SuctionFlg  = 0;                          /* 1596 */
    EneDmgParticleReq.EffectType  = EffectType;                 /* 1597 */

    EffectEndParticleEffectReq(&EneDmgParticleReq);             /* 1598 */
}
