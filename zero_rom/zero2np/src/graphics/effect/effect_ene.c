// FILE: /home/zero_rom/zero2np/src/graphics/effect/effect_ene.c
//
// DONE.  The ghost-damage effects (effect_ene.o, .text
// 0x00142020..0x00149d40 = 0x7d20).  89 functions, 20 of them ZERO2.MAP
// exports -- 20/20 and 88/88 of functions.txt's real entries implemented.
//
// Every piece of static data is verified against the ROM's bytes: the 38
// ENE_DMG_LARGE_HIT_PARAMETER presets, the 20 ENE_DMG_BLUR_CONTRAST_PARAMETER
// presets, both .rodata selector tables (0x3a6368 / 0x3a6400), the five ramp
// tables (0x3a6450..0x3a6490), HitDamegeCol / EffectEndCol, SetEneDmgEffect1_
// Sub2's `rgb`, and every .sdata scalar and local-aggregate .rodata blob.
//
// Two statics the ROM defines between EneHitEffectReq and EneHitEffectMain
// were inlined away by GCC and are named by neither ZERO2.MAP nor
// functions.txt; EneHitEffectSubFuncReq() and EneHitEffectBlurContrastCalc()
// are the port's names for them, flagged at their definitions.  See
// [[out-of-order-line-numbers-mean-inlined-static]].
//
// One deviation carried from effect_obj.o: the VU0 register file is not
// emulated, so the header inlines that left matrices in vf4..vf11 take them as
// parameters instead -- CalcStqXYZ (effect_obj.h), CalcPartsDeformXYZ and
// gra3dVu0ApplyMatrix2.  And one honest gap: SetEneDmgEffect1_Sub2's `camdat`
// SPRT_DAT table (.rodata 0x3a5ea8) is not transcribed; the port points its
// reference_fixed_array at effdat[] at the same indices, which is what
// SetSprFile2(0x1fa8000, 0) selects.
//
// Three effects share the file, all raised while the camera is damaging a
// ghost:
//
//   - EneDmgScreenEffectReq()  the damage sprite over the ghost, a nine-state
//                              ramp on ene_dmg_eff.flow;
//   - EneDmgLargeHitReq()      the hit flash, an A/B pair out of 38 authored
//                              presets plus a screen blur/contrast/shake;
//   - EneDmgParticleEffectReq()the spirit particles, up to 48 per burst, that
//                              either disperse or fly home to the equip tray.
//
// Absent dependencies, all greppable as `STUB: needed by effect_ene.c`: the
// nine IgEffectSubFunc*Req() in ingame_effect.c -- one per camera sub-function,
// and the only thing missing from a sub-function hit's visuals, since the three
// ordinary shot grades go straight to EneDmgLargeHitReq() here.
//
// A NOTE ON THE /* NNN */ ANNOTATIONS.  Function opening lines are read off
// the $LM that precedes each PROC record in symbols.txt; statement lines come
// from the same table rather than from Ghidra's `; Line` comments, which are
// only trustworthy where the enclosing SOL names this file.  Anything whose
// only work goes through an inline -- fixed_array.h 124/125, g3dxVu0.h
// 134/135, SingleLinkList.h 54/65/76 or effect.h 217 (EffectGetRandom) --
// leaves no $LM of its own and is interpolated into the measured gap.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "effect_ene.h"

#include <stdlib.h>                             /* rand */
#include <string.h>                             /* memset */

#include "effect.h"                             /* EffectGetRandom / SetEffects / EFFECT_MALLOC */
#include "effect_obj.h"                         /* CalcPartsDeformXYZ */
#include "effect_pak.h"                         /* Reserve2DPacket */
#include "effect_sub.h"                         /* GetCamI2DPos / Get2PosRot / Vector2Rot / Set3DPosTexure */

#include "../draw_env.h"                        /* SetDrawEnv */
#include "../graph2d/g2d_draw.h"                /* LocalCopyLtoB / StartDmaDirectTrans */
#include "../graph2d/tim2.h"                    /* SetSprFile2 */
#include "../graph2d/graph2d.h"                 /* SPRT_DAT effdat[] */
#include "../graph3d/g3dxVu0.h"
#include "../graph3d/gra3d.h"                   /* gra3dGetCamera / gra3dcamGetPosition */
#include "../graph3d/gra3dVu0.h"                /* gra3dVu0ApplyMatrix2 */
#include "../graph3d/ctl/fixed_array.h"
#include "../graphics.h"                        /* RendererPacket3D */
#include "../../common/SingleLinkList.h"
#include "../../common/utility.h"
#include "../../common/zero2_util.h"
#include "../../ingame/ingame_effect.h"         /* IgEffectSubFunc*Req */
#include "../../ingame/enemy/enemy.h"           /* ene_wrk / ENE_WRK */
#include "../../ingame/photo/freq_camera.h"     /* ReqFreqCamera */
#include "../../ingame/plyr/unit_ctl.h"         /* GetTrgtRot */
#include "../../system/pad/vib_manage.h"        /* SetVibrate */
#include "../../miopan/miopan_memory.h"         /* MioPan_GetHostPointer */
#include "../../miopan/rendering/miopan_renderer.h"
#include "../../sdk/libvu0.h"

/* The ROM's own pi, one ulp below 3.1415927 -- EE GCC truncated the literal,
 * so the .lit4 at 0x3edac4 is 3.1415925f.  Same value scene.c and
 * zero2_util.c carry.  See [[ee-gcc-truncates-float-literals]]. */
static const float EFE_PI = 3.1415925f;

/* ---- forward declarations for the file's statics, in ROM order ---------- */
static sceVu0FVECTOR *EfGetMpos(int eneno, int id);
static u_short EfGetDmgOld(int eneno);
static void SetEneDmgEffect1_Sub2(int num);
static void EneDmgParticleWorkInit(void);
static void EffectEndParticleWorkInit(void);
static void EneDmgScreenWorkInit(void);
static void EneHitEffectCtrlInit(void);
static void EneDmgParticleEffectCut(SINGLE_LINK_LIST *pSLL, SLL_CELL *pCell);
static void EneDmgParticleColorGet(int *Color, int Type, int EffectType);
static int  EneDmgParticleSpreadTimeGet(int Type);
static void EneDmgParticle(SINGLE_LINK_LIST *pSLL, SLL_CELL *pCell);
static float EneDmgParticleSizeWGet(int Type);
static float EneDmgParticleSizeHGet(int Type);
static int  EneDmgParticleAnmCountGet(int Type);
static float EneDmgParticleAlphaGet(int Type);
static void EneDmgParticleOneInit(ENEDMG_PARTICLE_ONE *pEneParticle,
                                  float *Center, float SpeedRate, int Type);
static int  EneDmgParticleOneUpdate(ENEDMG_PARTICLE_ONE *pEneParticle);
static void EneDmgParticleSuctionInit(ENEDMG_P_WRK *pEneDmgPWrk,
                                      const float *Start, const float *End);
static int  EneDmgParticleSuction(ENEDMG_P_WRK *pEneDmgPWrk);
static int  EneDmgParticleSuctionOne(ENEDMG_PARTICLE_ONE *pParticle,
                                     float *StartPos, float *SubVec,
                                     float Length, float LengthDiv6);
static void EneDmgParticleSuctionTailInit(ENEDMG_P_TAIL_WRK *pEneDmgTail,
                                          ENEDMG_PARTICLE_ONE *pParticle);
static int  EneDmgParticleSuctionTail(ENEDMG_P_TAIL_WRK *pEneDmgTail,
                                      ENEDMG_PARTICLE_ONE *pParticle);
static void DrawNewPerticleSub(int num, sceVu0FVECTOR *pos, u_char r1,
                               u_char g1, u_char b1, u_char r2, u_char g2,
                               u_char b2, u_char alpha);
static void EneDmgLargeHitCtrlInit(void);
static void EneDmgLargeHitCtrlMain(void);
static void EneDmgLargeHitInit(ENE_DMG_LARGE_HIT *pLargeHit,
                               const ENE_DMG_LARGE_HIT_PARAMETER *pLHParam);
static void EneDmgLargeHitEffectDisp(ENE_DMG_LARGE_HIT *pLargeHit, float Scale,
                                     float RotZ);
static void EneDmgLargeHitMakeVertex(sceVu0FVECTOR *pVtxBuf, float *Center,
                                     float Width, float Height,
                                     u_int VertexNumW, u_int VertexNumH);
static void SetEneDmgEffect2_Sub2(NEW_PERTICLE *np, float *bpos1, float *bpos2,
                                  u_char r1, u_char g1, u_char b1,
                                  u_char r2, u_char g2, u_char b2);

/* Declared inline in the ROM (its body is a .gnu.linkonce.t.*), and called from
 * EneDmgParticle() at 2008 before it is defined at 2083. */
void EneDmgParticleOneDraw(ENEDMG_PARTICLE_ONE *pEneParticle, float rot_x,
                           float rot_y, int r, int g, int b, float AlphaRate,
                           int Type);

/* The 38 preset arming helpers.  EneDmgLargeHitReq() (2836) switches over all
 * of them and they are not defined until 3021, so the ROM must declare them
 * ahead of it; where it does is not recoverable, so they are here. */
static void EneDmgLargeHitEffectSmallInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectSP_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectSP_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectSlow_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectSlow_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectZero_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectZero_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectZeroSC_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectZeroSC_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectZeroSP_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectZeroSP_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectKoku_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectKoku_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectKokuSC_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectKokuSC_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectKokuSP_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectKokuSP_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectParalyze_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectParalyze_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectView_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectView_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectMetsu_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectMetsu_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectMetsuSC_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectMetsuSC_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectMetsuSP_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectMetsuSP_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectRen_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectRen_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectRenSC_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectRenSC_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectRenSP_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectRenSP_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectTsui_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectTsui_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectFuu_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectFuu_BInit(ENE_DMG_LARGE_HIT *pLargeHit);

/* ==========================================================================
 *  The 38 large-hit presets.                                       ROM 155
 *
 *  Two per hit label -- an "A" flash and a "B" flash drawn together -- except
 *  the first two, which are the ordinary small and large shots and stand
 *  alone.  Every field but the two colours is a fixed-point thousandth;
 *  EneDmgLargeHitInit() is where they are divided down.
 *
 *  The pattern across the table: VertexNumW/H are 17 x 15 for every single
 *  preset, so the ring is always the same mesh, and only the colour, size,
 *  lifetime and blend differ.  AllFrame is the lifetime in frames and runs 1
 *  to 83 -- these are all sub-second flashes.  RotVal is 3600 (i.e. no spin,
 *  since EneDmgLargeHitInit() subtracts 3600) for 32 of the 38; only the six
 *  Metsu presets turn, by -117 or +54 degrees.
 * ======================================================================== */

/* CenterRgba[4], OutsideRgba[4], VertexNumW, VertexNumH, Size, LastScale,
 * AllFrame, MoveDist, Distance, RotVal, CaptureInterval, CaptureNumber,
 * AlphaBlend A, B, C, D, FIX */
ENE_DMG_LARGE_HIT_PARAMETER SmallHit =                          /* data 2e3590 */
{ { 222, 206, 255, 177 }, { 121, 188, 243,   0 }, 17, 15,  68, 120,  30,   16,  901, 3600, 2, 2, 0, 2, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER LargeHitType00 =                    /* data 2e35f0 */
{ { 188, 216, 255, 156 }, { 255, 201, 255,  47 }, 17, 15, 101, 472,  29,  259,  971, 3600, 2, 2, 0, 1, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER LargeHitSPAType00 =                 /* data 2e3650 */
{ { 233, 227, 191,  94 }, { 233, 227, 191, 255 }, 17, 15,  95, 284,  15,  327,  975, 3600, 1, 1, 0, 2, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER LargeHitSPBType00 =                 /* data 2e36b0 */
{ { 241, 189, 113,  56 }, { 241, 189, 113,   0 }, 17, 15, 100, 167,  46,    0, 1000, 3600, 1, 2, 0, 2, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER SlowHitAType00 =                    /* data 2e3710 */
{ { 100,  68, 146, 125 }, {  73,  75,  78,   0 }, 17, 15, 100, 176,  35,    0,  940, 3600, 1, 1, 0, 2, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER SlowHitBType00 =                    /* data 2e3770 */
{ {  73,  72,  73,  13 }, {  73,  72,  73,   0 }, 17, 15, 100, 169,  33,  948,  946, 3600, 1, 1, 0, 2, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER ZeroHitA =                          /* data 2e37d0 */
{ { 203, 154,  93,  12 }, { 203, 154,  93,   0 }, 17, 15, 100, 961,   8,   13,  946, 3600, 1, 6, 0, 2, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER ZeroHitB =                          /* data 2e3830 */
{ { 116, 125, 180, 197 }, { 116, 125, 180,   0 }, 17, 15, 100, 819,  31,  625,  992, 3600, 1, 1, 0, 2, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER ZeroHitSCA =                        /* data 2e3890 */
{ { 203, 154,  93,  40 }, { 203, 154,  93,  33 }, 17, 15, 100, 961,   8,   13,  946, 3600, 1, 6, 0, 2, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER ZeroHitSCB =                        /* data 2e38f0 */
{ { 116, 125, 180, 197 }, { 116, 125, 180,   0 }, 17, 15, 100, 819,  31,  625,  992, 3600, 1, 1, 0, 2, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER ZeroHitSPA =                        /* data 2e3950 */
{ { 203, 154,  93,  84 }, { 207, 154,  93,  84 }, 17, 15, 100, 961,   8,   13,  946, 3600, 1, 6, 0, 2, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER ZeroHitSPB =                        /* data 2e39b0 */
{ { 116, 125, 180, 197 }, { 116, 125, 180,   0 }, 17, 15, 100, 819,  31,  625,  992, 3600, 1, 1, 0, 2, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER KokuHitA =                          /* data 2e3a10 */
{ { 145, 152, 111,  67 }, { 175, 175, 114,   0 }, 17, 15, 101,  30,  11,    0,  974, 3600, 0, 0, 0, 2, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER KokuHitB =                          /* data 2e3a70 */
{ { 156, 153, 107, 113 }, { 172, 161, 111,   0 }, 17, 15, 104, 308,  29,   62,  992, 3600, 1, 1, 0, 2, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER KokuHitSCA =                        /* data 2e3ad0 */
{ { 145, 152, 111, 153 }, { 175, 175, 114,   0 }, 17, 15, 101,  30,  11,    0,  974, 3600, 2, 2, 0, 2, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER KokuHitSCB =                        /* data 2e3b30 */
{ { 156, 153, 107, 113 }, { 172, 161, 111,   0 }, 17, 15, 104, 308,  29,   62,  992, 3600, 2, 8, 0, 2, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER KokuHitSPA =                        /* data 2e3b90 */
{ { 184, 255, 193,   0 }, { 188, 255, 182, 255 }, 17, 15,  98, 209,  17,  320, 1001, 3600, 2, 2, 0, 2, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER KokuHitSPB =                        /* data 2e3bf0 */
{ { 156, 153, 107, 113 }, { 172, 161, 111,   0 }, 17, 15, 104, 308,  29,   62,  992, 3600, 2, 8, 0, 2, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER ParalyzeHitA =                      /* data 2e3c50 */
{ { 100, 158, 126, 145 }, { 100, 158, 126,   0 }, 17, 15, 100, 100,  33,    0, 1000, 3600, 1, 1, 0, 2, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER ParalyzeHitB =                      /* data 2e3cb0 */
{ { 116, 163, 159, 255 }, { 116, 127, 159,   0 }, 17, 15, 100, 501,  35,  636,  992, 3600, 1, 1, 0, 2, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER ViewHitA =                          /* data 2e3d10 */
{ { 255, 247, 175, 108 }, { 255, 233, 177, 255 }, 17, 15, 100, 105,  31,    0, 1001, 3600, 1, 1, 0, 2, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER ViewHitB =                          /* data 2e3d70 */
{ { 116, 127, 159,  39 }, { 116, 127, 159,  43 }, 17, 15, 100, 231,   1,    0,  992, 3600, 1, 1, 0, 2, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER MetsuHitA =                         /* data 2e3dd0 */
{ { 206, 101,  63, 255 }, { 196, 119, 114, 255 }, 17, 15, 101, 171,   4,   27,  974, 3654, 2, 2, 0, 2, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER MetsuHitB =                         /* data 2e3e30 */
{ { 234, 110, 107, 219 }, { 255, 114, 111,   0 }, 17, 15, 104, 308,   9,   62,  992, 3483, 5, 6, 0, 2, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER MetsuHitSCA =                       /* data 2e3e90 */
{ { 206, 101,  63, 255 }, { 196, 119, 114, 255 }, 17, 15, 101, 171,   4,   27,  974, 3654, 2, 2, 0, 2, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER MetsuHitSCB =                       /* data 2e3ef0 */
{ { 234, 110, 107, 219 }, { 255, 114, 111,   0 }, 17, 15, 104, 308,   9,   62,  992, 3483, 5, 6, 0, 2, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER MetsuHitSPA =                       /* data 2e3f50 */
{ { 206, 101,  63, 255 }, { 196, 119, 114, 255 }, 17, 15, 101, 171,   4,   27,  974, 3654, 2, 2, 0, 2, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER MetsuHitSPB =                       /* data 2e3fb0 */
{ { 234, 110, 107, 219 }, { 255, 114, 111,   0 }, 17, 15, 104, 308,   9,   62,  992, 3483, 5, 6, 0, 2, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER RenHitA =                           /* data 2e4010 */
{ { 114, 152, 111,  32 }, { 126, 175, 114,   0 }, 17, 15, 101,  10,   8,    1,  985, 3600, 0, 0, 0, 2, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER RenHitB =                           /* data 2e4070 */
{ { 114, 153, 107, 194 }, { 103, 161, 111, 123 }, 17, 15, 104, 308,   8,  881,  992, 3600, 1, 0, 0, 2, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER RenHitSCA =                         /* data 2e40d0 */
{ { 114, 152, 111, 153 }, { 126, 175, 114,   0 }, 17, 15, 101,  10,  11,    0,  974, 3600, 2, 2, 0, 2, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER RenHitSCB =                         /* data 2e4130 */
{ { 114, 153, 107, 113 }, { 103, 161, 111,   0 }, 17, 15, 104, 308,  10,   62,  992, 3600, 2, 3, 0, 2, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER RenHitSPA =                         /* data 2e4190 */
{ { 114, 152, 111, 153 }, { 126, 175, 114,   0 }, 17, 15, 101,  10,  11,    0,  974, 3600, 2, 2, 0, 2, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER RenHitSPB =                         /* data 2e41f0 */
{ { 114, 153, 107, 113 }, { 103, 161, 111,   0 }, 17, 15, 104, 308,  10,   62,  992, 3600, 2, 3, 0, 2, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER TsuiHitA =                          /* data 2e4250 */
{ { 255, 247, 193,  94 }, { 255, 233, 204, 255 }, 17, 15, 180, 100,  83,    0, 1001, 3600, 1, 1, 0, 2, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER TsuiHitB =                          /* data 2e42b0 */
{ { 116, 127, 159,  39 }, { 116, 127, 159,  43 }, 17, 15, 262, 312,   1,    0,  992, 3600, 1, 1, 0, 2, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER FuuHitA =                           /* data 2e4310 */
{ { 184, 255, 193,   0 }, { 188, 255, 182, 255 }, 17, 15,  98, 209,  17,  320, 1001, 3600, 2, 2, 0, 2, 0, 1, 0 };
ENE_DMG_LARGE_HIT_PARAMETER FuuHitB =                           /* data 2e4370 */
{ { 133, 226, 159,   0 }, { 144, 233, 159, 154 }, 17, 15, 100, 336,  10,  351,  992, 3600, 2, 2, 0, 2, 0, 1, 0 };

/* ==========================================================================
 *  The 20 blur / contrast presets -- one per ENE_HIT_EFFECT_LABEL.
 *
 *  StartFrame, InTime, KeepTime, OutTime, MinBlurScale, MaxBlurScale,
 *  MinBlurAlpha, MaxBlurAlpha, MinBlurRot, MaxBlurRot, BlurOnFlg,
 *  MinContrastColor, MaxContrastColor, MinContrastAlpha, MaxContrastAlpha,
 *  ContrastOnFlg, CameraShakeOnFlg, CameraShakeFrame, PadVibrateOnFlg,
 *  PadVibrateFrame.
 *
 *  SmallHit_Blur is the only one with every switch off -- an ordinary weak
 *  shot shakes nothing.  MinBlurScale/MaxBlurScale are thousandths, so 1000
 *  is "no zoom"; the rotations are tenths of a degree about 1800 (180.0).
 * ======================================================================== */
ENE_DMG_BLUR_CONTRAST_PARAMETER SmallHit_Blur =                 /* data 2e43d0 */
{ 0,  2, 12,  9, 1000, 1080,  0,  64, 1800, 1800, 0,  0,  80,  0,  80, 0, 0,  0, 0,  0 };
ENE_DMG_BLUR_CONTRAST_PARAMETER LargeHit_Blur =                 /* data 2e4420 */
{ 5,  0, 24,  0, 1000, 1207,  0,  15, 1800, 1800, 1,  0,  11,  0,  16, 1, 1,  2, 1,  2 };
ENE_DMG_BLUR_CONTRAST_PARAMETER LargeHitSP_Blur =               /* data 2e4470 */
{ 0,  0, 12,  0, 1000, 1283,  0,  64, 1800, 1800, 1, 30,  48, 27,  37, 1, 1, 11, 1, 10 };
ENE_DMG_BLUR_CONTRAST_PARAMETER SlowHit_Blur =                  /* data 2e44c0 */
{ 0, 27, 27, 78, 1000, 1080,  0,  64, 1800, 1800, 0,  0,  80,  0,  80, 0, 0,  4, 0,  4 };
ENE_DMG_BLUR_CONTRAST_PARAMETER ZeroHit_Blur =                  /* data 2e4510 */
{ 6, 20, 20, 20, 1000, 1080,  0,  64, 1800, 1820, 0,  0,  80,  0,  80, 0, 1,  7, 1,  7 };
ENE_DMG_BLUR_CONTRAST_PARAMETER ZeroHitSC_Blur =                /* data 2e4560 */
{ 6, 20, 20, 20, 1000, 1080,  0,  64, 1800, 1820, 0,  0,  80,  0,  80, 0, 1,  7, 1,  7 };
ENE_DMG_BLUR_CONTRAST_PARAMETER ZeroHitSP_Blur =                /* data 2e45b0 */
{ 6, 20, 20, 20, 1000, 1080,  0,  64, 1800, 1820, 0,  0,  80,  0,  80, 0, 1,  7, 1,  7 };
ENE_DMG_BLUR_CONTRAST_PARAMETER KokuHit_Blur =                  /* data 2e4600 */
{ 0, 20, 20, 20, 1030, 1683, 35,  21, 1800, 1800, 0, 42,  80, 74, 140, 0, 1,  7, 1,  7 };
ENE_DMG_BLUR_CONTRAST_PARAMETER KokuHitSC_Blur =                /* data 2e4650 */
{ 0, 20, 20, 20, 1030, 1683, 35,  21, 1800, 1800, 1, 42,  80, 74, 140, 1, 1,  7, 1,  7 };
ENE_DMG_BLUR_CONTRAST_PARAMETER KokuHitSP_Blur =                /* data 2e46a0 */
{ 0, 20, 20, 20, 1030, 1683, 35,  21, 1800, 1800, 1, 42,  80, 74, 140, 1, 1,  7, 1,  7 };
ENE_DMG_BLUR_CONTRAST_PARAMETER ParalyzeHit_Blur =              /* data 2e46f0 */
{ 0,  2,  8,  8, 1000, 1392, 75, 129, 1800, 1820, 1,  0,  80,  0,  80, 1, 0,  5, 1,  5 };
ENE_DMG_BLUR_CONTRAST_PARAMETER ViewHit_Blur =                  /* data 2e4740 */
{ 7, 20, 20, 20, 1000, 1080,  0,  64, 1800, 1820, 0,  0,  80,  0,  80, 0, 0,  6, 0,  6 };
ENE_DMG_BLUR_CONTRAST_PARAMETER MetsuHit_Blur =                 /* data 2e4790 */
{ 0, 20, 20, 20, 1030, 1683, 95,  93, 1571, 2073, 1, 42,  80, 74, 140, 1, 1,  7, 1,  7 };
ENE_DMG_BLUR_CONTRAST_PARAMETER MetsuHitSC_Blur =               /* data 2e47e0 */
{ 0, 20, 20, 20, 1030, 1683, 95,  93, 1571, 2073, 1, 42,  80, 74, 140, 1, 1,  7, 1,  7 };
ENE_DMG_BLUR_CONTRAST_PARAMETER MetsuHitSP_Blur =               /* data 2e4830 */
{ 0, 20, 20, 20, 1030, 1683, 95,  93, 1571, 2073, 1, 42,  80, 74, 140, 1, 1,  7, 1,  7 };
ENE_DMG_BLUR_CONTRAST_PARAMETER RenHit_Blur =                   /* data 2e4880 */
{ 0, 20, 20, 20, 1030, 1683, 35,  21, 1800, 1800, 1, 42,  80, 74, 140, 1, 1,  7, 1,  7 };
ENE_DMG_BLUR_CONTRAST_PARAMETER RenHitSC_Blur =                 /* data 2e48d0 */
{ 0, 20, 20, 20, 1030, 1683, 35,  21, 1800, 1800, 1, 42,  80, 74, 140, 1, 1,  7, 1,  7 };
ENE_DMG_BLUR_CONTRAST_PARAMETER RenHitSP_Blur =                 /* data 2e4920 */
{ 0, 20, 20, 20, 1030, 1683, 35,  21, 1800, 1800, 1, 42,  80, 74, 140, 1, 1,  7, 1,  7 };
ENE_DMG_BLUR_CONTRAST_PARAMETER TsuiHit_Blur =                  /* data 2e4970 */
{ 7,  5, 23, 23, 1000, 1194, 60, 127, 1800, 1800, 1, 49,  80, 63,  80, 1, 0,  6, 0,  6 };
ENE_DMG_BLUR_CONTRAST_PARAMETER FuuHit_Blur =                   /* data 2e49c0 */
{ 0,  2, 48, 27,  910, 1405, 16,  92, 1783, 1808, 1, 32,  64, 45, 158, 1, 0,  6, 0,  6 };

/* The two selector tables, .rodata at 0x3a6368 and 0x3a6400.  Neither is in
 * globals.txt -- only the reference_fixed_array wrappers are -- so the ROM's
 * own names for them are unrecoverable; the pointers are read out of the ELF
 * and are in declaration order.  The static-init helper at 0x00149c88 is what
 * seeds the two wrappers with them. */
static ENE_DMG_LARGE_HIT_PARAMETER *pLargeHitParameterTbl[38] =  /* rdata 3a6368 */
{
    &SmallHit,       &LargeHitType00, &LargeHitSPAType00, &LargeHitSPBType00,
    &SlowHitAType00, &SlowHitBType00, &ZeroHitA,          &ZeroHitB,
    &ZeroHitSCA,     &ZeroHitSCB,     &ZeroHitSPA,        &ZeroHitSPB,
    &KokuHitA,       &KokuHitB,       &KokuHitSCA,        &KokuHitSCB,
    &KokuHitSPA,     &KokuHitSPB,     &ParalyzeHitA,      &ParalyzeHitB,
    &ViewHitA,       &ViewHitB,       &MetsuHitA,         &MetsuHitB,
    &MetsuHitSCA,    &MetsuHitSCB,    &MetsuHitSPA,       &MetsuHitSPB,
    &RenHitA,        &RenHitB,        &RenHitSCA,         &RenHitSCB,
    &RenHitSPA,      &RenHitSPB,      &TsuiHitA,          &TsuiHitB,
    &FuuHitA,        &FuuHitB
};

static ENE_DMG_BLUR_CONTRAST_PARAMETER *pLargeHitBlurParameterTbl[20] = /* rdata 3a6400 */
{
    &SmallHit_Blur,    &LargeHit_Blur,     &LargeHitSP_Blur,  &SlowHit_Blur,
    &ZeroHit_Blur,     &ZeroHitSC_Blur,    &ZeroHitSP_Blur,   &KokuHit_Blur,
    &KokuHitSC_Blur,   &KokuHitSP_Blur,    &ParalyzeHit_Blur, &ViewHit_Blur,
    &MetsuHit_Blur,    &MetsuHitSC_Blur,   &MetsuHitSP_Blur,  &RenHit_Blur,
    &RenHitSC_Blur,    &RenHitSP_Blur,     &TsuiHit_Blur,     &FuuHit_Blur
};

static reference_fixed_array<ENE_DMG_LARGE_HIT_PARAMETER *, 38>
    pLargeHitParameter(pLargeHitParameterTbl);                  /* sbss 3f4b78 *//* 231 */
static reference_fixed_array<ENE_DMG_BLUR_CONTRAST_PARAMETER *, 20>
    pLargeHitBlurParameter(pLargeHitBlurParameterTbl);          /* sbss 3f4b80 *//* 232 */

/* ==========================================================================
 *  The damage-sprite ramps.
 *
 *  Five three-element tables, all .rodata at 0x3a6450..0x3a6490 and again
 *  unnamed in globals.txt.  The convention is the same for all five:
 *
 *      [0]  the value the ramp starts and ends at
 *      [1]  its maximum with the ghost right in front of the camera
 *      [2]  its maximum at 3000 units away
 *
 *  EneDmgMain()'s flow 2 interpolates between [1] and [2] on the measured
 *  distance, which is why a nearer ghost flashes brighter, bigger and turns
 *  further.  scl and rot are thousandths and tenths of a degree respectively.
 * ======================================================================== */
static int alp_tbl[3]  = {   33,  118,   75 };                  /* rdata 3a6450 */
static int scl_tbl[3]  = { 1000, 1080, 1010 };                  /* rdata 3a6460 */
static int rot_tbl[3]  = { 1800, 1820, 1804 };                  /* rdata 3a6470 */
static int ccol_tbl[3] = {   70,  150,   90 };                  /* rdata 3a6480 */
static int calp_tbl[3] = {   70,  150,   90 };                  /* rdata 3a6490 */

static reference_fixed_array<int, 3> alp(alp_tbl);              /* sbss 3f4b88 */
static reference_fixed_array<int, 3> scl(scl_tbl);              /* sbss 3f4b90 */
static reference_fixed_array<int, 3> rot(rot_tbl);              /* sbss 3f4b98 */
static reference_fixed_array<int, 3> ccol(ccol_tbl);            /* sbss 3f4ba0 */
static reference_fixed_array<int, 3> calp(calp_tbl);            /* sbss 3f4ba8 */

/* ==========================================================================
 *  Live state
 * ======================================================================== */

/* The per-ghost overlay records, and the single second-pass record. */
fixed_array<ENDMG1, 10> enedmg1;                                /* data 2e4a10 */
ENDMG2 enedmg2;                                                 /* data 2e5120 */
ENE_DMG_LARGE_HIT_CTRL EneDmgLargeHitCtrl;                      /* data 2e4fb0 */
ENE_HIT_EFFECT_CTRL EneHitEffectCtrl;                           /* data 2e5130 */
ENE_DMG_EFF ene_dmg_eff;                                        /* data 2e5180 */
SWORD_LINE sw_line;                                             /* data 2e5170 */

/* The second pass's sword-line ring and its particles. */
static fixed_array<TAIL_DMG2_DAT, 48> enedmg2_tail;             /* bss 423480 */
static fixed_array<NEW_PERTICLE, 48> new_perticle;              /* bss 42e880 */

/* The two burst lists.  EffectEndParticleList is registered on by nothing --
 * see EffectEndParticleEffectReq(). */
SINGLE_LINK_LIST EneParticleList;                               /* data 2e51d0 */
SINGLE_LINK_LIST EffectEndParticleList;                         /* data 2e51e0 */

/* How many particles reached the tray this frame; read and cleared by
 * CNEquipTrayWrk::Work() through EneDmgParticleSuctionNumGet(). */
static int EneDmgParticleSuctionNum;                            /* sbss 3f4bac */

int eneseal_status;                                             /* sdata 3efd98 */
int enedmg_status;                                              /* sdata 3efd9c */

/* The second pass's spread. */
float enedmg2_sp = 1.3f;                                        /* sdata 3efd5c */

/* The six phase durations of the damage-sprite ramp, in frames.  SEC0 is the
 * arming delay before the large hit is requested, SEC1..SEC5 the ramp legs. */
int SEC0 =  1;                                                  /* sdata 3efd60 */
int SEC1 =  2;                                                  /* sdata 3efd64 */
int SEC2 =  4;                                                  /* sdata 3efd68 */
int SEC3 = 10;                                                  /* sdata 3efd6c */
int SEC4 = 30;                                                  /* sdata 3efd70 */
int SEC5 = 15;                                                  /* sdata 3efd74 */

/* ==========================================================================
 *  Init
 * ======================================================================== */

/* 358 */
void InitEffectEne(void)
{
    int i;                                                      /* s0 */

    EneDmgLargeHitCtrlInit();                                   /* 361 */
    EneDmgParticleWorkInit();                                   /* 362 */
    EffectEndParticleWorkInit();                                /* 363 */
    EneDmgScreenWorkInit();                                     /* 364 */
    EneHitEffectCtrlInit();                                     /* 365 */

    for (i = 0; i < 10; i++)                                    /* 367 */
    {
        enedmg1[i].enedmg1_flg = 0;                             /* 369 */
    }

    enedmg2.enedmg2_flg = 0;                                    /* 370 */
    sw_line.sw  = 0;                                            /* 372 */
    sw_line.num = 1;                                            /* 373 */
    sw_line.top = 0;                                            /* 375 */
    enedmg_status  = 0;                                         /* 376 */
    eneseal_status = 0;                                         /* 377 */
    EneDmgParticleSuctionNum = 0;                               /* 379 */
}

/* 386 -- nothing to do; the EF half of the split init is empty for this
 * module, as it is for several other effect files. */
void InitEffectEneEF(void)                                      /* 387 */
{
}

/* ==========================================================================
 *  Helpers
 * ======================================================================== */

/* 394 -- one of the ten tracked points on a ghost's skeleton.
 *
 * Every case is the same subscript with a different member, so the whole
 * switch is attributed to fixed_array.h 124/125 and leaves no $LM of its own;
 * the ten case lines are interpolated into 399..408.  An out-of-range id
 * gives the zeroed static rather than faulting. */
static sceVu0FVECTOR *EfGetMpos(int eneno, int id)
{
    static sceVu0FVECTOR work;                                  /* data 2e5080 */

    switch (id)                                                 /* 397 */
    {
    case 0: return &ene_wrk[eneno].mpos.p0;                     /* 399 */
    case 1: return &ene_wrk[eneno].mpos.p1;                     /* 400 */
    case 2: return &ene_wrk[eneno].mpos.p2;                     /* 401 */
    case 3: return &ene_wrk[eneno].mpos.p3;                     /* 402 */
    case 4: return &ene_wrk[eneno].mpos.p4;                     /* 403 */
    case 5: return &ene_wrk[eneno].mpos.p5;                     /* 404 */
    case 6: return &ene_wrk[eneno].mpos.p6;                     /* 405 */
    case 7: return &ene_wrk[eneno].mpos.p7;                     /* 406 */
    case 8: return &ene_wrk[eneno].mpos.p8;                     /* 407 */
    case 9: return &ene_wrk[eneno].mpos.p9;                     /* 408 */
    }

    return &work;                                               /* 410 */
}                                                               /* 411 */

/* 417 -- the previous frame's damage.  The body is a bare `return 0;` (eight
 * bytes: jr ra + move v0,zero), so whatever it used to read out of
 * ene_wrk[eneno].st.dmg_old was cut before this build; the parameter is
 * unused.  Kept as found. */
static u_short EfGetDmgOld(int eneno)
{
    (void)eneno;

    return 0;                                                   /* 418 */
}

/* ==========================================================================
 *  The per-ghost damage overlay
 * ======================================================================== */

/* 448 -- the overlay's whole per-ghost job.
 *
 * Two camera-facing quads share one record.  Quad 0 is the flash on the ghost
 * itself: it holds for 15 frames at scale 1, then blooms over 40 frames while
 * its alpha falls from 80 to 0, and the frame it finishes it raises the second
 * overlay pass (SetEneDmgEffect2()).  Quad 1 is a slow spin behind it, 15
 * frames of hold and 40 of fade at a fixed scale, and its end clears the whole
 * record.  How far quad 0 blooms comes from the ghost's previous damage: a
 * weak shot reaches 1.6x and the heaviest 1.6 + 1.0.
 *
 * Both quads are then built as billboards *pulled towards the camera* --
 * `dist[]` is a per-quad offset along the eye ray, so the far one sits 200
 * units in front of the ghost and the near one 450 -- and drawn as a four-vertex
 * PACKED triangle strip with ST/RGBAQ/XYZF2 per vertex.  The first two vertices
 * carry ADC (bit 15 of the XYZF word) so they register without kicking.
 *
 * Only quad 1 is emitted: `st` is set to 1 the first time round the build loop
 * and the packet loop starts there, so quad 0's geometry and clip flags are
 * computed and thrown away.  That is the ROM's own arrangement -- quad 0 has no
 * texture entry in `textbl` either.
 *
 * Monochrome mode is forced off across the whole function and restored on the
 * way out; the `rgb` table below carries its own monochrome half, which is what
 * the inner EffWrkMonochroModeGet() calls read -- and they answer 0 for the
 * whole call, so the mono rows are dead in this build. */
static void SetEneDmgEffect1_Sub2(int num)
{                                                               /* 448 */
    /* .rodata 0x3a5ea8 -- a fourteen-entry SPRT_DAT table this file owns, held
       through a reference_fixed_array whose constructor runs on first entry.
       PORT DEVIATION: the ROM's table is in .rodata but effdat[] is the only
       reconstructed SPRT_DAT bank, and the two entries this reaches
       (textbl[1] + mono = 12, 13) are what `camdat` is indexed with.  Until the
       table itself is transcribed the effect_ene copy is the effdat window at
       the same indices, which is where SetSprFile2(0x1fa8000, 0) points. */
    static reference_fixed_array<SPRT_DAT, 14> camdat(effdat);  /* 465 */

    /* data 2e5090 -- [mono][quad][chance][rgb]. */
    static u_char rgb[2][2][3][3] =
    {
        {
            { { 128, 128, 128 }, { 255,  32,  32 }, { 255, 240, 255 } },
            { {  73, 138, 234 }, { 255,  80,  48 }, { 255, 250, 255 } }
        },
        {
            { { 128, 128, 128 }, { 106, 106, 106 }, { 106, 106, 106 } },
            { { 148, 148, 148 }, { 128, 128, 128 }, { 128, 128, 128 } }
        }
    };

    fixed_array<float[4], 4> wpos;
    float cpos[4];
    float rot_x;
    float rot_y;
    fixed_array<float, 4> scl;
    float wlm[4][4];
    float slm[4][4];
    fixed_array<int, 4>    clip;
    fixed_array<u_int, 4>  tw;
    fixed_array<u_int, 4>  th;
    fixed_array<u_long, 4> tex0;
    float ppos[4][4][4];
    /* PORT: the local->world corners of each quad, kept for the host draw in
       the packet loop -- wlm below is rebuilt on the second quad. */
    float wworld[2][4][4];
    sceVu0IVECTOR ivec[4][4];
    U32DATA ts[4][4];
    U32DATA tt[4][4];
    U32DATA tq[4][4];
    /* .rodata 0x3a6068..0x3a60b8, all copied into the frame. */
    float dist[4] = {  100.0f, -200.0f, -450.0f, -460.0f };     /* 508 */
    float bww[4]  = {  450.0f,   16.0f,   80.0f,   80.0f };     /* 509 */
    float bhh[4]  = {  450.0f,   16.0f,   80.0f,   80.0f };     /* 510 */
    float szw[4]  = {  256.0f,  256.0f,  128.0f,  128.0f };     /* 511 */
    float szh[4]  = {  256.0f,  256.0f,  128.0f,  128.0f };     /* 512 */
    static u_int camdat_tbl[4] = { 0, 12, 0, 0 };         /* .rodata 3a60b8 */
    reference_fixed_array<unsigned int, 4> textbl(camdat_tbl);
    int    st;
    int    mono;
    u_int  clpz2;
    ENDMG1 *dmg1;
    Q_WORDDATA  *pbuf;
    GRA3DCAMERA *pCam;
    int    i, j, k;
    int    ndpkt;
    float  f;

    st = 0;                                                     /* 489 */
    clpz2 = 0xffffff;                                           /* 527 */

    pCam = gra3dGetCamera();                                    /* 532 */
    float *cam_pos = gra3dcamGetPosition();                     /* 533 */

    if (EffWrkStopFlgGet() != 0)                                /* 540 */
    {
        return;
    }

    dmg1 = &enedmg1[num];

    if (dmg1->enedmg1_flg == 0)                                 /* 543 */
    {
        return;
    }

    mono = EffWrkMonochroModeGet();                             /* 547 */
    EffWrkMonochroModeSet(0);                                   /* 548 */

    if (dmg1->enedmg1_flg == 1)                                 /* 550 */
    {
        /* Both quads start on the ghost; EfGetMpos() is called twice, once per
           slot, rather than the result being reused. */
        g3dxVu0CopyVector(dmg1->wbpos[0], *EfGetMpos(dmg1->enedmg_no, 0)); /* 552 */
        g3dxVu0CopyVector(dmg1->wbpos[1], *EfGetMpos(dmg1->enedmg_no, 0)); /* 554 */

        for (i = 0; i < 2; i++)                                 /* 561 */
        {
            dmg1->scw[i]   = 0.0f;
            dmg1->sch[i]   = 0.0f;
            dmg1->alp[i]   = 0.0f;
            dmg1->flow[i]  = 0;
            dmg1->cnt[i]   = 0;
            dmg1->rot_z[0] = 0.0f;
        }

        enedmg_status = 0;                                      /* 562 */
        dmg1->enedmg1_flg = 2;                                  /* 563 */
    }

    /* 0.6 at 10 damage, 1.6 at 80 -- the extra bloom the flash gets. */
    i = EfGetDmgOld(dmg1->enedmg_no);                           /* 568 */
    if (i < 10) { i = 10; } else if (i > 80) { i = 80; }        /* 569 */
    f = (float)(i - 10) / 70.0f + 0.599999964f;                 /* 570 */

    /* ---- quad 0: hold, then bloom and fade ------------------------------ */
    if (dmg1->flow[0] == 1)                                     /* 572 */
    {
        dmg1->scw[0] = (float)dmg1->cnt[0] * f * 0.5f / 40.0f + 1.0f;
        dmg1->sch[0] = (float)dmg1->cnt[0] * f / 40.0f + 1.0f;
        dmg1->alp[0] = 80.0f - (float)dmg1->cnt[0] * 80.0f / 40.0f;

        if (dmg1->cnt[0] >= 40)
        {
            dmg1->flow[0]++;
            dmg1->cnt[0] = 0;
            dmg1->alp[0] = 0.0f;
        }
        else if (EffWrkStopFlgGet() == 0)                       /* 603 */
        {
            dmg1->cnt[0]++;
        }
    }
    else if (dmg1->flow[0] == 0)
    {
        dmg1->scw[0] = 1.0f;
        dmg1->sch[0] = 1.0f;
        dmg1->alp[0] = 0.0f;

        if (dmg1->cnt[0] >= 15)
        {
            dmg1->flow[0]++;
            dmg1->cnt[0] = 0;
            dmg1->alp[0] = 80.0f;
            SetEneDmgEffect2();                                 /* 588 */
        }
        else if (EffWrkStopFlgGet() == 0)                       /* 603 */
        {
            dmg1->cnt[0]++;
        }
    }

    /* ---- quad 1: the same shape, spinning, and it ends the record -------- */
    if (dmg1->flow[1] == 1)
    {
        dmg1->rot_z[1] += 0.034906581f;                 /* two degrees a frame */
        if (dmg1->rot_z[1] >= EFE_PI)
        {
            dmg1->rot_z[1] -= 6.28318501f;
        }

        dmg1->alp[1] = 64.0f - (float)dmg1->cnt[1] * 64.0f / 40.0f;

        if (dmg1->cnt[1] >= 40)
        {
            dmg1->flow[1]++;
            dmg1->cnt[1] = 0;
            dmg1->alp[1] = 0.0f;
        }
        else if (EffWrkStopFlgGet() == 0)
        {
            dmg1->cnt[1]++;
        }
    }
    else if (dmg1->flow[1] == 0)
    {
        dmg1->scw[1] = 1.0f;
        dmg1->sch[1] = 1.0f;
        dmg1->alp[1] = 0.0f;

        if (dmg1->cnt[1] >= 15)
        {
            dmg1->flow[1]++;
            dmg1->cnt[1] = 0;
            dmg1->alp[1] = 64.0f;
        }
        else if (EffWrkStopFlgGet() == 0)
        {
            dmg1->cnt[1]++;
        }
    }
    else if (dmg1->flow[1] == 2)
    {
        dmg1->enedmg1_flg = 0;
    }

    SetSprFile2(0x1fa8000, 0);                                  /* 643 */

    g3dxVu0CopyVector(cpos, cam_pos);
    cpos[3] = 1.0f;                                             /* 645 */

    for (i = 0; i < 2; i++)                                     /* 647 */
    {
        float dx = dmg1->wbpos[i][0] - cpos[0];                 /* 649 */
        float dy = dmg1->wbpos[i][1] - cpos[1];                 /* 650 */
        float dz = dmg1->wbpos[i][2] - cpos[2];                 /* 651 */
        float len = g3dxVu0Sqrt(dx * dx + dy * dy + dz * dz);

        /* Quad 1 grows with distance so the spin stays a constant size on
           screen; quad 0 does not. */
        if (i == 1)                                             /* 654 */
        {
            if (len < 200.0f)
            {
                scl[1] = 1.0f;
            }
            else if (len <= 2000.0f)
            {
                scl[1] = (len - 200.0f) * 24.0f / 1800.0f + 1.0f;
            }
            else
            {
                scl[1] = 25.0f;
            }
        }
        else
        {
            scl[i] = 1.0f;
        }

        ppos[i][0][0] = -bww[i] * scl[i];                       /* 661 */
        ppos[i][2][0] = -bww[i] * scl[i];
        ppos[i][1][0] =  bww[i] * scl[i];                       /* 662 */
        ppos[i][3][0] =  bww[i] * scl[i];
        ppos[i][0][1] = -bhh[i] * scl[i];                       /* 663 */
        ppos[i][1][1] = -bhh[i] * scl[i];
        ppos[i][2][1] =  bhh[i] * scl[i];                       /* 664 */
        ppos[i][3][1] =  bhh[i] * scl[i];
        ppos[i][0][2] = 0.0f; ppos[i][1][2] = 0.0f;             /* 665 */
        ppos[i][2][2] = 0.0f; ppos[i][3][2] = 0.0f;
        ppos[i][0][3] = 1.0f; ppos[i][1][3] = 1.0f;             /* 666 */
        ppos[i][2][3] = 1.0f; ppos[i][3][3] = 1.0f;

        /* Slide the billboard along the eye ray by dist[i]. */
        wpos[i][0] = cpos[0] + (dmg1->wbpos[i][0] - cpos[0]) * (len + dist[i]) / len;
        wpos[i][1] = cpos[1] + (dmg1->wbpos[i][1] - cpos[1]) * (len + dist[i]) / len;
        wpos[i][2] = cpos[2] + (dmg1->wbpos[i][2] - cpos[2]) * (len + dist[i]) / len;
        wpos[i][3] = 1.0f;
    }                                                           /* 673 */

    Get2PosRot(cam_pos, pCam->vTarget, &rot_x, &rot_y);         /* 675 */

    for (i = 0; i < 2; i++)                                     /* 677 */
    {
        sceVu0UnitMatrix(wlm);                                  /* 680 */
        wlm[0][0] = dmg1->scw[i];                               /* 681 */
        wlm[1][1] = dmg1->sch[i];                               /* 682 */
        wlm[2][2] = 25.0f;                                      /* 683 */
        sceVu0RotMatrixZ(wlm, wlm, dmg1->rot_z[i]);             /* 684 */
        sceVu0RotMatrixY(wlm, wlm, rot_y);                      /* 685 */
        sceVu0TransMatrix(wlm, wlm, wpos[i]);                   /* 686 */
        sceVu0MulMatrix(slm, pCam->matWorldScreen, wlm);        /* 687 */

        /* PORT: wlm is the local->world half of slm, so the same four corners
           in world space are what the host draw needs. */
        for (j = 0; j < 4; j++)
        {
            sceVu0ApplyMatrix(wworld[i][j], wlm, ppos[i][j]);
        }

        sceVu0RotTransPersN(ivec[i], slm, ppos[i], 4, 1);       /* 690 */

        if (i == 0)                                             /* 701 */
        {
            st = 1;                                             /* 703 */
        }
        else
        {
            /* The monochrome twin sits one slot up, the effdat convention. */
            tex0[i] = camdat[textbl[i] + EffWrkMonochroModeGet()].tex0;
            tw[i]   = camdat[textbl[i] + EffWrkMonochroModeGet()].w;
            th[i]   = camdat[textbl[i] + EffWrkMonochroModeGet()].h;
        }

        clip[i] = 1;                                            /* 724 */

        for (j = 0; j < 4; j++)
        {
            /* Same guard band as DrawNewPerticleSub, one step wider. */
            if ((u_int)ivec[i][j][0] < 0x300 || (u_int)ivec[i][j][0] > 0xfd00) /* 726 */
            {
                clip[i] = 0;
            }
            if ((u_int)ivec[i][j][1] < 0x300 || (u_int)ivec[i][j][1] > 0xfd00) /* 727 */
            {
                clip[i] = 0;
            }
            if ((u_int)ivec[i][j][2] == 0 || (u_int)ivec[i][j][2] > clpz2) /* 728 */
            {
                clip[i] = 0;
            }

            /* Perspective-correct STQ: Q is 1/w and S/T are premultiplied. */
            tq[i][j].fl32 = 1.0f / (float)ivec[i][j][3];        /* 730 */
            ts[i][j].fl32 = (float)tw[i] * tq[i][j].fl32 / szw[i]; /* 731 */
            tt[i][j].fl32 = (float)th[i] * tq[i][j].fl32 / szh[i]; /* 732 */
        }                                                       /* 733 */
    }                                                           /* 735 */

    /* .rodata 0x3a60c8, copied into the frame.  ALPHA 0x44 is (Cs-Cd)*As+Cd. */
    DRAW_ENV_5 de =                                             /* 738 */
    {
        0x0000000000000044ULL,      /* ALPHA                                   */
        0x0000000000000161ULL,      /* TEX1                                    */
        0x0000000000000000ULL,      /* CLAMP  repeat                           */
        0x000000000005000dULL,      /* TEST   ATST GEQUAL/0, ZTST GEQUAL       */
        0x000000010a000118ULL       /* ZBUF   PSMZ24 @0x118, ZMSK              */
    };

    SetDrawEnv(0, &de);                                         /* 745 */

    pbuf = StartDmaDirectTrans();                               /* 748 */
    Reserve2DPacket(0x10);                                      /* 750 */

    pbuf[0].ui32[0] = 0; pbuf[0].ui32[1] = 0;                   /* 753 */
    pbuf[0].ui32[2] = 0; pbuf[0].ui32[3] = 0;
    pbuf[1].ul64[0] = 0x1000000000008001ULL;                    /* 755 */
    pbuf[1].ul64[1] = 0x0e;                 /* A+D */           /* 756 */
    pbuf[2].ul64[0] = 0;                                        /* 758 */
    pbuf[2].ul64[1] = 0x3f;                 /* TEXFLUSH */      /* 759 */

    ndpkt = 3;                                                  /* 759 */

    for (i = st; i < 2; i++)                                    /* 762 */
    {
        if (clip[i] == 0)
        {
            continue;
        }

        /* PORT: the packet built below is inert -- dmaVif1 discards it -- so
           the same quad goes to the renderer from its world-space corners.
           ppos is laid out TL, TR, BL, BR and the ROM's ST resolves to
           (0,0) (tw/szw,0) (0,th/szh) (tw/szw,th/szh), which is exactly
           RendererPacket3D's sub-rectangle: unlike the candle flame's quad,
           the rectangle form is the right one here.  The draw env above is
           ZTST GEQUAL with ZMSK set, which is what RendererPacket3D queues. */
        {
            const int chance = dmg1->enedmg_chance;
            const int m      = EffWrkMonochroModeGet();

            RendererPacket3D(wworld[i], 4,
                             rgb[m][i][chance][0],
                             rgb[m][i][chance][1],
                             rgb[m][i][chance][2],
                             (int)dmg1->alp[i],
                             0.0f, 0.0f, (float)tw[i], (float)th[i],
                             szw[i], szh[i],
                             (const sceGsTex0 *)&tex0[i]);
        }

        pbuf[ndpkt].ul64[0] = 0x1000000000008001ULL;            /* 764 */
        pbuf[ndpkt].ul64[1] = 0x0e;             /* A+D */       /* 765 */
        pbuf[ndpkt + 1].ul64[0] = tex0[i];                      /* 767 */
        pbuf[ndpkt + 1].ul64[1] = 0x06;         /* TEX0_1 */    /* 768 */

        /* PACKED, NLOOP 4, NREG 3 (ST, RGBAQ, XYZF2); PRIM 0x54 is
           TRIANGLE_STRIP with TME and ABE, flat-shaded. */
        pbuf[ndpkt + 2].ul64[0] = 0x302a400000008004ULL;        /* 770 */
        pbuf[ndpkt + 2].ul64[1] = 0x412;                        /* 771 */
        ndpkt += 3;

        for (j = 0; j < 4; j++)                                 /* 773 */
        {
            /* The quad's four corners take (0,0) (1,0) (0,1) (1,1) of the
               texture, so S is present on the odd columns and T on the
               bottom row. */
            pbuf[ndpkt].ui32[0] = (j % 2 != 0) ? ts[i][j].ui32 : 0; /* 774 */
            pbuf[ndpkt].ui32[1] = (j / 2 != 0) ? tt[i][j].ui32 : 0; /* 775 */
            pbuf[ndpkt].ui32[2] = tq[i][j].ui32;                /* 776 */
            pbuf[ndpkt].ui32[3] = 0;                            /* 777 */

            k = dmg1->enedmg_chance;
            pbuf[ndpkt + 1].ui32[0] = rgb[EffWrkMonochroModeGet()][i][k][0]; /* 779 */
            pbuf[ndpkt + 1].ui32[1] = rgb[EffWrkMonochroModeGet()][i][k][1]; /* 780 */
            pbuf[ndpkt + 1].ui32[2] = rgb[EffWrkMonochroModeGet()][i][k][2]; /* 781 */
            pbuf[ndpkt + 1].ui32[3] = (u_int)dmg1->alp[i];      /* 782 */

            pbuf[ndpkt + 2].ui32[0] = (u_int)ivec[i][j][0];     /* 784 */
            pbuf[ndpkt + 2].ui32[1] = (u_int)ivec[i][j][1];     /* 785 */
            pbuf[ndpkt + 2].ui32[2] = (u_int)ivec[i][j][2] << 4; /* 786 */
            /* ADC on the first two: registered, not kicked -- a strip needs
               two vertices down before the third completes a triangle. */
            pbuf[ndpkt + 2].ui32[3] = (j < 2) ? (1 << 15) : 0;  /* 787 */

            ndpkt += 3;                                         /* 788 */
        }
    }

    EndDmaDirectTrans(&pbuf[ndpkt]);                            /* 790 */

    EffWrkMonochroModeSet(mono);                                /* 792 */
}                                                               /* 793 */

/* 799 */
void SetEneDmgEffect1_Sub(void)
{
    for (int i = 0; i < 10; i++)                                /* 801 */
    {
        SetEneDmgEffect1_Sub2(i);                               /* 803 */
    }
}

/* 823 -- one sword-line particle of the second overlay pass.
 *
 * A NEW_PERTICLE travels from bpos1 to bpos2 while spiralling about that line,
 * trailing a twelve-slot ring of world matrices (wmtxp) and positions (oposp)
 * behind it.  The tail is drawn first -- from the ring as it stands, three
 * points wide, tapering from 8 units at the head to nothing at the tail -- and
 * only then does the particle step, so what is on screen is always one frame
 * behind the position being computed.
 *
 * `cnt` is the flight clock, 0 .. 180, and the whole path comes off it:
 *
 *   - position, through a versine of cnt/180 -- fast in the middle, easing at
 *     both ends -- reaching r1l (a sixth of the way) at cnt 90 and r1l + r2l
 *     (all the way) at 180;
 *   - the sideways swing, np->y, from a half-period sine of the same shape
 *     scaled by np->n;
 *   - the roll about the line, np->rot plus 135 degrees of np->rotp spread over
 *     the whole flight;
 *   - and the clock's own speed, six authored bands off np->xp, so the particle
 *     leaps out, coasts and then crawls to a stop.
 *
 * EffWrkStopFlgGet() gates only the *stepping*: a frozen particle still draws.
 */
static void SetEneDmgEffect2_Sub2(NEW_PERTICLE *np, float *bpos1, float *bpos2,
                                  u_char r1, u_char g1, u_char b1,
                                  u_char r2, u_char g2, u_char b2)
{                                                               /* 823 */
    fixed_array<int, 10>      tbl;
    fixed_array<float[4], 30> wwpos;
    float bpos3[4];
    /* .rodata 0x3a60f0 / 0x3a6100, copied into the frame -- local aggregate
       initialisers.  wpos's y values are overwritten per tail segment below. */
    float opos1[4]   = { 0.0f, 100.0f, 0.0f, 1.0f };            /* 837 */
    float wpos[3][4] =                                          /* 838 */
    {
        {  5.0f, 0.0f, 0.0f, 1.0f },
        {  0.0f, 0.0f, 0.0f, 1.0f },
        { -5.0f, 0.0f, 0.0f, 1.0f }
    };
    fixed_array<float[4], 3> wkpos;
    float rot[4];
    float wlm[4][4];
    float rottt[4];
    int   i;
    int   n;
    float fx, fy, fz;
    float f1, f2;
    float rot_z;
    float r1l, r2l;
    GRA3DCAMERA *pCam;
    float Length;               /* no stab: a float local */

    pCam = gra3dGetCamera();                                    /* 845 */
    float *cam_pos = gra3dcamGetPosition();                     /* 846 */

    fx = bpos2[0] - bpos1[0];                                   /* 849 */
    fy = bpos2[1] - bpos1[1];                                   /* 850 */
    fz = bpos2[2] - bpos1[2];                                   /* 851 */

    Length = g3dxVu0Sqrt(fx * fx + fy * fy + fz * fz);          /* 853 */

    r1l = Length / 6.0f;                                        /* 855 */
    r2l = Length * 5.0f / 6.0f;                                 /* 856 */

    n = np->top;                                                /* 858 */
    for (i = 0; i < 12; i++)                                    /* 859 */
    {
        tbl[i] = n;

        n--; if (n < 0) { n = 11; }                             /* 861 */
    }                                                           /* 862 */

    /* Dead, exactly as in EneDmgParticleSuctionTail(): rot is measured, wrapped
       and never read -- the frame stored below faces the camera instead. */
    GetTrgtRot(np->oposp[tbl[1]], np->npos, rot, 3);            /* 864 */

    while (rot[0] <  -EFE_PI) { rot[0] += 6.28318501f; }        /* 866 */
    while (rot[0] >=  EFE_PI) { rot[0] -= 6.28318501f; }        /* 867 */
    while (rot[1] <  -EFE_PI) { rot[1] += 6.28318501f; }        /* 868 */
    while (rot[1] >=  EFE_PI) { rot[1] -= 6.28318501f; }        /* 869 */

    GetTrgtRot(cam_pos, pCam->vTarget, rottt, 3);               /* 871 */

    sceVu0UnitMatrix(wlm);                                      /* 873 */
    sceVu0RotMatrixY(wlm, wlm, rottt[1]);                       /* 874 */
    sceVu0RotMatrixX(wlm, wlm, rottt[0]);                       /* 875 */
    sceVu0TransMatrix(wlm, wlm, np->npos);                      /* 877 */

    sceVu0CopyMatrix(np->wmtxp[tbl[0]], wlm);                   /* 879 */

    n = 0;
    for (i = 0; i < np->num; i++)                               /* 881 */
    {
        wpos[0][1] =  (float)(np->num - i - 1) * 8.0f / (float)np->num; /* 882 */
        wpos[2][1] = -(float)(np->num - i - 1) * 8.0f / (float)np->num; /* 883 */

        sceVu0ApplyMatrix(wwpos[n * 3 + 0], np->wmtxp[tbl[i]], wpos[0]);
        sceVu0ApplyMatrix(wwpos[n * 3 + 1], np->wmtxp[tbl[i]], wpos[1]);
        sceVu0ApplyMatrix(wwpos[n * 3 + 2], np->wmtxp[tbl[i]], wpos[2]);
        n++;
    }                                                           /* 887 */

    DrawNewPerticleSub(n, &wwpos[0], r1, g1, b1, r2, g2, b2, 0x30); /* 889 */

    if (EffWrkStopFlgGet() != 0)                                /* 898 */
    {
        return;
    }

    if (np->cnt < 180.0f)                                       /* 899 */
    {
        np->top = (short)((np->top + 1) % 12);                  /* 900 */
        np->num = (short)((np->num + 1 > 12) ? 12 : np->num + 1); /* 901 */
    }
    else
    {
        np->num = (short)((np->num - 1 < 0) ? 0 : np->num - 1); /* 903 */
    }

    if (np->cnt < 90.0f)                                        /* 906 */
    {
        /* The sine is evaluated once in the ROM; `s` is the port's local. */
        float s = sinf(np->cnt * EFE_PI / 180.0f);
        f2 = g3dxVu0Sqrt(1.0f - (1.0f - s) * (1.0f - s));       /* 907 */

        f1    = f2 * r1l;                                       /* 909 */
        rot_z = f2 * EFE_PI;                                    /* 910 */
    }
    else
    {
        float s = sinf(np->cnt * EFE_PI / 180.0f);
        f2 = 1.0f - g3dxVu0Sqrt(1.0f - (1.0f - s) * (1.0f - s)); /* 912 */

        f1    = f2 * r2l + r1l;                                 /* 914 */
        rot_z = (f2 + 1.0f) * EFE_PI;                           /* 915 */
    }

    np->x = f1;                                                 /* 916 */
    np->y = sinf(rot_z * 0.5f) * np->n;                         /* 917 */

    bpos3[0] = bpos1[0] + (bpos2[0] - bpos1[0]) * np->x / Length; /* 918 */
    bpos3[1] = bpos1[1] + (bpos2[1] - bpos1[1]) * np->x / Length; /* 919 */
    bpos3[2] = bpos1[2] + (bpos2[2] - bpos1[2]) * np->x / Length; /* 920 */
    bpos3[3] = 1.0f;                                            /* 921 */

    opos1[0] = 0.0f;                                            /* 923 */
    opos1[1] = np->y;                                           /* 924 */
    opos1[2] = 0.0f;                                            /* 925 */
    opos1[3] = 1.0f;                                            /* 926 */

    rot_z = np->rot + np->rotp * np->x * 135.0f / (r1l + r2l);  /* 928 */

    while (rot_z < -EFE_PI) { rot_z += 6.28318501f; }           /* 929 */
    while (rot_z >  EFE_PI) { rot_z -= 6.28318501f; }           /* 930 */

    sceVu0UnitMatrix(wlm);                                      /* 931 */
    sceVu0RotMatrixZ(wlm, wlm, rot_z);                          /* 932 */
    sceVu0ApplyMatrix(opos1, wlm, opos1);                       /* 933 */

    GetTrgtRot(bpos1, bpos2, rot, 3);                           /* 935 */

    sceVu0UnitMatrix(wlm);                                      /* 936 */
    sceVu0RotMatrixX(wlm, wlm, rot[0]);                         /* 937 */
    sceVu0RotMatrixY(wlm, wlm, rot[1]);                         /* 938 */
    sceVu0TransMatrix(wlm, wlm, bpos3);                         /* 939 */

    sceVu0ApplyMatrix(wkpos[0], wlm, opos1);                    /* 941 */

    if (np->cnt < 40.0f)                                        /* 943 */
    {
        np->cnt += np->xp * 3.0f * enedmg2_sp;                  /* 944 */
    }
    else if (np->cnt < 70.0f)                                   /* 945 */
    {
        np->cnt += np->xp * 10.0f * enedmg2_sp;                 /* 946 */
    }
    else if (np->cnt < 90.0f)                                   /* 947 */
    {
        np->cnt += np->xp * 15.0f * enedmg2_sp;                 /* 948 */
    }
    else if (np->cnt < 105.0f)                                  /* 949 */
    {
        np->cnt += (np->xp + np->xp) * enedmg2_sp;              /* 950 */
    }
    else if (np->cnt < 120.0f)                                  /* 951 */
    {
        np->cnt += np->xp * enedmg2_sp;                         /* 952 */
    }
    else if (np->cnt < 180.0f)                                  /* 953 */
    {
        np->cnt += np->xp * 0.5f * enedmg2_sp;                  /* 954 */

        if (np->cnt >= 180.0f)                                  /* 955 */
        {
            np->cnt = 180.0f;                                   /* 960 */
        }
    }
    else
    {
        np->cnt = 180.0f;                                       /* 960 */
    }

    g3dxVu0CopyVector(np->oposp[np->top], np->npos);            /* 963 */
    g3dxVu0CopyVector(np->npos, wkpos[0]);                      /* 964 */
}

/* 970 -- the second overlay pass: the spirit strands the camera pulls out.
 *
 * On the frame enedmg2_flg turns 1 this seeds `nyoro_num` NEW_PERTICLEs at the
 * ghost and steps each one frame, so they start already in flight; every frame
 * after that it only draws and steps them.  How many there are is the ghost's
 * previous damage, clamped to 10..80 and mapped through
 * (dmg*40 - 400) / 70 + 4, i.e. 4 strands for a weak shot and 40 for the
 * heaviest.
 *
 * Where they fly *to* depends on the player's mode: in mode 6 (the finder up)
 * it is a point in front of the camera, otherwise a point in front of the
 * player -- and the camera's own FOV scales the finder one, so a zoomed lens
 * pulls them to a tighter spot.
 *
 * The two ints at the bottom are the pass's own end condition: `k` counts
 * strands still drawing tails and `j` remembers the last one whose clock ran
 * out.  Once something has finished and nothing is still trailing, the pass
 * reports 2 (done) and clears its flag; while both are true it reports 1.
 *
 * Monochrome mode is forced off for the whole pass and restored on the way
 * out, so the strands keep their colour even in a monochrome scene. */
void SetEneDmgEffect2_Sub(void)
{                                                               /* 970 */
    int fl;
    /* .rodata 0x3a6130 / 0x3a6148: three chance grades, each holding two
       colours -- [0..2] the strand and [4..6] the bright core, with [3]/[7] the
       alphas SetEneDmgEffect2_Sub2 is not given. */
    u_char rgb1[3][8] =                                         /* 972 */
    {
        { 0x80, 0xa0, 0xff, 0x48, 0x40, 0x60, 0x90, 0x80 },
        { 0xff, 0x32, 0x32, 0x48, 0x90, 0x20, 0x20, 0x80 },
        { 0xff, 0xef, 0xff, 0x48, 0x90, 0x88, 0x90, 0x80 }
    };
    u_char rgb2[3][8] =                                         /* 976 */
    {
        { 0x90, 0xb0, 0xff, 0x00, 0x40, 0x60, 0x90, 0x00 },
        { 0xff, 0x48, 0x48, 0x00, 0x90, 0x20, 0x20, 0x00 },
        { 0xff, 0xef, 0xff, 0x00, 0x90, 0x88, 0x90, 0x00 }
    };
    int   mono;
    int   c;
    int   i;
    int   j;
    int   k;
    int   nyoro_num;
    float rot_z;
    float span1;
    float span2;
    float r1l;
    float r2l;
    /* The strand's origin, kept between frames: a function-local static, which
       is why globals.txt does not list it. */
    static float bpos1[4];                                      /* bss 423470 */
    float bpos2[4];
    float opos1[4] = { 0.0f, 100.0f, 0.0f, 1.0f };              /* 995 */
    float rot[4];
    float wpos[4];
    float ppp2[4] = { -45.0f,  24.0f, 80.0f, 1.0f };            /* 997 */
    float ppp[4]  = { -22.0f, -690.0f, 80.0f, 1.0f };           /* 998 */
    float wlm[4][4];
    GRA3DCAMERA *pCam;
    DRAW_ENV de;
    float rot_x;
    float rot_y;
    float f;
    float Length;               /* no stab: a float local */

    pCam = gra3dGetCamera();                                    /* 1001 */
    float *cam_pos = gra3dcamGetPosition();                     /* 1002 */

    if (EffWrkStopFlgGet() != 0)                                /* 1006 */
    {
        return;
    }

    if (enedmg2.enedmg2_flg == 0)                               /* 1008 */
    {
        return;
    }

    ppp2[0] *= pCam->fFov / 0.89f;                              /* 1011 */
    ppp2[1] *= pCam->fFov / 0.89f;                              /* 1012 */

    mono = EffWrkMonochroModeGet();                             /* 1014 */
    EffWrkMonochroModeSet(0);                                   /* 1015 */

    fl = 0;
    if (enedmg2.enedmg2_flg == 1)                               /* 1017 */
    {
        g3dxVu0CopyVector(bpos1, *EfGetMpos(enedmg2.enedmg_no, 0)); /* 1019 */
        enedmg2.enedmg2_flg = 2;                                /* 1020 */
        fl = 1;                                                 /* 1021 */
    }

    c = enedmg2.enedmg_chance;                                  /* 1025 */

    nyoro_num = EfGetDmgOld(enedmg2.enedmg_no);                 /* 1027 */

    if (nyoro_num < 10) { nyoro_num = 10; } else if (nyoro_num > 80) { nyoro_num = 80; } /* 1028 */

    nyoro_num = (nyoro_num * 5 * 8 - 400) / 70 + 4;             /* 1029 */

    sceVu0UnitMatrix(wlm);                                      /* 1032 */

    if (plyr_wrk.cmn_wrk.mode != 6)                             /* 1033 */
    {
        sceVu0RotMatrixX(wlm, wlm, plyr_wrk.cmn_wrk.mbox.rot[0]); /* 1034 */
        sceVu0RotMatrixY(wlm, wlm, plyr_wrk.cmn_wrk.mbox.rot[1]); /* 1035 */
        sceVu0TransMatrix(wlm, wlm, plyr_wrk.cmn_wrk.mbox.pos); /* 1036 */
        sceVu0ApplyMatrix(bpos2, wlm, ppp);                     /* 1037 */
    }
    else
    {
        GetTrgtRot(cam_pos, pCam->vTarget, rot, 3);             /* 1039 */
        sceVu0RotMatrixX(wlm, wlm, rot[0]);                     /* 1040 */
        sceVu0RotMatrixY(wlm, wlm, rot[1]);                     /* 1041 */
        sceVu0TransMatrix(wlm, wlm, cam_pos);                   /* 1042 */
        sceVu0ApplyMatrix(bpos2, wlm, ppp2);                    /* 1043 */
    }

    Length = g3dxVu0Sqrt((bpos2[0] - bpos1[0]) * (bpos2[0] - bpos1[0])  /* 1047 */
                       + (bpos2[1] - bpos1[1]) * (bpos2[1] - bpos1[1])  /* 1048 */
                       + (bpos2[2] - bpos1[2]) * (bpos2[2] - bpos1[2])); /* 1049 */

    r1l   = Length * 1.0f / 6.0f;                               /* 1053 */
    r2l   = Length * 5.0f / 6.0f;                               /* 1054 */
    span1 = Length / 10.0f;                                     /* 1057 */
    span2 = span1 + 50.0f;                                      /* 1058 */

    if (fl != 0)                                                /* 1060 */
    {
        for (i = 0; i < nyoro_num; i++)                         /* 1061 */
        {
            NEW_PERTICLE *np = &new_perticle[i];

            g3dxVu0CopyVector(np->npos, bpos1);                 /* 1063 */

            np->wmtxp = enedmg2_tail[i].wmtx.data();            /* 1065 */
            np->oposp = enedmg2_tail[i].opos.data();            /* 1066 */

            np->rot  = EffectGetRandom(0.0f, 6.28318501f) - EFE_PI;   /* 1067 */
            np->rotp = EffectGetRandom(0.0f, 0.034906581f) - 0.0087266452f; /* 1068 */
            np->n    = EffectGetRandom(0.0f, span1) + span2;    /* 1069 */
            np->xp   = EffectGetRandom(0.0f, 0.299999982f) + 0.9f; /* 1070 */

            np->cnt  = 3.0f;                                    /* 1071 */
            np->time = 1;                                       /* 1073 */
            np->top  = 1;                                       /* 1074 */
            np->num  = 1;                                       /* 1075 */

            /* The same versine SetEneDmgEffect2_Sub2 uses, run once here so a
               strand's first drawn frame is already off the ghost.  cnt is 3,
               so GCC folded the whole argument to sinf into 0.0523598716. */
            float s = sinf(np->cnt * EFE_PI / 180.0f);
            f = g3dxVu0Sqrt(1.0f - (1.0f - s) * (1.0f - s));    /* 1077 */

            np->x = f * r1l;                                    /* 1079 */
            np->y = sinf(f * EFE_PI * 0.5f) * np->n;            /* 1080 */

            wpos[0] = bpos1[0] + (bpos2[0] - bpos1[0]) * np->x / Length; /* 1082 */
            wpos[1] = bpos1[1] + (bpos2[1] - bpos1[1]) * np->x / Length; /* 1083 */
            wpos[2] = bpos1[2] + (bpos2[2] - bpos1[2]) * np->x / Length; /* 1084 */
            wpos[3] = 1.0f;                                     /* 1085 */

            opos1[0] = 0.0f;                                    /* 1087 */
            opos1[1] = np->y;                                   /* 1088 */
            opos1[2] = 0.0f;                                    /* 1089 */
            opos1[3] = 1.0f;                                    /* 1090 */

            rot_z = np->rot + np->rotp * np->x * 135.0f / (r1l + r2l); /* 1092 */

            while (rot_z < -EFE_PI) { rot_z += 6.28318501f; }   /* 1093 */
            while (rot_z >  EFE_PI) { rot_z -= 6.28318501f; }   /* 1094 */

            sceVu0UnitMatrix(wlm);                              /* 1095 */
            sceVu0RotMatrixZ(wlm, wlm, rot_z);                  /* 1096 */
            sceVu0ApplyMatrix(opos1, wlm, opos1);               /* 1097 */

            GetTrgtRot(bpos1, bpos2, rot, 3);                   /* 1099 */

            sceVu0UnitMatrix(wlm);                              /* 1100 */
            sceVu0RotMatrixX(wlm, wlm, rot[0]);                 /* 1101 */
            sceVu0RotMatrixY(wlm, wlm, rot[1]);                 /* 1102 */
            sceVu0TransMatrix(wlm, wlm, wpos);                  /* 1103 */

            sceVu0ApplyMatrix(wpos, wlm, opos1);                /* 1105 */

            np->cnt += np->xp * enedmg2_sp;                     /* 1106 */

            g3dxVu0CopyVector(np->oposp[0], np->npos);          /* 1108 */
            g3dxVu0CopyVector(np->npos, wpos);                  /* 1109 */
        }                                                       /* 1110 */
    }

    j = -1;
    k = -1;

    for (i = 0; i < nyoro_num; i++)                             /* 1114 */
    {
        /* `time` is an int, compared against a float literal -- the ROM emits
           cvt.s.w and c.eq.s, so the comparison really is in float. */
        if (new_perticle[i].time != 1.0f)                       /* 1116 */
        {
            continue;
        }

        if (new_perticle[i].num > 0)                            /* 1118 */
        {
            SetEneDmgEffect2_Sub2(&new_perticle[i], bpos1, bpos2,
                                  rgb2[c][0], rgb2[c][1], rgb2[c][2],
                                  rgb2[c][4], rgb2[c][5], rgb2[c][6]); /* 1120 */
            k++;                                                /* 1121 */
        }

        if (new_perticle[i].cnt >= 180.0f)                      /* 1123 */
        {
            j = i;
            continue;
        }

        /* .rodata 0x3a6190 -- the same image EneDmgParticleOneDraw uses. */
        de.tex1  = 0x0000000000000161ULL;                       /* 1126 */
        de.alpha = 0x0000000000000048ULL;
        de.zbuf  = 0x000000010a000118ULL;
        de.test  = 0x000000000005000dULL;
        de.clamp = 0x0000000000000000ULL;
        de.prim  = 0x302a400000008004ULL;

        Get2PosRot(cam_pos, pCam->vTarget, &rot_x, &rot_y);     /* 1136 */

        f = 1.0f;
        sceVu0UnitMatrix(wlm);                                  /* 1139 */
        sceVu0RotMatrixX(wlm, wlm, rot_x);                      /* 1140 */
        sceVu0RotMatrixY(wlm, wlm, rot_y);                      /* 1141 */
        sceVu0TransMatrix(wlm, wlm, new_perticle[i].npos);      /* 1142 */

        if (plyr_wrk.cmn_wrk.mode != 6)                         /* 1144 */
        {
            /* Outside the finder the head fades as the strand's clock runs;
               with the finder up it stays at full brightness. */
            f = 1.0f - new_perticle[i].cnt / 180.0f;
        }

        Set3DPosTexure(wlm, &de, 0xc, 72.0f, 72.0f,
                       rgb1[c][0], rgb1[c][1], rgb1[c][2],
                       (u_char)(u_int)(f * 16.0f));             /* 1150 */
        Set3DPosTexure(wlm, &de, 0x5c, 20.0f, 20.0f,
                       rgb1[c][4], rgb1[c][5], rgb1[c][6],
                       (u_char)(u_int)(f * 110.0f));            /* 1152 */
    }                                                           /* 1156 */

    if (j != -1)                                                /* 1160 */
    {
        enedmg_status = 1;                                      /* 1166 */

        if (k == -1)
        {
            enedmg_status = 2;                                  /* 1167 */
            enedmg2.enedmg2_flg = 0;                            /* 1168 */
        }
    }

    EffWrkMonochroModeSet(mono);                                /* 1170 */
}                                                               /* 1171 */

/* 1176 -- arms the second overlay pass.  Raised from SetEneDmgEffect1_Sub2()
 * the frame the first pass's leading quad finishes its ramp. */
void SetEneDmgEffect2(void)
{
    enedmg2.enedmg2_flg = 1;                                    /* 1177 */
}

/* ==========================================================================
 *  Work init
 * ======================================================================== */

/* Both lists carry a whole ENEDMG_P_WRK per cell -- SingleLinkListAddEnd()
 * byte-copies the caller's stack copy into a fresh EFFECT_MALLOC block, so a
 * burst costs 0xf50 plus the cell header. */

/* 1265 */
static void EneDmgParticleWorkInit(void)
{
    SingleLinkListInit(&EneParticleList, sizeof(ENEDMG_P_WRK));  /* 1266 */
}

/* 1273 */
static void EffectEndParticleWorkInit(void)
{
    SingleLinkListInit(&EffectEndParticleList, sizeof(ENEDMG_P_WRK)); /* 1274 */
}

/* 1281 */
static void EneDmgScreenWorkInit(void)
{
    memset(&ene_dmg_eff, 0, sizeof(ENE_DMG_EFF));               /* 1282 */
}

/* 1304 */
static void EneHitEffectCtrlInit(void)
{
    memset(&EneHitEffectCtrl, 0, sizeof(ENE_HIT_EFFECT_CTRL));  /* 1305 */
}

/* ==========================================================================
 *  Requests
 * ======================================================================== */

/* 1316 -- start the screen-wide half of a hit: blur, contrast, camera shake
 * and pad rumble, all off one preset picked by label.
 *
 * There is only one slot, so a second hit inside the ramp restarts it. */
void EneHitEffectReq(int EneWrkNo, float *EneMposP0, int HitEffectLabel)
{
    g3dxVu0CopyVector(EneHitEffectCtrl.EneMposP0, EneMposP0);    /* 1317 */
    EneHitEffectCtrl.EneWrkNo = EneWrkNo;                        /* 1319 */

    EneHitEffectCtrl.pBlurContrast =
        EffEneDmgLargeHitBlurParameterPtrGet(HitEffectLabel);    /* 1324 */
    EneHitEffectCtrl.HitEffecType = HitEffectLabel;              /* 1326 */
    EneHitEffectCtrl.Flow = 1;                                   /* 1327 */

    EneHitEffectCtrl.Counter       = 0;                          /* 1328 */
    EneHitEffectCtrl.ContrastColor = 0;                          /* 1329 */
    EneHitEffectCtrl.ContrastAlpha = 0;                          /* 1330 */
    EneHitEffectCtrl.BlurScale     = 0;                          /* 1331 */
    EneHitEffectCtrl.BlurRot       = 0;                          /* 1332 */
    EneHitEffectCtrl.BlurAlpha     = 0;                          /* 1333 */
}

/* 1388 -- PORT NAME.  This helper and EneHitEffectBlurContrastCalc() below are
 * two `static` functions the ROM defines between EneHitEffectReq (…1333) and
 * EneHitEffectMain (1464…) and calls exactly once each, so GCC inlined both and
 * emitted no out-of-line copy: neither ZERO2.MAP nor functions.txt names them.
 * What is measured is that they exist and where they were -- EneHitEffectMain's
 * $LM run drops to 1390..1456 and 1340..1382 in the middle of its own
 * 1464..1524, which is [[out-of-order-line-numbers-mean-inlined-static]].  The
 * two names here are the port's; the line annotations are the ROM's.
 *
 * This one is the hit's *object-side* half, run on one frame only: clear both
 * flash slots, then raise whatever the hit label calls for.  The three ordinary
 * shot grades go straight to EneDmgLargeHitReq() -- SP as an A/B pair, which is
 * the only place in the build a pair is raised -- and each of the seventeen
 * sub-function labels goes to its own IgEffectSubFunc*Req() instead, with the
 * shutter-chance grade (0 / 1 SC / 2 SP) as the second argument. */
static void EneHitEffectSubFuncReq(void)
{
    int EneWrkNo;
    int HitEffectLabel;

    EneWrkNo       = EneHitEffectCtrl.EneWrkNo;
    HitEffectLabel = EneHitEffectCtrl.HitEffecType;             /* 1390 */

    EneDmgLargeHitAllOff();                                     /* 1391 */

    switch (HitEffectLabel)                                     /* 1393 */
    {
    case ENE_HIT_EFFECT_SMALL:
        EneDmgLargeHitReq(0);                                   /* 1395 */
        break;                                                  /* 1396 */
    case ENE_HIT_EFFECT_LARGE:
        EneDmgLargeHitReq(1);                                   /* 1398 */
        break;                                                  /* 1399 */
    case ENE_HIT_EFFECT_SP:
        EneDmgLargeHitReq(2);                                   /* 1401 */
        EneDmgLargeHitReq(3);                                   /* 1402 */
        break;                                                  /* 1403 */
    case ENE_HIT_EFFECT_SLOW:
        IgEffectSubFuncSlowReq(EneWrkNo);                       /* 1405 */
        break;                                                  /* 1406 */
    case ENE_HIT_EFFECT_ZERO:
        IgEffectSubFuncZeroReq(EneWrkNo, 0);                    /* 1408 */
        break;                                                  /* 1409 */
    case ENE_HIT_EFFECT_ZERO_SC:
        IgEffectSubFuncZeroReq(EneWrkNo, 1);                    /* 1411 */
        break;                                                  /* 1412 */
    case ENE_HIT_EFFECT_ZERO_SP:
        IgEffectSubFuncZeroReq(EneWrkNo, 2);                    /* 1414 */
        break;                                                  /* 1415 */
    case ENE_HIT_EFFECT_KOKU:
        IgEffectSubFuncKokuReq(EneWrkNo, 0);                    /* 1417 */
        break;                                                  /* 1418 */
    case ENE_HIT_EFFECT_KOKU_SC:
        IgEffectSubFuncKokuReq(EneWrkNo, 1);                    /* 1420 */
        break;                                                  /* 1421 */
    case ENE_HIT_EFFECT_KOKU_SP:
        IgEffectSubFuncKokuReq(EneWrkNo, 2);                    /* 1423 */
        break;                                                  /* 1424 */
    case ENE_HIT_EFFECT_PARALYZE:
        IgEffectSubFuncParalyzeReq(EneWrkNo);                   /* 1426 */
        break;                                                  /* 1427 */
    case ENE_HIT_EFFECT_VIEW:
        IgEffectSubFuncViewReq(EneWrkNo);                       /* 1429 */
        break;                                                  /* 1430 */
    case ENE_HIT_EFFECT_METSU:
        IgEffectSubFuncMetsuReq(EneWrkNo, 0);                   /* 1432 */
        break;                                                  /* 1433 */
    case ENE_HIT_EFFECT_METSU_SC:
        IgEffectSubFuncMetsuReq(EneWrkNo, 1);                   /* 1435 */
        break;                                                  /* 1436 */
    case ENE_HIT_EFFECT_METSU_SP:
        IgEffectSubFuncMetsuReq(EneWrkNo, 2);                   /* 1438 */
        break;                                                  /* 1439 */
    case ENE_HIT_EFFECT_REN:
        IgEffectSubFuncRenReq(EneWrkNo, 0);                     /* 1441 */
        break;                                                  /* 1442 */
    case ENE_HIT_EFFECT_REN_SC:
        IgEffectSubFuncRenReq(EneWrkNo, 1);                     /* 1444 */
        break;                                                  /* 1445 */
    case ENE_HIT_EFFECT_REN_SP:
        IgEffectSubFuncRenReq(EneWrkNo, 2);                     /* 1447 */
        break;                                                  /* 1448 */
    case ENE_HIT_EFFECT_TSUI:
        IgEffectSubFuncTsuiReq(EneWrkNo);                       /* 1450 */
        break;                                                  /* 1451 */
    case ENE_HIT_EFFECT_FUU:
        IgEffectSubFuncFuuReq(EneWrkNo);                        /* 1453 */
        break;                                                  /* 1456 */
    }
}

/* 1338 -- PORT NAME; see EneHitEffectSubFuncReq() above.
 *
 * The screen-wide half, run every frame of Flow 2: one in/keep/out ramp on
 * EneHitEffectCtrl.Counter, driving the blur and the contrast pass.  Before
 * StartFrame every term is forced to zero rather than left at whatever the
 * previous hit ended on, which is what makes back-to-back hits clean.
 *
 * A zero InTime or OutTime is a real case in the tables and each is guarded
 * separately -- an instant ramp reads as fully in, an instant fade as fully
 * out.  The two flags gate their halves independently, so a preset can shake
 * the screen without blurring it. */
static void EneHitEffectBlurContrastCalc(void)
{
    ENE_DMG_BLUR_CONTRAST_PARAMETER *pBlurContrast;
    int   NowTime;
    float Progress;

    pBlurContrast = EneHitEffectCtrl.pBlurContrast;             /* 1340 */

    if (EneHitEffectCtrl.Counter < pBlurContrast->StartFrame)   /* 1341 */
    {
        EneHitEffectCtrl.ContrastColor = 0;                     /* 1378 */
        EneHitEffectCtrl.ContrastAlpha = 0;                     /* 1379 */
        EneHitEffectCtrl.BlurScale     = 0;                     /* 1380 */
        EneHitEffectCtrl.BlurRot       = 0;                     /* 1381 */
        EneHitEffectCtrl.BlurAlpha     = 0;                     /* 1382 */
        return;
    }

    NowTime = EneHitEffectCtrl.Counter - pBlurContrast->StartFrame; /* 1343 */

    if (NowTime < pBlurContrast->InTime)                        /* 1345 */
    {
        if (pBlurContrast->InTime != 0)                         /* 1346 */
        {
            Progress = (float)NowTime / (float)pBlurContrast->InTime; /* 1347 */
        }
        else
        {
            Progress = 1.0f;                                    /* 1350 */
        }
    }
    else if (NowTime < pBlurContrast->InTime + pBlurContrast->KeepTime) /* 1354 */
    {
        Progress = 1.0f;
    }
    else
    {
        if (pBlurContrast->OutTime != 0)                        /* 1358 */
        {
            Progress = 1.0f
                     - (float)(NowTime - (pBlurContrast->InTime + pBlurContrast->KeepTime))
                     / (float)pBlurContrast->OutTime;           /* 1359 */
        }
        else
        {
            Progress = 0.0f;                                    /* 1362 */
        }
    }

    if (pBlurContrast->BlurOnFlg != 0)                          /* 1366 */
    {
        EneHitEffectCtrl.BlurScale = (int)
            ((float)(pBlurContrast->MaxBlurScale - pBlurContrast->MinBlurScale) * Progress
             + (float)pBlurContrast->MinBlurScale);             /* 1367 */
        EneHitEffectCtrl.BlurRot = (int)
            ((float)(pBlurContrast->MaxBlurRot - pBlurContrast->MinBlurRot) * Progress
             + (float)pBlurContrast->MinBlurRot);               /* 1368 */
        EneHitEffectCtrl.BlurAlpha = (u_char)(int)
            ((float)(pBlurContrast->MaxBlurAlpha - pBlurContrast->MinBlurAlpha) * Progress
             + (float)pBlurContrast->MinBlurAlpha);             /* 1369 */
    }

    if (pBlurContrast->ContrastOnFlg != 0)                      /* 1372 */
    {
        EneHitEffectCtrl.ContrastColor = (int)
            ((float)(pBlurContrast->MaxContrastColor - pBlurContrast->MinContrastColor) * Progress
             + (float)pBlurContrast->MinContrastColor);         /* 1373 */
        EneHitEffectCtrl.ContrastAlpha = (int)
            ((float)(pBlurContrast->MaxContrastAlpha - pBlurContrast->MinContrastAlpha) * Progress
             + (float)pBlurContrast->MinContrastAlpha);         /* 1374 */
    }
}

/* 1464 -- the screen-wide half of a hit, per frame.
 *
 * Four states on EneHitEffectCtrl.Flow: 0 idle, 1 the one-frame arming delay
 * (SEC0 frames, which is 1), 2 the ramp, 3 tear down.  The two blocks below the
 * switch redraw the blur and the contrast every frame of states 2 and 3 -- so
 * the last frame's values are still on screen while Flow 3 resets, and it is
 * the *next* frame that clears them.
 *
 * SetEffects is variadic and Ghidra drops the extra arguments; the disassembly
 * is what says id 3 takes five and id 0xd takes two, and the two floats going
 * through fptodp is what says tx/ty are the last pair. */
void EneHitEffectMain(void)
{                                                               /* 1464 */
    ENE_HIT_EFFECT_CTRL *pCtrl;
    int   AllTime;
    int   NowTime;
    float tx;
    float ty;

    pCtrl = &EneHitEffectCtrl;                                  /* 1465 */

    switch (pCtrl->Flow)                                        /* 1469 */
    {
    case 1:
        if (pCtrl->Counter == SEC0)                             /* 1475 */
        {
            EneHitEffectSubFuncReq();                           /* 1476 */
        }

        pCtrl->Counter++;                                       /* 1478 */

        if (pCtrl->Counter >= SEC0 + 1)                         /* 1479 */
        {
            pCtrl->Flow++;
            pCtrl->Counter = 0;                                 /* 1480 */
        }
        break;                                                  /* 1482 */

    case 2:
        EneHitEffectBlurContrastCalc();                         /* 1484 */

        pCtrl->Counter++;                                       /* 1486 */

        AllTime = pCtrl->pBlurContrast->StartFrame + pCtrl->pBlurContrast->InTime
                + pCtrl->pBlurContrast->KeepTime  + pCtrl->pBlurContrast->OutTime; /* 1487 */

        if (AllTime < pCtrl->Counter)                           /* 1488 */
        {
            pCtrl->Flow = 3;                                    /* 1489 */
        }

        NowTime = pCtrl->Counter - pCtrl->pBlurContrast->StartFrame; /* 1493 */

        if (pCtrl->pBlurContrast->CameraShakeOnFlg != 0)        /* 1494 */
        {
            if (NowTime == pCtrl->pBlurContrast->CameraShakeFrame) /* 1495 */
            {
                ReqFreqCamera();                                /* 1496 */
            }
        }

        if (pCtrl->pBlurContrast->PadVibrateOnFlg != 0)         /* 1499 */
        {
            if (NowTime == pCtrl->pBlurContrast->PadVibrateFrame) /* 1500 */
            {
                SetVibrate(0, 30, 1);                           /* 1501 */
                SetVibrate(1, 40, 255);                         /* 1502 */
            }
        }
        break;                                                  /* 1505 */

    case 3:
        pCtrl->Flow    = 0;                                     /* 1508 */
        pCtrl->Counter = 0;                                     /* 1509 */
        break;
    }                                                           /* 1513 */

    if (pCtrl->Flow < 2)                                        /* 1516 */
    {
        return;
    }

    if (pCtrl->pBlurContrast->BlurOnFlg != 0)                   /* 1517 */
    {
        GetCamI2DPos(pCtrl->EneMposP0, &tx, &ty);               /* 1520 */
        SetEffects_BLUR(0, 1, &pCtrl->BlurAlpha, pCtrl->BlurScale,
                        pCtrl->BlurRot, tx, ty);                /* 1521 */
    }

    if (pCtrl->pBlurContrast->ContrastOnFlg != 0)               /* 1523 */
    {
        SetEffects_NCONTRAST(0xd, 1, pCtrl->ContrastColor,
                             pCtrl->ContrastAlpha);             /* 1524 */
    }
}

/* 1537 -- start the damage sprite over the ghost.
 *
 * EneStatus bit 0x1000 is the shutter-chance flag; DmgType 4 (the fatal frame)
 * overrides it with 2, which is what gives that shot its own colour ramp.  The
 * distance is clamped to 3000 because that is the far end of every ramp table.
 *
 * One slot for the whole scene, so only the most recent hit shows. */
void EneDmgScreenEffectReq(float *EneMposP0, int DmgType, int EneStatus,
                           float DistPE)
{
    g3dxVu0CopyVector(ene_dmg_eff.MposP0, EneMposP0);           /* 1538 */

    ene_dmg_eff.chance = (u_char)((EneStatus >> 12) & 1);       /* 1539 */

    if (DmgType == 4)
    {
        ene_dmg_eff.chance = 2;
    }

    ene_dmg_eff.DmgType = DmgType;                              /* 1540 */
    ene_dmg_eff.flow    = 1;                                    /* 1541 */

    ene_dmg_eff.dist = (DistPE > 3000.0f) ? 3000.0f : DistPE;   /* 1542 */
}

/* 1552 -- start a burst of spirit particles.
 *
 * The tails are one block of Num ENEDMG_P_TAIL_WRK rather than one allocation
 * each; a failed allocation is what makes the whole request fail, and
 * PhotoDmgChk2() then banks the shot's power through
 * CNEquipTrayWrk::AbsorbImmediately() instead.
 *
 * SpeedRate is the player-to-ghost distance mapped onto 0.8 .. 1.8, flat below
 * 500 and above 2000 -- so particles from a distant ghost fly home faster and
 * the burst still lands in about the same time.  The two ends meet exactly at
 * 2000 ((2000-500)/1500 + 0.8 == 1.8), so the ramp is continuous. */
int EneDmgParticleEffectReq(const ENE_DMG_PARTICLE_REQ *pEneDmgReq)
{
    ENEDMG_P_WRK ParticleWork;

    g3dxVu0CopyVector(ParticleWork.StartPos, pEneDmgReq->StartPos); /* 1554 */

    ParticleWork.pEndPos = pEneDmgReq->pEndPos;                 /* 1557 */
    ParticleWork.Num     = pEneDmgReq->ParticleNum;             /* 1558 */

    if (ParticleWork.Num > ENEDMG_PARTICLE_MAX)                 /* 1559 */
    {
        ParticleWork.Num = ENEDMG_PARTICLE_MAX;                 /* 1560 */
    }

    ParticleWork.pTailWrk = (ENEDMG_P_TAIL_WRK *) EFFECT_MALLOC(ParticleWork.Num * sizeof(ENEDMG_P_TAIL_WRK)); /* 1562 */

    /* 1563/1564 -- GCC cross-jumped the three arms into one tail, so the whole
     * ladder carries the same pair of $LMs and the arm lines are interpolated. */
    if (pEneDmgReq->DistPE < 500.0f)                            /* 1563 */
    {
        ParticleWork.SpeedRate = 0.8f;                          /* 1564 */
    }
    else if (pEneDmgReq->DistPE > 2000.0f)
    {
        ParticleWork.SpeedRate = 1.8f;
    }
    else
    {
        ParticleWork.SpeedRate = (pEneDmgReq->DistPE - 500.0f) / 1500.0f + 0.8f;
    }

    ParticleWork.flow       = 1;                                /* 1565 */
    ParticleWork.SuctionFlg = pEneDmgReq->SuctionFlg;           /* 1566 */
    ParticleWork.Type       = 0;                                /* 1567 */

    if (ParticleWork.pTailWrk == nullptr)  /* 1570 */
    {
        return 0;                                               /* 1578 */
    }

    if (SingleLinkListAddEnd(&EneParticleList, &ParticleWork) == (SLL_CELL *)nullptr)                     /* 1571 */
    {
        EFFECT_FREE(ParticleWork.pTailWrk);                     /* 1572 */
        return 0;                                               /* 1573 */
    }

    return 1;                                                   /* 1580 */
}

/* 1589 -- the "ghost gone" burst: the same machinery with Type 1, no suction
 * and no tails, so the particles simply disperse and fade.
 *
 * ROM BUG, reproduced: this registers onto EneParticleList, not
 * EffectEndParticleList -- the store at 0x001454b0 is `addiu a0,a0,0x51d0`,
 * which is EneParticleList (EffectEndParticleList is 0x2e51e0).  Nothing
 * anywhere adds to EffectEndParticleList, so EffectEndParticleMain(), which
 * walks it, is dead code.  The effect itself still works: EneDmgMain()'s own
 * sweep over EneParticleList picks these bursts up and EneDmgParticle()
 * branches on Type, so the only casualty is the second sweep. */
int EffectEndParticleEffectReq(const ENE_DMG_PARTICLE_REQ *pEneDmgReq)
{
    ENEDMG_P_WRK ParticleWork;

    g3dxVu0CopyVector(ParticleWork.StartPos, pEneDmgReq->StartPos); /* 1591 */

    ParticleWork.pEndPos = pEneDmgReq->pEndPos;                 /* 1600 */
    ParticleWork.Num     = pEneDmgReq->ParticleNum;             /* 1601 */

    if (ParticleWork.Num > ENEDMG_PARTICLE_MAX)                 /* 1602 */
    {
        ParticleWork.Num = ENEDMG_PARTICLE_MAX;                 /* 1603 */
    }

    ParticleWork.pTailWrk   = (ENEDMG_P_TAIL_WRK *)nullptr;     /* 1605 */
    ParticleWork.SpeedRate  = 1.13f;                            /* 1606 */
    ParticleWork.flow       = 1;                                /* 1607 */
    ParticleWork.SuctionFlg = 0;                                /* 1608 */
    ParticleWork.Type       = 1;                                /* 1609 */
    ParticleWork.EffectType = pEneDmgReq->EffectType;           /* 1610 */

    return (SingleLinkListAddEnd(&EneParticleList, &ParticleWork)
                != (SLL_CELL *)nullptr);                        /* 1612 */
}

/* 1623 -- retire one burst: free its tail block, then unlink the cell.  The
 * null test is on the cell, not on the body, so a burst whose pTailWrk was
 * never allocated is still unlinked correctly. */
static void EneDmgParticleEffectCut(SINGLE_LINK_LIST *pSLL, SLL_CELL *pCell)
{
    ENEDMG_P_WRK *pEneDmgPWrk;

    if (pCell != (SLL_CELL *)nullptr)                           /* 1624 */
    {
        pEneDmgPWrk = (ENEDMG_P_WRK *)SingleLinkListCellBodyPtr(pCell); /* 1626 */

        if (pEneDmgPWrk->pTailWrk != (ENEDMG_P_TAIL_WRK *)nullptr) /* 1628 */
        {
            EFFECT_FREE(pEneDmgPWrk->pTailWrk);                 /* 1630 */
        }

        SingleLinkListRemove(pSLL, pCell);                      /* 1633 */
    }
}                                                               /* 1634 */

/* 1640 -- retarget every dispersing burst at the finder's suction point.
 *
 * CNPlyrCamera drives this once a frame while the viewfinder is up.  Only
 * Type 0 bursts are retargeted; the Type 1 "ghost gone" bursts keep whatever
 * they were given, which is how they stay put while the camera moves. */
void EneDmgParticleEndPosSet(const float (*pEndPos)[4])
{
    SLL_CELL     *pCell;
    ENEDMG_P_WRK *pEneDmgPWrk;

    for (pCell = SingleLinkListBeginCell(&EneParticleList);     /* 1642 */
         pCell != (SLL_CELL *)nullptr;
         pCell = SingleLinkListNextCell(pCell))
    {
        pEneDmgPWrk = (ENEDMG_P_WRK *)SingleLinkListCellBodyPtr(pCell); /* 1645 */

        if (pEneDmgPWrk->Type == 0)                             /* 1647 */
        {
            pEneDmgPWrk->pEndPos = (float (*)[4])pEndPos;       /* 1648 */
        }
    }
}

/* ==========================================================================
 *  The per-frame passes
 * ======================================================================== */

/* 1659 -- the damage overlay's clock, then the particle sweep.
 *
 * Nine states on ene_dmg_eff.flow.  0 and 8 are the idle ends, 1 the arming
 * delay that fires the large hit, 2 the one-shot that resolves every ramp's
 * maximum against the measured distance, and 3..7 the ramp legs -- in, hold,
 * hold, out, settle -- timed by SEC1, SEC2, SEC3, SEC4 and SEC5.
 *
 * Two details worth keeping straight.  The rotation ramp is frozen for
 * DmgType >= 2 (every state either interpolates rot or pins it to rot[0]), so
 * only the two weak damage grades spin the sprite.  And the draw at the bottom
 * is gated on flow > 1, which is why state 1's arming frames show nothing.
 *
 * Almost every statement here reads a ramp table through
 * reference_fixed_array::operator[], so it is attributed to fixed_array.h
 * 124/125 and leaves no $LM; the annotations below are the measured anchors
 * and the rest are interpolated between them. */
void EneDmgMain(void)
{
    SLL_CELL *pCell;
    float tx;
    float ty;

    switch (ene_dmg_eff.flow)                                   /* 1663 */
    {
    case 0:
        ene_dmg_eff.cnt = 0;                                    /* 1665 */
        ene_dmg_eff.alp    = (u_char)alp[0];                    /* 1667 */
        ene_dmg_eff.scl    = scl[0];                            /* 1668 */
        ene_dmg_eff.rot    = rot[0];                            /* 1669 */
        ene_dmg_eff.cntcol = ccol[0];                           /* 1670 */
        ene_dmg_eff.cntalp = calp[0];                           /* 1671 */
        break;

    case 1:
        ene_dmg_eff.alp    = (u_char)alp[0];                    /* 1674 */
        ene_dmg_eff.scl    = scl[0];                            /* 1675 */
        ene_dmg_eff.rot    = rot[0];                            /* 1676 */
        ene_dmg_eff.cntcol = ccol[0];                           /* 1677 */
        ene_dmg_eff.cntalp = calp[0];                           /* 1678 */

        if (ene_dmg_eff.cnt == (u_int)SEC0)                     /* 1679 */
        {
            if (ene_dmg_eff.DmgType == 2)                       /* 1680 */
            {
                EneDmgLargeHitReq(1);                           /* 1681 */
            }
            else if (ene_dmg_eff.DmgType >= 3)                  /* 1684 */
            {
                EneDmgLargeHitReq(2);                           /* 1685 */
                EneDmgLargeHitReq(3);                           /* 1686 */
            }
        }

        ene_dmg_eff.cnt++;                                      /* 1689 */

        if (ene_dmg_eff.cnt >= (u_int)(SEC0 + 5))               /* 1690 */
        {
            if (ene_dmg_eff.DmgType > 2)                        /* 1691 */
            {
                ReqFreqCamera();                                /* 1692 */
                SetVibrate(0, 30, 1);                           /* 1693 */
                SetVibrate(1, 40, 255);                         /* 1694 */
            }

            ene_dmg_eff.flow++;                                 /* 1699 */
        }
        break;

    case 2:
        /* One-shot: each ramp's maximum is its near value pulled towards its
         * far value by the measured distance, so a ghost right in front of the
         * camera flashes at alp[1] and one at 3000 units at alp[2]. */
        ene_dmg_eff.almx = alp[1]
            - (int)((float)(alp[1] - alp[2]) * ene_dmg_eff.dist / 3000.0f);   /* 1703 */
        ene_dmg_eff.scmx = scl[1]
            - (int)((float)(scl[1] - scl[2]) * ene_dmg_eff.dist / 3000.0f);   /* 1706 */
        ene_dmg_eff.rtmx = rot[1]
            - (int)((float)(rot[1] - rot[2]) * ene_dmg_eff.dist / 3000.0f);   /* 1709 */
        ene_dmg_eff.ccmx = ccol[1]
            - (int)((float)(ccol[1] - ccol[2]) * ene_dmg_eff.dist / 3000.0f); /* 1711 */
        ene_dmg_eff.camx = calp[1]
            - (int)((float)(calp[1] - calp[2]) * ene_dmg_eff.dist / 3000.0f); /* 1714 */

        ene_dmg_eff.flow++;                                     /* 1717 */
        /* falls through into the first ramp leg -- the ROM's own order, so the
         * frame that resolves the maxima also draws the first step. */

    case 3:
        ene_dmg_eff.alp = (u_char)alp[0];                       /* 1719 */
        ene_dmg_eff.scl = scl[0]
            + (int)((ene_dmg_eff.scmx - scl[0]) * (int)ene_dmg_eff.cnt) / SEC1; /* 1721 */

        if (ene_dmg_eff.DmgType < 2)                            /* 1723 */
        {
            ene_dmg_eff.rot = rot[0]
                + (int)((ene_dmg_eff.rtmx - rot[0]) * (int)ene_dmg_eff.cnt) / SEC1; /* 1725 */
        }
        else
        {
            ene_dmg_eff.rot = rot[0];                           /* 1727 */
        }

        ene_dmg_eff.cntcol = ccol[0]
            + (ene_dmg_eff.ccmx - ccol[0]) * (int)ene_dmg_eff.cnt / SEC1;   /* 1731 */
        ene_dmg_eff.cntalp = calp[0]
            + (ene_dmg_eff.camx - calp[0]) * (int)ene_dmg_eff.cnt / SEC1;   /* 1733 */

        ene_dmg_eff.cnt++;                                      /* 1734 */

        if (ene_dmg_eff.cnt >= (u_int)SEC1)                     /* 1735 */
        {
            ene_dmg_eff.cnt = 0;                                /* 1737 */
            ene_dmg_eff.flow++;                                 /* 1739 */
        }
        break;

    case 4:
        ene_dmg_eff.alp = (u_char)(alp[0]
            + (int)((ene_dmg_eff.almx - alp[0]) * (int)ene_dmg_eff.cnt) / SEC2); /* 1741 */
        ene_dmg_eff.scl = ene_dmg_eff.scmx;                     /* 1742 */
        ene_dmg_eff.rot = (ene_dmg_eff.DmgType > 1) ? rot[0] : ene_dmg_eff.rtmx; /* 1743 */
        ene_dmg_eff.cntcol = ene_dmg_eff.ccmx;                  /* 1747 */
        ene_dmg_eff.cntalp = ene_dmg_eff.camx;                  /* 1749 */

        ene_dmg_eff.cnt++;                                      /* 1750 */

        /* This tail and case 5's are identical and GCC cross-jumped them into
         * one copy, so only case 5's $LMs survive -- the source wrote both out
         * in full.  See [[gcc-cross-jumps-identical-call-tails]]. */
        if (ene_dmg_eff.cnt >= (u_int)SEC2)                     /* 1751 */
        {
            ene_dmg_eff.cnt = 0;                                /* 1752 */
            ene_dmg_eff.flow++;
            ene_dmg_eff.cntcol = ene_dmg_eff.ccmx;
            ene_dmg_eff.cntalp = ene_dmg_eff.camx;
        }
        break;

    case 5:
        ene_dmg_eff.alp = (u_char)ene_dmg_eff.almx;             /* 1753 */
        ene_dmg_eff.scl = ene_dmg_eff.scmx;                     /* 1755 */
        ene_dmg_eff.rot = (ene_dmg_eff.DmgType > 1) ? rot[0] : ene_dmg_eff.rtmx; /* 1757 */
        ene_dmg_eff.cntcol = ene_dmg_eff.ccmx;                  /* 1758 */
        ene_dmg_eff.cntalp = ene_dmg_eff.camx;                  /* 1759 */

        ene_dmg_eff.cnt++;

        if (ene_dmg_eff.cnt >= (u_int)SEC3)                     /* 1763 */
        {
            ene_dmg_eff.cnt = 0;                                /* 1765 */
            ene_dmg_eff.flow++;
            ene_dmg_eff.cntcol = ene_dmg_eff.ccmx;              /* 1767 */
            ene_dmg_eff.cntalp = ene_dmg_eff.camx;
        }
        break;

    case 6:
        ene_dmg_eff.alp = (u_char)(ene_dmg_eff.almx
            - (int)((ene_dmg_eff.almx - alp[0]) * (int)ene_dmg_eff.cnt) / SEC4); /* 1771 */
        ene_dmg_eff.scl = ene_dmg_eff.scmx;                     /* 1773 */
        ene_dmg_eff.rot = (ene_dmg_eff.DmgType > 1) ? rot[0] : ene_dmg_eff.rtmx; /* 1775 */
        ene_dmg_eff.cntcol = ene_dmg_eff.ccmx
            - (ene_dmg_eff.ccmx - ccol[0]) * (int)ene_dmg_eff.cnt / SEC4;   /* 1781 */
        ene_dmg_eff.cntalp = ene_dmg_eff.camx
            - (ene_dmg_eff.camx - calp[0]) * (int)ene_dmg_eff.cnt / SEC4;   /* 1783 */

        ene_dmg_eff.cnt++;                                      /* 1784 */

        /* Cross-jumped with case 7's tail, and with case 1's `flow++` on top
         * of that -- one shared exit for all three. */
        if (ene_dmg_eff.cnt >= (u_int)SEC4)                     /* 1785 */
        {
            ene_dmg_eff.cnt = 0;
            ene_dmg_eff.flow++;
        }
        break;

    case 7:
        ene_dmg_eff.alp = (u_char)alp[0];                       /* 1787 */
        ene_dmg_eff.scl = ene_dmg_eff.scmx
            - (int)((ene_dmg_eff.scmx - scl[0]) * (int)ene_dmg_eff.cnt) / SEC5; /* 1789 */

        if (ene_dmg_eff.DmgType < 2)
        {
            ene_dmg_eff.rot = ene_dmg_eff.rtmx
                - (int)((ene_dmg_eff.rtmx - rot[0]) * (int)ene_dmg_eff.cnt) / SEC5;
        }
        else
        {
            ene_dmg_eff.rot = rot[0];
        }

        ene_dmg_eff.cntcol = ccol[0];                           /* 1794 */
        ene_dmg_eff.cntalp = calp[0];

        ene_dmg_eff.cnt++;                                      /* 1797 */

        if (ene_dmg_eff.cnt >= (u_int)SEC5)                     /* 1798 */
        {
            ene_dmg_eff.cnt = 0;
            ene_dmg_eff.flow++;                                 /* 1800 */
        }
        break;

    case 8:
        ene_dmg_eff.alp    = (u_char)alp[0];
        ene_dmg_eff.scl    = scl[0];
        ene_dmg_eff.rot    = rot[0];
        ene_dmg_eff.cntcol = ccol[0];
        ene_dmg_eff.cntalp = calp[0];
        ene_dmg_eff.flow   = 0;                                 /* 1801 */
        break;
    }

    /* The sprite itself: id 3 is the scaled/rotated blit and id 0xd the
     * colour-and-alpha modulate that follows it.  &ene_dmg_eff.alp is handed
     * over by pointer, the same shape effect.c's own blur pass uses. */
    if (ene_dmg_eff.flow > 1)
    {
        GetCamI2DPos(ene_dmg_eff.MposP0, &tx, &ty);
        SetEffects_BLUR(0, 1, &ene_dmg_eff.alp, ene_dmg_eff.scl,
                        ene_dmg_eff.rot, tx, ty);
        SetEffects_NCONTRAST(0xd, 1, ene_dmg_eff.cntcol, ene_dmg_eff.cntalp);
    }

    for (pCell = SingleLinkListBeginCell(&EneParticleList);     /* 1806 */
         pCell != (SLL_CELL *)nullptr;
         pCell = SingleLinkListNextCell(pCell))
    {
        EneDmgParticle(&EneParticleList, pCell);                /* 1807 */
    }

    EneDmgLargeHitCtrlMain();                                   /* 1811 */
}

/* 1818 -- DEAD CODE.  Walks EffectEndParticleList, which nothing ever
 * registers on: EffectEndParticleEffectReq() adds to EneParticleList instead
 * (see there).  Reconstructed as found; it is called every frame from
 * EffectControl() and does nothing. */
void EffectEndParticleMain(void)
{
    SLL_CELL *pCell;

    for (pCell = SingleLinkListBeginCell(&EffectEndParticleList); /* 1820 */
         pCell != (SLL_CELL *)nullptr;
         pCell = SingleLinkListNextCell(pCell))
    {
        EneDmgParticle(&EffectEndParticleList, pCell);          /* 1821 */
    }
}                                                               /* 1823 */

/* 1834 -- non-zero while any burst is still live.  n_plyr_camera gates the
 * finder's suction point on this. */
int IsActiveEneDmgParticle(void)
{
    return (SingleLinkListRegCount(&EneParticleList) != 0);     /* 1834 */
}

/* ==========================================================================
 *  Per-particle parameters
 * ======================================================================== */

/* 1847 -- the burst's colour.
 *
 * Type 1 (the "ghost gone" burst) picks by EffectType, which is the ghost's
 * own effect colour; anything out of range falls back to entry 0 rather than
 * being clamped to 4.  Type 0 (a camera hit) is always the one blue. */
static void EneDmgParticleColorGet(int *Color, int Type, int EffectType)
{
    static sceVu0IVECTOR HitDamegeCol =                         /* data 2e50c0 */
        { 63, 96, 255, 0 };
    static sceVu0IVECTOR EffectEndCol[5] =                      /* data 2e50d0 */
    {
        { 229,  87, 230, 0 },
        {  74, 255,  74, 0 },
        { 218, 236,  79, 0 },
        { 224, 243,  22, 0 },
        {  49, 221,  95, 0 }
    };

    if (Type == 1)                                              /* 1849 */
    {
        if (EffectType > 4)                                     /* 1851 */
        {
            EffectType = 0;                                     /* 1853 */
        }

        Color[0] = EffectEndCol[EffectType][0];                 /* 1856 */
        Color[1] = EffectEndCol[EffectType][1];
        Color[2] = EffectEndCol[EffectType][2];
        Color[3] = EffectEndCol[EffectType][3];
        return;
    }

    Color[0] = HitDamegeCol[0];                                 /* 1884 */
    Color[1] = HitDamegeCol[1];
    Color[2] = HitDamegeCol[2];
    Color[3] = HitDamegeCol[3];                                 /* 1887 */
}

/* 1895 -- how long the burst blooms before it starts closing. */
static int EneDmgParticleSpreadTimeGet(int Type)
{
    return (Type == 1) ? 54 : 15;                               /* 1918 */
}

/* 2021 / 2048 -- the particle sprite's size.  The "ghost gone" burst uses a
 * much larger sprite (21.1 x 21.4) than a camera hit (6.6 x 6.3). */
static float EneDmgParticleSizeWGet(int Type)
{
    return (Type == 1) ? 6.6f : 21.1f;                          /* 2041 */
}

static float EneDmgParticleSizeHGet(int Type)
{
    return (Type == 1) ? 6.3f : 21.4f;                          /* 2068 */
}

/* 2216 -- the sprite's animation phase.
 *
 * Type 1 starts every particle at frame 20; a camera hit staggers them over
 * (-21, -7], so they do not all flash in step.  The negation is in float
 * (neg.S before the mul) and the -7 is added after the truncation to int, so
 * the two cannot be folded into one EffectGetRandom() call. */
static int EneDmgParticleAnmCountGet(int Type)
{
    if (Type == 1)                                              /* 2222 */
    {
        return 20;
    }

    return (int)(-EffectGetRandom(0.0f, 14.0f)) - 7;            /* 2233 */
}

/* 2240 -- the sprite's alpha.  Note the two arms are in different units: 138
 * for the end burst against 0.384 (98/255) for a camera hit.  Kept as found;
 * EneDmgParticle() is where each is finally scaled. */
static float EneDmgParticleAlphaGet(int Type)
{
    return (Type == 1) ? 138.0f : 0.384313703f;                 /* 2263 */
}

/* 2274 -- seed one particle.
 *
 * rrot is a uniform angle round the full circle, racc the outward speed
 * (SpeedRate * 40 .. 80, so a burst from a distant ghost starts faster) and
 * rbrk the per-frame decay applied to it.  rpos starts at the centre.
 *
 * Every one of the three is `EffectGetRandom(0.0f, width)` with the base added
 * outside, not `EffectGetRandom(base, base + width)`.  The .lit4 multiplier is
 * the exact width -- 0.06, 2*pi, SpeedRate*40 -- where GCC's fold of
 * `0.88f - 0.82f` would be one ulp higher and `SpeedRate*80 - SpeedRate*40`
 * would need a runtime subtract that the ROM does not emit. */
static void EneDmgParticleOneInit(ENEDMG_PARTICLE_ONE *pEneParticle,
                                  float *Center, float SpeedRate, int Type)
{
    pEneParticle->rrad = 0.0f;                                  /* 2276 */
    pEneParticle->rrot = EffectGetRandom(0.0f, 6.28318501f) - EFE_PI; /* 2277 */
    pEneParticle->racc = EffectGetRandom(0.0f, SpeedRate * 40.0f)
                       + SpeedRate * 40.0f;                     /* 2278 */
    pEneParticle->rbrk = EffectGetRandom(0.0f, 0.06f) + 0.82f;  /* 2279 */

    pEneParticle->AlphaRate = 1.0f;                             /* 2281 */
    pEneParticle->anm_count = EneDmgParticleAnmCountGet(Type);  /* 2282 */
    pEneParticle->ralp      = EneDmgParticleAlphaGet(Type);     /* 2283 */

    g3dxVu0CopyVector(pEneParticle->rpos, Center);              /* 2285 */
}

/* 2298 -- step one particle outward and brake it.  Returns 0 once the speed
 * has decayed below 0.1, which is what retires the particle. */
static int EneDmgParticleOneUpdate(ENEDMG_PARTICLE_ONE *pEneParticle)
{
    if (pEneParticle->racc > 0.1f)                              /* 2301 */
    {
        pEneParticle->rrad += pEneParticle->racc;               /* 2303 */
        pEneParticle->racc *= pEneParticle->rbrk;               /* 2304 */

        return 1;
    }

    return 0;                                                   /* 2307 */
}

/* 1925 -- one burst's per-frame step.
 *
 * `flow` is the burst's state: -1 dead, 1 a two-frame arming delay, 2 seed the
 * particles, 3/4 bloom, 5 fly home.  The tail of the function is shared by all
 * of them and gated on flow rather than run per case -- the sprite animation
 * from flow 3 on, the draw from flow 2 on -- which is why the switch falls out
 * into two more `if`s instead of each case drawing for itself.
 *
 * A burst that reaches SpreadTime either hands over to the suction path
 * (SuctionFlg) or is unlinked and freed.  Both `EneDmgParticleEffectCut` calls
 * below are one `jal` in the ROM: GCC cross-jumped them, which is what puts two
 * source lines (1977 and 1981) on the same instruction.  See
 * [[gcc-cross-jumps-identical-call-tails]]. */
static void EneDmgParticle(SINGLE_LINK_LIST *pSLL, SLL_CELL *pCell)
{                                                               /* 1925 */
    ENEDMG_P_WRK *edp;
    sceVu0IVECTOR Color;
    int   SpreadTime;
    int   i;
    float rot_x;
    float rot_y;
    float AlphaRate;

    edp = (ENEDMG_P_WRK *)SingleLinkListCellBodyPtr(pCell);     /* 1928 */

    SpreadTime = EneDmgParticleSpreadTimeGet(edp->Type);        /* 1930 */

    float *cam_dir = gra3dcamGetDirection();                    /* 1934 */

    if (edp->flow == -1)                                        /* 1936 */
    {
        return;
    }

    EneDmgParticleColorGet(Color, edp->Type, edp->EffectType);  /* 1938 */

    switch (edp->flow)                                          /* 1941 */
    {
    case 1:
        edp->cnt++;                                             /* 1945 */
        if (edp->cnt > 1)
        {
            edp->cnt = 0;                                       /* 1946 */
            edp->flow++;                                        /* 1947 */
        }
        break;                                                  /* 1949 */

    case 2:
        for (i = 0; i < edp->Num; i++)                          /* 1951 */
        {
            /* No $LM of its own: the subscript goes through fixed_array. */
            EneDmgParticleOneInit(&edp->particle[i], edp->StartPos,
                                  edp->SpeedRate, edp->Type);
        }                                                       /* 1953 */

        edp->cnt = 0;                                           /* 1955 */
        edp->flow++;                                            /* 1956 */
        /* fall through -- the seeded particles are stepped on the same frame */

    case 3:
    case 4:
        for (i = 0; i < edp->Num; i++)                          /* 1961 */
        {
            EneDmgParticleOneUpdate(&edp->particle[i]);
        }                                                       /* 1963 */

        edp->cnt++;                                             /* 1964 */

        if (edp->cnt >= SpreadTime)                             /* 1966 */
        {
            if (edp->SuctionFlg != 0)                           /* 1967 */
            {
                EneDmgParticleSuctionInit(edp, edp->particle[0].rpos,
                                          *edp->pEndPos);       /* 1969 */
                edp->flow = 5;                                  /* 1970 */
                edp->cnt  = 0;                                  /* 1971 */
            }
            else
            {
                EneDmgParticleEffectCut(pSLL, pCell);           /* 1977 */
            }
        }
        break;

    case 5:
        /* EffWrkStopFlgGet() is the global effect freeze -- the photo phase
           raises it -- so a burst in flight simply holds its positions. */
        if (EffWrkStopFlgGet() == 0)                            /* 1979 */
        {
            if (EneDmgParticleSuction(edp) == 0)                /* 1980 */
            {
                EneDmgParticleEffectCut(pSLL, pCell);           /* 1981 */
            }
        }
        break;
    }                                                           /* 1984 */

    if (edp->flow > 2)                                          /* 1988 */
    {
        for (i = 0; i < edp->Num; i++)                          /* 1989 */
        {
            if (edp->particle[i].anm_count < 100)
            {
                edp->particle[i].anm_count++;
            }
        }                                                       /* 1993 */
    }

    if (edp->flow > 1)                                          /* 1997 */
    {
        /* A dispersing burst fades out over its spread time; one flying home
           holds full alpha and is faded by the suction path instead. */
        AlphaRate = 1.0f;
        if (edp->SuctionFlg == 0)                               /* 2000 */
        {
            AlphaRate = (float)(SpreadTime - edp->cnt) / (float)SpreadTime; /* 2004 */
        }

        Vector2Rot(cam_dir, &rot_x, &rot_y);                    /* 2007 */

        for (i = 0; i < edp->Num; i++)                          /* 2008 */
        {
            EneDmgParticleOneDraw(&edp->particle[i], rot_x, rot_y,
                                  Color[0], Color[1], Color[2],
                                  AlphaRate, edp->Type);
        }                                                       /* 2012 */
    }
}                                                               /* 2015 */

/* 2083 -- draw one particle: four camera-facing sprites on one matrix.
 *
 * Declared inline in the ROM, so its body lives in
 * .gnu.linkonce.t.EneDmgParticleOneDraw at 0x002b2ba8 (1264 bytes) rather than
 * in effect_ene.o's own .text -- ZERO2.MAP files it under zero2_util.o for that
 * reason, but symbols.txt's SOL puts every one of its $LMs in effect_ene.c at
 * lines 2083..2208.  See [[linkonce-sections-hold-inlined-bodies]].
 *
 * The four sprites are the core (texture 0x5e), the ring around it ("wakka",
 * 0x60) and two copies of a bar ("bou", 0x44) crossed at 90 degrees.  All four
 * scale and fade off anm_count, which EneDmgParticle() steps once a frame and
 * clamps at 100: the bar over its first 18 frames, the ring over its first 20,
 * and both hold a fixed size after that.  A particle seeded with a negative
 * anm_count (EneDmgParticleAnmCountGet() staggers a camera hit over (-21, -7])
 * takes the else arm and draws at the start values until its count reaches 0.
 *
 * GCC hoisted all seven of those else-arm assignments above the branch, which
 * is why the `anm_count >= 0` test itself carries line 2181 rather than 2148 --
 * the last hoisted statement's $LM landed on it. */
void EneDmgParticleOneDraw(ENEDMG_PARTICLE_ONE *pEneParticle, float rot_x,
                           float rot_y, int r, int g, int b, float AlphaRate,
                           int Type)
{                                                               /* 2083 */
    float BouStartScaleX   = 0.609999955f;                      /* 2109 */
    float BouStartScaleY   = 2.98999977f;                       /* 2111 */
    float BouStartAlpha    = 0.0f;
    float BouStartRot      = 0.0f;                              /* 2114 */
    float WakkaStartScaleX = 0.099999994f;                      /* 2117 */
    float WakkaStartAlpha  = 0.0f;
    float WakkaEndAlpha    = 0.0f;                              /* 2122 */

    /* .rodata 0x3a6190, copied into the frame -- a local aggregate initialiser,
       not a static; see [[rodata-blob-into-stack-is-a-local-initialiser]].
       PRIM 0x8004 is SPRITE|TME|ABE and the GIFtag half above it is REGLIST
       NREG 3; ALPHA 0x48 is (Cs-0)*As+Cd, i.e. additive by source alpha. */
    DRAW_ENV de =                                               /* 2123 */
    {
        0x0000000000000161ULL,      /* TEX1   MMAG/MMIN LINEAR                 */
        0x0000000000000048ULL,      /* ALPHA  additive                         */
        0x000000010a000118ULL,      /* ZBUF   PSMZ24 @0x118, ZMSK              */
        0x000000000005000dULL,      /* TEST   ATST GEQUAL/0, ZTST GEQUAL       */
        0x0000000000000000ULL,      /* CLAMP  repeat                           */
        0x302a400000008004ULL       /* PRIM   SPRITE | TME | ABE               */
    };

    float ParticleSizeW;
    float ParticleSizeH;
    float wlm[4][4];
    float RotMat[4][4];
    float LocalWorldMat2[4][4];
    float zero[4];
    float BouScaleX;
    float BouScaleY;
    float BouAlpha;
    float BouRot;
    float WakkaScaleX;
    float WakkaScaleY;
    float WakkaAlpha;
    float AlphaBase;

    ParticleSizeW = EneDmgParticleSizeWGet(Type);               /* 2132 */
    ParticleSizeH = EneDmgParticleSizeHGet(Type);               /* 2133 */

    memset(zero, 0, sizeof(zero));                              /* 2135 */
    zero[3] = 1.0f;

    if (pEneParticle->ralp > WakkaEndAlpha)                     /* 2144 */
    {
        AlphaBase = AlphaRate * 255.0f;                         /* 2145 */

        if (pEneParticle->anm_count >= 0)                       /* 2148 */
        {
            if (pEneParticle->anm_count < 18)                   /* 2150 */
            {
                BouScaleX = BouStartScaleX
                          + ((float)pEneParticle->anm_count * -0.49999994f) / 18.0f; /* 2151 */
                BouScaleY = BouStartScaleY
                          + ((float)pEneParticle->anm_count * -2.87999964f) / 18.0f; /* 2152 */
                BouAlpha  = 0.429999977f
                          + ((float)pEneParticle->anm_count * -0.429999977f) / 18.0f; /* 2153 */
                BouRot    = BouStartRot
                          + ((float)pEneParticle->anm_count * 2.93215275f) / 18.0f;  /* 2154 */
            }
            else
            {
                BouScaleX = 0.11f;                              /* 2157 */
                BouScaleY = 0.11f;                              /* 2158 */
                BouAlpha  = 0.0f;
                BouRot    = 2.93215275f;                        /* 2160 */
            }

            if (pEneParticle->anm_count < 20)                   /* 2163 */
            {
                WakkaScaleX = WakkaStartScaleX
                            + ((float)pEneParticle->anm_count * 3.84999967f) / 20.0f; /* 2164 */
                WakkaScaleY = WakkaScaleX;                      /* 2165 */
                WakkaAlpha  = 0.669999957f
                            + ((float)pEneParticle->anm_count * -0.669999957f) / 20.0f; /* 2166 */
            }
            else
            {
                WakkaScaleX = 3.94999981f;                      /* 2169 */
                WakkaScaleY = 3.94999981f;                      /* 2170 */
                WakkaAlpha  = 0.0f;                             /* 2171 */
            }
        }
        else
        {
            BouScaleX   = BouStartScaleX;                       /* 2175 */
            BouScaleY   = BouStartScaleY;                       /* 2176 */
            BouAlpha    = BouStartAlpha;                        /* 2177 */
            BouRot      = BouStartRot;                          /* 2178 */
            WakkaScaleX = WakkaStartScaleX;                     /* 2179 */
            WakkaScaleY = WakkaStartScaleX;                     /* 2180 */
            WakkaAlpha  = WakkaStartAlpha;                      /* 2181 */
        }

        /* rrad is the spiral radius, so the translate below pushes the particle
           out along its own local X before the spin about rrot puts it round
           the circle -- and that is where Center comes from. */
        zero[0] = pEneParticle->rrad;                           /* 2184 */

        sceVu0UnitMatrix(wlm);                                  /* 2186 */
        sceVu0TransMatrix(wlm, wlm, zero);                      /* 2187 */
        sceVu0RotMatrixZ(wlm, wlm, pEneParticle->rrot);         /* 2188 */
        sceVu0RotMatrixX(wlm, wlm, rot_x);                      /* 2189 */
        sceVu0RotMatrixY(wlm, wlm, rot_y);                      /* 2190 */
        sceVu0TransMatrix(wlm, wlm, pEneParticle->rpos);        /* 2191 */

        g3dxVu0CopyVector(pEneParticle->Center, wlm[3]);        /* 2193 */

        Set3DPosTexure(wlm, &de, 0x5e, ParticleSizeW, ParticleSizeH,
                       (u_char)r, (u_char)g, (u_char)b,
                       (u_char)(u_int)(AlphaBase * pEneParticle->ralp)); /* 2195 */

        Set3DPosTexure(wlm, &de, 0x60,
                       ParticleSizeW * WakkaScaleX, ParticleSizeH * WakkaScaleY,
                       (u_char)r, (u_char)g, (u_char)b,
                       (u_char)(u_int)(AlphaBase * pEneParticle->ralp * WakkaAlpha)); /* 2198 */

        sceVu0UnitMatrix(RotMat);                               /* 2199 */
        sceVu0RotMatrixZ(RotMat, RotMat, BouRot);               /* 2200 */
        sceVu0MulMatrix(LocalWorldMat2, wlm, RotMat);           /* 2201 */

        Set3DPosTexure(LocalWorldMat2, &de, 0x44,
                       ParticleSizeW * BouScaleX, ParticleSizeH * BouScaleY,
                       (u_char)r, (u_char)g, (u_char)b,
                       (u_char)(u_int)(AlphaBase * pEneParticle->ralp * BouAlpha)); /* 2203 */

        sceVu0UnitMatrix(RotMat);                               /* 2204 */
        sceVu0RotMatrixZ(RotMat, RotMat, BouRot + 1.57079625f); /* 2205 */
        sceVu0MulMatrix(LocalWorldMat2, wlm, RotMat);           /* 2206 */

        Set3DPosTexure(LocalWorldMat2, &de, 0x44,
                       ParticleSizeW * BouScaleX, ParticleSizeH * BouScaleY,
                       (u_char)r, (u_char)g, (u_char)b,
                       (u_char)(u_int)(AlphaBase * pEneParticle->ralp * BouAlpha)); /* 2208 */
    }
}

/* ==========================================================================
 *  Suction -- the particles that fly home to the equip tray
 * ======================================================================== */

/* 2320 -- arm every particle of a burst for the flight home.
 *
 * Each gets its own flight speed (3.8 .. 4.1) and spin (-0.5 .. +1.5 degrees a
 * frame), so a burst arrives spread out rather than as one blob, and rrad_max
 * latches the radius it had bloomed to -- EneDmgParticleSuctionOne() winds that
 * radius back down as the particle closes on the tray.
 *
 * Note the ROM's random idiom here and in EneDmgParticleOneInit(): the offset is
 * applied *outside* the call, `EffectGetRandom(0.0f, width) + base`, not
 * `EffectGetRandom(base, base + width)`.  The two differ by one ulp once GCC
 * folds `max - min`, and the emitted .lit4 is the exact width, so the form is
 * measured rather than chosen. */
static void EneDmgParticleSuctionInit(ENEDMG_P_WRK *pEneDmgPWrk,
                                      const float *Start, const float *End)
{                                                               /* 2320 */
    float wlm[4][4];
    float zero[4];
    float rot_x;
    float rot_y;
    int   i;

    memset(zero, 0, sizeof(zero));                              /* 2322 */
    zero[3] = 1.0f;

    float *cam_dir = gra3dcamGetDirection();                    /* 2323 */

    g3dxVu0CopyVector(pEneDmgPWrk->StartPos, Start);            /* 2326 */
    g3dxVu0CopyVector(pEneDmgPWrk->EndPos,   End);              /* 2327 */

    Vector2Rot(cam_dir, &rot_x, &rot_y);                        /* 2333 */

    for (i = 0; i < pEneDmgPWrk->Num; i++)                      /* 2334 */
    {
        /* Every statement in the body goes through a fixed_array subscript, so
           none of them leaves an $LM of its own; only the four calls that take
           no subscripted argument do. */
        pEneDmgPWrk->particle[i].cnt_f    = 13.9f;
        pEneDmgPWrk->particle[i].cnt_spd  = EffectGetRandom(0.0f, 0.3f) + 3.8f;
        pEneDmgPWrk->particle[i].rrad_max = pEneDmgPWrk->particle[i].rrad;

        /* 0.034906581 is 2 degrees and 0.0087266452 half a degree, both one ulp
           low -- see [[ee-gcc-truncates-float-literals]]. */
        pEneDmgPWrk->particle[i].rot_spd  =
            EffectGetRandom(0.0f, 0.034906581f) - 0.0087266452f;

        zero[0] = pEneDmgPWrk->particle[i].rrad;

        sceVu0UnitMatrix(wlm);                                  /* 2345 */
        sceVu0TransMatrix(wlm, wlm, zero);                      /* 2346 */
        sceVu0RotMatrixZ(wlm, wlm, pEneDmgPWrk->particle[i].rrot);
        sceVu0RotMatrixX(wlm, wlm, rot_x);                      /* 2348 */
        sceVu0RotMatrixY(wlm, wlm, rot_y);                      /* 2349 */
        sceVu0TransMatrix(wlm, wlm, pEneDmgPWrk->particle[i].rpos);

        g3dxVu0CopyVector(pEneDmgPWrk->particle[i].Center, wlm[3]);

        EneDmgParticleSuctionTailInit(&pEneDmgPWrk->pTailWrk[i],
                                      &pEneDmgPWrk->particle[i]);
    }                                                           /* 2353 */
}

/* 2365 -- step a whole burst along its flight.
 *
 * The destination is re-read from pEndPos every frame, so a burst tracks the
 * finder as the player moves the camera.  Returns non-zero while any particle
 * is still travelling; EneDmgParticle() frees the burst on the frame it goes to
 * zero.  EneDmgParticleSuctionNum is reset here and counted up per particle
 * that *arrives* this frame -- CNEquipTrayWrk::Work() reads it once a frame and
 * banks 45 units of spirit power for each. */
static int EneDmgParticleSuction(ENEDMG_P_WRK *pEneDmgPWrk)
{                                                               /* 2365 */
    float SubVec[4];
    int   i;
    float fx;
    float fy;
    float fz;
    float r1l;
    int   end_count;
    float Length;               /* no stab: a float local, see functions.txt */

    end_count = 0;                                              /* 2374 */

    g3dxVu0CopyVector(pEneDmgPWrk->EndPos, *pEneDmgPWrk->pEndPos); /* 2377 */

    fx = pEneDmgPWrk->EndPos[0] - pEneDmgPWrk->StartPos[0];      /* 2380 */
    fy = pEneDmgPWrk->EndPos[1] - pEneDmgPWrk->StartPos[1];      /* 2381 */
    fz = pEneDmgPWrk->EndPos[2] - pEneDmgPWrk->StartPos[2];      /* 2382 */

    Length = g3dxVu0Sqrt(fx * fx + fy * fy + fz * fz);           /* 2384 */
    r1l    = Length / 6.0f;                                      /* 2386 */

    sceVu0SubVector(SubVec, pEneDmgPWrk->EndPos, pEneDmgPWrk->StartPos); /* 2388 */

    EneDmgParticleSuctionNum = 0;                                /* 2390 */

    for (i = 0; i < pEneDmgPWrk->Num; i++)                       /* 2391 */
    {
        if (EneDmgParticleSuctionOne(&pEneDmgPWrk->particle[i],
                                     pEneDmgPWrk->StartPos, SubVec,
                                     Length, r1l) == 1)
        {
            EneDmgParticleSuctionNum++;                          /* 2393 */
        }

        if (EneDmgParticleSuctionTail(&pEneDmgPWrk->pTailWrk[i],
                                      &pEneDmgPWrk->particle[i]) != 0)
        {
            end_count++;
        }
    }                                                            /* 2400 */

    /* GCC reuses the loop guard's compare for this, so a burst with Num <= 0
       answers 0 without the loop ever running. */
    return (end_count < pEneDmgPWrk->Num);                       /* 2406 */
}

/* 2426 -- step one particle along its flight.
 *
 * cnt_f is the flight clock, 0 .. 90, and every term is driven off it through
 * one shape: `f2 = 1 - cos(cnt_f/90 * pi/2)`, a versine that starts at 0 and
 * ends at 1 with zero slope at the start -- so the particle eases away from the
 * ghost and arrives at speed.  The position is that fraction along the straight
 * line to the tray, the spiral radius is a *different* phase of the same clock
 * (a full sine hump, so it blooms and closes again), and the spin rate scales
 * with it too.
 *
 * The clock itself runs at three speeds -- double below 15, single below 30,
 * half below 90 -- so the burst hangs, accelerates, and then drifts in.
 *
 * Returns 2 the frame the clock is *found* already at 90 (the arm below the
 * ladder), 1 the frame it first reaches it, 0 while it is still travelling; the
 * caller counts only the 1s, so a particle is banked exactly once. */
static int EneDmgParticleSuctionOne(ENEDMG_PARTICLE_ONE *pParticle,
                                    float *StartPos, float *SubVec,
                                    float Length, float LengthDiv6)
{                                                               /* 2426 */
    float bpos3[4];
    float f2;
    float PosRate;
    float RotRate;
    int   ret;

    ret = 0;                                                    /* 2431 */

    /* 1 - cos, built out of one sine.  The ROM reaches it through a g3dxVu0.h
       inline -- both the `1 - s*s` and the outer `1 - ` carry that header's
       line 2247, with the sqrt asm at 2252 -- and calls sinf exactly once.  `s`
       is the port's local; the ROM had no such variable, so writing the sine
       twice instead would call it twice. */
    float s = sinf(pParticle->cnt_f * EFE_PI / 90.0f * 0.5f);
    f2 = 1.0f - g3dxVu0Sqrt(1.0f - s * s);                      /* 2433 */

    PosRate = f2 * Length;                                      /* 2436 */
    RotRate = f2 * LengthDiv6;                                  /* 2437 */

    bpos3[0] = StartPos[0] + SubVec[0] * PosRate / Length;      /* 2438 */
    bpos3[1] = StartPos[1] + SubVec[1] * PosRate / Length;      /* 2439 */
    bpos3[2] = StartPos[2] + SubVec[2] * PosRate / Length;      /* 2440 */
    bpos3[3] = 1.0f;                                            /* 2441 */

    pParticle->rrad = sinf((f2 + 1.0f) * EFE_PI * 0.5f) * pParticle->rrad_max; /* 2443 */

    pParticle->rrot += pParticle->rot_spd * RotRate * 135.0f / 5.0f / Length;  /* 2445 */

    while (pParticle->rrot < -EFE_PI)                           /* 2446 */
    {
        pParticle->rrot += 6.28318501f;                         /* 2447 */
    }

    while (pParticle->rrot > EFE_PI)                            /* 2449 */
    {
        pParticle->rrot -= 6.28318501f;                         /* 2450 */
    }

    g3dxVu0CopyVector(pParticle->rpos, bpos3);                  /* 2452 */

    if (pParticle->cnt_f < 15.0f)                               /* 2454 */
    {
        pParticle->cnt_f += (pParticle->cnt_spd + pParticle->cnt_spd) * enedmg2_sp; /* 2455 */
    }
    else if (pParticle->cnt_f < 30.0f)                          /* 2456 */
    {
        pParticle->cnt_f += pParticle->cnt_spd * enedmg2_sp;    /* 2457 */
    }
    else if (pParticle->cnt_f < 90.0f)                          /* 2458 */
    {
        pParticle->cnt_f += pParticle->cnt_spd * 0.5f * enedmg2_sp; /* 2459 */
    }
    else
    {
        pParticle->cnt_f = 90.0f;                               /* 2461 */
        pParticle->ralp  = 0.0f;                                /* 2462 */
        ret = 2;                                                /* 2463 */
    }

    if (pParticle->cnt_f >= 90.0f)                              /* 2465 */
    {
        pParticle->cnt_f = 90.0f;                               /* 2466 */
        pParticle->ralp  = 0.0f;                                /* 2467 */

        if (ret == 0)                                           /* 2468 */
        {
            ret = 1;
        }
    }

    return ret;                                                 /* 2473 */
}

/* 2480 -- seed the comet tail.  Ten slots, all starting collapsed onto the
 * particle's own centre with an identity matrix, so the tail has no length
 * until the particle has actually moved. */
static void EneDmgParticleSuctionTailInit(ENEDMG_P_TAIL_WRK *pEneDmgTail,
                                          ENEDMG_PARTICLE_ONE *pParticle)
{
    int i;

    pEneDmgTail->NumMax = 10;                                   /* 2482 */
    pEneDmgTail->Num    = 1;                                    /* 2483 */
    pEneDmgTail->Top    = 1;                                    /* 2484 */

    for (i = 0; i < pEneDmgTail->NumMax; i++)                   /* 2487 */
    {
        sceVu0UnitMatrix(pEneDmgTail->LwMatrix[i]);             /* 2489 */
        g3dxVu0CopyVector(pEneDmgTail->OldPos[i], pParticle->Center); /* 2491 */
    }
}                                                               /* 2495 */

/* 2505 -- advance the comet tail and draw it.
 *
 * `tbl` is the ring walked newest-first from Top, so tbl[0] is the slot this
 * frame writes and tbl[1] the one it came from -- which is what the first
 * GetTrgtRot() measures, giving the direction the particle has just travelled.
 * That direction is *computed and wrapped and then never used*: the matrix the
 * frame is stored with faces the camera (rottt) instead.  `rot` is dead in the
 * ROM; the four wrap loops around it are dead with it.  Kept as found.
 *
 * Each ring slot becomes three vertices 8.9 apart across the flight direction,
 * so the whole tail is one triangle strip through DrawNewPerticleSub().  Once
 * the particle has landed (cnt_f 90) the ring stops advancing and Num winds
 * down instead, which is what fades the tail out behind it -- and reaching zero
 * is what this returns, i.e. "this particle is completely finished".
 *
 * Returns 1 for a NULL argument, which reads as finished; both callers hold the
 * tail array for the burst's whole life, so neither ever hits it. */
static int EneDmgParticleSuctionTail(ENEDMG_P_TAIL_WRK *pEneDmgTail,
                                     ENEDMG_PARTICLE_ONE *pParticle)
{                                                               /* 2505 */
    float wlm[4][4];
    /* .rodata 0x3a6260, copied into the frame -- three points across the tail's
       local X.  See [[rodata-blob-into-stack-is-a-local-initialiser]]. */
    float wpos[3][4] =                                          /* 2511 */
    {
        {  8.9f, 0.0f, 0.0f, 1.0f },
        {  0.0f, 0.0f, 0.0f, 1.0f },
        { -8.9f, 0.0f, 0.0f, 1.0f }
    };
    fixed_array<float[4], 30> wwpos;
    float rot[4];
    float rottt[4];
    GRA3DCAMERA *pCam;
    fixed_array<int, 10> tbl;
    int i;
    int n;
    int ret;
    /* .rodata 0x3a6290.  All three rows hold the same colour and only row 0 is
       ever read -- the ROM passes it as both of DrawNewPerticleSub's ends. */
    u_char rgb2[3][3] =                                         /* 2531 */
    {
        { 0x79, 0xae, 0xff },
        { 0x79, 0xae, 0xff },
        { 0x79, 0xae, 0xff }
    };

    pCam = gra3dGetCamera();                                    /* 2517 */
    float *cam_pos = gra3dcamGetPosition();                     /* 2518 */

    ret = 0;                                                    /* 2523 */

    if (pEneDmgTail == (ENEDMG_P_TAIL_WRK *)nullptr
        || pParticle == (ENEDMG_PARTICLE_ONE *)nullptr)         /* 2538 */
    {
        return 1;
    }

    n = pEneDmgTail->Top;
    for (i = 0; i < pEneDmgTail->NumMax; i++)                   /* 2541 */
    {
        tbl[i] = n;

        n--; if (n < 0) { n = pEneDmgTail->NumMax - 1; }        /* 2543 */
    }                                                           /* 2544 */

    /* Dead: rot is written here, wrapped four times, and never read. */
    GetTrgtRot(pEneDmgTail->OldPos[tbl[1]], pParticle->Center, rot, 3); /* 2547 */

    while (rot[0] <  -EFE_PI) { rot[0] += 6.28318501f; }        /* 2548 */
    while (rot[0] >=  EFE_PI) { rot[0] -= 6.28318501f; }        /* 2549 */
    while (rot[1] <  -EFE_PI) { rot[1] += 6.28318501f; }        /* 2550 */
    while (rot[1] >=  EFE_PI) { rot[1] -= 6.28318501f; }        /* 2551 */

    GetTrgtRot(cam_pos, pCam->vTarget, rottt, 3);               /* 2553 */

    sceVu0UnitMatrix(wlm);                                      /* 2555 */
    sceVu0RotMatrixY(wlm, wlm, rottt[1]);                       /* 2556 */
    sceVu0RotMatrixX(wlm, wlm, rottt[0]);                       /* 2557 */
    sceVu0TransMatrix(wlm, wlm, pParticle->Center);             /* 2558 */

    /* A g3dxVu0.h inline in the ROM (1291/1292, four lq/sq pairs); the port has
       no such header helper, and sceVu0CopyMatrix is the same four quadwords. */
    sceVu0CopyMatrix(pEneDmgTail->LwMatrix[tbl[0]], wlm);       /* 2560 */

    n = 0;
    for (i = 0; i < pEneDmgTail->Num; i++)                      /* 2562 */
    {
        sceVu0ApplyMatrix(wwpos[n * 3 + 0], pEneDmgTail->LwMatrix[tbl[i]], wpos[0]);
        sceVu0ApplyMatrix(wwpos[n * 3 + 1], pEneDmgTail->LwMatrix[tbl[i]], wpos[1]);
        sceVu0ApplyMatrix(wwpos[n * 3 + 2], pEneDmgTail->LwMatrix[tbl[i]], wpos[2]);
        n++;
    }                                                           /* 2566 */

    if (n != 0)                                                 /* 2568 */
    {
        DrawNewPerticleSub(n, &wwpos[0],
                           rgb2[0][0], rgb2[0][1], rgb2[0][2],
                           rgb2[0][0], rgb2[0][1], rgb2[0][2], 25);
    }

    if (pParticle->cnt_f < 90.0f)                               /* 2573 */
    {
        pEneDmgTail->Top = (pEneDmgTail->Top + 1) % pEneDmgTail->NumMax; /* 2574 */
        pEneDmgTail->Num = (pEneDmgTail->Num + 1 < pEneDmgTail->NumMax)
                         ? pEneDmgTail->Num + 1 : pEneDmgTail->NumMax;   /* 2575 */
    }
    else
    {
        pEneDmgTail->Num = (pEneDmgTail->Num - 1 < 0) ? 0 : pEneDmgTail->Num - 1; /* 2578 */
        ret = (pEneDmgTail->Num == 0);                          /* 2579 */
    }

    return ret;                                                 /* 2584 */
}

/* 2601 -- draw a trail as two gouraud triangle strips.
 *
 * `pos` is num groups of three points -- left edge, centre, right edge -- and
 * the two passes of the j loop draw (left, centre) and (centre, right), so the
 * trail is a ribbon whose middle is one colour and whose edges are the other.
 * Which colour goes where flips with j, which is what makes the centre bright
 * on both halves.
 *
 * Alpha tapers along the trail: `(num - i - 1) * a / num` is 1 at the head and
 * 0 at the tail, and outside player mode 6 it is multiplied by `i / num` again,
 * so the trail fades at *both* ends instead of just behind.
 *
 * Vertices go out as one REGLIST run of RGBAQ+XYZF pairs, and clipping is done
 * by the register list rather than by branching -- 0x41 is RGBAQ then XYZF2
 * (kicks), 0xc1 is RGBAQ then XYZF3 (does not).  Eight vertices fill the 64-bit
 * REGS word, so `cnt` reaching 8 closes the GIFtag at `tagnum`, opens a new one
 * and starts over.  A triangle is dropped unless all three of its corners
 * passed the guard-band test, and the very first vertex of each strip never
 * kicks because there is no triangle yet. */
static void DrawNewPerticleSub(int num, sceVu0FVECTOR *pos, u_char r1,
                               u_char g1, u_char b1, u_char r2, u_char g2,
                               u_char b2, u_char a)
{                                                               /* 2601 */
    u_int  clpz2;
    u_char rr1, gg1, bb1;
    u_char rr2, gg2, bb2;
    int    i, j, k;
    int    alp;
    int    cl;
    int    tagnum;
    int    cnt;
    fixed_array<int, 30>    clip;
    fixed_array<int[4], 30> ivec;
    fixed_array<u_long, 30> xyzf;
    u_long rgbaq1;
    u_long rgbaq2;
    u_long reg;
    int    ndpkt;
    Q_WORDDATA  *pbuf;
    GRA3DCAMERA *pCam;

    clpz2 = 0xffffff;                                           /* 2607 */

    pCam = gra3dGetCamera();                                    /* 2624 */

    if (EffWrkMonochroModeGet() != 0)                           /* 2627 */
    {
        rr1 = gg1 = bb1 = (u_char)(((int)r1 + (int)g1 + (int)b1) / 3); /* 2628 */
        rr2 = gg2 = bb2 = (u_char)(((int)r2 + (int)g2 + (int)b2) / 3); /* 2629 */
    }
    else
    {
        rr1 = r1;                                               /* 2631 */
        gg1 = g1;                                               /* 2632 */
        bb1 = b1;                                               /* 2633 */
        rr2 = r2;                                               /* 2634 */
        gg2 = g2;                                               /* 2635 */
        bb2 = b2;                                               /* 2636 */
    }

    rgbaq2 = (u_long)rr2 | ((u_long)gg2 << 8) | ((u_long)bb2 << 16); /* 2639 */

    sceVu0RotTransPersN(&ivec[0], pCam->matWorldScreen, pos, num * 3, 1); /* 2642 */

    for (i = 0; i < num * 3; i++)                               /* 2644 */
    {
        /* Guard band, in the GS's 12.4 screen coordinates: anything within 8
           pixels of either edge of the 4096-wide space, or with a z of 0 or
           past clpz2, is marked off.  No $LM of its own -- every access here is
           a fixed_array subscript. */
        clip[i] = 1;

        if ((u_int)ivec[i][0] < 0x80 || (u_int)ivec[i][0] > 0xff80)
        {
            clip[i] = 0;
        }
        if ((u_int)ivec[i][1] < 0x80 || (u_int)ivec[i][1] > 0xff80)
        {
            clip[i] = 0;
        }
        if (ivec[i][2] == 0 || (u_int)ivec[i][2] > clpz2)
        {
            clip[i] = 0;
        }

        xyzf[i] = (u_long)(u_int)ivec[i][0]
                | ((u_long)(u_int)ivec[i][1] << 16)
                | ((u_long)(u_int)ivec[i][2] << 32);
    }                                                           /* 2651 */

    /* .rodata 0x3a62a0, copied into the frame.  ALPHA 0x48 is additive by
       source alpha; TEST is ATST GEQUAL/0 with ZTST GREATER, so the trail is
       depth-tested but writes no depth (ZMSK in ZBUF). */
    DRAW_ENV_5 de =                                             /* 2657 */
    {
        0x0000000000000048ULL,      /* ALPHA  additive                         */
        0x0000000000000161ULL,      /* TEX1                                    */
        0x0000000000000000ULL,      /* CLAMP  repeat                           */
        0x0000000000050003ULL,      /* TEST   ATST GEQUAL/0, ZTST GREATER      */
        0x000000010a000118ULL       /* ZBUF   PSMZ24 @0x118, ZMSK              */
    };

    SetDrawEnv(0, &de);                                         /* 2668 */

    pbuf = StartDmaDirectTrans();                               /* 2672 */
    Reserve2DPacket(0x10);                                      /* 2674 */

    /* PRIM 0x14c: TRIANGLE_STRIP with IIP (gouraud) and ABE, no texture. */
    pbuf[0].ul64[0] = 0x2400400000008001ULL;                    /* 2676 */
    pbuf[0].ul64[1] = 0xf0;                 /* PRIM, NOP */     /* 2677 */
    pbuf[1].ul64[0] = 0x14c;                                    /* 2679 */
    pbuf[1].ul64[1] = 0;                                        /* 2680 */

    tagnum = 2; ndpkt = 3;                                      /* 2682 */
    reg = 0;                                                    /* 2683 */
    cnt = 0;                                                    /* 2684 */

    for (j = 0; j < 2; j++)                                     /* 2686 */
    {
        for (i = 0; i < num; i++)                               /* 2687 */
        {
            alp = (num - i - 1) * a / num;                      /* 2688 */

            if (plyr_wrk.cmn_wrk.mode != 6)                     /* 2690 */
            {
                alp = i * alp / num;                            /* 2691 */
            }

            k = j + i * 3;                                      /* 2693 */

            rgbaq1 = (u_long)rr1 | ((u_long)gg1 << 8) | ((u_long)bb1 << 16)
                   | ((u_long)alp << 24);                       /* 2694 */

            pbuf[ndpkt].ul64[0] = (j == 0) ? rgbaq2 : rgbaq1;   /* 2696 */
            pbuf[ndpkt].ul64[1] = xyzf[k];                      /* 2697 */
            ndpkt++;                                            /* 2698 */

            cl = (i == 0) ? 0 : clip[k - 3] + clip[k - 2] + clip[k]; /* 2699 */

            if (cl >= 3)                                        /* 2700 */
            {
                reg |= (u_long)0x41 << (cnt * 8);               /* 2701 */
            }
            else
            {
                reg |= (u_long)0xc1 << (cnt * 8);               /* 2703 */
            }

            cnt++;                                              /* 2705 */

            pbuf[ndpkt].ul64[0] = (j == 0) ? rgbaq1 : rgbaq2;   /* 2707 */
            pbuf[ndpkt].ul64[1] = xyzf[k + 1];                  /* 2708 */
            ndpkt++;                                            /* 2709 */

            cl = (i == 0) ? 0 : clip[k - 2] + clip[k] + clip[k + 1]; /* 2710 */

            if (cl >= 3)                                        /* 2711 */
            {
                reg |= (u_long)0x41 << (cnt * 8);               /* 2712 */
            }
            else
            {
                reg |= (u_long)0xc1 << (cnt * 8);               /* 2714 */
            }

            cnt++;                                              /* 2716 */

            if (cnt >= 8)                                       /* 2718 */
            {
                pbuf[tagnum].ul64[0] = ((u_long)(cnt * 2) << 60)
                                     | 0x0400400000008001ULL;   /* 2719 */
                pbuf[tagnum].ul64[1] = reg;                     /* 2720 */
                tagnum = ndpkt++;                               /* 2721 */
                reg = 0;                                        /* 2722 */
                cnt = 0;                                        /* 2723 */
            }
        }                                                       /* 2725 */
    }                                                           /* 2726 */

    if (cnt > 0)                                                /* 2727 */
    {
        pbuf[tagnum].ul64[0] = ((u_long)(cnt * 2) << 60)
                             | 0x0400400000008001ULL;           /* 2728 */
        pbuf[tagnum].ul64[1] = reg;                             /* 2729 */
    }
    else
    {
        ndpkt--;                                                /* 2731 */
    }

    EndDmaDirectTrans(&pbuf[ndpkt]);                            /* 2734 */

    /* PORT: the packet above is inert -- dmaVif1 discards it -- so the same
       ribbon is queued as a triangle list.  Each strip is rebuilt in the ROM's
       own emission order (two vertices per group, A then B), and a triangle is
       kicked on exactly the test the ADC bit encodes: the strip's last three
       vertices all inside the guard band.  `num` is at most 10 (the fixed_array
       bounds above), so 2 strips x 9 groups x 2 triangles fills 108 vertices.

       The colours are per vertex and that is the whole look: rgbaq2 is built
       with no alpha byte at all, so the edge it lands on is transparent while
       the centre carries `alp` -- and which side gets which flips with j, which
       is what makes the centre bright on both halves of the ribbon. */
    {
        float  sv[20][4];
        u_char sc[20][4];
        int    sk[20];
        static float  tri_pos[108][4];
        static u_char tri_col[108][4];
        int    nvtx = 0;
        int    t;

        for (j = 0; j < 2; j++)
        {
            int nv = 0;

            for (i = 0; i < num; i++)
            {
                int alpv = (num - i - 1) * a / num;             /* 2688 */
                int e;

                if (plyr_wrk.cmn_wrk.mode != 6)                 /* 2690 */
                {
                    alpv = i * alpv / num;                      /* 2691 */
                }

                for (e = 0; e < 2; e++)
                {
                    int kk = j + i * 3 + e;

                    g3dxVu0CopyVector(sv[nv], pos[kk]);

                    if (e == j)             /* the rgbaq2 side: no alpha */
                    {
                        sc[nv][0] = rr2; sc[nv][1] = gg2;
                        sc[nv][2] = bb2; sc[nv][3] = 0;
                    }
                    else                    /* the rgbaq1 side: the taper */
                    {
                        sc[nv][0] = rr1; sc[nv][1] = gg1;
                        sc[nv][2] = bb1; sc[nv][3] = (u_char)alpv;
                    }

                    sk[nv] = clip[kk];
                    nv++;
                }
            }

            for (t = 2; t < nv; t++)
            {
                int c;

                if (sk[t - 2] + sk[t - 1] + sk[t] < 3 || nvtx + 3 > 108)
                {
                    continue;
                }

                for (c = 0; c < 3; c++)
                {
                    int s = t - 2 + c;

                    g3dxVu0CopyVector(tri_pos[nvtx], sv[s]);
                    tri_col[nvtx][0] = sc[s][0];
                    tri_col[nvtx][1] = sc[s][1];
                    tri_col[nvtx][2] = sc[s][2];
                    tri_col[nvtx][3] = sc[s][3];
                    nvtx++;
                }
            }
        }

        if (nvtx != 0)
        {
            RendererGouraudTriangles3D(tri_pos, &tri_col[0][0], nvtx);
        }
    }
}

/* 2741 -- how many particles landed in the tray.  Read once a frame by
 * CNEquipTrayWrk::Work(), which banks 45 units of spirit power for each. */
int EneDmgParticleSuctionNumGet(void)
{
    return EneDmgParticleSuctionNum;                            /* 2742 */
}

/* ==========================================================================
 *  The large-hit flash
 * ======================================================================== */

/* 2752 / 2970 -- these two are byte-identical: the same loop clearing both
 * Status slots, emitted twice.  Kept as two functions, as the ROM has them. */
static void EneDmgLargeHitCtrlInit(void)
{
    int i;

    for (i = 0; i < 2; i++)                                     /* 2754 */
    {
        EneDmgLargeHitCtrl.Status[i] = 0;                       /* 2756 */
    }
}                                                               /* 2757 */

/* 2970 */
void EneDmgLargeHitAllOff(void)
{
    int i;

    for (i = 0; i < 2; i++)                                     /* 2972 */
    {
        EneDmgLargeHitCtrl.Status[i] = 0;                       /* 2974 */
    }
}                                                               /* 2975 */

/* 2764 -- step both flash slots.
 *
 * Slot 0 doubles as the frame grabber: on its first frame, and then every
 * CaptureInterval frames until CaptureNumber runs out, it copies the live
 * framebuffer to the back buffer and halves it.  The `(sys_wrk.count + 1) & 1`
 * picks whichever of the two buffers is not being drawn to this frame.
 *
 * Both slots then run the same ramp: t = NowFrame / AllFrame drives the scale
 * from 1.0 to LastScale, both alphas from their initial value to zero, and the
 * drift along the camera Z.  The rotation is wrapped into [-pi, pi] by two
 * bare while loops rather than by fmod. */
static void EneDmgLargeHitCtrlMain(void)
{
    int   i;
    float t;
    float Scale;
    float Rot;

    if (EneDmgLargeHitCtrl.Status[0] != 0)                      /* 2769 */
    {
        if (EneDmgLargeHitCtrl.Work[0].NowFrame == 0)           /* 2770 */
        {
            /* The ROM writes this address as a dsll32/dsra32 pair -- Ghidra
               prints ((x * 0x23) << 0x27) >> 0x20, which is (x * 0x23) << 7,
               i.e. x * 0x1180: the frame buffer that is currently on screen.
               0x23 on its own is a block address in the middle of nothing. */
            LocalCopyLtoB(0, 0, ((sys_wrk.count + 1) & 1) * 0x1180); /* 2772 */
            EffImageHalf32((u_int *)MioPan_GetHostPointer(0x1e79b00), 640, 448);
        }
        else if ((EneDmgLargeHitCtrl.Work[0].CaptureNumber != 0) &&
                 (EneDmgLargeHitCtrl.Work[0].CaptureInterval != 0)) /* 2775 */
        {
            if ((int)EneDmgLargeHitCtrl.Work[0].NowFrame
                    % (int)EneDmgLargeHitCtrl.Work[0].CaptureInterval == 0) /* 2776 */
            {
                LocalCopyLtoB(0, 0, ((sys_wrk.count + 1) & 1) * 0x1180); /* 2777 */
                EffImageHalf32((u_int *)MioPan_GetHostPointer(0x1e79b00), 640, 448);
                EneDmgLargeHitCtrl.Work[0].CaptureNumber--;     /* 2779 */
            }
        }
    }                                                           /* 2780 */

    for (i = 0; i < 2; i++)                                     /* 2786 */
    {
        if (EneDmgLargeHitCtrl.Status[i] == 0)
        {
            continue;
        }

        if (EneDmgLargeHitCtrl.Work[i].Delay != 0)              /* 2795 */
        {
            EneDmgLargeHitCtrl.Work[i].Delay--;                 /* 2822 */
            continue;
        }

        t = (float)EneDmgLargeHitCtrl.Work[i].NowFrame
            / (float)EneDmgLargeHitCtrl.Work[i].AllFrame;       /* 2798 */

        EneDmgLargeHitCtrl.Work[i].CenterPos[2] =
            -EneDmgLargeHitCtrl.Work[i].MoveDist * t;           /* 2801 */

        Scale = (EneDmgLargeHitCtrl.Work[i].LastScale - 1.0f) * t + 1.0f; /* 2804 */

        EneDmgLargeHitCtrl.Work[i].CenterRgba[3] = (u_char)
            (int)((float)EneDmgLargeHitCtrl.Work[i].InitCenterAlpha * (1.0f - t));
        EneDmgLargeHitCtrl.Work[i].OutsideRgba[3] = (u_char)
            (int)((float)EneDmgLargeHitCtrl.Work[i].InitOutsideAlpha * (1.0f - t));

        Rot = EneDmgLargeHitCtrl.Work[i].RotVal * t;            /* 2811 */

        while (Rot > EFE_PI)
        {
            Rot -= EFE_PI * 2.0f;
        }
        while (Rot < -EFE_PI)                                   /* 2812 */
        {
            Rot += EFE_PI * 2.0f;
        }

        EneDmgLargeHitEffectDisp(&EneDmgLargeHitCtrl.Work[i], Scale, Rot); /* 2813 */

        EneDmgLargeHitCtrl.Work[i].NowFrame++;                  /* 2815 */

        if (EneDmgLargeHitCtrl.Work[i].NowFrame
                >= EneDmgLargeHitCtrl.Work[i].AllFrame)         /* 2816 */
        {
            EneDmgLargeHitCtrl.Status[i] = 0;                   /* 2820 */
        }
    }                                                           /* 2823 */
}                                                               /* 2827 */

/* 2836 -- arm one flash.
 *
 * `EffectType` is a *preset* index 0..37, not an ENE_HIT_EFFECT_LABEL: the
 * switch is a flat 1:1 map onto the 38 EneDmgLargeHitEffect*Init helpers.  An
 * A/B pair therefore takes two calls, which is what EneHitEffectMain() does.
 *
 * The first free slot of the two wins and the request is dropped when both are
 * busy -- there is no eviction.  Delay = 1 makes the flash sit out the frame it
 * was armed on, so EneDmgLargeHitCtrlMain()'s frame-0 back-buffer capture sees
 * the scene *without* it. */
void EneDmgLargeHitReq(int EffectType)
{
    int i;

    for (i = 0; i < 2; i++)                                     /* 2839 */
    {
        /* No $LM of its own: the subscript goes through fixed_array. */
        if (EneDmgLargeHitCtrl.Status[i] != 0)                   /* 2840 */
        {
            continue;
        }

        EneDmgLargeHitCtrl.Status[i] = 1;                        /* 2841 */

        switch (EffectType)                                      /* 2842 */
        {
        case  0: EneDmgLargeHitEffectSmallInit(&EneDmgLargeHitCtrl.Work[i]);       break; /* 2845 */
        case  1: EneDmgLargeHitEffectInit(&EneDmgLargeHitCtrl.Work[i]);            break; /* 2848 */
        case  2: EneDmgLargeHitEffectSP_AInit(&EneDmgLargeHitCtrl.Work[i]);        break; /* 2851 */
        case  3: EneDmgLargeHitEffectSP_BInit(&EneDmgLargeHitCtrl.Work[i]);        break; /* 2854 */
        case  4: EneDmgLargeHitEffectSlow_AInit(&EneDmgLargeHitCtrl.Work[i]);      break; /* 2857 */
        case  5: EneDmgLargeHitEffectSlow_BInit(&EneDmgLargeHitCtrl.Work[i]);      break; /* 2860 */
        case  6: EneDmgLargeHitEffectZero_AInit(&EneDmgLargeHitCtrl.Work[i]);      break; /* 2863 */
        case  7: EneDmgLargeHitEffectZero_BInit(&EneDmgLargeHitCtrl.Work[i]);      break; /* 2866 */
        case  8: EneDmgLargeHitEffectZeroSC_AInit(&EneDmgLargeHitCtrl.Work[i]);    break; /* 2869 */
        case  9: EneDmgLargeHitEffectZeroSC_BInit(&EneDmgLargeHitCtrl.Work[i]);    break; /* 2872 */
        case 10: EneDmgLargeHitEffectZeroSP_AInit(&EneDmgLargeHitCtrl.Work[i]);    break; /* 2875 */
        case 11: EneDmgLargeHitEffectZeroSP_BInit(&EneDmgLargeHitCtrl.Work[i]);    break; /* 2878 */
        case 12: EneDmgLargeHitEffectKoku_AInit(&EneDmgLargeHitCtrl.Work[i]);      break; /* 2881 */
        case 13: EneDmgLargeHitEffectKoku_BInit(&EneDmgLargeHitCtrl.Work[i]);      break; /* 2884 */
        case 14: EneDmgLargeHitEffectKokuSC_AInit(&EneDmgLargeHitCtrl.Work[i]);    break; /* 2887 */
        case 15: EneDmgLargeHitEffectKokuSC_BInit(&EneDmgLargeHitCtrl.Work[i]);    break; /* 2890 */
        case 16: EneDmgLargeHitEffectKokuSP_AInit(&EneDmgLargeHitCtrl.Work[i]);    break; /* 2893 */
        case 17: EneDmgLargeHitEffectKokuSP_BInit(&EneDmgLargeHitCtrl.Work[i]);    break; /* 2896 */
        case 18: EneDmgLargeHitEffectParalyze_AInit(&EneDmgLargeHitCtrl.Work[i]);  break; /* 2899 */
        case 19: EneDmgLargeHitEffectParalyze_BInit(&EneDmgLargeHitCtrl.Work[i]);  break; /* 2902 */
        case 20: EneDmgLargeHitEffectView_AInit(&EneDmgLargeHitCtrl.Work[i]);      break; /* 2905 */
        case 21: EneDmgLargeHitEffectView_BInit(&EneDmgLargeHitCtrl.Work[i]);      break; /* 2908 */
        case 22: EneDmgLargeHitEffectMetsu_AInit(&EneDmgLargeHitCtrl.Work[i]);     break; /* 2911 */
        case 23: EneDmgLargeHitEffectMetsu_BInit(&EneDmgLargeHitCtrl.Work[i]);     break; /* 2914 */
        case 24: EneDmgLargeHitEffectMetsuSC_AInit(&EneDmgLargeHitCtrl.Work[i]);   break; /* 2917 */
        case 25: EneDmgLargeHitEffectMetsuSC_BInit(&EneDmgLargeHitCtrl.Work[i]);   break; /* 2920 */
        case 26: EneDmgLargeHitEffectMetsuSP_AInit(&EneDmgLargeHitCtrl.Work[i]);   break; /* 2923 */
        case 27: EneDmgLargeHitEffectMetsuSP_BInit(&EneDmgLargeHitCtrl.Work[i]);   break; /* 2926 */
        case 28: EneDmgLargeHitEffectRen_AInit(&EneDmgLargeHitCtrl.Work[i]);       break; /* 2929 */
        case 29: EneDmgLargeHitEffectRen_BInit(&EneDmgLargeHitCtrl.Work[i]);       break; /* 2932 */
        case 30: EneDmgLargeHitEffectRenSC_AInit(&EneDmgLargeHitCtrl.Work[i]);     break; /* 2935 */
        case 31: EneDmgLargeHitEffectRenSC_BInit(&EneDmgLargeHitCtrl.Work[i]);     break; /* 2938 */
        case 32: EneDmgLargeHitEffectRenSP_AInit(&EneDmgLargeHitCtrl.Work[i]);     break; /* 2941 */
        case 33: EneDmgLargeHitEffectRenSP_BInit(&EneDmgLargeHitCtrl.Work[i]);     break; /* 2944 */
        case 34: EneDmgLargeHitEffectTsui_AInit(&EneDmgLargeHitCtrl.Work[i]);      break; /* 2947 */
        case 35: EneDmgLargeHitEffectTsui_BInit(&EneDmgLargeHitCtrl.Work[i]);      break; /* 2950 */
        case 36: EneDmgLargeHitEffectFuu_AInit(&EneDmgLargeHitCtrl.Work[i]);       break; /* 2953 */
        case 37: EneDmgLargeHitEffectFuu_BInit(&EneDmgLargeHitCtrl.Work[i]);       break; /* 2956 */
        }

        /* Reached by the default arm too -- an out-of-range EffectType still
           claims the slot and arms an uninitialised flash.  The ROM's own
           order; nothing in the build passes one. */
        EneDmgLargeHitCtrl.Work[i].Delay = 1;                    /* 2960 */
        return;                                                  /* 2961 */
    }                                                            /* 2963 */
}

/* 2985 -- unpack one preset into a live flash.
 *
 * Note the per-field divisors: Size and LastScale by 100, MoveDist and
 * Distance by 10, and RotVal offset by 3600 before the degrees-to-radians
 * conversion.  The five AlphaBlend selectors are packed into the GS ALPHA
 * register layout -- A at bits 0-1, B at 2-3, C at 4-5, D at 6-7 and FIX at
 * 32-39. */
static void EneDmgLargeHitInit(ENE_DMG_LARGE_HIT *pLargeHit,
                               const ENE_DMG_LARGE_HIT_PARAMETER *pLHParam)
{
    int i;

    for (i = 0; i < 4; i++)                                     /* 2987 */
    {
        pLargeHit->CenterRgba[i]  = (u_char)pLHParam->CenterRgba[i];
        pLargeHit->OutsideRgba[i] = (u_char)pLHParam->OutsideRgba[i];
    }

    pLargeHit->VertexNumW = pLHParam->VertexNumW;
    pLargeHit->VertexNumH = pLHParam->VertexNumH;

    pLargeHit->NowFrame = 0;
    pLargeHit->AllFrame = pLHParam->AllFrame;

    pLargeHit->InitCenterAlpha  = pLHParam->CenterRgba[3];
    pLargeHit->InitOutsideAlpha = pLHParam->OutsideRgba[3];

    pLargeHit->Scale     = (float)pLHParam->Size      / 100.0f;
    pLargeHit->LastScale = (float)pLHParam->LastScale / 100.0f;
    pLargeHit->MoveDist  = (float)pLHParam->MoveDist  /  10.0f;
    pLargeHit->Distance  = (float)pLHParam->Distance  /  10.0f;

    pLargeHit->CaptureInterval = pLHParam->CaptureInterval;
    pLargeHit->CaptureNumber   = pLHParam->CaptureNumber;

    pLargeHit->AlphaBlend = (u_long)pLHParam->AlphaBlendA
                          | ((u_long)pLHParam->AlphaBlendB << 2)
                          | ((u_long)pLHParam->AlphaBlendC << 4)
                          | ((u_long)pLHParam->AlphaBlendD << 6)
                          | ((u_long)pLHParam->AlphaBlendFIX << 32);

    pLargeHit->RotVal = (float)(pLHParam->RotVal - 3600) * (EFE_PI / 180.0f);

    pLargeHit->CenterPos[0] = 0.0f;
    pLargeHit->CenterPos[1] = 0.0f;
    pLargeHit->CenterPos[2] = 0.0f;
    pLargeHit->CenterPos[3] = 1.0f;
}                                                               /* 3009 */

/* The 38 preset selectors.  Each is the same three lines with a different
 * index; the index order is exactly the declaration order of the presets
 * above, which is also the order of the .rodata pointer table. */

/* 3021 -- SmallHit */
static void EneDmgLargeHitEffectSmallInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(0);                           /* 3025 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3028 */
}

/* 3037 -- LargeHitType00 */
static void EneDmgLargeHitEffectInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(1);                           /* 3041 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3044 */
}

/* 3056 -- LargeHitSPAType00 */
static void EneDmgLargeHitEffectSP_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(2);                           /* 3060 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3063 */
}

/* 3075 -- LargeHitSPBType00 */
static void EneDmgLargeHitEffectSP_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(3);                           /* 3079 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3082 */
}

/* 3094 -- SlowHitAType00 */
static void EneDmgLargeHitEffectSlow_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(4);                           /* 3098 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3101 */
}

/* 3112 -- SlowHitBType00 */
static void EneDmgLargeHitEffectSlow_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(5);                           /* 3116 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3119 */
}

/* 3132 -- ZeroHitA */
static void EneDmgLargeHitEffectZero_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(6);                           /* 3136 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3139 */
}

/* 3152 -- ZeroHitB */
static void EneDmgLargeHitEffectZero_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(7);                           /* 3156 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3159 */
}

/* 3171 -- ZeroHitSCA */
static void EneDmgLargeHitEffectZeroSC_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(8);                           /* 3175 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3178 */
}

/* 3191 -- ZeroHitSCB */
static void EneDmgLargeHitEffectZeroSC_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(9);                           /* 3195 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3198 */
}

/* 3210 -- ZeroHitSPA */
static void EneDmgLargeHitEffectZeroSP_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(10);                          /* 3214 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3217 */
}

/* 3230 -- ZeroHitSPB */
static void EneDmgLargeHitEffectZeroSP_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(11);                          /* 3234 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3237 */
}

/* 3250 -- KokuHitA */
static void EneDmgLargeHitEffectKoku_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(12);                          /* 3254 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3257 */
}

/* 3270 -- KokuHitB */
static void EneDmgLargeHitEffectKoku_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(13);                          /* 3274 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3277 */
}

/* 3289 -- KokuHitSCA */
static void EneDmgLargeHitEffectKokuSC_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(14);                          /* 3293 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3296 */
}

/* 3308 -- KokuHitSCB */
static void EneDmgLargeHitEffectKokuSC_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(15);                          /* 3312 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3315 */
}

/* 3327 -- KokuHitSPA */
static void EneDmgLargeHitEffectKokuSP_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(16);                          /* 3331 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3334 */
}

/* 3346 -- KokuHitSPB */
static void EneDmgLargeHitEffectKokuSP_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(17);                          /* 3350 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3353 */
}

/* 3597 -- ParalyzeHitA */
static void EneDmgLargeHitEffectParalyze_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(18);                          /* 3601 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3604 */
}

/* 3616 -- ParalyzeHitB */
static void EneDmgLargeHitEffectParalyze_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(19);                          /* 3620 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3623 */
}

/* 3635 -- ViewHitA */
static void EneDmgLargeHitEffectView_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(20);                          /* 3639 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3642 */
}

/* 3654 -- ViewHitB */
static void EneDmgLargeHitEffectView_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(21);                          /* 3658 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3661 */
}

/* 3366 -- MetsuHitA */
static void EneDmgLargeHitEffectMetsu_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(22);                          /* 3370 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3373 */
}

/* 3386 -- MetsuHitB */
static void EneDmgLargeHitEffectMetsu_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(23);                          /* 3390 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3393 */
}

/* 3405 -- MetsuHitSCA */
static void EneDmgLargeHitEffectMetsuSC_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(24);                          /* 3409 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3412 */
}

/* 3424 -- MetsuHitSCB */
static void EneDmgLargeHitEffectMetsuSC_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(25);                          /* 3428 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3431 */
}

/* 3443 -- MetsuHitSPA */
static void EneDmgLargeHitEffectMetsuSP_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(26);                          /* 3447 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3450 */
}

/* 3462 -- MetsuHitSPB */
static void EneDmgLargeHitEffectMetsuSP_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(27);                          /* 3466 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3469 */
}

/* 3482 -- RenHitA */
static void EneDmgLargeHitEffectRen_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(28);                          /* 3486 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3489 */
}

/* 3502 -- RenHitB */
static void EneDmgLargeHitEffectRen_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(29);                          /* 3506 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3509 */
}

/* 3521 -- RenHitSCA */
static void EneDmgLargeHitEffectRenSC_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(30);                          /* 3525 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3528 */
}

/* 3540 -- RenHitSCB */
static void EneDmgLargeHitEffectRenSC_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(31);                          /* 3544 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3547 */
}

/* 3559 -- RenHitSPA */
static void EneDmgLargeHitEffectRenSP_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(32);                          /* 3563 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3566 */
}

/* 3578 -- RenHitSPB */
static void EneDmgLargeHitEffectRenSP_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(33);                          /* 3582 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3585 */
}

/* 3673 -- TsuiHitA */
static void EneDmgLargeHitEffectTsui_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(34);                          /* 3677 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3680 */
}

/* 3692 -- TsuiHitB */
static void EneDmgLargeHitEffectTsui_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(35);                          /* 3696 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3699 */
}

/* 3711 -- FuuHitA */
static void EneDmgLargeHitEffectFuu_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(36);                          /* 3715 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3718 */
}

/* 3730 -- FuuHitB */
static void EneDmgLargeHitEffectFuu_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    ENE_DMG_LARGE_HIT_PARAMETER *pLHParam;

    pLHParam = EffEneDmgLargeHitParameterPtrGet(37);                          /* 3734 */
    EneDmgLargeHitInit(pLargeHit, pLHParam);                          /* 3737 */
}

/* ==========================================================================
 *  The flash geometry
 * ======================================================================== */

/* 3747 -- draw one flash.
 *
 * The flash is a screen refraction, not a sprite.  LocalCopyBtoL() copies the
 * back buffer into local memory at 0x2bc0 and the ring is drawn textured with
 * it, each vertex carrying the STQ of the screen point *it* sits over -- so the
 * ring re-displays the scene behind it, pulled about by its own geometry.  That
 * is why there is no texture number anywhere in the file.
 *
 * The ring lives on a billboard 25 units square, turned to face the camera by
 * Vector2Rot() of the camera's own direction and pushed `Distance` units towards
 * it, then scaled and spun by the caller's ramp.  MakeVertex builds it in local
 * space, CalcStqXYZ projects each vertex through the *unspun* matrix (so the
 * screen sample stays put while the ring turns over it), and MakePacket draws
 * it as one triangle fan under the spun one. */
static void EneDmgLargeHitEffectDisp(ENE_DMG_LARGE_HIT *pLargeHit, float Scale,
                                     float RotZ)
{                                                               /* 3747 */
    float LocalWorldMat[4][4];
    float LocalScreenMat[4][4];
    float RotScaleMat[4][4];
    float stqparam[3][4];
    fixed_array<float[4], 256> VtxBuf;
    fixed_array<float[4], 256> StqBuf;
    float Pos[4];
    int   VertexNum;
    float EffectW;
    float EffectH;
    int   i;
    float RotX;
    float RotY;
    GRA3DCAMERA *pCam;

    pCam = gra3dGetCamera();                                    /* 3762 */
    float *pDir    = gra3dcamGetDirection();                    /* 3763 */
    float *pCamPos = gra3dcamGetPosition();                     /* 3764 */

    /* Back buffer -> local 0x2bc0, which is the TEX0 MakePacket installs. */
    LocalCopyBtoL(2, 0, 0x2bc0);                                /* 3768 */

    sceVu0ScaleVector(Pos, pDir, pLargeHit->Distance);          /* 3771 */
    sceVu0AddVector(Pos, Pos, pCamPos);                         /* 3772 */

    /* One centre vertex, then the perimeter of a VertexNumW x VertexNumH grid
       with the first perimeter vertex repeated to close the fan:
       1 + (W-1) + (H-1) + (W-1) + H = 2*(W + H) - 2. */
    VertexNum = (pLargeHit->VertexNumW + pLargeHit->VertexNumH) * 2 - 2; /* 3775 */

    Vector2Rot(pDir, &RotX, &RotY);                             /* 3778 */

    sceVu0UnitMatrix(LocalWorldMat);                            /* 3780 */
    LocalWorldMat[0][0] = 25.0f; LocalWorldMat[1][1] = 25.0f; LocalWorldMat[2][2] = 25.0f; /* 3781 */
    sceVu0RotMatrixX(LocalWorldMat, LocalWorldMat, RotX);       /* 3782 */
    sceVu0RotMatrixY(LocalWorldMat, LocalWorldMat, RotY);       /* 3783 */
    sceVu0TransMatrix(LocalWorldMat, LocalWorldMat, Pos);       /* 3784 */
    sceVu0MulMatrix(LocalScreenMat, pCam->matWorldScreen, LocalWorldMat); /* 3785 */

    /* The screen-sample parameters -- see CalcStqXYZ in effect_obj.h.  1727.5
       and 1823.5 are the GS XYOFFSET for a 640x448 frame (2048 - 320.5 and
       2048 - 224.5); 639/447 clamp to it; 1/1024 and 1/512 normalise into the
       512x512 texture the copy landed in, and the trailing 1/16 scales s, t and
       q together so it cancels in s/q.  stqparam[0][3] is deliberately never
       written -- nothing reads vf8.w. */
    stqparam[0][0] = 1727.5f;                                   /* 3788 */
    stqparam[0][1] = 1823.5f;                                   /* 3789 */
    stqparam[0][2] = 1.0f;                                      /* 3790 */
    EffectSetScreenSampleClamp(stqparam[1]);                    /* 3791 */
    stqparam[2][0] = 0.0009765625f; stqparam[2][1] = 0.001953125f; stqparam[2][2] = 1.0f; stqparam[2][3] = 0.0625f; /* 3792 */

    /* PORT DEVIATION: the ROM primed vf4..vf7 with LocalScreenMat here
       (gra3dVu0.h 119) and vf8..vf10 with stqparam (effect_obj.h 199/200), then
       ran CalcStqXYZ against the register file.  Both are parameters here --
       the deviation gra3dVu0.h:189 and CalcPartsDeformXYZ already carry. */

    EffectW = pLargeHit->Scale * 3.1999998f;                    /* 3798 */
    EffectH = pLargeHit->Scale * 2.23999977f;                   /* 3799 */

    EneDmgLargeHitMakeVertex(&VtxBuf[0], pLargeHit->CenterPos, EffectW, EffectH,
                             pLargeHit->VertexNumW, pLargeHit->VertexNumH); /* 3801 */

    for (i = 0; i < VertexNum; i++)                             /* 3802 */
    {
        CalcStqXYZ(0, &VtxBuf[i], &StqBuf[i], LocalScreenMat, stqparam); /* 3803 */
    }                                                           /* 3804 */

    /* The spin and the ramped scale go on *after* the STQ pass, so the ring
       turns over a screen sample that does not turn with it. */
    sceVu0UnitMatrix(RotScaleMat);                              /* 3807 */
    RotScaleMat[0][0] = Scale; RotScaleMat[1][1] = Scale; RotScaleMat[2][2] = Scale; /* 3808 */
    sceVu0RotMatrixZ(RotScaleMat, RotScaleMat, RotZ);           /* 3809 */
    sceVu0MulMatrix(LocalWorldMat, LocalWorldMat, RotScaleMat); /* 3810 */

    /* GCC builds a partially-constant aggregate initialiser in a temp and then
       copies it into the named object, which is why the ROM emits the five
       stores twice.  ZTST ALWAYS plus ZMSK means the flash neither tests depth
       nor writes it -- it is drawn over everything and occludes nothing. */
    const DRAW_ENV_5 DrawEnv =                                  /* 3813 */
    {
        pLargeHit->AlphaBlend,
        0x0000000000000161ULL,      /* TEX1   MMAG/MMIN LINEAR                 */
        0x0000000000000005ULL,      /* CLAMP  clamp s and t                    */
        0x000000000003000dULL,      /* TEST   ATST GEQUAL/0, ZTST ALWAYS       */
        0x000000010a000118ULL       /* ZBUF   PSMZ24 @0x118, ZMSK (no z write) */
    };

    SetDrawEnv(0, &DrawEnv);                                    /* 3820 */

    /* TEX0: TBP0 0x2bc0 (the copy above), TBW 5, PSM PSMCT24, TW/TH 9 (512
       square), TCC RGB, TFX MODULATE, CLD 1. */
    EneDmgLargeHitMakePacket(&VtxBuf[0], VertexNum, LocalWorldMat,
                             pLargeHit->CenterRgba.data(),
                             pLargeHit->OutsideRgba.data(),
                             &StqBuf[0], 0x2000000264116bc0ULL); /* 3822 */
}

/* 3837 -- build the ring.
 *
 * Vertex 0 is Center itself (the fan's hub, and the only one that carries the
 * centre colour); the rest walk the perimeter of a Width x Height rectangle
 * anticlockwise from the top-left corner, VertexNumW-1 steps along each
 * horizontal edge and VertexNumH-1 along each vertical one.
 *
 * The fourth edge's bound is VertexNumH, not VertexNumH-1.  That extra vertex
 * lands back on the first perimeter one and is what closes the fan -- it is the
 * reason the count is 2*(W + H) - 2 rather than 2*(W + H) - 3. */
static void EneDmgLargeHitMakeVertex(sceVu0FVECTOR *pVtxBuf, float *Center,
                                     float Width, float Height,
                                     u_int VertexNumW, u_int VertexNumH)
{                                                               /* 3837 */
    int   VertexCount;
    float CalcX;
    float CalcY;
    float VertexUnitW;
    float VertexUnitH;
    u_int i;

    VertexUnitW = Width  / (float)(VertexNumW - 1);             /* 3843 */
    VertexUnitH = Height / (float)(VertexNumH - 1);             /* 3844 */

    g3dxVu0CopyVector(pVtxBuf[0], Center);                      /* 3846 */

    VertexCount = 1;                                            /* 3848 */

    CalcX = -Width * 0.5f;                                      /* 3850 */
    CalcY = -Height * 0.5f;                                     /* 3851 */
    for (i = 0; i < VertexNumW - 1; i++)                        /* 3852 */
    {
        pVtxBuf[VertexCount][0] = CalcX; pVtxBuf[VertexCount][1] = CalcY; pVtxBuf[VertexCount][2] = 0.0f; pVtxBuf[VertexCount][3] = 1.0f; /* 3853 */
        CalcX += VertexUnitW;                                   /* 3854 */
        VertexCount++;                                          /* 3855 */
    }                                                           /* 3856 */

    CalcX = Width * 0.5f;                                       /* 3858 */
    CalcY = -Height * 0.5f;                                     /* 3859 */
    for (i = 0; i < VertexNumH - 1; i++)                        /* 3860 */
    {
        pVtxBuf[VertexCount][0] = CalcX; pVtxBuf[VertexCount][1] = CalcY; pVtxBuf[VertexCount][2] = 0.0f; pVtxBuf[VertexCount][3] = 1.0f; /* 3861 */
        CalcY += VertexUnitH;                                   /* 3862 */
        VertexCount++;                                          /* 3863 */
    }                                                           /* 3864 */

    CalcX = Width * 0.5f;                                       /* 3866 */
    CalcY = Height * 0.5f;                                      /* 3867 */
    for (i = 0; i < VertexNumW - 1; i++)                        /* 3868 */
    {
        pVtxBuf[VertexCount][0] = CalcX; pVtxBuf[VertexCount][1] = CalcY; pVtxBuf[VertexCount][2] = 0.0f; pVtxBuf[VertexCount][3] = 1.0f; /* 3869 */
        CalcX -= VertexUnitW;                                   /* 3870 */
        VertexCount++;                                          /* 3871 */
    }                                                           /* 3872 */

    CalcX = -Width * 0.5f;                                      /* 3874 */
    CalcY = Height * 0.5f;                                      /* 3875 */
    for (i = 0; i < VertexNumH; i++)                            /* 3876 */
    {
        pVtxBuf[VertexCount][0] = CalcX; pVtxBuf[VertexCount][1] = CalcY; pVtxBuf[VertexCount][2] = 0.0f; pVtxBuf[VertexCount][3] = 1.0f; /* 3877 */
        CalcY -= VertexUnitH;                                   /* 3878 */
        VertexCount++;                                          /* 3879 */
    }                                                           /* 3880 */
}

/* 3888 -- emit the ring's GIF packet.
 *
 * One TRIANGLE_FAN over pVertexBuf, hub first, with the centre colour on the
 * hub and the outside colour on every rim vertex -- which is the whole of the
 * flash's gradient; there is no per-vertex interpolation beyond the GS's own
 * gouraud across each triangle.
 *
 * Culling is by register list rather than by branch, the same trick
 * MakePartsDeformPacket uses: each vertex's XYZ register is XYZF2 (0x4, kicks
 * the triangle) or XYZF3 (0xc, does not).  The first two vertices never kick --
 * a fan needs two registered before it can complete a triangle -- and after
 * that a triangle is dropped only when *all three* of its corners (the hub,
 * the previous rim vertex and this one) are outside the guard band. */
void EneDmgLargeHitMakePacket(sceVu0FVECTOR *pVertexBuf, int VertexNum,
                              float (*LocalWorldMat)[4], u_char *pCenterRgba,
                              u_char *pOutsideRgba, sceVu0FVECTOR *pStq,
                              u_long tex0)
{                                                               /* 3888 */
    fixed_array<int[4], 256> VertexBuf_I;
    fixed_array<int, 256>    clip;
    Q_WORDDATA  *pPacket;
    int          i;
    u_long      *plong;
    GRA3DCAMERA *pCam;

    /* PORT DEVIATION: the ROM left both products in vf4..vf11 and passed
       nothing -- see CalcPartsDeformXYZ and gra3dVu0.h:189. */
    float matLocalScreen[4][4];
    float matLocalClipPolygon[4][4];

    pCam = gra3dGetCamera();                                    /* 3896 */
    gra3dVu0ApplyMatrix2(matLocalScreen,      pCam->matWorldScreen,      LocalWorldMat,
                         matLocalClipPolygon, pCam->matWorldClipPolygon, LocalWorldMat);

    for (i = 0; i < VertexNum; i++)                             /* 3903 */
    {
        /* No $LM of its own: both subscripts go through fixed_array. */
        clip[i] = CalcPartsDeformXYZ(VertexBuf_I[i], pVertexBuf[i],
                                     matLocalScreen, matLocalClipPolygon,
                                     nullptr) & 0x3f;
    }                                                           /* 3905 */

    pPacket = StartDmaDirectTrans();                            /* 3908 */

    /* Two A+D writes: flush the texture cache, then install the frame-buffer
       copy as this draw's texture. */
    pPacket->ul64[0] = 0x1000000000008002ULL;                   /* 3911 */
    pPacket->ul64[1] = 0x0e;                /* A+D */           /* 3912 */
    pPacket++;                                                  /* 3913 */
    pPacket->ul64[0] = 0;                                       /* 3915 */
    pPacket->ul64[1] = 0x3f;                /* TEXFLUSH */      /* 3916 */
    pPacket++;                                                  /* 3917 */
    pPacket->ul64[0] = tex0;                                    /* 3919 */
    pPacket->ul64[1] = 0x06;                /* TEX0_1 */        /* 3920 */
    pPacket++;                                                  /* 3921 */

    /* Then the PRIM the fan is drawn with.  0x5d is TRIANGLE_FAN with IIP
       (gouraud), TME (textured) and ABE (alpha blend); FGE is off. */
    pPacket->ul64[0] = 0x2400400000008001ULL;                   /* 3923 */
    pPacket->ul64[1] = 0xf0;                /* PRIM, NOP */     /* 3924 */
    pPacket++;                                                  /* 3925 */
    pPacket->ul64[0] = 0x5d;                                    /* 3928 */
    pPacket->ul64[1] = 0;                                       /* 3929 */
    pPacket++;                                                  /* 3930 */

    for (i = 0; i < VertexNum; i++)                             /* 3932 */
    {
        if (i < 2 || (clip[0] != 0 && clip[i - 1] != 0 && clip[i] != 0)) /* 3933 */
        {
            pPacket->ul64[1] = 0x00000c12;  /* ST RGBAQ XYZF3 */
        }
        else
        {
            pPacket->ul64[1] = 0x00000412;  /* ST RGBAQ XYZF2 */ /* 3939 */
        }

        pPacket->ul64[0] = 0x3400400000008001ULL;  /* REGLIST, NREG 3 */ /* 3946 */

        plong = (u_long *)(pPacket + 1);                        /* 3949 */

        /* ST is two floats in one quadword half and the ROM copies them as raw
           words, so they go through a u_int view rather than the FPU. */
        ((u_int *)plong)[0] = *(const u_int *)&pStq[i][0];      /* 3950 */
        ((u_int *)plong)[1] = *(const u_int *)&pStq[i][1];      /* 3951 */

        if (i != 0)                                             /* 3952 */
        {
            plong[1] = (u_long)pOutsideRgba[0]
                     | ((u_long)pOutsideRgba[1] << 8)
                     | ((u_long)pOutsideRgba[2] << 16)
                     | ((u_long)pOutsideRgba[3] << 24)
                     | ((u_long)*(const u_int *)&pStq[i][2] << 32); /* 3953 */
        }
        else
        {
            plong[1] = (u_long)pCenterRgba[0]
                     | ((u_long)pCenterRgba[1] << 8)
                     | ((u_long)pCenterRgba[2] << 16)
                     | ((u_long)pCenterRgba[3] << 24)
                     | ((u_long)*(const u_int *)&pStq[i][2] << 32); /* 3956 */
        }

        plong[2] = (u_long)(u_int)VertexBuf_I[i][0]
                 | ((u_long)(u_int)VertexBuf_I[i][1] << 16)
                 | ((u_long)(u_int)VertexBuf_I[i][2] << 32);

        /* NREG 3 is 24 bytes; the fourth slot pads the REGLIST out to a whole
           quadword and the ROM zeroes it rather than leaving it stale. */
        plong[3] = 0;                                           /* 3959 */

        pPacket += 3;                                           /* 3961 */
    }                                                           /* 3962 */

    EndDmaDirectTrans(pPacket);                                 /* 3964 */

    /* PORT: the fan above is inert -- dmaVif1 discards it -- so the same fan
     * goes to the renderer as a triangle list.  Triangle i is (centre, i-1, i),
     * dropped on the ROM's own test: all three vertices outside the guard band.
     *
     * VertexBuf_I holds GS window coordinates in 12.4 fixed, so the XYOFFSET
     * the 3D env installs comes back off -- 1728 = 2048 - 320, 1824 = 2048 -
     * 224, the 640x448 frame centred in the GS's 4096x4096 space.  ST is
     * normalised and the GS divides it by Q, so the bridge is handed S/Q, T/Q
     * and told the coordinates are already normalised. */
    {
        static float  xy[256 * 3 * 2];
        static float  uv[256 * 3 * 2];
        static u_char rgba[256 * 3 * 4];
        int n = 0;

        for (i = 2; i < VertexNum; i++)
        {
            int corner[3];
            int c;

            if (clip[0] != 0 && clip[i - 1] != 0 && clip[i] != 0)
            {
                continue;
            }

            corner[0] = 0;
            corner[1] = i - 1;
            corner[2] = i;

            for (c = 0; c < 3; c++)
            {
                int            v = corner[c];
                const u_char  *col = (v == 0) ? pCenterRgba : pOutsideRgba;
                float          q = pStq[v][2];

                if (q == 0.0f)
                {
                    q = 1.0f;
                }

                xy[n * 2 + 0] = (float)VertexBuf_I[v][0] / 16.0f - 1728.0f;
                xy[n * 2 + 1] = (float)VertexBuf_I[v][1] / 16.0f - 1824.0f;
                uv[n * 2 + 0] = pStq[v][0] / q;
                uv[n * 2 + 1] = pStq[v][1] / q;
                rgba[n * 4 + 0] = col[0];
                rgba[n * 4 + 1] = col[1];
                rgba[n * 4 + 2] = col[2];
                rgba[n * 4 + 3] = col[3];
                n++;
            }
        }

        if (n != 0)
        {
            /* No depth: both callers' draw envs are TEST 0x3000d -- ZTE with
               ZTST ALWAYS -- so the flash is drawn over everything and
               occludes nothing.  Hence the NULL rather than a depth array. */
            MioPan_RendererDrawTexturedTriangles2D((sceGsTex0 *)&tex0, xy, uv,
                                                   rgba, nullptr, n, 1, 1, 0);
        }
    }
}

/* ==========================================================================
 *  Preset lookups
 * ======================================================================== */

/* 3971 -- both of these are a bare bounds-checked subscript; the check is
 * fixed_array's own, which asserts rather than clamping. */
ENE_DMG_LARGE_HIT_PARAMETER *EffEneDmgLargeHitParameterPtrGet(int Label)
{
    return pLargeHitParameter[Label];                           /* 3971 */
}

/* 3981 */
ENE_DMG_BLUR_CONTRAST_PARAMETER *EffEneDmgLargeHitBlurParameterPtrGet(int Label)
{
    return pLargeHitBlurParameter[Label];                       /* 3981 */
}
