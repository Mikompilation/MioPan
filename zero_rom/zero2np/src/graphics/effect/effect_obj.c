// FILE: /home/zero_rom/zero2np/src/graphics/effect/effect_obj.c
//
// Placed-object effects (effect_obj.o, .text 0x00149d40..0x00151cd8 = 0x7f98).
// 91 functions, 38 of them ZERO2.MAP exports; all 38 and all 91 are
// reconstructed, verified 38/38 against the map and 91/91 against
// functions.txt.
//
// Six sub-systems live here, and only the first is large:
//
//   parts deform   the 17x17 refracting grid.  Seven arming calls into one
//                  SetEffects_PDEFORM(), two passes -- SubPartsDeform1 for
//                  per-effect shapes and SubPartsDeform2 for the shared ones --
//                  twelve per-vertex kernels, and one packet builder.  About
//                  half the module.
//   lens flare     nine ghosts along the light-to-centre line plus a star
//                  burst, gated by how nearly the camera faces the light.
//   light shafts   a scrolling beam whose alpha follows camera angle or
//                  distance, with three authored vertex-colour variants.
//   water flow     per-surface S/T scroll from a two-level rate lookup.
//   camera flash   the burst from the player's own camera: a refracting
//                  corona out of the frame buffer plus a textured flare.
//   model alpha / lantern   two small list-driven ramps.
//
// All static data is verified against the ROM's bytes: both 17x17 alpha masks,
// the two water-cue position sets, the nine-ghost flare table, and the fact
// that the 92 KB `efi` work area really is entirely zero.  Every float literal
// is checked bit-for-bit against the ROM's .lit4 block (0x3edb88..0x3edce0),
// which is where the one-ulp-low spellings come from.
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

#include "effect_obj.h"

#include <math.h>                               /* cosf / sinf */
#include <stdio.h>                              /* printf */
#include <stdlib.h>                             /* rand */
#include <string.h>                             /* memset */

#include "effect.h"                             /* EffectGetRandom / EFFECT_CONT */
#include "effect_pak.h"                         /* Reserve2DPacket */
#include "effect_spr.h"                         /* SetEffSQTex */
#include "effect_sub.h"                         /* Vector2Rot / GetCamI2DPos */

#include "../draw_env.h"
#include "../graph2d/g2d_draw.h"                /* LocalCopyLtoL / StartDmaDirectTrans */
#include "../graph2d/tim2.h"
#include "../graph3d/g3dxVu0.h"
#include "../graph3d/gra3d.h"
#include "../graph3d/gra3dSGD.h"                 /* gra3dChangeST */
#include "../graph3d/gra3dVu0.h"                 /* gra3dVu0ClipFlags / ApplyMatrix2 */
#include "../graph3d/ctl/fixed_array.h"
#include "../motion/mdlwork.h"                  /* ManmdlSetAlpha / ANI_CTRL */
#include "../motion/motion.h"                   /* motGetLocalWorldMatrix */
#include "../../ingame/plyr/plyr_mdl.h"         /* plyr_mdlGetANI_CTRL */
#include "effect_ene.h"                         /* EneDmgLargeHitMakePacket */
#include "../../common/SingleLinkList.h"
#include "../../common/utility.h"
#include "../../common/variable.h"              /* sys_wrk */
#include "../../miopan/miopan_memory.h"         /* MioPan_GetHostPointer */
#include "../../miopan/rendering/miopan_renderer.h"
#include "../../sdk/libvu0.h"

/* ==========================================================================
 *  Live state
 * ======================================================================== */

/* The eight parts-deform work slots.  At 0x2d30 each this is 92 KB of .data
 * and by far the largest object in the module -- but it is a work area, not an
 * authored table: every byte of it is zero in the ROM. */
fixed_array<EFFINFO2, 8> efi;                                   /* data 2e51f0 */

/* The deform's motion-blur trail. */
static EFF_PARTSBLUR eff_partsblur;                             /* bss 43d6e0 */
static int init_pdef2;                                          /* sbss 3f4bb4 */

LIGHT_COME_IN_CTRL LightComeInCtrl;                             /* data 2fbbd0 */
SINGLE_LINK_LIST WaterFlowList;                                 /* data 2fbbe0 */
SINGLE_LINK_LIST ModelAlphaChangeList;                          /* data 2fbbf0 */
SINGLE_LINK_LIST TourouFreaList;                                /* data 2fbc00 */
SINGLE_LINK_LIST TourouBaseList;                                /* data 2fbc10 */

/* The 0x60 of .data between efi and LightComeInCtrl is NOT a file-scope
 * global, which is why globals.txt lists nothing there.  functions.txt's
 * function-local static list has both halves:
 *
 *     0x2fbb70  static float passcnt[10]        SubPartsDeform1 (still stubbed)
 *     0x2fbba0  static float SePosition[3][4]   EffectWaterFlowRegist
 *
 * Each is declared inside its owning function below.  An earlier pass here
 * guessed them as a "CameraFlashPos"/"CameraFlashPosTex" pair from their
 * shape alone; check the per-function static list before naming an unlisted
 * object. */

/* The deform grid's alpha mask -- a soft radial falloff over the 17x17 grid,
 * so a deformed model fades out towards its edges.  Two variants; SubPartsDeform1()
 * picks between them by deform type. */
static const u_char pdeform_alpha1[PDEFORM_VERTEX_MAX] =        /* rdata 3a68d8 */
{
      0,   0,   0,   0,   0,   0,   1,   8,   8,   8,   1,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   1,  16,  32,  64,  64,  64,  64,  32,  32,  16,   0,   0,   0,
      0,   0,   0,   1,  32,  64,  96, 128, 128, 128,  96,  96,  64,  32,   8,   0,   0,
      0,   0,   1,  32,  64, 128, 128, 128, 128, 128, 128, 128,  96,  64,  32,  16,   0,
      0,   1,  32,  64, 128, 128, 128, 128, 128, 128, 128, 128, 128,  96,  64,  32,   0,
      0,  16,  64, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,  96,  32,   0,
      1,  32,  96, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,  96,  64,   1,
      8,  64, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,  64,   8,
      8,  64, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,  64,   8,
      8,  64, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,  64,   8,
      1,  64,  96, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,  96,  32,   1,
      0,  32,  96, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,  64,  16,   0,
      0,  32,  64,  96, 128, 128, 128, 128, 128, 128, 128, 128, 128,  64,  32,   1,   0,
      0,  16,  32,  64,  96, 128, 128, 128, 128, 128, 128, 128,  64,  32,   1,   0,   0,
      0,   0,   8,  32,  64,  96,  96, 128, 128, 128,  96,  64,  32,   1,   0,   0,   0,
      0,   0,   0,  16,  32,  32,  64,  64,  64,  64,  32,  16,   1,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   1,   8,   8,   8,   1,   0,   1,   0,   0,   0,   0
};

static const u_char pdeform_alpha2[PDEFORM_VERTEX_MAX] =        /* rdata 3a6a00 */
{
      0,   0,   0,   0,   0,   0,   1,   8,   8,   8,   1,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   1,  16,  32,  64,  64,  64,  64,  32,  32,  16,   0,   0,   0,
      0,   0,   0,   1,  32,  64,  88, 104, 104, 104,  88,  88,  64,  32,   8,   0,   0,
      0,   0,   1,  32,  64, 104, 104, 104, 104, 104, 104, 104,  88,  64,  32,  16,   0,
      0,   1,  32,  64, 104, 104, 104, 104, 104, 104, 104, 104, 104,  88,  64,  32,   0,
      0,  16,  64, 104, 104, 104, 104, 104, 104, 104, 104, 104, 104, 104,  88,  32,   0,
      1,  32,  88, 104, 104, 104, 104, 104, 104, 104, 104, 104, 104, 104,  88,  64,   1,
      8,  64, 104, 104, 104, 104, 104, 104, 104, 104, 104, 104, 104, 104, 104,  64,   8,
      8,  64, 104, 104, 104, 104, 104, 104, 104, 104, 104, 104, 104, 104, 104,  64,   8,
      8,  64, 104, 104, 104, 104, 104, 104, 104, 104, 104, 104, 104, 104, 104,  64,   8,
      1,  64,  88, 104, 104, 104, 104, 104, 104, 104, 104, 104, 104, 104,  88,  32,   1,
      0,  32,  88, 104, 104, 104, 104, 104, 104, 104, 104, 104, 104, 104,  64,  16,   0,
      0,  32,  64,  88, 104, 104, 104, 104, 104, 104, 104, 104, 104,  64,  32,   1,   0,
      0,  16,  32,  64,  88, 104, 104, 104, 104, 104, 104, 104,  64,  32,   1,   0,   0,
      0,   0,   8,  32,  64,  88,  88, 104, 104, 104,  88,  64,  32,   1,   0,   0,   0,
      0,   0,   0,  16,  32,  32,  64,  64,  64,  64,  32,  16,   1,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   1,   8,   8,   8,   1,   0,   1,   0,   0,   0,   0
};


/* ---- forward declarations for the file's statics, in ROM order ---------- */
static void MakePartsDeformPacket(int pnumw, int pnumh, sceVu0FVECTOR *vt,
                                  float (*wlm)[4], sceVu0FVECTOR *stq,
                                  const u_char *use_alpha, float aprate,
                                  u_long tex0, int Red, int Green, int Blue,
                                  int DrawEnvType);
static int  GetFreePartsDeformCtrlNo(void);
static EFFINFO2 *GetPartsDeformCtrlWrk(int wrk_no);
static int  PartsDeformClipCheck(float (*slm)[4]);
static float GetDistancePosToCamera(float *pos);
static float PartsDeformAddIso(float now_iso, float add_val);
static u_char SubPartsDeform1(EFFECT_CONT *ec, u_char num, int page, int sbj,
                              float sclx, float scly, float vol, int fl,
                              float spd, float rate, float trate);
static void PartsDeformCalcWaveY(sceVu0FVECTOR *vt, int vnumw, int vnumh,
                                 float base_iso, float rate);
static void PartsDeformCalcWaveZ(sceVu0FVECTOR *vt, int vnumw, int vnumh,
                                 float tsw, float tsh, float cntw, float cnth,
                                 float base_iso, float rate);
static void PartsDeformCalcWaveIsoZ(sceVu0FVECTOR *vt, int vnumw, int vnumh,
                                    float tsw, float tsh, float cntw, float cnth,
                                    float base_iso, float rate);
static void PartsDeformCalcWaveRot(sceVu0FVECTOR *vt, int vnumw, int vnumh,
                                   float tsw, float tsh, float cntw, float cnth,
                                   float base_iso, const EFFINFO2 *pefi);
static void PartsDeformCalcWaveRotZ(sceVu0FVECTOR *vt, int vnumw, int vnumh,
                                    float tsw, float tsh, float cntw, float cnth,
                                    float base_iso, const EFFINFO2 *pefi);
static void PartsDeformCalcWaveBound(sceVu0FVECTOR *vt, int vnumw, int vnumh,
                                     float tsw, float tsh, float cntw, float cnth,
                                     float base_iso, float renz);
static void PartsDeformCalcWaveRenz1(sceVu0FVECTOR *vt, int vnumw, int vnumh,
                                     float tsw, float tsh, float cntw, float cnth,
                                     float renz);
static void PartsDeformCalcWaveRenz2(sceVu0FVECTOR *vt, int vnumw, int vnumh,
                                     float tsw, float tsh, float cntw, float cnth,
                                     float renz);
static void PartsDeformEffInf02Init(EFFINFO2 *pEffInfo, int type, int vnumw,
                                    int vnumh, float tsw, float tsh,
                                    float cntw, float cnth, float spd,
                                    PDEFORM_PARA *pPara);
static u_char SubPartsDeform2(EFFECT_CONT *ec, u_char num, int page, int sbj,
                              float sclx, float scly, float vol, int fl,
                              float spd, float rate, float trate);
static void PartsDeform2CalcWaveY(sceVu0FVECTOR *vt, int vnumw, int vnumh,
                                  float comp, float rate, float spd,
                                  EFFINFO2 *pefi);
static void PartsDeform2CalcWaveZ(sceVu0FVECTOR *vt, int vnumw, int vnumh,
                                  float comp, float rate, float spd,
                                  EFFINFO2 *pefi);
static void PartsDeform2CalcWaveY2(sceVu0FVECTOR *vt, int vnumw, int vnumh,
                                   float comp, float rate, float spd,
                                   PDEFORM_PARA *pPara, EFFINFO2 *pefi);
static void PartsDeform2CalcWaveType2(sceVu0FVECTOR *vt, int vnumw, int vnumh,
                                      float comp, float rate, float spd,
                                      PDEFORM_PARA *pPara, EFFINFO2 *pefi);
static void SetStarRay(float *bpos, int tp, float sc, int no, float pw);
static float EffectLightComeInCalcAlphaRateCamDir(void *pSgdTop);
static float EffectLightComeInCalcAlphaRateCamDist(void *pSgdTop);
static void EffectLightComeInSetVertexColorNormal(void *pSgdTop);
static void EffectLightComeInSetVertexColorF607(void *pSgdTop);
static void EffectLightComeInSetVertexColorF609(void *pSgdTop);
static int  EffectLightComeInGetScrollU(int Type);
static int  EffectLightComeInGetScrollV(int Type);
static void EffectLightComeInInit(void);
static int  EffectLightComeInGetAlphaMax(int Type);
static void EffectLightComeInSetVertexColor(void *pSgdTop, int Type);
static void EffectWaterFlowInit(void);
static int  EffectWaterFlowGetAlpha(int MoveType);
static int  EffectWaterFlowGetChangeFrame(int MoveType, int MoveDataNo);
static void EffectWaterFlowChangeCtrl(WATER_FLOW_DATA *pData);
static void EffectWaterFlowExecOne(WATER_FLOW_DATA *pData);
static float EffectWaterFlowGetAddS(int MoveType, int MoveDataNo, int Count);
static float EffectWaterFlowGetAddT(int MoveType, int MoveDataNo, int Count);
static float EffectLightComeInAddSTScrollCtrl(float AddST, float TotalST);
static int  EffectCameraFlashGetPosition(float *Position, float *PositionTex);
static void CameraFlashMakeVertex(sceVu0FVECTOR *pVtxBuf, float *Center,
                                  float Radius, int VertexNum);
static int  EffectCameraFlashDraw(float *Position, int Count);
static void EffectCameraFlashDrawSub(float *Position, float PolygonScale,
                                     float Scale, u_char *CenterRgba,
                                     u_char *EdgeRgba, u_long AlphaBlend);
static int  EffectCameraFlashDrawTex(float *Position, int Count);
static void EffectModelAlphaChangeInit(void);
static void EffectTourouFreaInit(void);
static void EffectTourouBaseInit(void);
static float EffectTourouFreaGetScaleX(int Counter, int AllTime);
static float EffectTourouFreaGetScaleY(int Counter, int AllTime);
static int  EffectTourouFreaGetAlpha(int Counter, int AllTime);
static void EffectTourouBaseGetColor(int *Color, int Counter, int AllTime);

/* ==========================================================================
 *  Init
 * ======================================================================== */

/* 207 -- the cold init.  Clears both halves of every deform slot, resets the
 * blur trail, and brings the five list-backed sub-systems up. */
void InitEffectObj(void)
{
    int i;

    for (i = 0; i < 8; i++)                                     /* 209 */
    {
        efi[i].use  = 0;                                        /* 211 */
        efi[i].pass = 0;                                        /* 212 */
    }

    for (i = 0; i < 10; i++)                                    /* 215 */
    {
        eff_partsblur.on[i]  = 0;                               /* 217 */
        eff_partsblur.flg[i] = 0;                               /* 218 */
    }

    eff_partsblur.flow = 3;                                     /* 221 */
    eff_partsblur.cnt  = 0;
    eff_partsblur.in   = 0;
    eff_partsblur.keep = 0;
    eff_partsblur.out  = 0;
    eff_partsblur.max  = 0;
    eff_partsblur.vol  = (float *)nullptr;

    init_pdef2 = 1;                                             /* 229 */

    EffectLightComeInInit();                                    /* 231 */
    EffectWaterFlowInit();                                      /* 232 */
    EffectModelAlphaChangeInit();                               /* 233 */
    EffectTourouFreaInit();                                     /* 234 */
    EffectTourouBaseInit();                                     /* 235 */
}

/* 241 -- the per-frame half, and the deform slots' garbage collector.
 *
 * `pass` is raised by whatever drew a slot this frame and cleared here
 * unconditionally, so a slot that is `use`d but went a whole frame without
 * being drawn is released.  That is what frees a deform whose owner stopped
 * calling rather than deleting. */
void InitEffectObjEF(void)
{
    int i;

    for (i = 0; i < 8; i++)                                     /* 243 */
    {
        if ((efi[i].use == 1) && (efi[i].pass == 0))            /* 245 */
        {
            efi[i].use = 0;                                     /* 247 */
        }

        efi[i].pass = 0;                                        /* 249 */
    }
}                                                               /* 250 */

/* ==========================================================================
 *  Parts deform
 *
 *  The 17x17 grid machinery: seven arming entry points, the two
 *  SubPartsDeform passes, the packet builder and the twelve per-vertex
 *  displacement kernels.  ~15 KB of the module's 32 KB.
 * ======================================================================== */

/* 271 / 295 / 319 / 337 / 358 / 381 / 408 -- the arming calls.  Every one is a
 * single SetEffects_PDEFORM() (effect id 0x18) taking
 *
 *     (fl, type, max, sclx, scly, pWrk, in, keep, out,
 *      pDrive, pSpeed, pWave, pXYZ, r, g, b)
 *
 * so the family is one call with different slots defaulted.  Two shapes:
 * fl = 4 runs the in/keep/out flow and takes `alp` (or a hardcoded 100) as the
 * ceiling; fl = 2 hands the level over to *vol and passes 128 instead.  Only
 * CallPartsDeform6 lets the caller colour the grid; the rest pass 128,128,128.
 *
 * PORT NOTE: the ROM wrote the unused pointer slots as a bare 0 in a variadic
 * argument tail, which read back as four bytes of adjacent garbage on a 64-bit
 * host; they are spelled `nullptr` and the parameter list now enforces it.
 * The u_int in/keep/out slots keep the ROM's 0.
 *
 * 192 / 204 / 204 / 168 / 168 / 192 / 236 bytes. */
void *CallPartsDeform2(int type, float scale, void *pos,
                       u_int in, u_int keep, u_int out)
{                                                               /* 271 */
    return SetEffects_PDEFORM(4, type, 100, scale, scale, pos,
                              in, keep, out,
                              nullptr, nullptr, nullptr, nullptr,
                              0x80, 0x80, 0x80);                /* 274 */
}

void *CallPartsDeform3(int type, float scale, void *pos,
                       u_int in, u_int keep, u_int out, int alp)
{                                                               /* 295 */
    return SetEffects_PDEFORM(4, type, alp, scale, scale, pos,
                              in, keep, out,
                              nullptr, nullptr, nullptr, nullptr,
                              0x80, 0x80, 0x80);                /* 298 */
}

void *CallPartsDeform3_2(int type, float sclx, float scly, void *pos,
                         u_int in, u_int keep, u_int out, int alp)
{                                                               /* 319 */
    return SetEffects_PDEFORM(4, type, alp, sclx, scly, pos,
                              in, keep, out,
                              nullptr, nullptr, nullptr, nullptr,
                              0x80, 0x80, 0x80);                /* 320 */
}

void *CallPartsDeform4(int type, float scale, void *pos, float *vol)
{                                                               /* 337 */
    return SetEffects_PDEFORM(2, type, 128, scale, scale, pos,
                              0, 0, 0,
                              vol, nullptr, nullptr, nullptr,
                              0x80, 0x80, 0x80);                /* 340 */
}

void *CallPartsDeform5(int type, float sclx, float scly, void *pos,
                       float *vol)
{                                                               /* 358 */
    return SetEffects_PDEFORM(2, type, 128, sclx, scly, pos,
                              0, 0, 0,
                              vol, nullptr, nullptr, nullptr,
                              0x80, 0x80, 0x80);                /* 361 */
}

void *CallPartsDeform5_2(int type, float sclx, float scly, void *pos,
                         float *vol, float *pSpd, float *pRate)
{                                                               /* 381 */
    return SetEffects_PDEFORM(2, type, 128, sclx, scly, pos,
                              0, 0, 0,
                              vol, pSpd, pRate, nullptr,
                              0x80, 0x80, 0x80);                /* 384 */
}

void *CallPartsDeform6(int type, float scale, void *pos,
                       u_int in, u_int keep, u_int out, int alp,
                       float *pSpd, float *pRate, int r, int g, int b)
{                                                               /* 408 */
    return SetEffects_PDEFORM(4, type, alp, scale, scale, pos,
                              in, keep, out,
                              nullptr, pSpd, pRate, nullptr,
                              r, g, b);                         /* 411 */
}

/* 420 -- the deform's draw, dispatched by EffectZSort().  776 bytes, source
 * 420..478.  Three jobs in order: work out this frame's alpha `ef` (from the
 * flow countdown, from a caller-supplied float, or a flat max), read the three
 * optional drive floats, then hand the whole thing to whichever of the two
 * passes the type's tens digit selects.
 *
 * `ec->dat.uc8[2]` is the deform type and splits two ways: the tens digit is
 * the *page* -- 2 selects SubPartsDeform2, everything else SubPartsDeform1 --
 * and the units digit is the sub-type the wave kernels switch on.  Type 25 is
 * the odd one out at both ends: it skips the whole alpha block (430) and, once
 * inside the page-2 arm, is the release request rather than a shape (467).
 *
 * The stabs list exactly six locals -- ef, n0, sp, rt, vol, tr -- so `n0 / 10`
 * and `n0 % 10` have no local of their own and are written out at each use;
 * GCC hoisted the single `div` that serves both to the top of the function,
 * which is where the otherwise unaccounted 425/426 line notes sit.  See
 * [[unstabbed-register-is-a-cse-temp]].
 *
 * `tr` is 2.0f minus the caller's value, not the value: *pnt[5] counts up from
 * 0 as the deform decays and the two passes want what is left. */
void SetPartsDeform(EFFECT_CONT *ec)
{                                                               /* 420 */
    int   ef = 0;                                               /* 421 */
    int   n0;
    float sp, rt, vol, tr;

    n0 = ec->dat.uc8[2];                                        /* 424 */

    if (n0 != 25)                                               /* 430 */
    {
        if ((ec->dat.uc8[1] & 4) != 0)                          /* 433 */
        {
            if (ec->flow == 0)                                  /* 434 */
            {
                if (ec->in != 0)                                /* 435 */
                {
                    ef = (int)((float)(ec->cnt * ec->max)
                               / (float)ec->in);                /* 436 */
                }
            }
            else if (ec->flow == 1)                             /* 439 */
            {
                ef = ec->max;                                   /* 440 */
            }
            else if (ec->flow == 2)                             /* 442 */
            {
                if (ec->out != 0)                               /* 443 */
                {
                    ef = ec->max - (int)((float)(ec->cnt * ec->max)
                                         / (float)ec->out);     /* 444 */
                }
            }
            else if (ec->flow == 3)                             /* 447 */
            {
                ResetEffects(ec);                               /* 448 */
                return;
            }

            EffInKeepOutFlowCtrl(ec);                           /* 451 */
        }
        else if (ec->pnt[1] == nullptr)                         /* 453 */
        {
            ef = ec->max;
        }
        else
        {
            ef = (int)*(float *)ec->pnt[1];                     /* 454 */
        }
    }

    sp  = (ec->pnt[2] != nullptr) ? *(float *)ec->pnt[2] : 1.0f;        /* 460 */
    rt  = (ec->pnt[4] != nullptr) ? *(float *)ec->pnt[4] : 1.0f;        /* 461 */
    tr  = (ec->pnt[5] != nullptr) ? 2.0f - *(float *)ec->pnt[5] : 1.0f; /* 462 */
    vol = (float)ef / 100.0f;                                   /* 463 */

    if (n0 / 10 == 2)                                           /* 466 */
    {
        if (n0 % 10 == 5)                                       /* 467 */
        {
            ResetEffects(ec);                                   /* 468 */
        }
        else
        {
            /* The page argument is the literal 2, not n0 / 10: GCC
             * materialised it with a `li` rather than reusing the register the
             * branch above had already proved equal.  See
             * [[branch-condition-does-not-constant-propagate]]. */
            ec->dat.uc8[4] = SubPartsDeform2(ec, ec->dat.uc8[4], 2, n0 % 10,
                                             ec->dat.fl32[2], ec->dat.fl32[3],
                                             vol, 1, sp, rt, tr);       /* 470 */
        }
    }
    else
    {
        ec->dat.uc8[4] = SubPartsDeform1(ec, ec->dat.uc8[4], n0 / 10, n0 % 10,
                                         ec->dat.fl32[2], ec->dat.fl32[3],
                                         vol, 1, sp, rt, tr);           /* 473 */
    }

    if ((ec->dat.uc8[1] & 1) != 0)                              /* 477 */
    {
        ResetEffects(ec);                                       /* 478 */
    }
}

/* 486 -- does this effect want to be drawn into the blur source rather than
 * straight to the frame?  68 bytes, source 486..500.  Page 1 of both the parts
 * deform (0x18) and the ghost-entry effect (0x23) is the blurred one. */
int EffectObjPartsDeformBlurCheck(EFFECT_CONT *ec)
{                                                               /* 486 */
    int RetVal = 0;
    int n0;

    if ((ec->dat.uc8[0] == 0x18) || (ec->dat.uc8[0] == 0x23))   /* 489 */
    {
        n0 = ec->dat.uc8[2];                                    /* 492 */
        n0 = n0 / 10;                                           /* 493 */

        RetVal = (n0 == 1);                                     /* 495 */
    }

    return RetVal;                                              /* 500 */
}

/* 515 -- project one deform vertex to GS screen coordinates and report whether
 * it left the guard band.  Raw VU0 macro-mode in the ROM (0x14a878, 108 bytes,
 * source 515..549), and the whole body is one basic block:
 *
 *      lqc2 vf12,(a1)                          v = *vf
 *      vf13 = vf4*v.x + vf5*v.y + vf6*v.z + vf7        (screen)
 *      vf14 = vf8*v.x + vf9*v.y + vf10*v.z + vf11      (clip)
 *      vdiv Q,vf0w,vf13w / vclipw.xyz vf14,vf14 / vwaitq
 *      vmulq.xyz vf13,vf13,Q / vftoi4.xy / vftoi0.z
 *      sqc2 vf13,(a0) / cfc2 v0,$18
 *
 * Two details the shape hides.  The final accumulate is `vmaddw vf13,vf7,vf0`,
 * taking w from vf0 -- so the input vertex's own w is ignored and the transform
 * is always affine on (x, y, z, 1).  And `vmulq.xyz` divides z as well as x and
 * y, so only the ftoi conversion differs between them: 12.4 fixed point for the
 * GS's XYZ2 x/y, a plain integer for its z.
 *
 * PORT DEVIATION -- signature.  vf4..vf11 are the two local->clip matrices an
 * earlier gra3dVu0ApplyMatrix2() left in the VU0 register file; the ROM stored
 * neither and this function picked them up implicitly.  There is no host
 * register file, so they become explicit parameters -- exactly the deviation
 * gra3dVu0.h:189 already documents for gra3dVu0ApplyMatrix2 itself, and the
 * reason it is a deviation rather than a rewrite: the preload is not hand
 * written asm, it is that header inline (gra3dVu0.h 443/456) expanded into both
 * callers.  Passing the matrices also makes the dependency impossible to forget,
 * where a global register-file emulation would fail silently.
 *
 * matLocalScreen is matWorldScreen (camera +0xe0) concatenated onto the object's
 * local->world; matLocalClipPolygon is matWorldClipPolygon (+0x120) onto the
 * same.  Both callers -- MakePartsDeformPacket here and EneDmgLargeHitMakePacket
 * in effect_ene.c -- build them that way and then mask this result with 0x3f. */
int CalcPartsDeformXYZ(int *vi, float *vf,
                       float (*matLocalScreen)[4], float (*matLocalClipPolygon)[4],
                       float *pNdcZ)
{
    float vScreen[4], vClip[4];
    float q;
    int   c;

    for (c = 0; c < 4; c++)                                     /* 549 */
    {
        vScreen[c] = matLocalScreen[0][c] * vf[0]
                   + matLocalScreen[1][c] * vf[1]
                   + matLocalScreen[2][c] * vf[2]
                   + matLocalScreen[3][c];
        vClip[c]   = matLocalClipPolygon[0][c] * vf[0]
                   + matLocalClipPolygon[1][c] * vf[1]
                   + matLocalClipPolygon[2][c] * vf[2]
                   + matLocalClipPolygon[3][c];
    }

    q = 1.0f / vScreen[3];              /* vdiv Q,vf0w,vf13w                  */

    vi[0] = (int)(vScreen[0] * q * 16.0f);      /* vftoi4.x -- 12.4 fixed     */
    vi[1] = (int)(vScreen[1] * q * 16.0f);      /* vftoi4.y                   */
    vi[2] = (int)(vScreen[2] * q);              /* vftoi0.z -- integer        */

    /* sqc2 writes the whole quadword and neither ftoi touched w, so the fourth
       word keeps the pre-divide w as float bits.  Neither caller reads it, but
       the store is the ROM's. */
    *(float *)&vi[3] = vScreen[3];

    /* PORT ADDITION: the same clip-space vertex as a depth the host renderer
       can test.  vClip[3] is the view depth, and every vertex a caller goes on
       to draw has passed the +-w test below, so it is positive there; the
       guard is for the ones that did not, since the flags are the caller's
       business rather than this function's. */
    if (pNdcZ != nullptr)
    {
        *pNdcZ = vClip[3] != 0.0f ? vClip[2] / vClip[3] : 0.0f;
    }

    /* cfc2 v0,$18 is the CLIP register, a shift register holding the last four
       vclip results.  Only one vclipw ran, and both callers mask with 0x3f, so
       the six flags of that one test are the whole answer. */
    return (int)gra3dVu0ClipFlags(vClip);
}

/* 558 -- build the deform's GIF packet and send it.  Turns the 17x17 grid of
 * deformed vertices into one triangle strip per patch row, projecting each
 * vertex with CalcPartsDeformXYZ() and letting the register list rather than a
 * branch drop the triangles that fell outside the guard band.
 *
 * The caller passes PATCH counts, so the vertex grid is one larger in each
 * axis; `clip` and `vtiw` are both fixed_array<...,289> locals, which is why
 * every statement that touches them is attributed to fixed_array.h 124/125 and
 * leaves no $LM of its own.  Those lines are interpolated into the gaps.
 *
 * The GS state it sets is worth reading once.  TEXFLUSH then TEX0_1 install the
 * texture; PRIM 0x5c is TRIANGLE_STRIP | IIP | TME | ABE.  Each column step is
 * one REGLIST GIFtag of six registers -- ST, RGBAQ, XYZF for two vertices --
 * and the per-quad REGS word swaps XYZF2 (0x4, kicks) for XYZF3 (0xc, does
 * not) on either vertex according to the summed clip flags of that triangle's
 * three corners.  0x00412412 draws both, 0x00c12c12 neither, and the mixed
 * pair 0x00412c12 / 0x00c12412 drop one each.
 *
 * DrawEnvType picks between two DRAW_ENV_5 images that differ in exactly one
 * field: the door-seal one runs with ZTST ALWAYS so the dissolve is not
 * occluded.  Both are transcribed from .rodata below. */
static void MakePartsDeformPacket(int pnumw, int pnumh, sceVu0FVECTOR *vt,
                                  float (*wlm)[4], sceVu0FVECTOR *stq,
                                  const u_char *use_alpha, float aprate,
                                  u_long tex0, int Red, int Green, int Blue,
                                  int DrawEnvType)
{
    /* .rodata 0x3a6620.  A function-local static in the ROM, and const -- it is
       in rdata, which is the only tell (globals.txt strips const).  Identical to
       `env` below except for TEST: ZTST is ALWAYS here and GEQUAL there, so the
       door seal draws over whatever is already in front of it. */
    static const DRAW_ENV_5 DoorSealEnv =
    {
        0x0000000000000044ULL,      /* ALPHA  (Cs-Cd)*As+Cd                    */
        0x0000000000000161ULL,      /* TEX1                                    */
        0x0000000000000005ULL,      /* CLAMP  clamp s and t                    */
        0x0000000000030003ULL,      /* TEST   ATST ALWAYS, ZTST ALWAYS         */
        0x000000010a000118ULL       /* ZBUF   PSMZ24 @0x118, ZMSK (no z write) */
    };
    /* globals.txt calls this one `env.1030` with no section -- a local
       aggregate whose initialiser GCC parked in .rodata (0x3a65f8) and passed
       by address, because SetDrawEnv takes a const pointer and never writes. */
    const DRAW_ENV_5 env =
    {
        0x0000000000000044ULL,
        0x0000000000000161ULL,
        0x0000000000000005ULL,
        0x000000000005000dULL,      /* TEST   ATST GEQUAL/0, ZTST GEQUAL       */
        0x000000010a000118ULL
    };

    fixed_array<int, PDEFORM_VERTEX_MAX>    clip;
    fixed_array<int[4], PDEFORM_VERTEX_MAX> vtiw;
    /* PORT ADDITION: the depth the GS took from each XYZF2, kept in clip space
       for the renderer.  Not the ROM's -- see CalcPartsDeformXYZ. */
    float ndcz[PDEFORM_VERTEX_MAX];
    GRA3DCAMERA *pCam;
    Q_WORDDATA  *ppbuf;
    u_long      *plong;
    int          i, j, k, l;

    /* PORT DEVIATION: the ROM left these two in vf4..vf11 -- see
       CalcPartsDeformXYZ above and gra3dVu0.h:189. */
    float matLocalScreen[4][4];
    float matLocalClipPolygon[4][4];

    pCam = gra3dGetCamera();
    gra3dVu0ApplyMatrix2(matLocalScreen,      pCam->matWorldScreen,       wlm,
                         matLocalClipPolygon, pCam->matWorldClipPolygon,  wlm); /* 566 */

    for (i = 0; i < (pnumw + 1) * (pnumh + 1); i++)             /* 572 */
    {
        /* No $LM of its own: both subscripts go through fixed_array, which
           swallows the statement's line (fixed_array.h 124/125). */
        clip[i] = CalcPartsDeformXYZ(vtiw[i], vt[i],
                                     matLocalScreen, matLocalClipPolygon,
                                     &ndcz[i]) & 0x3f;
    }                                                           /* 576 */

    if (DrawEnvType == 0)                                       /* 578 */
    {
        SetDrawEnv(0, &env);                                    /* 586 */
    }
    else
    {
        SetDrawEnv(0, &DoorSealEnv);                            /* 597 */
    }

    ppbuf = StartDmaDirectTrans();                              /* 601 */

    /* Two A+D writes: flush the texture cache, then install this deform's
       TEX0.  PACKED, NLOOP 2, NREG 1. */
    ppbuf->ul64[0] = 0x1000000000008002ULL;                     /* 604 */
    ppbuf->ul64[1] = 0x0e;                  /* A+D */           /* 605 */
    ppbuf++;                                                    /* 606 */
    ppbuf->ul64[0] = 0;                                         /* 608 */
    ppbuf->ul64[1] = 0x3f;                  /* TEXFLUSH */      /* 609 */
    ppbuf++;                                                    /* 610 */
    ppbuf->ul64[0] = tex0;                                      /* 612 */
    ppbuf->ul64[1] = 0x06;                  /* TEX0_1 */        /* 613 */
    ppbuf++;                                                    /* 614 */

    /* Then the PRIM the whole run is drawn with.  0x5c is TRIANGLE_STRIP with
       IIP (gouraud), TME (textured) and ABE (alpha blend); FGE is off. */
    ppbuf->ul64[0] = 0x2400400000008001ULL;                     /* 616 */
    ppbuf->ul64[1] = 0xf0;                  /* PRIM, NOP */     /* 617 */
    ppbuf++;                                                    /* 618 */
    ppbuf->ul64[0] = 0x5c;                                      /* 621 */
    ppbuf->ul64[1] = 0;                                         /* 622 */
    ppbuf++;

    /* One iteration per (patch row, vertex column), emitting the two vertices a
       triangle strip needs at that step: the one on this row and the one below.
       Which of the pair actually kicks a triangle is carried in the REGS word --
       XYZF2 (0x4) draws, XYZF3 (0xc) does not -- so a clipped triangle is
       dropped by the register list rather than by branching around the data. */
    for (i = 0; i < pnumh * (pnumw + 1); i++)                   /* 625 */
    {
        k = i + pnumw;                                          /* 626 */
        j = k + 1;                                              /* 627 */

        if (i % (pnumw + 1) == 0)                               /* 629 */
        {
            /* First column of a row: both vertices are emitted but neither
               kicks, which restarts the strip instead of bridging a triangle
               back across from the end of the previous row. */
            ppbuf->ul64[1] = 0x00c12c12;    /* ST RGBAQ XYZF3 x2 */  /* 631 */
        }
        else
        {
            /* Sum the three corners' clip flags per triangle.  These statements
               leave no $LM either -- every access is a fixed_array subscript.
               The ROM reuses k's register for the first sum, which is safe
               because GCC has already strength-reduced clip[] to cursors. */
            k = clip[i - 1] + clip[i + pnumw] + clip[i];
            l = clip[i + pnumw] + clip[i] + clip[j];

            if (k + l == 0)                                     /* 635 */
            {
                ppbuf->ul64[1] = 0x00412412;   /* both drawn */ /* 637 */
            }
            else if (k == 0)                                    /* 638 */
            {
                /* ROM ODDITY, reproduced: this `l == 0` can never be true --
                   k + l != 0 above already ruled out both being zero -- so the
                   `continue` is dead and 646 always runs. */
                if (l == 0)                                     /* 644 */
                {
                    continue;
                }
                ppbuf->ul64[1] = 0x00c12412;   /* A draws, B does not */ /* 646 */
            }
            else if (l == 0)
            {
                ppbuf->ul64[1] = 0x00412c12;   /* A does not, B draws */ /* 643 */
            }
            else
            {
                ppbuf->ul64[1] = 0x00c12c12;   /* neither */    /* 640 */
            }
        }

        ppbuf->ul64[0] = 0x6400400000008001ULL;  /* REGLIST, NREG 6 */ /* 653 */

        plong = (u_long *)(ppbuf + 1);                          /* 656 */

        /* ST is two floats in one quadword half and the ROM copies them as raw
           words, so they go through a u_int view rather than the FPU. */
        ((u_int *)plong)[0] = *(const u_int *)&stq[i][0];        /* 658 */
        ((u_int *)plong)[1] = *(const u_int *)&stq[i][1];        /* 659 */
        plong[1] = (u_long)(u_int)Red
                 | ((u_long)(u_int)Green << 8)
                 | ((u_long)(u_int)Blue << 16)
                 | ((u_long)((u_int)(use_alpha[i] * aprate) & 0xff) << 24)
                 | ((u_long)*(const u_int *)&stq[i][2] << 32);   /* 660 */
        plong[2] = (u_long)(u_int)vtiw[i][0]
                 | ((u_long)(u_int)vtiw[i][1] << 16)
                 | ((u_long)(u_int)vtiw[i][2] << 32);

        ((u_int *)plong)[6] = *(const u_int *)&stq[j][0];        /* 665 */
        ((u_int *)plong)[7] = *(const u_int *)&stq[j][1];        /* 666 */
        plong[4] = (u_long)(u_int)Red
                 | ((u_long)(u_int)Green << 8)
                 | ((u_long)(u_int)Blue << 16)
                 | ((u_long)((u_int)(use_alpha[j] * aprate) & 0xff) << 24)
                 | ((u_long)*(const u_int *)&stq[j][2] << 32);   /* 667 */
        plong[5] = (u_long)(u_int)vtiw[j][0]
                 | ((u_long)(u_int)vtiw[j][1] << 16)
                 | ((u_long)(u_int)vtiw[j][2] << 32);

        ppbuf += 4;                                             /* 673 */
    }                                                           /* 674 */

    EndDmaDirectTrans(ppbuf);                                   /* 676 */

    /* PORT: the strip above is inert -- dmaVif1 discards it -- so the same grid
     * goes to the renderer as a triangle list.  The strip emits the pair
     * (i, i + pnumw + 1) at each step, so the cell to the left of column c is
     * bounded by i-1, i, i+pnumw and i+pnumw+1; the two triangles and their
     * clip tests are the ROM's own, including its stricter rule -- a triangle
     * is dropped unless *every* corner is inside the guard band, where the
     * large-hit fan drops only when all three are outside.
     *
     * vtiw holds GS window coordinates in 12.4 fixed, so the 3D env's XYOFFSET
     * comes back off (1728 = 2048 - 320, 1824 = 2048 - 224).  ST is normalised
     * and the GS divides it by Q. */
    {
        static float  xy[PDEFORM_VERTEX_MAX * 6 * 2];
        static float  uv[PDEFORM_VERTEX_MAX * 6 * 2];
        static float  zz[PDEFORM_VERTEX_MAX * 6];
        static u_char rgba[PDEFORM_VERTEX_MAX * 6 * 4];
        int n = 0;

        for (i = 0; i < pnumh * (pnumw + 1); i++)
        {
            int tri[2][3];
            int ntri = 0;
            int t;

            if (i % (pnumw + 1) == 0)
            {
                continue;               /* first column only restarts the strip */
            }

            j = i + pnumw + 1;

            if (clip[i - 1] + clip[i + pnumw] + clip[i] == 0)
            {
                tri[ntri][0] = i - 1;
                tri[ntri][1] = i + pnumw;
                tri[ntri][2] = i;
                ntri++;
            }
            if (clip[i + pnumw] + clip[i] + clip[j] == 0)
            {
                tri[ntri][0] = i + pnumw;
                tri[ntri][1] = i;
                tri[ntri][2] = j;
                ntri++;
            }

            for (t = 0; t < ntri; t++)
            {
                int c;

                for (c = 0; c < 3; c++)
                {
                    int   v = tri[t][c];
                    float q = stq[v][2];

                    if (q == 0.0f)
                    {
                        q = 1.0f;
                    }

                    xy[n * 2 + 0] = (float)vtiw[v][0] / 16.0f - 1728.0f;
                    xy[n * 2 + 1] = (float)vtiw[v][1] / 16.0f - 1824.0f;
                    uv[n * 2 + 0] = stq[v][0] / q;
                    uv[n * 2 + 1] = stq[v][1] / q;
                    zz[n] = ndcz[v];
                    rgba[n * 4 + 0] = (u_char)Red;
                    rgba[n * 4 + 1] = (u_char)Green;
                    rgba[n * 4 + 2] = (u_char)Blue;
                    rgba[n * 4 + 3] = (u_char)((u_int)(use_alpha[v] * aprate) & 0xff);
                    n++;
                }
            }
        }

        if (n != 0)
        {
            /* The two draw envs above differ only in ZTST: `env` is GEQUAL, so
               the refraction is occluded by whatever stands in front of it,
               and DoorSealEnv is ALWAYS so the dissolve is not.  ZMSK is set in
               both, and the bridge never writes depth, so only the test moves. */
            MioPan_RendererDrawTexturedTriangles2D((sceGsTex0 *)&tex0, xy, uv,
                                                   rgba, zz, n, 1, 1,
                                                   DrawEnvType == 0);
        }
    }
}

/* 687 -- claim a work slot, marking it used on the way out.  0xff, not -1, is
 * the "all eight busy" answer, and every caller tests against that. */
static int GetFreePartsDeformCtrlNo(void)
{
    int i;

    for (i = 0; i < 8; i++)                                     /* 689 */
    {
        if (efi[i].use == 0)                                    /* 691 */
        {
            efi[i].use = 1;                                     /* 693 */
            return i;                                           /* 694 */
        }
    }

    return 0xff;                                                /* 698 */
}                                                               /* 699 */

/* 706 -- the slot by index, NULL if out of range.  The unsigned compare makes
 * a negative index fail the range test rather than indexing backwards. */
static EFFINFO2 *GetPartsDeformCtrlWrk(int wrk_no)
{
    if ((u_int)wrk_no < 8)                                      /* 708 */
    {
        return &efi[wrk_no];                                    /* 710 */
    }

    return (EFFINFO2 *)nullptr;                                 /* 712 */
}                                                               /* 713 */

/* 720 -- is the deform's origin off screen?
 *
 * Projects the local origin and range-checks the result with unsigned
 * subtractions, which is what folds "less than the near edge" and "past the
 * far edge" into one compare each: x and y must land in [0x300, 0x300+64000]
 * and z in [1, 0xfffff].  Returns non-zero to clip. */
static int PartsDeformClipCheck(float (*slm)[4])
{
    sceVu0FVECTOR fzero;
    sceVu0IVECTOR ivec;
    u_int clip;

    memset(fzero, 0, sizeof(fzero));                            /* 722 */
    fzero[3] = 1.0f;                                            /* 723 */

    sceVu0RotTransPers(ivec, slm, fzero, 0);                    /* 725 */

    clip = (u_int)((u_int)(ivec[0] - 0x300) > 64000);            /* 727 */

    if ((u_int)(ivec[1] - 0x300) > 64000)                       /* 729 */
    {
        clip = 1;
    }

    if ((u_int)(ivec[2] - 1) > 0xffffe)                          /* 732 */
    {
        clip = 1;
    }

    return (int)clip;                                            /* 735 */
}                                                               /* 736 */

/* 743 -- straight-line distance from a point to the camera, through the usual
 * VU0 sqrt idiom (vsqrt / vwaitq / vaddq against vf0). */
static float GetDistancePosToCamera(float *pos)
{
    float dx;
    float dy;
    float dz;

    /* gra3dcamGetPosition() returns a reference to the camera's own vector,
     * not a pointer -- binding it as a reference is what the other callers do. */
    float (&rCamPos)[4] = gra3dcamGetPosition();                /* 745 */

    dx = pos[0] - rCamPos[0];                                   /* 747 */
    dy = pos[1] - rCamPos[1];                                   /* 748 */
    dz = pos[2] - rCamPos[2];                                   /* 749 */

    return g3dxVu0Sqrt(dx * dx + dy * dy + dz * dz);             /* 752 */
}                                                               /* 753 */

/* 767 -- advance the deform's phase, wrapped into [0, 360).
 *
 * The whole step is skipped while the effect system is paused, so a deform
 * freezes rather than jumping when play resumes.  Note the wrap is a single
 * subtract, not a fmod: an add_val over 360 would leave it out of range, and
 * no caller passes one. */
static float PartsDeformAddIso(float now_iso, float add_val)
{
    if (EffWrkStopFlgGet() == 0)                                /* 769 */
    {
        now_iso += add_val;                                     /* 771 */

        if (now_iso > 360.0f)
        {
            now_iso -= 360.0f;
        }
    }

    return now_iso;                                             /* 772 */
}                                                               /* 773 */

/* 506 -- seed the VU0 random-number unit.  The whole body is
 * `mfc1 v0,f12 / qmtc2 v0,vf12 / vrinit R,vf12`, which loads the VU0 R register
 * (a 23-bit LFSR) from the float's bit pattern.
 *
 * DEAD CODE.  A jal+j scan of every loadable segment finds no call site, and
 * vrnext/vrget/vrxor appear nowhere in effect_obj.o, effect_ene.o or
 * effect_oth.o -- nothing ever reads the R register back.  The deform kernels
 * draw their randomness from EffectGetRandom()/MioPan_Rand() instead.  Same pattern as
 * FlyInit/FlyAct and ene_mot_ctrl.o's exported-but-unused pair: exported,
 * reachable from nothing.
 *
 * So this stays a no-op rather than growing an LFSR emulation, and the recorded
 * seed exists only so the store is not optimised away and the symbol survives. */
static float vu_rand_seed;

void SetVURand(float x)
{
    vu_rand_seed = x;                                           /* 507 */
}

/* 780 -- pass 1: the refracting deform.  2728 bytes, source 780..1091, and the
 * largest body in the file.
 *
 * The shape, in order: claim (or re-claim) a work slot, build the local->world
 * and local->screen matrices from the camera's facing and the effect's world
 * position, clip-reject, copy the back buffer into local memory and point TEX0
 * at it, lay out a flat 17x17 grid of vertices, project every one of them into
 * a *screen* texture coordinate (CalcStqXYZ -- which is what makes the mesh
 * re-display the scene behind it), then let one of eight kernels displace the
 * grid and hand the result to MakePartsDeformPacket.
 *
 * Because the STQ pass runs before the kernels, the sampled image does not move
 * with the geometry: the grid slides over a fixed picture, which is the
 * refraction rather than a warp of the mesh's own texture.
 *
 * `aprate` is a distance fade: full strength past 200 units, nothing under 100,
 * a linear ramp between -- so walking into a deform does not fill the screen
 * with it.
 *
 * Its four statics are all effectively empty at load, which is worth knowing
 * before assuming a table was missed:
 *   sdata 0x3efde4  static float r2   = 0.0f
 *   sdata 0x3efde8  static float renz = 1.0f
 *   data  0x2fbb70  static float passcnt[10]  -- all zero
 *   sbss  0x3f4bb0  static reference_fixed_array<int,10> passflg
 * `passflg` is built lazily at line 816 under a guard word at 0x3efde0 (the
 * fixed_array.h 231/232 pair), and it points at **.rodata 0x3a6648**, which is
 * ten zeroed ints -- so this is another writable-through-.rodata case, see
 * [[reference-fixed-array-in-rodata-is-writable]].
 *
 * passcnt/passflg exist so several effects sharing one sub-type advance the
 * phase only once a frame: `n` is "nobody has stepped this sub-type yet this
 * frame", and every kernel that has a phase gates its PartsDeformAddIso() on
 * it.  Which of the two stores the phase depends on ec->dat.uc8[1] & 1 -- a
 * one-shot effect keeps its own phase in ec->fw[0] instead of the shared slot.
 *
 * ROM ODDITY, reproduced: `cntw` and `cnth` are both `(8 % vnumw) * ...`.  GCC
 * emits one real `div 8, vnumw` and takes the REMAINDER for both, so the second
 * is a copy-paste of the first -- the row offset should be a `/`, and it only
 * goes unnoticed because the grid is square.  The dividend is an already-folded
 * literal 8 (the centre column of a 17-wide grid); the divisor survives as a
 * register because MIPS `div` has no immediate form, so cprop could not fold it
 * away even knowing vnumw is 17.
 *
 * `case 0` returns instead of breaking -- it is the only arm that skips the
 * passflg update and the packet, and GCC cross-jumped its `return ret_num` onto
 * the one at 1090.  An out-of-range sub-type does *not*: it falls through to
 * the tail and draws an undisplaced grid.
 *
 * Cases 1, 3, 4 and 5 share one `if (n) r = PartsDeformAddIso(r, spd + spd);`
 * tail and case 2 shares its call -- GCC cross-jumped four identical copies,
 * which is why their `break`s carry the line numbers and the bodies do not.
 * See [[gcc-cross-jumps-identical-call-tails]]. */
static u_char SubPartsDeform1(EFFECT_CONT *ec, u_char num, int page, int sbj,
                              float sclx, float scly, float vol, int fl,
                              float spd, float rate, float trate)
{
    /* r2 is written once here and read by nothing, anywhere in the object;
     * renz is read by cases 7 and 8 and written by nothing.  Both are the ROM's
     * -- a lens-warp strength and a phase snapshot whose drivers never shipped. */
    static float r2;                                            /* sdata 3efde4 */
    static float renz = 1.0f;                                   /* sdata 3efde8 */
    static int   passflg_data[10];                              /* rdata 3a6648 */
    static reference_fixed_array<int, 10> passflg(passflg_data); /* sbss 3f4bb0 */
    static float passcnt[10];                                   /* data 2fbb70 */

    EFFINFO2    *pefi;
    u_char       ret_num;
    int          i, j, k, n;
    int          vnumw, vnumh;
    float        l;
    float        cntw, cnth;
    float        tsw, tsh;
    float        rot_x, rot_y;
    float        f3, f4;
    float        xx, yy;
    float        stqparam[3][4];
    u_long       tex0;
    fixed_array<sceVu0FVECTOR, PDEFORM_VERTEX_MAX> stq;
    fixed_array<sceVu0FVECTOR, PDEFORM_VERTEX_MAX> vt;
    float        vpos[4];
    float        slm[4][4];
    float        wlm[4][4];
    float        wfw, wfh;
    float        aprate;
    float        r;
    GRA3DCAMERA *pCam;
    float        fw;

    (void)fl;

    r = 0.0f;                                                   /* 805 */

    vnumw = PDEFORM_VERTEX_W;
    vnumh = PDEFORM_VERTEX_H;

    pCam = gra3dGetCamera();                                    /* 818 */
    float (&cam_dir)[4] = gra3dcamGetDirection();               /* 819 */

    tsw = sclx + sclx;                                          /* 821 */
    tsh = scly + scly;                                          /* 822 */

    cntw = (8 % vnumw) * tsw;                                   /* 827 */
    cnth = (8 % vnumw) * tsh;                                   /* 828 */

    if (num == 0xff)                                            /* 831 */
    {
        ret_num = (u_char)GetFreePartsDeformCtrlNo();           /* 833 */
        pefi    = GetPartsDeformCtrlWrk(ret_num);               /* 834 */

        if (pefi == nullptr)                                    /* 835 */
        {
            printf("PartsDeform Buffer is Full!!\n");           /* 836 */
            return ret_num;                                     /* 837 */
        }

        r2 = r;                                                 /* 842 */

        /* Only the two Rot kernels read ep[].lng, so only they pay for it. */
        if (sbj >= 4 && sbj < 6)                                /* 844 */
        {
            for (i = 0; i < PDEFORM_VERTEX_MAX; i++)            /* 845 */
            {                                                   /* 846 */
                wfw = (i % vnumw) * tsw - cntw;                 /* 849 */
                wfh = (i / vnumw) * tsh - cnth;                 /* 850 */

                if (i == 0)                                     /* 854 */
                {
                    r = g3dxVu0Sqrt2(wfw, wfh);
                }

                l = r - g3dxVu0Sqrt2(wfw, wfh);                 /* 855 */

                pefi->ep[i].lng = l * l * 0.00599999959f * rate;
            }                                                   /* 857 */
        }
    }
    else
    {
        pefi = GetPartsDeformCtrlWrk(num);                      /* 863 */

        if (pefi == nullptr)                                    /* 864 */
        {
            return num;
        }

        ret_num = num;                                          /* 865 */
    }

    pefi->pass = 1;                                             /* 867 */

    Vector2Rot(cam_dir, &rot_x, &rot_y);                        /* 881 */

    g3dxVu0CopyVector(vpos, (const float *)ec->pnt[0]);

    fw = GetDistancePosToCamera(vpos);                          /* 885 */

    aprate = (fw > 200.0f) ? vol
           : (fw < 100.0f) ? 0.0f
                           : (fw - 100.0f) * vol / 100.0f;      /* 886 */

    sceVu0UnitMatrix(wlm);                                      /* 889 */
    wlm[0][0] = 25.0f; wlm[1][1] = 25.0f; wlm[2][2] = 25.0f;    /* 890 */
    sceVu0RotMatrixY(wlm, wlm, rot_y);                          /* 891 */
    sceVu0TransMatrix(wlm, wlm, vpos);                          /* 892 */
    sceVu0MulMatrix(slm, pCam->matWorldScreen, wlm);            /* 893 */

    if (PartsDeformClipCheck(slm) != 0)                         /* 896 */
    {
        return ret_num;
    }

    /* Grab the frame the deform is going to re-display.  Page 1 samples the
     * blur buffer at 0x7aa0 instead and so needs no copy of its own -- see
     * EffectObjPartsDeformBlurCheck(), which is what routes it there. */
    if (page == 0)                                              /* 904 */
    {
        LocalCopyLtoL(3, (sys_wrk.count & 1) * 0x1180, 0x2bc0); /* 905 */
        tex0 = 0x2000000264116bc0ULL;                           /* 906 */
    }
    else
    {
        tex0 = 0x2000000224117aa0ULL;                           /* 909 */

        if (page != 1)
        {
            LocalCopyLtoL(3, ((sys_wrk.count + 1) & 1) * 0x1180, 0x2bc0); /* 913 */
            tex0 = 0x2000000264116bc0ULL;                       /* 914 */
        }
    }

    /* The screen-sample parameters -- see CalcStqXYZ in effect_obj.h.  Unlike
     * effect_ene.o's expansion, [0][2] is not 1.0f here: `trate` pre-scales the
     * vertex's own x/y, so a deform winding down samples a shrinking window of
     * the screen as well as fading.  [0][3] is deliberately never written. */
    stqparam[0][0] = 1727.5f;                                   /* 933 */
    stqparam[0][1] = 1823.5f;                                   /* 934 */
    stqparam[0][2] = trate;                                     /* 935 */
    EffectSetScreenSampleClamp(stqparam[1]);                    /* 936 */
    stqparam[2][0] = 0.0009765625f; stqparam[2][1] = 0.001953125f; stqparam[2][2] = 1.0f; stqparam[2][3] = 0.0625f; /* 937 */

    /* PORT DEVIATION: the ROM primed vf4..vf7 with slm (gra3dVu0.h 119) and
     * vf8..vf10 with stqparam (effect_obj.h 199/200) here, then ran CalcStqXYZ
     * against the register file.  Both are parameters on the host. */

    k  = 0;                                                     /* 941 */
    yy = 0.0f - tsh * 8.0f;                                     /* 942 */

    for (j = 0; j < vnumh; j++)                                 /* 943 */
    {
        xx = 0.0f - tsw * 8.0f;                                 /* 944 */

        for (i = 0; i < vnumw; i++)                             /* 945 */
        {
            vt[k][0] = xx;
            vt[k][1] = yy;
            vt[k][2] = 0.0f;
            vt[k][3] = 1.0f;

            xx += tsw;                                          /* 947 */
            k++;                                                /* 948 */
        }                                                       /* 949 */

        yy += tsh;
    }                                                           /* 951 */

    for (i = 0; i < vnumw * vnumh; i++)                         /* 952 */
    {
        CalcStqXYZ(0, &vt[i], &stq[i], slm, stqparam);
    }                                                           /* 954 */

    if ((ec->dat.uc8[1] & 1) != 0)                              /* 956 */
    {
        r = passcnt[sbj];                                       /* 957 */
        n = (passflg[sbj] != (int)sys_wrk.count);               /* 958 */
    }
    else
    {
        r = ec->fw[0];
        n = 1;                                                  /* 961 */
    }

    switch (sbj)                                                /* 966 */
    {
    case 0:
        return ret_num;

    case 1:
        PartsDeformCalcWaveY(&vt[0], vnumw, vnumh, r, rate);    /* 974 */

        if (n != 0)
        {
            r = PartsDeformAddIso(r, spd + spd);
        }
        break;                                                  /* 981 */

    case 2:
        PartsDeformCalcWaveZ(&vt[0], vnumw, vnumh, tsw, tsh, cntw, cnth,
                             r, rate);                          /* 986 */

        if (n != 0)                                             /* 989 */
        {
            r = PartsDeformAddIso(r, spd * 4.0f);               /* 990 */
        }
        break;                                                  /* 993 */

    case 3:
        PartsDeformCalcWaveIsoZ(&vt[0], vnumw, vnumh, tsw, tsh, cntw, cnth,
                                r, rate);                       /* 998 */

        if (n != 0)
        {
            r = PartsDeformAddIso(r, spd + spd);
        }
        break;                                                  /* 1005 */

    case 4:
        PartsDeformCalcWaveRot(&vt[0], vnumw, vnumh, tsw, tsh, cntw, cnth,
                               r, pefi);                        /* 1010 */

        if (n != 0)
        {
            r = PartsDeformAddIso(r, spd + spd);
        }
        break;                                                  /* 1016 */

    case 5:
        PartsDeformCalcWaveRotZ(&vt[0], vnumw, vnumh, tsw, tsh, cntw, cnth,
                                r, pefi);                       /* 1021 */

        if (n != 0)                                             /* 1023 */
        {
            r = PartsDeformAddIso(r, spd + spd);                /* 1024 */
        }
        break;                                                  /* 1027 */

    case 6:
        PartsDeformCalcWaveBound(&vt[0], vnumw, vnumh, tsw, tsh, cntw, cnth,
                                 r, ec->fw[1]);

        f3 = cosf((r * 3.1415925f) / 180.0f) * ec->fw[1];       /* 1034 */

        if (n != 0)                                             /* 1036 */
        {
            r = PartsDeformAddIso(r, 6.0f);                     /* 1037 */
        }

        /* The bounce loses four fifths of its height every time the phase
         * crosses a half period, and is snapped to rest once it is small
         * enough -- otherwise it would ring for ever. */
        f4 = cosf(r * 0.0174532905f) * f3;                      /* 1039 */

        if (f4 < 0.0f)
        {
            if (ec->fw[1] <= 0.02f)
            {
                ec->fw[1] = 0.0f;
            }
            else
            {
                ec->fw[1] = ec->fw[1] * 0.799999952f;           /* 1047 */
            }
        }
        break;

    case 7:
        PartsDeformCalcWaveRenz1(&vt[0], vnumw, vnumh, tsw, tsh, cntw, cnth,
                                 renz);
        break;                                                  /* 1053 */

    case 8:
        PartsDeformCalcWaveRenz2(&vt[0], vnumw, vnumh, tsw, tsh, cntw, cnth,
                                 renz);
        break;
    }

    passflg[sbj] = (int)sys_wrk.count;                          /* 1061 */

    if ((ec->dat.uc8[1] & 1) != 0)                              /* 1065 */
    {
        passcnt[sbj] = r;                                       /* 1066 */
    }
    else
    {
        ec->fw[0] = r;
    }

    Reserve2DPacket(0x10);                                      /* 1071 */

    if ((sclx != 0.0f) && (scly != 0.0f))                       /* 1075 */
    {
        if (page != 0)                                          /* 1078 */
        {
            MakePartsDeformPacket(16, 16, &vt[0], wlm, &stq[0], pdeform_alpha2,
                                  aprate, tex0, ec->r, ec->g, ec->b,
                                  ec->dat.uc8[5]);
        }
        else
        {
            MakePartsDeformPacket(16, 16, &vt[0], wlm, &stq[0], pdeform_alpha1,
                                  aprate, tex0, ec->r, ec->g, ec->b,
                                  ec->dat.uc8[5]);
        }
    }

    return ret_num;                                             /* 1090 */
}                                                               /* 1091 */

/* 1103..1427 -- the eight pass-1 displacement kernels.  Six of the eight are
 * reconstructed below; PartsDeformCalcWaveRot and ...RotZ (528 bytes each) are
 * still stubs.
 *
 * They all have the same skeleton: walk the vnumw x vnumh deform grid as one
 * flat run of vertices, recover each vertex's grid position from the index, and
 * drive a displacement from its distance to a centre:
 *
 *      wfw = (i % vnumw) * tsw - cntw;         offset from the centre
 *      wfh = (i / vnumw) * tsh - cnth;
 *      len = g3dxVu0Sqrt2(wfw, wfh);           and maxl, the same thing
 *                                              measured from grid origin (0,0)
 *
 * so `len / maxl` is a normalised radius and `1 - len / maxl` the usual soft
 * falloff, full at the centre and zero at the far corner.  What differs between
 * the kernels is which component they write, whether they add to it or replace
 * it, and where the strength term enters the falloff.
 *
 * Two things about the debug info hold for all six.  The int locals list is
 * exhaustive, but the FLOAT list is not -- neither the per-vertex length nor
 * the pre-loop `maxl` carries a stab, though `wfw`, `wfh` and `l` beside them
 * do (see [[float-locals-leave-no-stab]]).  `maxl` has to be a real local: it is
 * computed from wfw/wfh before the loop overwrites them, so there is nowhere
 * else the value could live.  The per-vertex length genuinely is a CSE temp --
 * the source spells the expression out at each use.
 *
 * And GCC hoists the one `div` (which serves both `i / vnumw` and `i % vnumw`)
 * to the top of the loop body, where it collects the $LM labels of the loop's
 * first source lines -- 1110/1111, 1144/1145, 1187/1188, 1340/1341, 1379/1380.
 * No separable statement accounts for those lines; they are the scheduler.
 * PI/180 is written out at every use, so it occupies four consecutive .lit4
 * slots (0x3edbb0..0x3edbbc) all holding the same word. */
static void PartsDeformCalcWaveY(sceVu0FVECTOR *vt, int vnumw, int vnumh,
                                 float base_iso, float rate)
{
    float vtrate;
    int   i;

    vtrate = rate * 0.799999952f;                               /* 1108 */

    for (i = 0; i < vnumw * vnumh; i++)                         /* 1109 */
    {
        vt[i][1] += sinf((base_iso + (i / vnumw) * 30)
                         * 0.0174532905f) * vtrate;             /* 1113 */
    }                                                           /* 1114 */
}

static void PartsDeformCalcWaveZ(sceVu0FVECTOR *vt, int vnumw, int vnumh,
                                 float tsw, float tsh, float cntw, float cnth,
                                 float base_iso, float rate)
{
    float vtrate;
    int   i;
    float l;
    float wfw;
    float wfh;
    float maxl;                     /* no stab -- see the block comment */

    wfw  = 0.0f - cntw;                                         /* 1138 */
    wfh  = 0.0f - cnth;                                         /* 1139 */
    maxl = g3dxVu0Sqrt2(wfw, wfh);                              /* 1140 */

    vtrate = rate * 3.0f;                                       /* 1142 */

    for (i = 0; i < vnumw * vnumh; i++)                         /* 1143 */
    {
        wfw = (i % vnumw) * tsw - cntw;                         /* 1147 */
        wfh = (i / vnumw) * tsh - cnth;                         /* 1148 */

        l = g3dxVu0Sqrt2(wfw, wfh) * 30.0f;                     /* 1152 */

        vt[i][2] = sinf((base_iso + l) * 0.0174532905f)
                 * (1.0f - g3dxVu0Sqrt2(wfw, wfh) / maxl)
                 * vtrate;                                      /* 1155 */
    }                                                           /* 1156 */
}

/* The iso variant differs from CalcWaveZ in where the phase comes from: the
 * base angle is turned into an amplitude ONCE, before the loop, and the
 * per-vertex radius then drives the phase -- so the ripple runs outward in
 * rings (iso-lines) rather than sweeping across the grid. */
static void PartsDeformCalcWaveIsoZ(sceVu0FVECTOR *vt, int vnumw, int vnumh,
                                    float tsw, float tsh, float cntw, float cnth,
                                    float base_iso, float rate)
{
    float rr;
    int   i;
    float wfw;
    float wfh;
    float l;
    float maxl;                     /* no stab -- see the block comment */

    wfw  = 0.0f - cntw;                                         /* 1180 */
    wfh  = 0.0f - cnth;                                         /* 1181 */
    maxl = g3dxVu0Sqrt2(wfw, wfh);                              /* 1182 */

    rr = sinf(base_iso * 0.0174532905f) * 60.0f;                /* 1184 */

    for (i = 0; i < vnumw * vnumh; i++)                         /* 1186 */
    {
        wfw = (i % vnumw) * tsw - cntw;                         /* 1190 */
        wfh = (i / vnumw) * tsh - cnth;                         /* 1191 */

        l = g3dxVu0Sqrt2(wfw, wfh) * 10.0f;                     /* 1195 */

        vt[i][2] = sinf(rr * l * 0.0174532905f)
                 * (1.0f - g3dxVu0Sqrt2(wfw, wfh) / maxl)
                 * rate;                                        /* 1197 */
    }                                                           /* 1198 */
}

/* The two rotation kernels spin each vertex about the grid centre by an angle
 * the grid itself carries: `pefi->ep[i].lng`, scaled by one sine of the base
 * angle.  So unlike every other kernel here the displacement is authored
 * per-vertex rather than derived from the radius, which is what lets a deform
 * twist unevenly.
 *
 * The border is pinned.  A vertex on any of the four edges skips the rotation
 * and is only re-centred, so the sheet shears inside a fixed frame.
 *
 * TWO ROM ODDITIES, both reproduced.  The row index is tested against `pnumw`,
 * not `pnumh` -- `pnumh` is dead after line 1227 and GCC reuses its register
 * for the vertex cursor, so this is the source's own text, invisible only
 * because the grid is square (17 x 17).  And ...RotZ is a byte-for-byte copy of
 * ...Rot: 130 of their 132 words are identical and the two that differ are the
 * gp offsets of duplicate .lit4 slots holding the same PI/180 and PI.  Despite
 * the name it writes x and y, not z.  That is the fourth same-body-twice pair
 * in this folder, after EffectHazeGetParameterPtr/Org, EneDmgLargeHitCtrlInit/
 * AllOff and GetChangeFrame's two identical jump tables.
 *
 * The angle and the two trig results have no stabs; the names here are ours.
 * The ROM's own locals really are called f1..f4 -- that is functions.txt, not
 * an invention. */
static void PartsDeformCalcWaveRot(sceVu0FVECTOR *vt, int vnumw, int vnumh,
                                   float tsw, float tsh, float cntw, float cnth,
                                   float base_iso, const EFFINFO2 *pefi)
{
    int   pnumw;
    int   pnumh;
    int   i;
    float rr;
    float wfw;
    float wfh;
    float f1;
    float f2;
    float f3;
    float f4;
    float ang, fs, fc;              /* no stabs -- names ours */

    pnumw = vnumw - 1;                                          /* 1222 */
    pnumh = vnumh - 1;                                          /* 1223 */
    rr    = sinf(base_iso * 0.0174532905f) * 60.0f;             /* 1224 */

    f1 = pnumw * 0.5f * tsw;                                    /* 1226 */
    f2 = pnumh * 0.5f * tsh;                                    /* 1227 */

    for (i = 0; i < vnumw * vnumh; i++)                         /* 1229 */
    {
        f3  = (i % vnumw) * tsw;                                /* 1233 */
        f4  = (i / vnumw) * tsh;                                /* 1234 */
        wfw = f3 - cntw;                                        /* 1236 */
        wfh = f4 - cnth;                                        /* 1237 */

        /* pnumw on both axes is the ROM's -- see the block comment. */
        if (i % vnumw != 0 && i % vnumw != pnumw &&
            i / vnumw != 0 && i / vnumw != pnumw)               /* 1239 */
        {
            ang = rr * pefi->ep[i].lng * 3.1415925f / 180.0f;   /* 1243 */
            fs  = sinf(ang);                                    /* 1244 */
            fc  = cosf(ang);                                    /* 1245 */

            vt[i][0] = wfw * fc - wfh * fs + cntw - f1;         /* 1247 */
            vt[i][1] = wfw * fs + wfh * fc + cnth - f2;         /* 1248 */
        }
        else
        {
            vt[i][0] = f3 - f1;                                 /* 1250 */
            vt[i][1] = f4 - f2;                                 /* 1251 */
        }
    }                                                           /* 1253 */
}

static void PartsDeformCalcWaveRotZ(sceVu0FVECTOR *vt, int vnumw, int vnumh,
                                    float tsw, float tsh, float cntw, float cnth,
                                    float base_iso, const EFFINFO2 *pefi)
{
    int   pnumw;
    int   pnumh;
    int   i;
    float rr;
    float wfw;
    float wfh;
    float f1;
    float f2;
    float f3;
    float f4;
    float ang, fs, fc;              /* no stabs -- names ours */

    pnumw = vnumw - 1;                                          /* 1279 */
    pnumh = vnumh - 1;                                          /* 1280 */
    rr    = sinf(base_iso * 0.0174532905f) * 60.0f;             /* 1281 */

    f1 = pnumw * 0.5f * tsw;                                    /* 1283 */
    f2 = pnumh * 0.5f * tsh;                                    /* 1284 */

    for (i = 0; i < vnumw * vnumh; i++)                         /* 1286 */
    {
        f3  = (i % vnumw) * tsw;                                /* 1290 */
        f4  = (i / vnumw) * tsh;                                /* 1291 */
        wfw = f3 - cntw;                                        /* 1293 */
        wfh = f4 - cnth;                                        /* 1294 */

        if (i % vnumw != 0 && i % vnumw != pnumw &&
            i / vnumw != 0 && i / vnumw != pnumw)               /* 1296 */
        {
            ang = rr * pefi->ep[i].lng * 3.1415925f / 180.0f;   /* 1300 */
            fs  = sinf(ang);                                    /* 1301 */
            fc  = cosf(ang);                                    /* 1302 */

            vt[i][0] = wfw * fc - wfh * fs + cntw - f1;         /* 1304 */
            vt[i][1] = wfw * fs + wfh * fc + cnth - f2;         /* 1305 */
        }
        else
        {
            vt[i][0] = f3 - f1;                                 /* 1307 */
            vt[i][1] = f4 - f2;                                 /* 1308 */
        }
    }                                                           /* 1310 */
}

/* Bound is the only pass-1 kernel that moves a vertex in the grid PLANE rather
 * than out of it: it writes x and y, and it adds its displacement to the
 * vertex's own grid position.  The strength is a cosine of the base angle taken
 * once per vertex, so the whole sheet swells and relaxes together.
 *
 * `- tsw * 8.0f` / `- tsh * 8.0f` re-centre the grid on the origin.  For the
 * 17 x 17 grid the deform actually uses that is exactly cntw / cnth, but the
 * ROM writes the literal 8, so it is written out here too. */
static void PartsDeformCalcWaveBound(sceVu0FVECTOR *vt, int vnumw, int vnumh,
                                     float tsw, float tsh, float cntw, float cnth,
                                     float base_iso, float renz)
{
    int   i;
    float wfw;
    float wfh;
    float fw;
    float maxl;                     /* no stab -- see the block comment */

    wfw  = 0.0f - cntw;                                         /* 1335 */
    wfh  = 0.0f - cnth;                                         /* 1336 */
    maxl = g3dxVu0Sqrt2(wfw, wfh);                              /* 1337 */

    for (i = 0; i < vnumw * vnumh; i++)                         /* 1339 */
    {
        wfw = (i % vnumw) * tsw - cntw;                         /* 1343 */
        wfh = (i / vnumw) * tsh - cnth;                         /* 1344 */

        fw = cosf(base_iso * 0.0174532905f) * renz;             /* 1348 */

        vt[i][0] = (1.0f - g3dxVu0Sqrt2(wfw, wfh) / maxl) * fw * wfw
                 + (i % vnumw) * tsw - tsw * 8.0f;              /* 1349 */
        vt[i][1] = (1.0f - g3dxVu0Sqrt2(wfw, wfh) / maxl) * fw * wfh
                 + (i / vnumw) * tsh - tsh * 8.0f;              /* 1350 */
    }                                                           /* 1351 */
}

/* The two lens kernels pull the grid in towards its centre -- with the falloff
 * at 1 a vertex keeps its own position, at 0 it collapses onto (cntw, cnth) --
 * which is the barrel warp the viewfinder lens applies.
 *
 * They differ only in where `renz` enters, and that is the whole difference
 * between them: Renz1 scales the DISPLACEMENT by it (strength of the pull),
 * Renz2 scales the RADIUS term inside the falloff (reach of the pull).  So
 * Renz1 with renz > 1 can push a vertex past its own position and Renz2 cannot. */
static void PartsDeformCalcWaveRenz1(sceVu0FVECTOR *vt, int vnumw, int vnumh,
                                     float tsw, float tsh, float cntw, float cnth,
                                     float renz)
{
    int   i;
    float wfw;
    float wfh;
    float maxl;                     /* no stab -- see the block comment */

    wfw  = 0.0f - cntw;                                         /* 1373 */
    wfh  = 0.0f - cnth;                                         /* 1374 */
    maxl = g3dxVu0Sqrt2(wfw, wfh);                              /* 1375 */

    for (i = 0; i < vnumw * vnumh; i++)                         /* 1378 */
    {
        wfw = (i % vnumw) * tsw - cntw;                         /* 1382 */
        wfh = (i / vnumw) * tsh - cnth;                         /* 1383 */

        vt[i][0] = (1.0f - g3dxVu0Sqrt2(wfw, wfh) / maxl) * wfw * renz
                 + cntw - tsw * 8.0f;                           /* 1387 */
        vt[i][1] = (1.0f - g3dxVu0Sqrt2(wfw, wfh) / maxl) * wfh * renz
                 + cnth - tsh * 8.0f;                           /* 1388 */
    }                                                           /* 1389 */
}

static void PartsDeformCalcWaveRenz2(sceVu0FVECTOR *vt, int vnumw, int vnumh,
                                     float tsw, float tsh, float cntw, float cnth,
                                     float renz)
{
    int   i;
    float wfw;
    float wfh;
    float maxl;                     /* no stab -- see the block comment */

    wfw  = 0.0f - cntw;                                         /* 1411 */
    wfh  = 0.0f - cnth;                                         /* 1412 */
    maxl = g3dxVu0Sqrt2(wfw, wfh);                              /* 1413 */

    for (i = 0; i < vnumw * vnumh; i++)                         /* 1416 */
    {
        wfw = (i % vnumw) * tsw - cntw;                         /* 1420 */
        wfh = (i / vnumw) * tsh - cnth;                         /* 1421 */

        vt[i][0] = (1.0f - g3dxVu0Sqrt2(wfw, wfh) / maxl * renz) * wfw
                 + cntw - tsw * 8.0f;                           /* 1425 */
        vt[i][1] = (1.0f - g3dxVu0Sqrt2(wfw, wfh) / maxl * renz) * wfh
                 + cnth - tsh * 8.0f;                           /* 1426 */
    }                                                           /* 1427 */
}

/* 1445 -- seed a pass-2 work slot's 289 vertices.  1184 bytes, source
 * 1445..1522.  Every vertex gets a phase (`r`), an amplitude (`h`) and a
 * per-frame step (`add`); which distribution they are drawn from is what the
 * type selects.
 *
 *   1  free-running:  phase anywhere in the circle, amplitude 0.1 .. 0.5,
 *      step (1 .. 2.5) * spd -- so nothing on the grid is in step with
 *      anything else, which is the boiling look.
 *   2  radial:  the same free-running phase and a bigger amplitude, plus
 *      `lng` = the distance from the grid centre times 150.  That is the term
 *      the Rot kernels rotate about and the Renz ones collapse towards.
 *   3  woven:  the phase and amplitude are *inherited* rather than drawn --
 *      each vertex takes its left neighbour's `h` and its upstairs
 *      neighbour's `r` plus a small random drift, so the grid ripples as one
 *      sheet instead of as 289 independent points.  Column 0 and row 0 seed
 *      the walk from pPara.
 *
 * The jump table (.rodata 3a66c0) has six entries and the range check is a
 * bare `(u_int)type < 6`, no subtraction -- so the case labels really do span
 * 0..5 and 0, 4 and 5 are empty cases pointing at the break label, not holes.
 * See [[jump-table-slot-at-break-is-a-missing-case]].
 *
 * The random idiom is the file's usual `EffectGetRandom(0.0f, width) + base`,
 * not `EffectGetRandom(base, base + width)`: the .lit4 at 3edbd4 is 0x3ecccccc,
 * EE GCC's truncation of a written `0.4f`, where GCC's fold of `0.5f - 0.1f`
 * would be 0x3ecccccd.  Same measurement effect_ene.c records at its own two
 * particle seeders.
 *
 * Line 1473 / 1503 sit on the single `div` that serves both `i / vnumw` and
 * `i % vnumw`; the stabs list only i, wfw and wfh, so no separable statement
 * accounts for them. */
static void PartsDeformEffInf02Init(EFFINFO2 *pEffInfo, int type, int vnumw,
                                    int vnumh, float tsw, float tsh,
                                    float cntw, float cnth, float spd,
                                    PDEFORM_PARA *pPara)
{
    int   i;
    float wfw;
    float wfh;

    pEffInfo->r = 0.0f;                                         /* 1451 */

    switch (type)                                               /* 1454 */
    {
    case 0:
        break;

    case 1:
        for (i = 0; i < vnumw * vnumh; i++)                     /* 1458 */
        {
            pEffInfo->ep[i].r   = EffectGetRandom(0.0f, 360.0f);
            pEffInfo->ep[i].h   = EffectGetRandom(0.0f, 0.4f) + 0.1f;
            pEffInfo->ep[i].add = (EffectGetRandom(0.0f, 1.5f) + 1.0f) * spd;
        }                                                       /* 1462 */
        break;

    case 2:
        for (i = 0; i < vnumw * vnumh; i++)                     /* 1472 */
        {                                                       /* 1473 */
            wfw = (i % vnumw) * tsw - cntw;                     /* 1475 */
            wfh = (i / vnumw) * tsh - cnth;                     /* 1476 */

            pEffInfo->ep[i].lng = g3dxVu0Sqrt2(wfw, wfh) * 150.0f;
            pEffInfo->ep[i].r   = EffectGetRandom(0.0f, 360.0f);
            pEffInfo->ep[i].h   = EffectGetRandom(0.0f, 4.0f) + 1.0f;
            pEffInfo->ep[i].add = (EffectGetRandom(0.0f, 3.0f) + 2.0f) * spd;
        }                                                       /* 1484 */
        break;

    case 3:
        for (i = 0; i < vnumw * vnumh; i++)                     /* 1502 */
        {                                                       /* 1503 */
            if (i % vnumw == 0)                                 /* 1506 */
            {
                pEffInfo->ep[i].h = EffectGetRandom(0.0f, pPara->pr11)
                                  + pPara->pr12;
            }
            else
            {
                pEffInfo->ep[i].h = pEffInfo->ep[i - 1].h
                                  + EffectGetRandom(0.0f, pPara->pr21)
                                  - pPara->pr22;
            }

            if (i / vnumw == 0)                                 /* 1511 */
            {
                pEffInfo->ep[i].r = EffectGetRandom(0.0f, pPara->pr11)
                                  + pPara->pr12;
            }
            else
            {
                pEffInfo->ep[i].r = pEffInfo->ep[i - vnumw].r
                                  + EffectGetRandom(0.0f, pPara->pr21)
                                  - pPara->pr22;
            }
        }                                                       /* 1516 */
        break;

    case 4:
        break;

    case 5:
        break;
    }
}                                                               /* 1522 */

/* 1530 -- pass 2: the *shared-shape* deform.  1848 bytes, source 1530..1703.
 *
 * Same skeleton as pass 1 -- matrices, clip check, flat grid, per-vertex screen
 * STQ, one kernel, one packet -- with three real differences.
 *
 * 1. The per-vertex phase table is not per-effect.  `pefi_once[5]` is seeded
 *    once, on the first call after `init_pdef2` is raised (InitEffectObj), and
 *    a one-shot effect (ec->dat.uc8[1] & 1) borrows the slot its sub-type owns
 *    instead of claiming one.  So every door seal in the room boils in step,
 *    which is the point: they are meant to read as one material.  Slot 4 is the
 *    override for a non-zero DrawEnvType -- and PartsDeformEffInf02Init's case 5
 *    is empty, so that slot is deliberately left at zero.
 * 2. `comp` is a distance *gain* rather than a fade: beyond 750 units it grows
 *    with distance (d / 1300), below that it is pinned at the same ratio.  The
 *    four pass-2 kernels take it as their amplitude, so a far-away seal deforms
 *    harder to survive being small on screen.  `aprate` is the pass-1 fade,
 *    unchanged.
 * 3. The screen copy happens at the *end*, just before the packet, not before
 *    the grid -- and there is no page split, so pass 2 always samples the frame
 *    buffer at 0x2bc0.
 *
 * `page`, `fl` and `trate` are all dead: SetPartsDeform passes 2, 1 and the
 * computed trate, and nothing here reads any of them.
 *
 * `pefi->pass = 1` is reached only on the two `num` paths -- the branch out of
 * the pefi_once arm lands one instruction past the store, so a borrowed slot is
 * never marked live and InitEffectObjEF() cannot recycle it.  It does not need
 * to: pefi_once is not part of `efi`. */
static u_char SubPartsDeform2(EFFECT_CONT *ec, u_char num, int page, int sbj,
                              float sclx, float scly, float vol, int fl,
                              float spd, float rate, float trate)
{
    static fixed_array<EFFINFO2, 5> pefi_once;                  /* bss 42f480 */

    EFFINFO2    *pefi;
    u_char       ret_num;
    int          i, j, k;
    int          vnumw;
    float        l;
    float        cntw, cnth;
    float        tsw, tsh;
    float        rot_x, rot_y;
    PDEFORM_PARA para;
    float        comp;
    float        aprate;
    float        xx, yy;
    float        stqparam[3][4];
    fixed_array<sceVu0FVECTOR, PDEFORM_VERTEX_MAX> stq;
    fixed_array<sceVu0FVECTOR, PDEFORM_VERTEX_MAX> vt;
    float        vpos[4];
    float        slm[4][4];
    float        wlm[4][4];
    GRA3DCAMERA *pCam;

    (void)page; (void)fl; (void)trate;

    ret_num = 0;                                                /* 1535 */

    /* A local aggregate initialiser: GCC copies it out of .rodata 3a66d8 into
     * the frame.  globals.txt is right not to list it. */
    para.pr11 = 1.0f; para.pr12 = -0.299999982f;
    para.pr21 = 0.399999976f; para.pr22 = 0.199999988f;         /* 1543 */

    vnumw = PDEFORM_VERTEX_W;

    tsw  = sclx + sclx;                                         /* 1559 */
    pCam = gra3dGetCamera();                                    /* 1556 */
    float (&cam_dir)[4] = gra3dcamGetDirection();               /* 1557 */
    tsh  = scly + scly;                                         /* 1560 */

    cntw = (8 % vnumw) * tsw;                                   /* 1565 */
    cnth = (8 % vnumw) * tsh;                                   /* 1566 */

    if (init_pdef2 != 0)                                        /* 1568 */
    {
        for (i = 0; i < 5; i++)                                 /* 1569 */
        {
            PartsDeformEffInf02Init(&pefi_once[i], i + 1,
                                    vnumw, PDEFORM_VERTEX_H,
                                    tsw, tsh, cntw, cnth, spd, &para); /* 1573 */
        }                                                       /* 1574 */

        init_pdef2 = 0;                                         /* 1575 */
    }

    if ((ec->dat.uc8[1] & 1) != 0)                              /* 1578 */
    {
        if (ec->dat.uc8[5] == 0)                                /* 1579 */
        {
            pefi = &pefi_once[sbj - 1];
        }
        else
        {
            pefi = &pefi_once[4];
        }
    }
    else
    {
        if (num == 0xff)                                        /* 1588 */
        {
            ret_num = (u_char)GetFreePartsDeformCtrlNo();       /* 1591 */
            pefi    = GetPartsDeformCtrlWrk(ret_num);           /* 1592 */

            if (pefi == nullptr)                                /* 1593 */
            {
                printf("PartsDeform Buffer is Full!!\n");       /* 1594 */
                return ret_num;                                 /* 1595 */
            }

            PartsDeformEffInf02Init(pefi, sbj, vnumw, PDEFORM_VERTEX_H,
                                    tsw, tsh, cntw, cnth, spd, &para); /* 1599 */
        }
        else
        {
            pefi = GetPartsDeformCtrlWrk(num);                  /* 1601 */

            if (pefi == nullptr)                                /* 1602 */
            {
                return num;
            }

            ret_num = num;                                      /* 1603 */
        }

        pefi->pass = 1;                                         /* 1605 */
    }

    Vector2Rot(cam_dir, &rot_x, &rot_y);                        /* 1610 */

    g3dxVu0CopyVector(vpos, (const float *)ec->pnt[0]);

    l = GetDistancePosToCamera(vpos);                           /* 1614 */

    /* 750/1300 folds exactly to the ROM's 0x3f13b13b, so the floor really is
     * the ratio at the knee rather than a separate literal. */
    comp = (l > 750.0f) ? l / 1300.0f : 750.0f / 1300.0f;       /* 1615 */

    aprate = (l > 200.0f) ? vol
           : (l < 100.0f) ? 0.0f
                          : (l - 100.0f) * vol / 100.0f;        /* 1616 */

    sceVu0UnitMatrix(wlm);                                      /* 1619 */
    wlm[0][0] = 25.0f; wlm[1][1] = 25.0f; wlm[2][2] = 25.0f;    /* 1620 */
    sceVu0RotMatrixY(wlm, wlm, rot_y);                          /* 1621 */
    sceVu0TransMatrix(wlm, wlm, vpos);                          /* 1622 */
    sceVu0MulMatrix(slm, pCam->matWorldScreen, wlm);            /* 1623 */

    if (PartsDeformClipCheck(slm) != 0)                         /* 1626 */
    {
        return ret_num;
    }

    stqparam[0][0] = 1727.5f;                                   /* 1632 */
    stqparam[0][1] = 1823.5f;                                   /* 1633 */
    stqparam[0][2] = 1.0f;                                      /* 1634 */
    EffectSetScreenSampleClamp(stqparam[1]);                    /* 1635 */
    stqparam[2][0] = 0.0009765625f; stqparam[2][1] = 0.001953125f; stqparam[2][2] = 1.0f; stqparam[2][3] = 0.0625f; /* 1636 */

    /* PORT DEVIATION: gra3dVu0.h 119 and effect_obj.h 199/200 primed vf4..vf10
     * here; both are CalcStqXYZ parameters on the host. */

    k  = 0;                                                     /* 1639 */
    yy = tsh * -8.0f;                                           /* 1640 */

    for (j = 0; j < PDEFORM_VERTEX_H; j++)                      /* 1641 */
    {
        xx = tsw * -8.0f;                                       /* 1642 */

        for (i = 0; i < vnumw; i++)                             /* 1643 */
        {
            vt[k][0] = xx;
            vt[k][1] = yy;
            vt[k][2] = 0.0f;
            vt[k][3] = 1.0f;

            CalcStqXYZ(0, &vt[k], &stq[k], slm, stqparam);

            xx += tsw;                                          /* 1646 */
            k++;
        }                                                       /* 1648 */

        yy += tsh;
    }                                                           /* 1650 */

    switch (sbj)                                                /* 1653 */
    {
    case 0:
        return ret_num;

    case 1:
        PartsDeform2CalcWaveY(&vt[0], vnumw, PDEFORM_VERTEX_H,
                              comp, rate, spd, pefi);           /* 1660 */
        break;                                                  /* 1662 */

    case 2:
        PartsDeform2CalcWaveZ(&vt[0], vnumw, PDEFORM_VERTEX_H,
                              comp, rate, spd, pefi);           /* 1666 */
        break;                                                  /* 1668 */

    case 3:
        PartsDeform2CalcWaveY2(&vt[0], vnumw, PDEFORM_VERTEX_H,
                               comp, rate, spd, &para, pefi);
        break;                                                  /* 1674 */

    case 4:
        PartsDeform2CalcWaveType2(&vt[0], vnumw, PDEFORM_VERTEX_H,
                                  comp, rate, spd, &para, pefi);
        break;
    }

    Reserve2DPacket(0x10);                                      /* 1685 */

    if ((sclx != 0.0f) && (scly != 0.0f))                       /* 1689 */
    {
        LocalCopyLtoL(3, (sys_wrk.count & 1) * 0x1180, 0x2bc0); /* 1694 */

        MakePartsDeformPacket(16, 16, &vt[0], wlm, &stq[0], pdeform_alpha1,
                              aprate, 0x2000000264116bc0ULL,
                              ec->r, ec->g, ec->b, ec->dat.uc8[5]);
    }

    return ret_num;                                             /* 1702 */
}                                                               /* 1703 */

/* 1718..1941 -- the four pass-2 kernels (652 / 624 / 888 / 644 bytes).
 *
 * They differ from the pass-1 eight in three ways.  They pin the whole border
 * (a vertex on any edge is skipped entirely, not just displaced less), they
 * carry their own per-vertex phase in the EFFINFO2 slot rather than taking one
 * scalar from the caller, and three of them re-seed a vertex's amplitude as it
 * crosses zero -- which is what keeps a boiling surface from settling into a
 * repeating pattern.  `EFFPOS::ox`/`oy` are the previous frame's displacement
 * for exactly that test, not a rest position.
 *
 * `EffWrkStopFlgGet()` is the freeze: the shape still draws, it just stops
 * advancing.  Note it is polled twice per vertex in WaveY and WaveZ, once for
 * the re-seed and once for the phase step, rather than hoisted. */
static void PartsDeform2CalcWaveY(sceVu0FVECTOR *vt, int vnumw, int vnumh,
                                  float comp, float rate, float spd,
                                  EFFINFO2 *pefi)
{
    int   i;
    float l;

    for (i = 0; i < vnumw * vnumh; i++)                         /* 1724 */
    {                                                           /* 1725 */
        if ((i % vnumw != 0) && (i % vnumw != vnumw - 1) &&
            (i / vnumw != 0) && (i / vnumw != vnumh - 1))       /* 1729 */
        {
            l = sinf((pefi->ep[i].r + (i / vnumw) * 50.0f) * 3.1415925f / 180.0f)
                * pefi->ep[i].h * comp * rate;                  /* 1730 */
            vt[i][0] += l;                                      /* 1731 */

            l = sinf((pefi->ep[i].r + (i % vnumw) * 50.0f) * 3.1415925f / 180.0f)
                * pefi->ep[i].h * comp * rate;                  /* 1733 */
            vt[i][1] += l;                                      /* 1734 */

            if (EffWrkStopFlgGet() == 0)                        /* 1736 */
            {
                if ((vt[i][0] >= 0.0f) && (pefi->ep[i].ox < 0.0f)) /* 1737 */
                {
                    pefi->ep[i].h   = EffectGetRandom(0.0f, 0.4f) + 0.1f;
                    pefi->ep[i].add = EffectGetRandom(0.0f, 1.5f) * spd + spd;
                }
            }
        }

        if (EffWrkStopFlgGet() == 0)                            /* 1744 */
        {
            pefi->ep[i].r = (pefi->ep[i].r - pefi->ep[i].add < -360.0f)
                          ? pefi->ep[i].r + 360.0f - pefi->ep[i].add
                          : pefi->ep[i].r - pefi->ep[i].add;    /* 1745 */
            pefi->ep[i].ox = vt[i][0];                          /* 1746 */
        }
    }                                                           /* 1748 */
}

/* 1763 -- the same machine driving z instead of x and y, with the radial
 * falloff EffInf02Init's case 2 baked into ep[].lng: a vertex at the rim of the
 * grid barely moves and the centre moves fully.  ep[0].lng is the reference
 * length, so the grid must have been seeded by case 2 for this to mean
 * anything. */
static void PartsDeform2CalcWaveZ(sceVu0FVECTOR *vt, int vnumw, int vnumh,
                                  float comp, float rate, float spd,
                                  EFFINFO2 *pefi)
{
    int   i;
    float ml;
    float l;

    ml = pefi->ep[0].lng;

    for (i = 0; i < vnumw * vnumh; i++)                         /* 1771 */
    {                                                           /* 1772 */
        l = pefi->ep[i].lng;

        if ((i % vnumw != 0) && (i % vnumw != vnumw - 1) &&
            (i / vnumw != 0) && (i / vnumw != vnumh - 1))       /* 1787 */
        {
            vt[i][2] = sinf((pefi->ep[i].r + l) * 3.1415925f / 180.0f)
                     * (1.0f - l / ml) * pefi->ep[i].h * comp * rate; /* 1788 */

            if (EffWrkStopFlgGet() == 0)                        /* 1791 */
            {
                if ((vt[i][2] >= 0.0f) && (pefi->ep[i].ox < 0.0f)) /* 1792 */
                {
                    pefi->ep[i].h   = EffectGetRandom(0.0f, 4.0f) + 1.0f;
                    pefi->ep[i].add = (EffectGetRandom(0.0f, 3.0f) + 2.0f) * spd;
                }
            }
        }

        if (EffWrkStopFlgGet() == 0)                            /* 1798 */
        {
            pefi->ep[i].r = (pefi->ep[i].r - pefi->ep[i].add < -360.0f)
                          ? pefi->ep[i].r + 360.0f - pefi->ep[i].add
                          : pefi->ep[i].r - pefi->ep[i].add;    /* 1799 */
            pefi->ep[i].ox = vt[i][2];                          /* 1800 */
        }
    }                                                           /* 1802 */
}

/* 1818 -- the woven one.  There is a single phase for the whole grid
 * (EFFINFO2::r), and what varies per vertex is the *amplitude* pair -- ep[].r
 * for x, ep[].h for y -- which is inherited from the left and upstairs
 * neighbours by EffInf02Init's case 3 and drifts by pPara whenever a vertex
 * crosses zero.  That is what makes it read as a sheet rather than as 289
 * independent points.
 *
 * The freeze branch is a full second copy of the loop that re-applies the last
 * displacement without advancing anything, so a frozen surface keeps its shape
 * instead of snapping flat. */
static void PartsDeform2CalcWaveY2(sceVu0FVECTOR *vt, int vnumw, int vnumh,
                                   float comp, float rate, float spd,
                                   PDEFORM_PARA *pPara, EFFINFO2 *pefi)
{
    int   i;
    float vtrate;
    float add;

    add = spd + spd;                                            /* 1825 */

    if (EffWrkStopFlgGet() == 0)                                /* 1827 */
    {
        vtrate = comp * rate;                                   /* 1828 */

        for (i = 0; i < vnumw * vnumh; i++)                     /* 1830 */
        {                                                       /* 1831 */
            pefi->ep[i].x = sinf((pefi->r + (i / vnumw) * 32.0f) * 0.0174532905f)
                          * pefi->ep[i].r * vtrate;             /* 1836 */
            pefi->ep[i].y = sinf((pefi->r + (i % vnumw) * 32.0f) * 0.0174532905f)
                          * pefi->ep[i].h * vtrate;             /* 1837 */

            if ((pefi->ep[i].y >= 0.0f) && (pefi->ep[i].oy < 0.0f)) /* 1839 */
            {
                if (i % vnumw == 0)                             /* 1840 */
                {
                    pefi->ep[i].h = EffectGetRandom(0.0f, pPara->pr11)
                                  + pPara->pr12;
                }
                else
                {
                    pefi->ep[i].h = pefi->ep[i - 1].h
                                  + EffectGetRandom(0.0f, pPara->pr21)
                                  - pPara->pr22;
                }
            }

            if ((pefi->ep[i].x >= 0.0f) && (pefi->ep[i].ox < 0.0f)) /* 1847 */
            {
                if (i / vnumw == 0)                             /* 1848 */
                {
                    pefi->ep[i].r = EffectGetRandom(0.0f, pPara->pr11)
                                  + pPara->pr12;
                }
                else
                {
                    pefi->ep[i].r = pefi->ep[i - vnumw].r
                                  + EffectGetRandom(0.0f, pPara->pr21)
                                  - pPara->pr22;
                }
            }

            pefi->ep[i].ox = pefi->ep[i].x;                     /* 1855 */
            pefi->ep[i].oy = pefi->ep[i].y;                     /* 1856 */

            vt[i][0] += pefi->ep[i].x;                          /* 1858 */
            vt[i][1] += pefi->ep[i].y;                          /* 1859 */
        }                                                       /* 1860 */

        pefi->r += add;                                         /* 1862 */
        if (pefi->r > 360.0f) { pefi->r -= 360.0f; }            /* 1863 */
    }
    else
    {
        for (i = 0; i < vnumw * vnumh; i++)                     /* 1865 */
        {
            vt[i][0] += pefi->ep[i].x;                          /* 1867 */
            vt[i][1] += pefi->ep[i].y;                          /* 1868 */
        }                                                       /* 1869 */
    }
}

/* 1886 -- the cheap one: a 17-entry sine table indexed by row and by column,
 * so the whole grid is one separable ripple and nothing is per-vertex at all.
 * There is no EFFPOS state, no re-seed and no freeze branch -- the phase always
 * advances.  `pPara` is declared and never read.
 *
 * SinCalcBuf is only 17 long while the loops run to vnumw/vnumh, so both index
 * sites are guarded by an explicit `< 17` and recompute the sine when the grid
 * is wider than the cache.  It never is in this build. */
static void PartsDeform2CalcWaveType2(sceVu0FVECTOR *vt, int vnumw, int vnumh,
                                      float comp, float rate, float spd,
                                      PDEFORM_PARA *pPara, EFFINFO2 *pefi)
{
    float add;
    float ll;
    float fw;
    int   i;
    int   j;
    fixed_array<float, 17> SinCalcBuf;

    (void)comp; (void)pPara;

    fw = rate / 10.0f;                                          /* 1894 */

    for (i = 0; i < 17; i++)                                    /* 1896 */
    {
        SinCalcBuf[i] = sinf((pefi->r + i * 50.0f) * 3.1415925f / 180.0f) * fw;
    }                                                           /* 1898 */

    for (j = 1; j < vnumh - 1; j++)                             /* 1901 */
    {
        for (i = 1; i < vnumw - 1; i++)                         /* 1902 */
        {
            ll = (j < 17) ? SinCalcBuf[j]                       /* 1903 */
                          : sinf((pefi->r + j * 50.0f) * 3.1415925f / 180.0f)
                            * fw;                               /* 1907 */
            vt[j * vnumw + i][0] += ll;                         /* 1909 */

            ll = (i < 17) ? SinCalcBuf[i]                       /* 1911 */
                          : sinf((pefi->r + i * 50.0f) * 3.1415925f / 180.0f)
                            * fw;                               /* 1915 */
            vt[j * vnumw + i][1] += ll;                         /* 1917 */
        }                                                       /* 1918 */
    }                                                           /* 1919 */

    add = spd + spd;                                            /* 1940 */

    pefi->r = (pefi->r + add > 360.0f) ? (pefi->r + add) - 360.0f
                                       : (pefi->r + add);       /* 1941 */
}

/* ==========================================================================
 *  Lens flare
 * ======================================================================== */

/* 1955 -- 32 bytes; a two-argument forward to GetCornHitCheck2() that throws
 * the two rates away. */
int GetCornHitCheck(float *bpos, float power)
{                                                               /* 1955 */
    float rrate;
    float lrate;

    return GetCornHitCheck2(bpos, power, &rrate, &lrate);       /* 1958 */
}

/* 1974 -- is a world point inside the light's 40-degree cone?  468 bytes,
 * source 1974..2010.  The cone stands at the origin looking down +z, so the
 * test is: transform the point into cone space, then check it is in front, no
 * further than `power`, and inside a radius that grows with z.
 *
 * `rrate` comes back as how far out along that widening radius the point sits
 * (1.0 at the cone's edge) and `lrate` as how far along its length.  Both are
 * written whether or not the answer is yes.
 *
 * ROM ODDITY, reproduced: `rot_x` and `rot_y` are seeded to 0 and never
 * assigned, so the cone never rotates and the two normalise-to-[-pi, pi] loops
 * around them cannot fire.  Lines 1982..1987 hold no code -- the six-line gap
 * where the angles were presumably computed compiles to nothing.  Kept as
 * found; deleting the loops would lose the fact that they are there. */
int GetCornHitCheck2(float *bpos, float power, float *rrate, float *lrate)
{                                                               /* 1974 */
    float wlm1[4][4];
    float wlm2[4][4];
    float wpos[4];
    float rot_x;
    float rot_y;
    float lc;
    float work[4] = { 0.0f, 0.0f, 0.0f, 1.0f };                 /* 1979 */

    rot_x = 0.0f;                                               /* 1981 */
    rot_y = 0.0f;

    while (rot_y < -3.1415925f) { rot_y += 6.28318501f; }       /* 1988 */
    while (rot_y >= 3.1415925f) { rot_y -= 6.28318501f; }       /* 1989 */

    sceVu0UnitMatrix(wlm1);                                     /* 1992 */
    sceVu0RotMatrixX(wlm1, wlm1, rot_x);                        /* 1993 */
    sceVu0RotMatrixY(wlm1, wlm1, rot_y);                        /* 1994 */
    sceVu0TransMatrix(wlm1, wlm1, work);                        /* 1996 */

    sceVu0InversMatrix(wlm2, wlm1);                             /* 1999 */

    sceVu0ApplyMatrix(wpos, wlm2, bpos);                        /* 2002 */

    lc = wpos[2] * tanf(0.698131621f);                          /* 2004 */

    *rrate = g3dxVu0Sqrt2(wpos[0], wpos[1]) / lc;               /* 2007 */
    *lrate = wpos[2] / power;                                   /* 2008 */

    return ((wpos[2] > 0.0f) && (wpos[2] < power)
            && (g3dxVu0Sqrt2(wpos[0], wpos[1]) < lc)) ? 1 : 0;  /* 2010 */
}

/* 2022 -- the lens flare itself.  2496 bytes, source 2022..2179.
 *
 * The strength is the product of three things: how nearly the camera is looking
 * along the light's own facing in yaw (`rx`) and in pitch (`ry`), each a linear
 * ramp that reaches zero at 60 degrees off-axis, and a per-frame random
 * flicker (`f1bk`, 0.25 .. 0.26).  If any of the three is zero nothing is
 * drawn at all.
 *
 * The ghosts are then laid along the screen-space line from the light to the
 * centre, and the fourth -- the one whose lscl is 0.0, i.e. the light itself --
 * additionally gets a three-layer corona and, if the effect asked for them,
 * `ec->dat.uc8[2]` star rays turned by `-ang`.
 *
 * Two things worth knowing before touching it.  The whole `rx`/`ry` ramp is
 * computed in software DOUBLE precision -- the 60.0 literals are doubles and
 * every step is a libgcc call -- and the absolute value is spelled out twice
 * per axis rather than held in a temporary, which is what makes it four calls
 * instead of two.  And lines 2115..2151 hold no code: a 37-line gap between the
 * last matrix statement and the flicker.
 *
 * `f1`/`f2` are reused three ways -- Vector2Rot's two out-params, then
 * GetCamI2DPos's, then f1 alone as the final brightness. */
void SetRenzFlare(EFFECT_CONT *ec)
{                                                               /* 2022 */
    static float f1bk;                                          /* sdata 3efdf0 */

    float        wlm[4][4];
    float        slm[4][4];
    float        tpos[4];
    float        pos1[4];
    float        vpos[4];
    float        trot[4];
    float        wppos[4] = { 0.0f, 0.0f, 0.0f, 1.0f };         /* 2028 */
    float        t1rot_x, t2rot_x;
    float        t1rot_y, t2rot_y;
    float        f1, f2;
    float        rx, ry;
    float        mx, my;
    float        ang;
    int          i;
    GRA3DCAMERA *pCam;

    /* The nine ghosts.  `lscl` is the position along the line from the light to
     * the centre of the screen -- negative is past the light, positive is past
     * the centre, and the one at 0.0 sits exactly on the light itself, which is
     * why the corona and the star rays hang off i == 4.  `tscl` is the size in
     * tens of pixels and rgba[3] the alpha the brightness scales.
     *
     * A LOCAL aggregate initialiser -- GCC copies it out of .rodata 3a6708 into
     * the frame, which is why globals.txt does not list it.  See
     * [[rodata-blob-into-stack-is-a-local-initialiser]]. */
    EFRENZ efrenz[9] =                                          /* 2043 */
    {
        { 2, { 160, 200, 255,  8 }, -1.19999993f,  4.5f },
        { 4, { 160, 255, 160, 10 }, -0.7f,         1.5f },
        { 4, { 200, 160, 255, 14 }, -0.5f,         2.5f },
        { 2, { 255, 160, 160, 20 },  0.0f,         3.5f },
        { 4, { 160, 160, 255, 10 },  0.199999988f, 2.0f },
        { 4, { 160, 160, 255, 14 },  0.299999982f, 1.5f },
        { 4, { 224, 160, 255, 14 },  0.599999964f, 2.5f },
        { 2, { 160, 255, 160, 10 },  0.9f,         5.0f },
        { 2, { 160, 200, 255,  8 },  1.19999993f,  8.0f }
    };

    float (&cam_dir)[4] = gra3dcamGetDirection();               /* 2033 */
    pCam = gra3dGetCamera();                                    /* 2034 */

    t2rot_x = 0.0f; t2rot_y = 0.0f;                             /* 2056 */

    g3dxVu0CopyVector(tpos, (const float *)ec->pnt[0]);
    g3dxVu0CopyVector(trot, (const float *)ec->pnt[1]);

    Vector2Rot(cam_dir, &f1, &f2);                              /* 2061 */

    t1rot_x = trot[0] * 180.0f / 3.1415925f;                    /* 2062 */
    t1rot_y = trot[1] * 180.0f / 3.1415925f;                    /* 2063 */

    /* +/-31.4159241 is 10*pi, i.e. five full turns either way -- a sanity
     * bound on Vector2Rot's answer rather than a real range. */
    if ((f1 < 31.4159241f) && (f1 > -31.4159241f))              /* 2067 */
    {
        t2rot_x = f1 * 180.0f / 3.1415925f - 180.0f;            /* 2068 */
    }
    else
    {
        printf("Illegal Camera Position!?\n");                  /* 2070 */
    }

    if ((f2 < 31.4159241f) && (f2 > -31.4159241f))              /* 2072 */
    {
        t2rot_y = f2 * 180.0f / 3.1415925f - 180.0f;            /* 2073 */
    }
    else
    {
        printf("Illegal Camera Position!!?\n");                 /* 2075 */
    }

    /* Bring both angles positive, then within 180 degrees of each other, so the
     * difference below is the real one and not a wrap. */
    while (t1rot_x < 0.0f)             { t1rot_x += 360.0f; }    /* 2077 */
    while (t2rot_x < 0.0f)             { t2rot_x += 360.0f; }    /* 2078 */
    while (t1rot_x + 180.0f < t2rot_x) { t1rot_x += 360.0f; }    /* 2079 */
    while (t2rot_x + 180.0f < t1rot_x) { t2rot_x += 360.0f; }    /* 2080 */

    while (t1rot_y < 0.0f)             { t1rot_y += 360.0f; }    /* 2081 */
    while (t2rot_y < 0.0f)             { t2rot_y += 360.0f; }    /* 2082 */
    while (t1rot_y + 180.0f < t2rot_y) { t1rot_y += 360.0f; }    /* 2083 */
    while (t2rot_y + 180.0f < t1rot_y) { t2rot_y += 360.0f; }    /* 2084 */

    if (fabs(t1rot_x - t2rot_x) < 60.0)                         /* 2086 */
    {
        rx = (float)(60.0 - fabs(t1rot_x - t2rot_x)) / 60.0f;   /* 2087 */
    }
    else
    {
        rx = 0.0f;                                              /* 2089 */
    }

    if (fabs(t1rot_y - t2rot_y) < 60.0)                         /* 2091 */
    {
        ry = (float)(60.0 - fabs(t1rot_y - t2rot_y)) / 60.0f;   /* 2092 */
    }
    else
    {
        ry = 0.0f;                                              /* 2094 */
    }

    GetCamI2DPos(tpos, &f1, &f2);                               /* 2098 */

    mx = f1 - 320.0f;                                           /* 2099 */
    my = f2 - 224.0f;                                           /* 2100 */

    ang = atan2f(mx, my);                                       /* 2104 */
    ang = ang * 180.0f / 3.1415925f + 45.0f;                    /* 2105 */

    sceVu0UnitMatrix(wlm);                                      /* 2107 */
    wlm[0][0] = 25.0f; wlm[1][1] = 25.0f; wlm[2][2] = 25.0f;    /* 2108 */
    sceVu0TransMatrix(wlm, wlm, tpos);                          /* 2109 */
    sceVu0MulMatrix(slm, pCam->matWorldScreen, wlm);            /* 2110 */

    sceVu0ApplyMatrix(pos1, slm, wppos);                        /* 2112 */
    sceVu0ScaleVector(pos1, pos1, 1.0f / pos1[3]);              /* 2113 */

    if (EffWrkStopFlgGet() == 0)                                /* 2152 */
    {
        f1bk = EffectGetRandom(0.0f, 1.0f) / 100.0f + 0.25f;    /* 2154 */
    }

    f1 = f1bk;                                                  /* 2156 */
    f1 = rx * ry * f1;                                          /* 2158 */

    if (f1 != 0.0f)                                             /* 2159 */
    {
        for (i = 1; i < 10; i++)                                /* 2160 */
        {
            vpos[0] = pos1[0] + mx * efrenz[i - 1].lscl;        /* 2161 */
            vpos[1] = pos1[1] + my * efrenz[i - 1].lscl;        /* 2162 */
            vpos[2] = pos1[2];                                  /* 2163 */
            vpos[3] = 1.0f;                                     /* 2164 */

            if (i == 4)                                         /* 2166 */
            {
                SetEffSQTex(0xc, vpos, 0, 80.0f, 80.0f,
                            0xff, 0x80, 0x80, (u_char)(f1 * 16.0f)); /* 2167 */

                if (ec->dat.uc8[2] != 0)                        /* 2168 */
                {
                    SetStarRay(vpos, 0, f1, ec->dat.uc8[2], -ang); /* 2169 */
                }

                SetEffSQTex(0xc, vpos, 0, 24.0f, 24.0f,
                            0xff, 0xc0, 0xd0, (u_char)(f1 * 32.0f)); /* 2171 */
                SetEffSQTex(0xc, vpos, 0, 12.0f, 12.0f,
                            0xff, 0xc0, 0xd0, (u_char)(f1 * 48.0f)); /* 2172 */
            }

            SetEffSQTex(efrenz[i - 1].type, vpos, 0,
                        efrenz[i - 1].tscl * 10.0f, efrenz[i - 1].tscl * 10.0f,
                        efrenz[i - 1].rgba[0], efrenz[i - 1].rgba[1],
                        efrenz[i - 1].rgba[2],
                        (u_char)(efrenz[i - 1].rgba[3] * f1)); /* 2174 */
        }                                                       /* 2175 */
    }

    if ((ec->dat.uc8[1] & 1) != 0)                              /* 2178 */
    {
        ResetEffects(ec);                                       /* 2179 */
    }
}

/* 2187 -- the star burst at the middle of the flare.  1196 bytes, source
 * 2187..2278.  `num` spikes, evenly spaced round the circle and all turned
 * together by `aang`, drawn straight into a DIRECT packet as untextured
 * Gouraud triangle strips.
 *
 * The spike shape is four points: a 8-pixel-tall base at the origin, the
 * bright centre, and a tip 100 pixels out along -x.  `pos[i][2]` is not a
 * z coordinate -- it is the per-vertex ALPHA (0, 28, 0, 0), scaled by `sc`, so
 * a spike is a thin wedge that is opaque only where it meets the flare.  The
 * first two vertices carry ADC = 0x8000 and so only prime the strip; the last
 * two kick one triangle each.
 *
 * Y is divided by `div` (2.0 interlaced, 1.0 progressive) because the packet's
 * coordinates are field lines, not frame lines.
 *
 * The GS packet is inert in this port, so each spike is queued as two host
 * triangles inside the clip test below. */
static void SetStarRay(float *bpos, int tp, float sc, int num, float aang)
{                                                               /* 2187 */
    int                   i;
    fixed_array<int, 4>   x;
    fixed_array<int, 4>   y;
    u_int                 z;
    float                 div;
    float                 f;
    float                 ss;
    float                 cc;
    float                 ang;
    float                 pos2[4][2];
    u_char                rr, gg, bb;
    float                 bx, by;
    Q_WORDDATA           *pbuf;
    int                   ClipFlg;

    float pos[4][3] =                                           /* 2195 */
    {
        {    0.0f,  4.0f,  0.0f },
        {    0.0f,  0.0f, 28.0f },
        { -100.0f,  0.0f,  0.0f },
        {    0.0f, -4.0f,  0.0f }
    };

    if (EffWrkMonochroModeGet() != 0)                           /* 2208 */
    {
        rr = 0xda; gg = 0xda; bb = 0xda;                        /* 2209 */
    }
    else
    {
        rr = 0xff;                                              /* 2211 */
        gg = 0xc0;                                              /* 2212 */
        bb = 0xd0;                                              /* 2213 */
    }

    bx = bpos[0];                                               /* 2215 */
    by = bpos[1];

    z = (u_int)(bpos[2] * 16.0f);                               /* 2217 */

    div = (g_bInterlace != 0) ? 2.0f : 1.0f;                    /* 2219 */
    ang = 360.0f / (float)num;                                  /* 2220 */

    /* One source line -- a local aggregate initialiser, so all three stores
     * carry 2225.  The stabs do not name it, which is why functions.txt's
     * local list has nothing at 0x70(sp). */
    DRAW_ENV_NOTEX denv = { 0x44, 0x5000d,
                            ((u_long)tp << 32) | 0x0a000118ULL }; /* 2225 */

    SetDrawEnvNoTex(0, &denv);                                  /* 2230 */

    for (f = 0.0f; f < 360.0f; f += ang)                        /* 2233 */
    {
        ClipFlg = 0;                                            /* 2234 */

        ss = sinf((f + aang) * 0.0174532905f);                  /* 2236 */
        cc = cosf((f + aang) * 0.0174532905f);                  /* 2237 */

        for (i = 0; i < 4; i++)                                 /* 2239 */
        {
            pos2[i][0] = pos[i][0] * ss - pos[i][1] * cc;       /* 2241 */
            pos2[i][1] = pos[i][0] * cc + pos[i][1] * ss;       /* 2242 */

            x[i] = (int)((pos2[i][0] + bx) * 16.0f);
            y[i] = (int)((pos2[i][1] / div + by) * 16.0f);
        }                                                       /* 2247 */

        for (i = 0; i < 4; i++)                                 /* 2250 */
        {
            if ((x[i] < 0x4000) || (x[i] > 0xc000)) { ClipFlg = 1; } /* 2251 */
            if ((y[i] < 0x4000) || (y[i] > 0xc000)) { ClipFlg = 1; } /* 2252 */
        }                                                       /* 2253 */

        if ((u_int)(z - 0xff) > 0x0fffff00) { ClipFlg = 1; }     /* 2254 */

        if (ClipFlg == 0)                                       /* 2255 */
        {
            /* PORT: the DIRECT packet below is inert -- dmaVif1 discards it --
               so the same spike goes out as two host triangles.  The strip is
               v0,v1,v2,v3 with ADC on the first two, so it kicks (0,1,2) and
               (1,2,3), and the ROM's clip is per spike rather than per
               triangle -- this whole block is already inside it.

               pos[i][2] is the per-vertex ALPHA, not a z: (0, 28, 0, 0) scaled
               by sc, which is what makes a spike a wedge that is opaque only
               where it meets the flare.  Colour is flat across the four.

               x/y are GS window coordinates in 12.4 fixed, so 2048 comes off
               and the half-screen bias goes on -- the same conversion
               effect_oth.c's diamond particles use into this bridge.

               No depth, deliberately: the ROM depth-tests this (TEST 0x5000d,
               ZTST GEQUAL) but the flare layers either side of it are
               SetEffSQTex() quads that carry no host depth either, so the
               flare stays coherent with itself instead of half-occluded. */
            {
                static const int strip[6] = { 0, 1, 2, 1, 2, 3 };
                float  sxy[6 * 2];
                u_char srgba[6 * 4];
                int    v;

                for (v = 0; v < 6; v++)
                {
                    int s = strip[v];

                    sxy[v * 2 + 0] = (float)x[s] / 16.0f - 2048.0f + 320.0f;
                    sxy[v * 2 + 1] = (float)y[s] / 16.0f - 2048.0f + 224.0f;

                    srgba[v * 4 + 0] = rr;
                    srgba[v * 4 + 1] = gg;
                    srgba[v * 4 + 2] = bb;
                    srgba[v * 4 + 3] = (u_char)(int)(pos[s][2] * sc);
                }

                MioPan_RendererDrawSolidTriangles2D(sxy, srgba, 6);
            }

            pbuf = StartDmaDirectTrans();                       /* 2259 */
            Reserve2DPacket(0x10);                              /* 2260 */

            /* PRIM: triangle strip, Gouraud, alpha blend, no texture. */
            pbuf[0].ul64[0] = 0x2026400000008004ULL;            /* 2263 */
            pbuf[0].ul64[1] = 0x41;          /* REGS: RGBAQ, XYZ2 */ /* 2264 */

            for (i = 0; i < 4; i++)                             /* 2266 */
            {
                pbuf[i * 2 + 1].ui32[0] = (u_int)rr;            /* 2268 */
                pbuf[i * 2 + 1].ui32[1] = (u_int)gg;            /* 2269 */
                pbuf[i * 2 + 1].ui32[2] = (u_int)bb;            /* 2270 */
                pbuf[i * 2 + 1].ui32[3] = (u_int)(int)(pos[i][2] * sc); /* 2271 */

                pbuf[i * 2 + 2].ui32[0] = (u_int)x[i];
                pbuf[i * 2 + 2].ui32[1] = (u_int)y[i];
                pbuf[i * 2 + 2].ui32[2] = z;                    /* 2274 */
                /* ADC: the first two vertices only prime the strip. */
                pbuf[i * 2 + 2].ui32[3] = (i < 2) ? 0x8000 : 0; /* 2275 */
            }                                                   /* 2276 */

            EndDmaDirectTrans(pbuf + 9);                        /* 2277 */
        }
    }                                                           /* 2278 */
}

/* ==========================================================================
 *  Light shafts
 *
 *  A registered model plus a scroll rate: the shaft's own texture creeps and
 *  its alpha follows either the camera's angle to the beam or its distance
 *  from it.  The vertex colours are baked in once, at registration.
 * ======================================================================== */

/* 2296 */
static void EffectLightComeInInit(void)
{
    SingleLinkListInit(&LightComeInCtrl.LightList,
                       sizeof(LIGHT_COME_IN_DATA));             /* 2297 */
}

/* 2304 -- register a shaft.  AlphaMax is resolved once, here, rather than per
 * frame, and the vertex colours are baked into the model immediately -- which
 * is why a shaft's tint never changes after registration. */
void EffectLightComeInRegist(void *pSgdTop, int MapBuffId, int Type)
{
    LIGHT_COME_IN_DATA LightData;

    if (pSgdTop != nullptr)                                     /* 2306 */
    {
        LightData.pSgdTop   = pSgdTop;                          /* 2308 */
        LightData.TotalS    = 0.0f;                             /* 2309 */
        LightData.TotalT    = 0.0f;                             /* 2310 */
        LightData.MapBuffId = MapBuffId;                        /* 2311 */
        LightData.AlphaMax  = EffectLightComeInGetAlphaMax(Type); /* 2312 */
        LightData.Type      = Type;                             /* 2313 */

        SingleLinkListAddEnd(&LightComeInCtrl.LightList, &LightData); /* 2318 */

        EffectLightComeInSetVertexColor(pSgdTop, Type);         /* 2320 */
    }
}                                                               /* 2321 */

/* 2354 -- wrap an accumulated texture scroll.
 *
 * Note the asymmetry, which is the ROM's: the positive test is on
 * `AddST + TotalST` but the negative one is on `TotalST` alone.  So the
 * upward wrap happens one step early and the downward one one step late.
 * Reproduced as found. */
static float EffectLightComeInAddSTScrollCtrl(float AddST, float TotalST)
{
    if (AddST + TotalST > 1.0f)                                 /* 2356 */
    {
        AddST -= 1.0f;                                          /* 2358 */
    }
    else if (TotalST < -1.0f)                                   /* 2360 */
    {
        AddST += 1.0f;                                          /* 2362 */
    }

    return AddST;                                               /* 2365 */
}                                                               /* 2367 */

/* 2503 -- find a shaft by its model pointer. */
LIGHT_COME_IN_DATA *EffectLightComeInGetDataPtr(void *pSgdTop)
{
    SLL_CELL *pCell;
    LIGHT_COME_IN_DATA *pData;

    for (pCell = SingleLinkListBeginCell(&LightComeInCtrl.LightList); /* 2505 */
         pCell != (SLL_CELL *)nullptr;
         pCell = SingleLinkListNextCell(pCell))
    {
        pData = (LIGHT_COME_IN_DATA *)SingleLinkListCellBodyPtr(pCell);

        if (pData->pSgdTop == pSgdTop)                          /* 2509 */
        {
            return pData;                                       /* 2511 */
        }
    }

    return (LIGHT_COME_IN_DATA *)nullptr;                       /* 2517 */
}                                                               /* 2519 */

/* 2795 / 2823 -- the per-type scroll rates.
 *
 * Both are biased by 10000, the same convention effect_oth's HAZE_PARAMETER
 * offsets use, so 10009 is +9 and 9992 is -8.  The default (type NORMAL)
 * scrolls u fastest; F609 is the only one that scrolls v backwards.
 *
 * The ROM tests for 1 and then 2 with the default last, so an out-of-range
 * type gets the NORMAL rate rather than asserting. */
static int EffectLightComeInGetScrollU(int Type)
{
    if (Type == EFF_LIGHT_COMEIN_TYPE_F607)                     /* 2797 */
    {
        return 9996;                                            /* 2799 */
    }

    if (Type == EFF_LIGHT_COMEIN_TYPE_F609)                     /* 2805 */
    {
        return 9995;                                            /* 2807 */
    }

    return 10009;                                               /* 2814 */
}                                                               /* 2816 */

static int EffectLightComeInGetScrollV(int Type)
{
    if (Type == EFF_LIGHT_COMEIN_TYPE_F607)                     /* 2825 */
    {
        return 9997;                                            /* 2827 */
    }

    if (Type == EFF_LIGHT_COMEIN_TYPE_F609)                     /* 2833 */
    {
        return 9992;                                            /* 2835 */
    }

    return 9997;                                                /* 2842 */
}                                                               /* 2844 */

/* 2851 -- and the alpha ceiling: the plain shaft is opaque, F607 is dimmed to
 * 160 and F609 to 100. */
static int EffectLightComeInGetAlphaMax(int Type)
{
    if (Type == EFF_LIGHT_COMEIN_TYPE_F607)                     /* 2853 */
    {
        return 160;                                             /* 2855 */
    }

    if (Type == EFF_LIGHT_COMEIN_TYPE_F609)                     /* 2861 */
    {
        return 100;                                             /* 2863 */
    }

    return 255;                                                 /* 2870 */
}                                                               /* 2872 */


/* 2374 -- the shaft's facing, as an X/Y rotation pair.
 *
 * Takes the model's local-world matrix, zeroes its translation row so only the
 * rotation survives, pushes +Z through it and converts the result to angles.
 * TmpMat[3][3] is set to 1.0 as well, so the copy is a proper affine matrix
 * rather than a rotation with a stale w. */
static void EffectLightComeInMatrix2Rot(float (*LWMatrix)[4], float *pRotX,
                                        float *pRotY)
{
    sceVu0FMATRIX TmpMat;
    sceVu0FVECTOR BaseVec;
    sceVu0FVECTOR DirVec;

    BaseVec[0] = 0.0f;                                          /* 2376 */
    BaseVec[1] = 0.0f;
    BaseVec[2] = 1.0f;
    BaseVec[3] = 1.0f;

    sceVu0CopyMatrix(TmpMat, LWMatrix);                         /* 2377 */

    TmpMat[3][0] = 0.0f;                                        /* 2378 */
    TmpMat[3][1] = 0.0f;
    TmpMat[3][2] = 0.0f;
    TmpMat[3][3] = 1.0f;

    sceVu0ApplyMatrix(DirVec, TmpMat, BaseVec);                 /* 2380 */
    Vector2Rot(DirVec, pRotX, pRotY);                           /* 2381 */
}                                                               /* 2382 */

/* 2421 -- the distance half of the alpha rate.
 *
 * A shaft fades *in* with distance, not out: nothing below 100 units, a linear
 * ramp to 500, full beyond.  That is what stops a shaft washing out the view
 * when the player walks into it.  The model's world position is read out of
 * the SGD header at +0x70. */
static float EffectLightComeInCalcAlphaRateCamDist(void *pSgdTop)
{
    float Distance;

    float (&rCamPos)[4] = gra3dcamGetPosition();                /* 2423 */

    if (pSgdTop == nullptr)                                     /* 2425 */
    {
        return 0.0f;
    }

    Distance = GetDistV(rCamPos,(((SGDFILEHEADER *)pSgdTop)->pCoord->matLocalWorld[3])); /* 2429 */

    if (Distance < 100.0f)                                      /* 2431 */
    {
        return 0.0f;
    }

    if (Distance < 500.0f)                                      /* 2437 */
    {
        return (Distance - 100.0f) / 400.0f;                    /* 2439 */
    }

    return 1.0f;                                                /* 2444 */
}                                                               /* 2447 */

/* 2453 -- step one shaft: scroll its texture, then set its alpha.
 *
 * The scroll rates come back biased by 10000 and are divided by 10000 here, so
 * the stored 10009 means +0.0009 of a texture per frame.
 *
 * The two alpha modes are not alternatives: DIRECTION is always computed, and
 * DISTANCE additionally takes the *smaller* of the two.  So a shaft in
 * distance mode is never brighter than it would be in direction mode. */
void EffectLightComeInExecOne(LIGHT_COME_IN_DATA *pData, int AlphaCalcMode)
{
    float AddS;
    float AddT;
    float AlphaRate;
    float DistRate;

    if (pData != nullptr)                 /* 2455 */
    {
        AddS = (float)(EffectLightComeInGetScrollU(pData->Type) - 10000)
                   / 10000.0f;                                  /* 2457 */
        AddT = (float)(EffectLightComeInGetScrollV(pData->Type) - 10000)
                   / 10000.0f;                                  /* 2458 */

        AddS = EffectLightComeInAddSTScrollCtrl(AddS, pData->TotalS); /* 2460 */
        AddT = EffectLightComeInAddSTScrollCtrl(AddT, pData->TotalT); /* 2461 */

        pData->TotalS += AddS;                                  /* 2463 */
        pData->TotalT += AddT;                                  /* 2464 */

        gra3dChangeST((SGDFILEHEADER*)pData->pSgdTop, AddS, AddT);              /* 2466 */

        AlphaRate = EffectLightComeInCalcAlphaRateCamDir(pData->pSgdTop); /* 2470 */

        if (AlphaCalcMode == EFF_LIGHT_COMEIN_ALPHA_CALC_DISTANCE) /* 2472 */
        {
            DistRate = EffectLightComeInCalcAlphaRateCamDist(pData->pSgdTop);

            if (DistRate < AlphaRate)                           /* 2476 */
            {
                AlphaRate = DistRate;
            }
        }

        ManmdlSetAlpha(pData->pSgdTop,
                       (u_char)(int)((float)pData->AlphaMax * AlphaRate)); /* 2490 */

        EffectLightComeInSetVertexColor(pData->pSgdTop, pData->Type); /* 2495 */
    }
}                                                               /* 2497 */

/* 2526 -- dispatch to the three authored vertex-colour variants.  Same shape
 * as the per-type accessors: 1 and 2 named, everything else NORMAL. */
static void EffectLightComeInSetVertexColor(void *pSgdTop, int Type)
{
    if (Type == EFF_LIGHT_COMEIN_TYPE_F607)                     /* 2528 */
    {
        EffectLightComeInSetVertexColorF607(pSgdTop);           /* 2530 */
        return;
    }

    if (Type == EFF_LIGHT_COMEIN_TYPE_F609)                     /* 2531 */
    {
        EffectLightComeInSetVertexColorF609(pSgdTop);           /* 2533 */
        return;
    }

    EffectLightComeInSetVertexColorNormal(pSgdTop);             /* 2534 */
}                                                               /* 2535 */

/* ==========================================================================
 *  Water flow
 *
 *  A registered surface plus a move type: the S and T scroll rates come from a
 *  two-level lookup and are wrapped by the light shaft's own helper.
 * ======================================================================== */

/* 2885 */
static void EffectWaterFlowInit(void)
{
    SingleLinkListInit(&WaterFlowList, sizeof(WATER_FLOW_DATA)); /* 2886 */
}


/* 2893 -- the surface's base alpha, one per move type.
 *
 * A plain switch with a jump table (.rodata 0x3a6830), so the eight arms are
 * in index order and an out-of-range type gets 0x80.  The lake surfaces are
 * the opaque ones (0xff, 0xb4); the rivers are much thinner. */
static int EffectWaterFlowGetAlpha(int MoveType)
{
    switch (MoveType)                                           /* 2931 */
    {
    case EFF_WATER_MOVE_RIVER0:          return 0x50;           /* 2933 */
    case EFF_WATER_MOVE_RIVER1:          return 0x91;           /* 2936 */
    case EFF_WATER_MOVE_LAKE0:           return 0x6c;           /* 2939 */
    case EFF_WATER_MOVE_LAKE1:           return 0xff;           /* 2942 */
    case EFF_WATER_MOVE_RIVER_MINAKAMI0: return 0x51;           /* 2945 */
    case EFF_WATER_MOVE_RIVER_MINAKAMI1: return 0xb4;           /* 2948 */
    case EFF_WATER_MOVE_LAKE_FUKAMICHI0: return 0x54;           /* 2951 */
    case EFF_WATER_MOVE_LAKE_FUKAMICHI1: return 0x6f;           /* 2954 */
    }

    return 0x80;                                                /* 2957 */
}                                                               /* 2962 */

/* 3341 -- how long the current A/B/C leg lasts, in frames.
 *
 * The ROM splits on move type first -- the two lakes (2, 3) take one table and
 * everything else another -- but **both tables hold the same six values**
 * (.rodata 0x3a6890 and 0x3a68b0, [363, 60, 90, 60, 279, 60]).  So the split
 * makes no difference to the answer; it is kept because it is what the ROM
 * does, and the pair is a third instance of the same-body-twice pattern
 * EffectHazeGetParameterPtr/Org and EneDmgLargeHitCtrlInit/AllOff show.
 *
 * The shape of the cycle: the three hold legs are long and uneven (363, 90,
 * 279 frames) and all three transitions are exactly 60, which is what makes a
 * lake surface drift for a while, slide over a second, and drift again.
 *
 * An out-of-range MoveDataNo returns 0, which EffectWaterFlowChangeCtrl()
 * reads as "advance immediately". */
static int EffectWaterFlowGetChangeFrame(int MoveType, int MoveDataNo)
{
    int Frame;

    Frame = 0;                                                  /* 3394 */

    if ((u_int)(MoveType - 2) < 2)
    {
        switch (MoveDataNo)                                     /* 3397 */
        {
        case EFF_WATER_MOVE_TYPE_A:      Frame = 363; break;    /* 3400 */
        case EFF_WATER_MOVE_TYPE_A_TO_B: Frame =  60; break;
        case EFF_WATER_MOVE_TYPE_B:      Frame =  90; break;
        case EFF_WATER_MOVE_TYPE_B_TO_C: Frame =  60; break;
        case EFF_WATER_MOVE_TYPE_C:      Frame = 279; break;
        case EFF_WATER_MOVE_TYPE_C_TO_A: Frame =  60; break;
        }
    }
    else
    {
        switch (MoveDataNo)                                     /* 3419 */
        {
        case EFF_WATER_MOVE_TYPE_A:      Frame = 363; break;    /* 3422 */
        case EFF_WATER_MOVE_TYPE_A_TO_B: Frame =  60; break;    /* 3425 */
        case EFF_WATER_MOVE_TYPE_B:      Frame =  90; break;    /* 3428 */
        case EFF_WATER_MOVE_TYPE_B_TO_C: Frame =  60; break;    /* 3431 */
        case EFF_WATER_MOVE_TYPE_C:      Frame = 279; break;    /* 3434 */
        case EFF_WATER_MOVE_TYPE_C_TO_A: Frame =  60; break;    /* 3436 */
        }
    }

    return Frame;                                               /* 3442 */
}

/* 3248 -- register a flowing surface.
 *
 * Three of the eight move types carry a looping water cue, each with its own
 * fixed emitter position: the river (0), the Minakami river (4) and the
 * Fukamichi lake (6).  The other five are silent.  SePosition is the ROM's
 * own function-local static, not a file-scope table. */
void EffectWaterFlowRegist(void *pSgdTop, int Id, int MoveType)
{
    static float SePosition[3][4] =                             /* data 2fbba0 */
    {
        {  1150.0f,  3275.0f, 30275.0f, 1.0f },
        { -7350.0f,  2475.0f, 16450.0f, 1.0f },
        { -6425.0f, -5500.0f, 36207.0f, 1.0f }
    };

    WATER_FLOW_DATA WaterFlowData;
    int Alpha;

    if (pSgdTop != nullptr)                                     /* 3250 */
    {
        Alpha = EffectWaterFlowGetAlpha(MoveType);              /* 3252 */

        WaterFlowData.pSgdTop    = pSgdTop;                     /* 3254 */
        WaterFlowData.TotalS     = 0.0f;                        /* 3255 */
        WaterFlowData.TotalT     = 0.0f;                        /* 3256 */
        WaterFlowData.Id         = Id;                          /* 3257 */
        WaterFlowData.MoveType   = MoveType;                    /* 3258 */
        WaterFlowData.Count      = 0;                           /* 3259 */
        WaterFlowData.MoveDataNo = 0;                           /* 3260 */

        SingleLinkListAddEnd(&WaterFlowList, &WaterFlowData);   /* 3266 */

        ManmdlSetAlpha(pSgdTop, (u_char)Alpha);                 /* 3268 */

        if (MoveType == EFF_WATER_MOVE_RIVER0)                  /* 3270 */
        {
            EffectSndPlay(0xcc3, 0, 1, 0, (float (*)[3])SePosition[0]);
        }
        else if (MoveType == EFF_WATER_MOVE_RIVER_MINAKAMI0)    /* 3275 */
        {
            EffectSndPlay(0xd11, 0, 1, 0, (float (*)[3])SePosition[1]);
        }
        else if (MoveType == EFF_WATER_MOVE_LAKE_FUKAMICHI0)    /* 3280 */
        {
            EffectSndPlay(0xd1b, 0, 1, 0, (float (*)[3])SePosition[2]);
        }
    }
}                                                               /* 3284 */

/* 3290 -- drop every surface with this label, stopping and releasing the cue
 * first for the three types that have one. */
void EffectWaterFlowDelete(int Id)
{
    SLL_CELL *pCell;
    SLL_CELL *pNext;
    WATER_FLOW_DATA *pData;

    for (pCell = SingleLinkListBeginCell(&WaterFlowList);       /* 3292 */
         pCell != (SLL_CELL *)nullptr;
         pCell = pNext)
    {
        pData = (WATER_FLOW_DATA *)SingleLinkListCellBodyPtr(pCell);
        pNext = SingleLinkListNextCell(pCell);

        if (pData->Id == Id)                                    /* 3298 */
        {
            if (pData->MoveType == EFF_WATER_MOVE_RIVER0)       /* 3300 */
            {
                EffectSndStop(0xcc3, 0, 1);
                EffectSndFileRelease(0xcc3);
            }
            else if (pData->MoveType == EFF_WATER_MOVE_RIVER_MINAKAMI0) /* 3305 */
            {
                EffectSndStop(0xd11, 0, 1);
                EffectSndFileRelease(0xd11);
            }
            else if (pData->MoveType == EFF_WATER_MOVE_LAKE_FUKAMICHI0) /* 3310 */
            {
                EffectSndStop(0xd1b, 0, 1);
                EffectSndFileRelease(0xd1b);
            }

            SingleLinkListRemove(&WaterFlowList, pCell);        /* 3314 */
        }
    }
}                                                               /* 3316 */

/* 3324 */
void EffectWaterFlowExec(void)
{
    SLL_CELL *pCell;

    for (pCell = SingleLinkListBeginCell(&WaterFlowList);       /* 3326 */
         pCell != (SLL_CELL *)nullptr;
         pCell = SingleLinkListNextCell(pCell))
    {
        EffectWaterFlowExecOne(
            (WATER_FLOW_DATA *)SingleLinkListCellBodyPtr(pCell)); /* 3328 */
    }
}                                                               /* 3331 */

/* 3449 -- advance the A -> A_TO_B -> B -> ... cycle.
 *
 * Only four of the eight move types cycle: the two lakes (2, 3) and the two
 * Fukamichi ones (6, 7).  The rivers scroll at a constant rate and never
 * change leg, which is what makes a river read as flowing and a lake as
 * breathing.  MoveDataNo wraps at 6, one per EFF_WATER_FLOW_MOVE_STATUS. */
static void EffectWaterFlowChangeCtrl(WATER_FLOW_DATA *pData)
{
    if (((u_int)(pData->MoveType - 2) < 2)
        || (pData->MoveType == EFF_WATER_MOVE_LAKE_FUKAMICHI0)
        || (pData->MoveType == EFF_WATER_MOVE_LAKE_FUKAMICHI1)) /* 3451 */
    {
        if (pData->Count
                < EffectWaterFlowGetChangeFrame(pData->MoveType,
                                                pData->MoveDataNo))  /* 3453 */
        {
            pData->Count++;                                     /* 3455 */
        }
        else
        {
            pData->Count = 0;                                   /* 3458 */
            pData->MoveDataNo++;                                /* 3459 */

            if (pData->MoveDataNo > 5)                          /* 3460 */
            {
                pData->MoveDataNo = 0;                          /* 3462 */
            }
        }
    }
}                                                               /* 3465 */

/* 3474 -- step one surface's texture scroll.
 *
 * Note it reuses the light shaft's wrap helper -- that is the ROM's own
 * cross-subsystem call, not a port convenience -- and that gra3dChangeST() is
 * handed the *wrapped deltas*, not the running totals.  The totals are only
 * kept so the next wrap decision has something to test. */
static void EffectWaterFlowExecOne(WATER_FLOW_DATA *pData)
{
    float AddS;
    float AddT;

    if (pData != (WATER_FLOW_DATA *)nullptr)                    /* 3476 */
    {
        AddS = EffectWaterFlowGetAddS(pData->MoveType, pData->MoveDataNo,
                                      pData->Count);            /* 3478 */
        AddT = EffectWaterFlowGetAddT(pData->MoveType, pData->MoveDataNo,
                                      pData->Count);            /* 3479 */

        AddS = EffectLightComeInAddSTScrollCtrl(AddS, pData->TotalS); /* 3481 */
        AddT = EffectLightComeInAddSTScrollCtrl(AddT, pData->TotalT); /* 3482 */

        pData->TotalS += AddS;                                  /* 3484 */
        pData->TotalT += AddT;                                  /* 3485 */

        gra3dChangeST((SGDFILEHEADER*)pData->pSgdTop, AddS, AddT);              /* 3487 */

        EffectWaterFlowChangeCtrl(pData);                       /* 3489 */
    }
}                                                               /* 3496 */

/* 2327 -- drop every shaft belonging to a room buffer, which is how a room
 * unload takes its light shafts with it. */
void EffectLightComeInDeleteMapBuffId(int MapBuffId)
{
    SLL_CELL *pCell;
    SLL_CELL *pNext;
    LIGHT_COME_IN_DATA *pData;

    for (pCell = SingleLinkListBeginCell(&LightComeInCtrl.LightList); /* 2329 */
         pCell != (SLL_CELL *)nullptr;
         pCell = pNext)
    {
        pData = (LIGHT_COME_IN_DATA *)SingleLinkListCellBodyPtr(pCell);
        pNext = SingleLinkListNextCell(pCell);

        if (pData->MapBuffId == MapBuffId)                      /* 2335 */
        {
            SingleLinkListRemove(&LightComeInCtrl.LightList, pCell); /* 2337 */
        }
    }
}                                                               /* 2339 */

/* ==========================================================================
 *  Camera flash
 *
 *  The burst from the player's own camera when a picture is taken.  Two halves
 *  drawn from the same request: a screen-space refracting corona out of the
 *  frame buffer (Draw / DrawSub) and a plain textured flare (DrawTex).  Both
 *  are hung off bone 6 of the player model, so the flash follows the camera
 *  rather than the player's root.
 * ======================================================================== */

/* 3505 -- arm one.  76 bytes.  ReqFlg 1 marks it the *first* of the pair;
 * SetCameraFlash() re-requests with 0 on its second frame, which is what makes
 * the second (unreflected) flash appear one frame behind the first without
 * recursing for ever. */
void *EffectCameraFlashReq(void)
{                                                               /* 3505 */
    float Position[4];
    float PositionTex[4];

    if (EffectCameraFlashGetPosition(Position, PositionTex) != 0) /* 3509 */
    {
        return SetEffects_CAMERA_FLASH(2, Position, PositionTex, 1); /* 3511 */
    }

    return nullptr;
}                                                               /* 3512 */

/* 3567 -- the per-frame job.  308 bytes.
 *
 * On frame 0 it snapshots the back buffer into EE memory and halves it, which
 * is the image DrawSub then refracts.  It runs both halves every frame and only
 * releases the effect once *both* report they are finished -- `ExecFlg` is the
 * OR of the two, so the longer of the corona (29 frames) and the flare
 * (9 frames) sets the lifetime.
 *
 * The `flow == 0 && cnt == 1` test is one 64-bit load of the adjacent flow and
 * cnt fields compared against 0x100000000; GCC merged the pair. */
void SetCameraFlash(EFFECT_CONT *ec)
{                                                               /* 3567 */
    float PositionTex[4];
    int   ExecFlg;

    if (ec->cnt == 0)                                           /* 3571 */
    {
        LocalCopyLtoB(0, 0, ((sys_wrk.count + 1) & 1) * 0x1180); /* 3572 */
        /* 0x1e79b00 is an EE memory-map constant inside the emulated window,
           so it has to be translated -- EffImageHalf32() writes 320x448 words
           through it.  effect_scr.c's SetOverRap() does the same. */
        EffImageHalf32((u_int *)MioPan_GetHostPointer(0x1e79b00),
                       0x280, 0x1c0);                           /* 3574 */
    }

    PositionTex[0] = ec->fw[0];
    PositionTex[1] = ec->fw[1];
    PositionTex[2] = ec->fw[2];
    PositionTex[3] = 1.0f;                                      /* 3577 */

    ExecFlg  = EffectCameraFlashDraw((float *)ec->pnt[0], ec->cnt);   /* 3579 */
    ExecFlg |= EffectCameraFlashDrawTex(PositionTex, ec->cnt);        /* 3580 */

    if ((ec->flow == 0) && (ec->cnt == 1))                      /* 3582 */
    {
        SetEffects_CAMERA_FLASH(2, ec->pnt[0], PositionTex, 0); /* 3584 */
        ec->flow = 1;                                           /* 3585 */
    }

    if (ExecFlg == 0)                                           /* 3588 */
    {
        ResetEffects(ec);                                       /* 3589 */
    }
    else
    {
        ec->cnt++;                                              /* 3592 */
    }
}


/* ==========================================================================
 *  Sub-system internals
 *
 *  The statics of the light-shaft, water-flow and camera-flash sub-systems,
 *  kept together at the end in the ROM's own order.
 * ======================================================================== */

/* ---- light shafts ------------------------------------------------------- */

/* 2389 (384) -- the direction half of the alpha rate: how square-on the camera
 * is to the shaft.
 *
 * Both yaws are brought positive, the difference folded into a half turn, and
 * then measured against a quarter turn -- so the answer is 1.0 when the camera
 * looks straight along or straight against the beam and 0.0 when it looks
 * across it.  A shaft is brightest edge-on to the eye, which is what makes it
 * read as a volume of lit dust rather than a flat card.
 *
 * The two `fabs` and the final divide are software double precision in the ROM
 * (fptodp / dpcmp / dpsub / dpdiv), and 1.570796251296997 is a real .rodata
 * double at 3a6820 rather than a promoted float.  Only the yaws matter; the
 * pitches come back from both helpers and are thrown away. */
static float EffectLightComeInCalcAlphaRateCamDir(void *pSgdTop)
{
    float AlphaRate;
    float CamRotX, CamRotY;
    float LightRotX, LightRotY;

    float (&rCamDir)[4] = gra3dcamGetDirection();               /* 2390 */

    AlphaRate = 0.0f;                                           /* 2397 */

    if (pSgdTop != nullptr)
    {
        Vector2Rot(rCamDir, &CamRotX, &CamRotY);                /* 2402 */
        EffectLightComeInMatrix2Rot( (((SGDFILEHEADER *)pSgdTop)->pCoord->matLocalWorld), &LightRotX, &LightRotY); /* 2403 */

        while (CamRotY < 0.0f)   { CamRotY   += 6.28318501f; }  /* 2404 */
        while (LightRotY < 0.0f) { LightRotY += 6.28318501f; }  /* 2405 */

        AlphaRate = (float)fabs((double)(CamRotY - LightRotY)); /* 2407 */

        if (AlphaRate > 3.1415925f)                             /* 2408 */
        {
            AlphaRate -= 3.1415925f;
        }

        AlphaRate = (float)(fabs((double)(AlphaRate - 1.57079625f))
                            / 1.570796251296997);               /* 2409 */
    }

    return AlphaRate;                                           /* 2413 */
}

/* 2543 / 2643 / 2719 (856 / 848 / 840) -- the three authored vertex-colour
 * variants.  One routine written out three times with different constants: the
 * same 38 vertices in the same order, split into three groups of four colour
 * slots, and only the middle group is ever non-black.
 *
 * These bake the beam's own lighting into the model, once, when the shaft is
 * registered -- there is no per-frame light for them.  A shaft's mesh is
 * therefore black everywhere except the band of vertices in the middle group,
 * which is what gives it its soft core; the type only changes how bright and
 * how cool that core is (66/46/18 warm, 55/55/56 neutral, 30/35/37 dim blue).
 *
 * The zeroing of the first and third groups is not redundant with the register:
 * gra3dSetVertexColorPreset() writes into the model's own preset table, which
 * survives between rooms.
 *
 * Every index 0x00..0x25 appears exactly once across the three groups. */
static void EffectLightComeInSetVertexColorNormal(void *pSgdTop)
{                                                                   /* 2543 */
    SGDFILEHEADER *pSgd = (SGDFILEHEADER *)pSgdTop;
    VECTOR3 vSetColor0, vSetColor1, vSetColor2, vSetColor3;

    vSetColor0[0] = 0.0f; vSetColor0[1] = 0.0f; vSetColor0[2] = 0.0f;  /* 2556 */
    vSetColor1[0] = 0.0f; vSetColor1[1] = 0.0f; vSetColor1[2] = 0.0f;  /* 2557 */
    vSetColor2[0] = 0.0f; vSetColor2[1] = 0.0f; vSetColor2[2] = 0.0f;  /* 2558 */
    vSetColor3[0] = 0.0f; vSetColor3[1] = 0.0f; vSetColor3[2] = 0.0f;  /* 2559 */

    gra3dSetVertexColorPreset(pSgd, 0x00, vSetColor0);              /* 2562 */
    gra3dSetVertexColorPreset(pSgd, 0x16, vSetColor0);              /* 2563 */

    gra3dSetVertexColorPreset(pSgd, 0x02, vSetColor1);              /* 2565 */
    gra3dSetVertexColorPreset(pSgd, 0x17, vSetColor1);              /* 2566 */
    gra3dSetVertexColorPreset(pSgd, 0x21, vSetColor1);              /* 2567 */

    gra3dSetVertexColorPreset(pSgd, 0x04, vSetColor2);              /* 2569 */
    gra3dSetVertexColorPreset(pSgd, 0x0a, vSetColor2);              /* 2570 */
    gra3dSetVertexColorPreset(pSgd, 0x22, vSetColor2);              /* 2571 */
    gra3dSetVertexColorPreset(pSgd, 0x23, vSetColor2);              /* 2572 */

    gra3dSetVertexColorPreset(pSgd, 0x08, vSetColor3);              /* 2574 */
    gra3dSetVertexColorPreset(pSgd, 0x25, vSetColor3);              /* 2575 */

    vSetColor0[0] = 0.0f; vSetColor0[1] = 0.0f; vSetColor0[2] = 0.0f;  /* 2584 */
    vSetColor1[0] = 66.0f; vSetColor1[1] = 46.0f; vSetColor1[2] = 18.0f;  /* 2585 */
    vSetColor2[0] = 64.0f; vSetColor2[1] = 48.0f; vSetColor2[2] = 18.0f;  /* 2586 */
    vSetColor3[0] = 0.0f; vSetColor3[1] = 0.0f; vSetColor3[2] = 0.0f;  /* 2587 */

    gra3dSetVertexColorPreset(pSgd, 0x01, vSetColor0);              /* 2590 */
    gra3dSetVertexColorPreset(pSgd, 0x0f, vSetColor0);              /* 2591 */
    gra3dSetVertexColorPreset(pSgd, 0x18, vSetColor0);              /* 2592 */

    gra3dSetVertexColorPreset(pSgd, 0x03, vSetColor1);              /* 2594 */
    gra3dSetVertexColorPreset(pSgd, 0x0c, vSetColor1);              /* 2595 */
    gra3dSetVertexColorPreset(pSgd, 0x11, vSetColor1);              /* 2596 */
    gra3dSetVertexColorPreset(pSgd, 0x19, vSetColor1);              /* 2597 */
    gra3dSetVertexColorPreset(pSgd, 0x20, vSetColor1);              /* 2598 */

    gra3dSetVertexColorPreset(pSgd, 0x05, vSetColor2);              /* 2600 */
    gra3dSetVertexColorPreset(pSgd, 0x0b, vSetColor2);              /* 2601 */
    gra3dSetVertexColorPreset(pSgd, 0x13, vSetColor2);              /* 2602 */
    gra3dSetVertexColorPreset(pSgd, 0x1d, vSetColor2);              /* 2603 */

    gra3dSetVertexColorPreset(pSgd, 0x06, vSetColor3);              /* 2605 */
    gra3dSetVertexColorPreset(pSgd, 0x09, vSetColor3);              /* 2606 */
    gra3dSetVertexColorPreset(pSgd, 0x1b, vSetColor3);              /* 2607 */
    gra3dSetVertexColorPreset(pSgd, 0x24, vSetColor3);              /* 2608 */

    vSetColor0[0] = 0.0f; vSetColor0[1] = 0.0f; vSetColor0[2] = 0.0f;  /* 2617 */
    vSetColor1[0] = 0.0f; vSetColor1[1] = 0.0f; vSetColor1[2] = 0.0f;  /* 2618 */
    vSetColor2[0] = 0.0f; vSetColor2[1] = 0.0f; vSetColor2[2] = 0.0f;  /* 2619 */
    vSetColor3[0] = 0.0f; vSetColor3[1] = 0.0f; vSetColor3[2] = 0.0f;  /* 2620 */

    gra3dSetVertexColorPreset(pSgd, 0x0e, vSetColor0);              /* 2623 */
    gra3dSetVertexColorPreset(pSgd, 0x10, vSetColor0);              /* 2624 */
    gra3dSetVertexColorPreset(pSgd, 0x1a, vSetColor0);              /* 2625 */

    gra3dSetVertexColorPreset(pSgd, 0x0d, vSetColor1);              /* 2627 */
    gra3dSetVertexColorPreset(pSgd, 0x12, vSetColor1);              /* 2628 */
    gra3dSetVertexColorPreset(pSgd, 0x1f, vSetColor1);              /* 2629 */

    gra3dSetVertexColorPreset(pSgd, 0x14, vSetColor2);              /* 2631 */
    gra3dSetVertexColorPreset(pSgd, 0x1e, vSetColor2);              /* 2632 */

    gra3dSetVertexColorPreset(pSgd, 0x07, vSetColor3);              /* 2634 */
    gra3dSetVertexColorPreset(pSgd, 0x15, vSetColor3);              /* 2635 */
    gra3dSetVertexColorPreset(pSgd, 0x1c, vSetColor3);              /* 2636 */
}

static void EffectLightComeInSetVertexColorF607(void *pSgdTop)
{                                                                   /* 2643 */
    SGDFILEHEADER *pSgd = (SGDFILEHEADER *)pSgdTop;
    VECTOR3 vSetColor0, vSetColor1, vSetColor2, vSetColor3;

    vSetColor0[0] = 0.0f; vSetColor0[1] = 0.0f; vSetColor0[2] = 0.0f;  /* 2647 */
    vSetColor1[0] = 0.0f; vSetColor1[1] = 0.0f; vSetColor1[2] = 0.0f;  /* 2648 */
    vSetColor2[0] = 0.0f; vSetColor2[1] = 0.0f; vSetColor2[2] = 0.0f;  /* 2649 */
    vSetColor3[0] = 0.0f; vSetColor3[1] = 0.0f; vSetColor3[2] = 0.0f;  /* 2650 */

    gra3dSetVertexColorPreset(pSgd, 0x00, vSetColor0);              /* 2652 */
    gra3dSetVertexColorPreset(pSgd, 0x16, vSetColor0);              /* 2653 */

    gra3dSetVertexColorPreset(pSgd, 0x02, vSetColor1);              /* 2655 */
    gra3dSetVertexColorPreset(pSgd, 0x17, vSetColor1);              /* 2656 */
    gra3dSetVertexColorPreset(pSgd, 0x21, vSetColor1);              /* 2657 */

    gra3dSetVertexColorPreset(pSgd, 0x04, vSetColor2);              /* 2659 */
    gra3dSetVertexColorPreset(pSgd, 0x0a, vSetColor2);              /* 2660 */
    gra3dSetVertexColorPreset(pSgd, 0x22, vSetColor2);              /* 2661 */
    gra3dSetVertexColorPreset(pSgd, 0x23, vSetColor2);              /* 2662 */

    gra3dSetVertexColorPreset(pSgd, 0x08, vSetColor3);              /* 2664 */
    gra3dSetVertexColorPreset(pSgd, 0x25, vSetColor3);              /* 2665 */

    vSetColor0[0] = 0.0f; vSetColor0[1] = 0.0f; vSetColor0[2] = 0.0f;  /* 2668 */
    vSetColor1[0] = 55.0f; vSetColor1[1] = 55.0f; vSetColor1[2] = 56.0f;  /* 2669 */
    vSetColor2[0] = 50.0f; vSetColor2[1] = 53.0f; vSetColor2[2] = 56.0f;  /* 2670 */
    vSetColor3[0] = 0.0f; vSetColor3[1] = 0.0f; vSetColor3[2] = 0.0f;  /* 2671 */

    gra3dSetVertexColorPreset(pSgd, 0x01, vSetColor0);              /* 2680 */
    gra3dSetVertexColorPreset(pSgd, 0x0f, vSetColor0);              /* 2681 */
    gra3dSetVertexColorPreset(pSgd, 0x18, vSetColor0);              /* 2682 */

    gra3dSetVertexColorPreset(pSgd, 0x03, vSetColor1);              /* 2684 */
    gra3dSetVertexColorPreset(pSgd, 0x0c, vSetColor1);              /* 2685 */
    gra3dSetVertexColorPreset(pSgd, 0x11, vSetColor1);              /* 2686 */
    gra3dSetVertexColorPreset(pSgd, 0x19, vSetColor1);              /* 2687 */
    gra3dSetVertexColorPreset(pSgd, 0x20, vSetColor1);              /* 2688 */

    gra3dSetVertexColorPreset(pSgd, 0x05, vSetColor2);              /* 2690 */
    gra3dSetVertexColorPreset(pSgd, 0x0b, vSetColor2);              /* 2691 */
    gra3dSetVertexColorPreset(pSgd, 0x13, vSetColor2);              /* 2692 */
    gra3dSetVertexColorPreset(pSgd, 0x1d, vSetColor2);              /* 2693 */

    gra3dSetVertexColorPreset(pSgd, 0x06, vSetColor3);              /* 2695 */
    gra3dSetVertexColorPreset(pSgd, 0x09, vSetColor3);              /* 2696 */
    gra3dSetVertexColorPreset(pSgd, 0x1b, vSetColor3);              /* 2697 */
    gra3dSetVertexColorPreset(pSgd, 0x24, vSetColor3);              /* 2698 */

    vSetColor0[0] = 0.0f; vSetColor0[1] = 0.0f; vSetColor0[2] = 0.0f;  /* 2694 */
    vSetColor1[0] = 0.0f; vSetColor1[1] = 0.0f; vSetColor1[2] = 0.0f;  /* 2695 */
    vSetColor2[0] = 0.0f; vSetColor2[1] = 0.0f; vSetColor2[2] = 0.0f;  /* 2696 */
    vSetColor3[0] = 0.0f; vSetColor3[1] = 0.0f; vSetColor3[2] = 0.0f;  /* 2697 */

    gra3dSetVertexColorPreset(pSgd, 0x0e, vSetColor0);              /* 2713 */
    gra3dSetVertexColorPreset(pSgd, 0x10, vSetColor0);              /* 2714 */
    gra3dSetVertexColorPreset(pSgd, 0x1a, vSetColor0);              /* 2715 */

    gra3dSetVertexColorPreset(pSgd, 0x0d, vSetColor1);              /* 2717 */
    gra3dSetVertexColorPreset(pSgd, 0x12, vSetColor1);              /* 2718 */
    gra3dSetVertexColorPreset(pSgd, 0x1f, vSetColor1);              /* 2719 */

    gra3dSetVertexColorPreset(pSgd, 0x14, vSetColor2);              /* 2721 */
    gra3dSetVertexColorPreset(pSgd, 0x1e, vSetColor2);              /* 2722 */

    gra3dSetVertexColorPreset(pSgd, 0x07, vSetColor3);              /* 2724 */
    gra3dSetVertexColorPreset(pSgd, 0x15, vSetColor3);              /* 2725 */
    gra3dSetVertexColorPreset(pSgd, 0x1c, vSetColor3);              /* 2726 */
}

static void EffectLightComeInSetVertexColorF609(void *pSgdTop)
{                                                                   /* 2719 */
    SGDFILEHEADER *pSgd = (SGDFILEHEADER *)pSgdTop;
    VECTOR3 vSetColor0, vSetColor1, vSetColor2, vSetColor3;

    vSetColor0[0] = 0.0f; vSetColor0[1] = 0.0f; vSetColor0[2] = 0.0f;  /* 2723 */
    vSetColor1[0] = 0.0f; vSetColor1[1] = 0.0f; vSetColor1[2] = 0.0f;  /* 2724 */
    vSetColor2[0] = 0.0f; vSetColor2[1] = 0.0f; vSetColor2[2] = 0.0f;  /* 2725 */
    vSetColor3[0] = 0.0f; vSetColor3[1] = 0.0f; vSetColor3[2] = 0.0f;  /* 2726 */

    gra3dSetVertexColorPreset(pSgd, 0x00, vSetColor0);              /* 2728 */
    gra3dSetVertexColorPreset(pSgd, 0x16, vSetColor0);              /* 2729 */

    gra3dSetVertexColorPreset(pSgd, 0x02, vSetColor1);              /* 2731 */
    gra3dSetVertexColorPreset(pSgd, 0x17, vSetColor1);              /* 2732 */
    gra3dSetVertexColorPreset(pSgd, 0x21, vSetColor1);              /* 2733 */

    gra3dSetVertexColorPreset(pSgd, 0x04, vSetColor2);              /* 2735 */
    gra3dSetVertexColorPreset(pSgd, 0x0a, vSetColor2);              /* 2736 */
    gra3dSetVertexColorPreset(pSgd, 0x22, vSetColor2);              /* 2737 */
    gra3dSetVertexColorPreset(pSgd, 0x23, vSetColor2);              /* 2738 */

    gra3dSetVertexColorPreset(pSgd, 0x08, vSetColor3);              /* 2740 */
    gra3dSetVertexColorPreset(pSgd, 0x25, vSetColor3);              /* 2741 */

    vSetColor0[0] = 0.0f; vSetColor0[1] = 0.0f; vSetColor0[2] = 0.0f;  /* 2744 */
    vSetColor1[0] = 30.0f; vSetColor1[1] = 35.0f; vSetColor1[2] = 37.0f;  /* 2745 */
    vSetColor2[0] = 30.0f; vSetColor2[1] = 35.0f; vSetColor2[2] = 37.0f;  /* 2746 */
    vSetColor3[0] = 0.0f; vSetColor3[1] = 0.0f; vSetColor3[2] = 0.0f;  /* 2747 */

    gra3dSetVertexColorPreset(pSgd, 0x01, vSetColor0);              /* 2756 */
    gra3dSetVertexColorPreset(pSgd, 0x0f, vSetColor0);              /* 2757 */
    gra3dSetVertexColorPreset(pSgd, 0x18, vSetColor0);              /* 2758 */

    gra3dSetVertexColorPreset(pSgd, 0x03, vSetColor1);              /* 2760 */
    gra3dSetVertexColorPreset(pSgd, 0x0c, vSetColor1);              /* 2761 */
    gra3dSetVertexColorPreset(pSgd, 0x11, vSetColor1);              /* 2762 */
    gra3dSetVertexColorPreset(pSgd, 0x19, vSetColor1);              /* 2763 */
    gra3dSetVertexColorPreset(pSgd, 0x20, vSetColor1);              /* 2764 */

    gra3dSetVertexColorPreset(pSgd, 0x05, vSetColor2);              /* 2766 */
    gra3dSetVertexColorPreset(pSgd, 0x0b, vSetColor2);              /* 2767 */
    gra3dSetVertexColorPreset(pSgd, 0x13, vSetColor2);              /* 2768 */
    gra3dSetVertexColorPreset(pSgd, 0x1d, vSetColor2);              /* 2769 */

    gra3dSetVertexColorPreset(pSgd, 0x06, vSetColor3);              /* 2771 */
    gra3dSetVertexColorPreset(pSgd, 0x09, vSetColor3);              /* 2772 */
    gra3dSetVertexColorPreset(pSgd, 0x1b, vSetColor3);              /* 2773 */
    gra3dSetVertexColorPreset(pSgd, 0x24, vSetColor3);              /* 2774 */

    vSetColor0[0] = 0.0f; vSetColor0[1] = 0.0f; vSetColor0[2] = 0.0f;  /* 2770 */
    vSetColor1[0] = 0.0f; vSetColor1[1] = 0.0f; vSetColor1[2] = 0.0f;  /* 2771 */
    vSetColor2[0] = 0.0f; vSetColor2[1] = 0.0f; vSetColor2[2] = 0.0f;  /* 2772 */
    vSetColor3[0] = 0.0f; vSetColor3[1] = 0.0f; vSetColor3[2] = 0.0f;  /* 2773 */

    gra3dSetVertexColorPreset(pSgd, 0x0e, vSetColor0);              /* 2789 */
    gra3dSetVertexColorPreset(pSgd, 0x10, vSetColor0);              /* 2790 */
    gra3dSetVertexColorPreset(pSgd, 0x1a, vSetColor0);              /* 2791 */

    gra3dSetVertexColorPreset(pSgd, 0x0d, vSetColor1);              /* 2793 */
    gra3dSetVertexColorPreset(pSgd, 0x12, vSetColor1);              /* 2794 */
    gra3dSetVertexColorPreset(pSgd, 0x1f, vSetColor1);              /* 2795 */

    gra3dSetVertexColorPreset(pSgd, 0x14, vSetColor2);              /* 2797 */
    gra3dSetVertexColorPreset(pSgd, 0x1e, vSetColor2);              /* 2798 */

    gra3dSetVertexColorPreset(pSgd, 0x07, vSetColor3);              /* 2800 */
    gra3dSetVertexColorPreset(pSgd, 0x15, vSetColor3);              /* 2801 */
    gra3dSetVertexColorPreset(pSgd, 0x1c, vSetColor3);              /* 2802 */
}

/* ---- water flow --------------------------------------------------------- */

/* 2970 (360) / 3108 (352) -- the per-frame S and T scroll steps, indexed by
 * move type, the A/B/C leg (MoveDataNo) and the frame count within it.
 *
 * Both functions are two dispatches in a row and the shapes are not the same:
 * MoveType is an if / else-if chain (each comparison carries its own line, and
 * a switch over the contiguous 0..6 would have been a jump table), MoveDataNo
 * is a real switch with one (.rodata 0x3a6850 for S, 0x3a6870 for T).
 *
 * Four move types -- 0, 1, 4 and 5, the rivers -- return a constant and never
 * reach the second dispatch at all.  The other four cycle three legs:
 *
 *      MoveDataNo  0 hold A   1 A->B   2 hold B   3 B->C   4 hold C   5 C->A
 *
 * with every transition a straight lerp over 60 frames, which is what
 * EffectWaterFlowGetChangeFrame()'s three 60s are.  A MoveDataNo of 6 or more
 * falls out of the switch and returns the declaration's 0.0f -- there is no
 * default arm.
 *
 * Every constant here is a .lit4 literal (effect_obj.o's block is 0x3edb88),
 * not an authored table, so they are written out at the site as the ROM has
 * them.  Lines 2972..3038 and 3110..3178 carry no code: ~68 lines of comment or
 * disabled text sit between each declaration and its first comparison. */
static float EffectWaterFlowGetAddS(int MoveType, int MoveDataNo, int Count)
{
    float AddS = 0.0f;                                          /* 2971 */
    float AddS_A = 0.0f;
    float AddS_B = 0.0f;
    float AddS_C = 0.0f;

    if (MoveType == 0)                                          /* 3039 */
    {
        AddS = -0.00399999972f;                                         /* 3040 */
    }
    else if (MoveType == 1)                                     /* 3043 */
    {
        AddS = 0.00169999991f;                                         /* 3044 */
    }
    else if (MoveType == 4)                                     /* 3047 */
    {
        AddS = -0.0033f;                                        /* 3048 */
    }
    else if (MoveType == 5)                                     /* 3051 */
    {
        AddS = 0.00309999986f;                                         /* 3052 */
    }
    /* Types 2 and 6 are byte-identical arms; GCC cross-jumped them onto the
       later one's code, so only 6's line numbers survived.  Type 3 and the
       default hold the same three values but assign them in a different order
       (B, A, C against A, B, C), which is why they were only merged from the
       last store on -- and why the same -0.0005f occupies two .lit4 slots. */
    else if (MoveType == 2)                                     /* 3055 */
    {
        AddS_A =  0.00059999997f;                                      /* 3068 */
        AddS_C = -0.0007f;                                      /* 3070 */
        AddS_B =  0.0f;
    }
    else if (MoveType == 3)                                     /* 3061 */
    {
        AddS_B = -0.000499999966f;                                      /* 3063 */
        AddS_A =  0.0f;                                         /* 3064 */
        AddS_C =  0.000499999966f;                                      /* 3065 */
    }
    else if (MoveType == 6)                                     /* 3067 */
    {
        AddS_A =  0.00059999997f;                                      /* 3068 */
        AddS_C = -0.0007f;                                      /* 3070 */
        AddS_B =  0.0f;
    }
    else
    {
        AddS_A =  0.0f;                                         /* 3073 */
        AddS_B = -0.000499999966f;                                      /* 3074 */
        AddS_C =  0.000499999966f;                                      /* 3075 */
    }

    switch (MoveDataNo)                                         /* 3078 */
    {
    case 0:
        AddS = AddS_A;                                          /* 3080 */
        break;
    case 1:
        AddS = (AddS_B - AddS_A) * Count / 60.0f + AddS_A;      /* 3083 */
        break;
    case 2:
        AddS = AddS_B;                                          /* 3086 */
        break;
    case 3:
        AddS = (AddS_C - AddS_B) * Count / 60.0f + AddS_B;      /* 3089 */
        break;
    case 4:
        AddS = AddS_C;                                          /* 3092 */
        break;
    case 5:
        AddS = (AddS_A - AddS_C) * Count / 60.0f + AddS_C;      /* 3095 */
        break;
    }

    return AddS;                                                /* 3101 */
}

static float EffectWaterFlowGetAddT(int MoveType, int MoveDataNo, int Count)
{
    float AddT = 0.0f;                                          /* 3109 */
    float AddT_A = 0.0f;
    float AddT_B = 0.0f;
    float AddT_C = 0.0f;

    if (MoveType == 0)                                          /* 3179 */
    {
        AddT = -0.0079f;                                        /* 3180 */
    }
    else if (MoveType == 1)                                     /* 3183 */
    {
        AddT = 0.022f;                                          /* 3184 */
    }
    else if (MoveType == 4)                                     /* 3187 */
    {
        AddT = 0.0156f;                                         /* 3188 */
    }
    else if (MoveType == 5)                                     /* 3191 */
    {
        AddT = 0.0105f;                                         /* 3192 */
    }
    else if (MoveType == 2)                                     /* 3195 */
    {
        AddT_B =  0.0008f;                                      /* 3209 */
        AddT_C = -0.00059999997f;                                      /* 3210 */
        AddT_A =  0.0f;
    }
    /* Unlike GetAddS, type 3 and the default merged completely -- one body, one
       set of line numbers, one .lit4 slot.  AddT_C is emitted as a register copy
       of AddT_A because the two hold the same literal and GCC CSE'd the load. */
    else if (MoveType == 3)                                     /* 3201 */
    {
        AddT_A = -0.000499999966f;                                      /* 3213 */
        AddT_B =  0.0f;                                         /* 3214 */
        AddT_C = -0.000499999966f;                                      /* 3215 */
    }
    else if (MoveType == 6)                                     /* 3207 */
    {
        AddT_B =  0.0008f;                                      /* 3209 */
        AddT_C = -0.00059999997f;                                      /* 3210 */
        AddT_A =  0.0f;
    }
    else
    {
        AddT_A = -0.000499999966f;                                      /* 3213 */
        AddT_B =  0.0f;                                         /* 3214 */
        AddT_C = -0.000499999966f;                                      /* 3215 */
    }

    switch (MoveDataNo)                                         /* 3218 */
    {
    case 0:
        AddT = AddT_A;                                          /* 3220 */
        break;
    case 1:
        AddT = (AddT_B - AddT_A) * Count / 60.0f + AddT_A;      /* 3223 */
        break;
    case 2:
        AddT = AddT_B;                                          /* 3226 */
        break;
    case 3:
        AddT = (AddT_C - AddT_B) * Count / 60.0f + AddT_B;      /* 3229 */
        break;
    case 4:
        AddT = AddT_C;                                          /* 3232 */
        break;
    case 5:
        AddT = (AddT_A - AddT_C) * Count / 60.0f + AddT_C;      /* 3235 */
        break;
    }

    return AddT;                                                /* 3241 */
}

/* ---- camera flash ------------------------------------------------------- */

/* 3518 (148) -- where the flash sits: bone 6 of the player model, in world
 * space.  Both offsets are the bone's own origin, so the two points come back
 * identical -- the second exists because the textured half is placed from a
 * separate vector that later code was free to bias, and never was.
 *
 * Returns 0 with both outputs untouched when the player has no model. */
static int EffectCameraFlashGetPosition(float *Position, float *PositionTex)
{                                                               /* 3518 */
    float     LocalWorld[4][4];
    ANI_CTRL *pAniCtrl;
    float     Offset[4]    = { 0.0f, 0.0f, 0.0f, 1.0f };        /* 3538 */
    float     OffsetTex[4] = { 0.0f, 0.0f, 0.0f, 1.0f };        /* 3543 */

    pAniCtrl = plyr_mdlGetANI_CTRL();                           /* 3551 */

    if (pAniCtrl == nullptr)                                    /* 3554 */
    {
        return 0;
    }

    motGetLocalWorldMatrix(LocalWorld, pAniCtrl->mpk_p, 6);     /* 3556 */

    sceVu0ApplyMatrix(Position, LocalWorld, Offset);            /* 3557 */
    sceVu0ApplyMatrix(PositionTex, LocalWorld, OffsetTex);      /* 3558 */

    return 1;                                                   /* 3560 */
}                                                               /* 3561 */

/* 3608 (336) -- lay out the corona as a triangle fan: the centre, then
 * VertexNum-2 points evenly spaced round a circle of radius `Radius`, then the
 * first rim point repeated to close it.  The sweep starts at -pi so the seam
 * lands on the far side.
 *
 * Note the loop bound: `i` runs 1 .. VertexNum-2, so the caller's count
 * includes both the centre and the closing repeat. */
static void CameraFlashMakeVertex(sceVu0FVECTOR *pVtxBuf, float *Center,
                                  float Radius, int VertexNum)
{                                                               /* 3608 */
    float  RotMat[4][4];
    float  RotZ;
    int    i;
    int    PolygonNum;
    float *pv0;
    float *pv1;

    float BaseVec[4] = { Radius, 0.0f, 0.0f, 1.0f };            /* 3610 */

    PolygonNum = VertexNum - 2;                                 /* 3613 */

    if (PolygonNum > 0)                                         /* 3615 */
    {
        g3dxVu0CopyVector(pVtxBuf[0], Center);

        for (i = 1; i < VertexNum - 1; i++)                     /* 3620 */
        {
            RotZ = (float)(i - 1) * 6.28318501f / (float)PolygonNum
                 - 3.1415925f;                                  /* 3621 */

            sceVu0UnitMatrix(RotMat);                           /* 3623 */
            sceVu0RotMatrixZ(RotMat, RotMat, RotZ);             /* 3624 */
            sceVu0ApplyMatrix(pVtxBuf[i], RotMat, BaseVec);     /* 3625 */
            sceVu0AddVector(pVtxBuf[i], pVtxBuf[i], Center);    /* 3626 */
        }                                                       /* 3627 */

        pv0 = pVtxBuf[VertexNum - 1];
        pv1 = pVtxBuf[1];
        g3dxVu0CopyVector(pv0, pv1);
    }
}                                                               /* 3629 */

/* 3635 (252) -- the refracting corona's 29-frame ramp.  Answers 0 once it is
 * over, which is half of SetCameraFlash()'s lifetime test.
 *
 * The edge colour's alpha ceiling is 0, so `EdgeRgba[3]` is always 0 and the
 * ring fades to nothing at its rim -- the whole `(1 - progress) * AlphaMaxEdge`
 * expression is dead arithmetic the ROM emits anyway.  A negative Count (which
 * nothing passes) short-circuits to full brightness. */
static int EffectCameraFlashDraw(float *Position, int Count)
{                                                               /* 3635 */
    float  Scale;
    int    AlphaCenter;
    int    AlphaEdge;
    float  StartScale;
    int    AlphaMaxCenter;
    int    AlphaMaxEdge;
    int    AlphaOutTime;
    float  Progress;

    StartScale     = 1.04f;
    AlphaMaxCenter = 129;
    AlphaMaxEdge   = 0;
    AlphaOutTime   = 28;                                        /* 3661 */

    u_char CenterRgba[4] = { 0xff, 0xff, 0xff, 0 };             /* 3663 */
    u_char EdgeRgba[4]   = { 0xff, 0xff, 0xff, 0 };             /* 3664 */

    if (Count > AlphaOutTime)                                   /* 3668 */
    {
        return 0;
    }

    Scale = (float)Count * 0.159999967f / (float)AlphaOutTime
          + StartScale;                                         /* 3671 */

    if (Count < 0)                                              /* 3674 */
    {
        AlphaCenter = AlphaMaxCenter;                           /* 3681 */
        AlphaEdge   = AlphaMaxEdge;
    }
    else
    {
        Progress = (float)Count / (float)AlphaOutTime;          /* 3691 */

        AlphaCenter = (int)((1.0f - Progress) * (float)AlphaMaxCenter); /* 3693 */
        AlphaEdge   = (int)((1.0f - Progress) * (float)AlphaMaxEdge);   /* 3694 */
    }

    CenterRgba[3] = (u_char)AlphaCenter;                        /* 3702 */
    EdgeRgba[3]   = (u_char)AlphaEdge;                          /* 3703 */

    EffectCameraFlashDrawSub(Position, 4.04f, Scale,
                             CenterRgba, EdgeRgba, 0x48);       /* 3705 */

    return 1;                                                   /* 3707 */
}                                                               /* 3708 */

/* 3714 (728) -- draw the corona.  Copies the back buffer into local memory as
 * a texture, builds a ten-vertex fan facing the camera, projects every vertex
 * to the screen coordinate it lands on (CalcStqXYZ), and hands the fan to
 * effect_ene.o's packet builder -- the same one the ghost's large-hit ring and
 * effect_oth's haze use, which is why that one is exported.
 *
 * The two matrices are built separately on purpose: `LocalWorldMat` carries the
 * PolygonScale used to lay the geometry out and *then* has `Scale` folded in
 * afterwards, so the STQ pass samples a fixed-size window while the drawn ring
 * grows.  That is the expansion of the flash. */
static void EffectCameraFlashDrawSub(float *Position, float PolygonScale,
                                     float Scale, u_char *CenterRgba,
                                     u_char *EdgeRgba, u_long AlphaBlend)
{                                                               /* 3714 */
    float         LocalWorldMat[4][4];
    float         LocalScreenMat[4][4];
    float         ScaleMat[4][4];
    float         stqparam[3][4];
    sceVu0FVECTOR VtxBuf[10];
    sceVu0FVECTOR StqBuf[10];
    int           i;
    float         RotX, RotY;
    GRA3DCAMERA  *pCam;

    pCam = gra3dGetCamera();                                    /* 3724 */
    float (&rCamDir)[4] = gra3dcamGetDirection();               /* 3725 */

    float Center[4] = { 0.0f, 0.0f, 0.0f, 1.0f };               /* 3727 */

    LocalCopyBtoL(2, 0, 0x2bc0);                                /* 3730 */

    Vector2Rot(rCamDir, &RotX, &RotY);                          /* 3734 */

    sceVu0UnitMatrix(LocalWorldMat);                            /* 3736 */
    LocalWorldMat[0][0] = PolygonScale * 25.0f;
    LocalWorldMat[1][1] = PolygonScale * 25.0f;
    LocalWorldMat[2][2] = PolygonScale * 25.0f;                 /* 3737 */
    sceVu0RotMatrixX(LocalWorldMat, LocalWorldMat, RotX);       /* 3738 */
    sceVu0RotMatrixY(LocalWorldMat, LocalWorldMat, RotY);       /* 3739 */
    sceVu0TransMatrix(LocalWorldMat, LocalWorldMat, Position);  /* 3740 */
    sceVu0MulMatrix(LocalScreenMat, pCam->matWorldScreen, LocalWorldMat); /* 3741 */

    CameraFlashMakeVertex(VtxBuf, Center, 10.0f, 10);           /* 3744 */

    /* See CalcStqXYZ in effect_obj.h. */
    stqparam[0][0] = 1727.5f;                                   /* 3747 */
    stqparam[0][1] = 1823.5f;                                   /* 3748 */
    stqparam[0][2] = 1.0f;                                      /* 3749 */
    EffectSetScreenSampleClamp(stqparam[1]);                    /* 3750 */
    stqparam[2][0] = 0.0009765625f; stqparam[2][1] = 0.001953125f; stqparam[2][2] = 1.0f; stqparam[2][3] = 0.0625f; /* 3751 */

    /* PORT DEVIATION: gra3dVu0.h 119 and effect_obj.h 199/200 primed vf4..vf10
     * here; both are CalcStqXYZ parameters on the host. */

    for (i = 0; i < 10; i++)                                    /* 3755 */
    {
        CalcStqXYZ(0, &VtxBuf[i], &StqBuf[i], LocalScreenMat, stqparam);
    }                                                           /* 3757 */

    sceVu0UnitMatrix(ScaleMat);                                 /* 3760 */
    ScaleMat[0][0] = Scale; ScaleMat[1][1] = Scale; ScaleMat[2][2] = Scale; /* 3761 */
    sceVu0MulMatrix(LocalWorldMat, LocalWorldMat, ScaleMat);    /* 3762 */

    DRAW_ENV_5 DrawEnv =                                        /* 3765 */
    {
        AlphaBlend,
        0x0000000000000161ULL,      /* TEX1  MMAG/MMIN LINEAR                  */
        0x0000000000000005ULL,      /* CLAMP clamp s and t                     */
        0x000000000003000dULL,      /* TEST  ATST GEQUAL/0, ZTST GEQUAL        */
        0x000000010a000118ULL       /* ZBUF  PSMZ24 @0x118, ZMSK (no z write)  */
    };

    SetDrawEnv(0, &DrawEnv);                                    /* 3776 */

    /* TEX0: TBP0 0x2bc0 (the copy above), TBW 5, PSMCT24, 512 square. */
    EneDmgLargeHitMakePacket(VtxBuf, 10, LocalWorldMat, CenterRgba, EdgeRgba,
                             StqBuf, 0x2000000264116bc0ULL);    /* 3780 */
}

/* 3791 (476) -- the plain textured flare, effect sprite 6, over nine frames:
 * one frame in, three at full, then a four-frame fade.  It faces the camera and
 * is placed at the same bone as the corona.  Answers 0 once it is over.
 *
 * `Count <= 0` cannot happen from SetCameraFlash() (cnt starts at 0 and the
 * test is the *first* thing) -- the ramp-in arm is dead. */
static int EffectCameraFlashDrawTex(float *Position, int Count)
{                                                               /* 3791 */
    float    Scale;
    int      Alpha;
    float    StartScale;
    int      AlphaMax;
    int      AlphaInTime;
    int      AlphaOutTime;
    int      AllTime;
    float    LocalWorldMat[4][4];
    float    RotX, RotY;
    DRAW_ENV DrawEnv;
    float    Progress;

    StartScale   = 5.74f;
    AlphaMax     = 35;                                          /* 3808 */
    AlphaInTime  = 1;                                           /* 3809 */
    AlphaOutTime = 4;

    u_char Rgba[4] = { 0xff, 0xff, 0xff, 0 };                   /* 3813 */

    float (&rCamDir)[4] = gra3dcamGetDirection();               /* 3818 */
    AllTime = 8;

    if (Count > AllTime)                                        /* 3821 */
    {
        return 0;
    }

    Scale = (float)Count * 8.7f / (float)AllTime + StartScale;  /* 3824 */

    DrawEnv.tex1  = 0x0000000000000161ULL;
    DrawEnv.alpha = 0x0000000000000048ULL;
    DrawEnv.zbuf  = 0x000000010a000118ULL;
    DrawEnv.test  = 0x000000000003000dULL;
    DrawEnv.clamp = 0;
    /* PRIM: triangle strip, textured, Gouraud, alpha blend, FST. */
    DrawEnv.prim  = 0x302a400000008004ULL;                      /* 3826 */

    if (Count <= 0)                                             /* 3835 */
    {
        Alpha = Count * AlphaMax / AlphaInTime;                 /* 3837 */
    }
    else if (Count < AlphaOutTime)                              /* 3844 */
    {
        Alpha = AlphaMax;                                       /* 3845 */
    }
    else
    {
        Progress = (float)(Count - AlphaOutTime) / (float)AlphaOutTime; /* 3849 */

        Alpha = (int)((1.0f - Progress) * (float)AlphaMax);     /* 3851 */
    }

    Rgba[3] = (u_char)Alpha;                                    /* 3854 */

    Vector2Rot(rCamDir, &RotX, &RotY);                          /* 3862 */

    sceVu0UnitMatrix(LocalWorldMat);                            /* 3864 */
    sceVu0RotMatrixX(LocalWorldMat, LocalWorldMat, RotX);       /* 3866 */
    sceVu0RotMatrixY(LocalWorldMat, LocalWorldMat, RotY);       /* 3867 */
    sceVu0TransMatrix(LocalWorldMat, LocalWorldMat, Position);  /* 3868 */

    Set3DPosTexure(LocalWorldMat, &DrawEnv, 6,
                   Scale * 100.0f, Scale * 100.0f,
                   Rgba[0], Rgba[1], Rgba[2], Rgba[3]);         /* 3872 */

    return 1;                                                   /* 3874 */
}                                                               /* 3875 */

/* ==========================================================================
 *  Model alpha ramp
 *
 *  A list of registered models, each of which can be handed a start alpha, an
 *  end alpha and a duration; Exec() walks the ramp once a frame and stops
 *  itself by zeroing Time.
 * ======================================================================== */

/* 3888 */
static void EffectModelAlphaChangeInit(void)
{
    SingleLinkListInit(&ModelAlphaChangeList,
                       sizeof(MODEL_ALPHA_CAHNGE_DATA));        /* 3889 */
}

/* 3899 -- register a model.  Everything but the handle and the label starts at
 * zero, so a freshly registered model is not ramping. */
void EffectModelAlphaChangeRegist(void *pSgdTop, int Id)
{
    MODEL_ALPHA_CAHNGE_DATA AlphaChangeData;

    if (pSgdTop != nullptr)                                     /* 3901 */
    {
        AlphaChangeData.pSgdTop    = pSgdTop;                   /* 3903 */
        AlphaChangeData.Id         = Id;                        /* 3904 */
        AlphaChangeData.AlphaStart = 0;                         /* 3905 */
        AlphaChangeData.AlphaEnd   = 0;                         /* 3906 */
        AlphaChangeData.Time       = 0;                         /* 3907 */
        AlphaChangeData.Counter    = 0;                         /* 3908 */

        SingleLinkListAddEnd(&ModelAlphaChangeList, &AlphaChangeData); /* 3912 */
    }
}                                                               /* 3914 */

/* 3920 -- drop one model by exact label. */
void EffectModelAlphaChangeDelete(int Id)
{
    SLL_CELL *pCell;
    SLL_CELL *pNext;
    MODEL_ALPHA_CAHNGE_DATA *pData;

    for (pCell = SingleLinkListBeginCell(&ModelAlphaChangeList); /* 3922 */
         pCell != (SLL_CELL *)nullptr;
         pCell = pNext)
    {
        pData = (MODEL_ALPHA_CAHNGE_DATA *)SingleLinkListCellBodyPtr(pCell);
        pNext = SingleLinkListNextCell(pCell);

        if (pData->Id == Id)                                    /* 3928 */
        {
            SingleLinkListRemove(&ModelAlphaChangeList, pCell);  /* 3930 */
        }
    }
}                                                               /* 3932 */

/* 3940 -- drop a whole group.  The label's thousands digit is the group, so
 * labels 7000..7999 all belong to group 7. */
void EffectModelAlphaChangeDeleteGroup(int Id)
{
    SLL_CELL *pCell;
    SLL_CELL *pNext;
    MODEL_ALPHA_CAHNGE_DATA *pData;

    for (pCell = SingleLinkListBeginCell(&ModelAlphaChangeList); /* 3942 */
         pCell != (SLL_CELL *)nullptr;
         pCell = pNext)
    {
        pData = (MODEL_ALPHA_CAHNGE_DATA *)SingleLinkListCellBodyPtr(pCell);
        pNext = SingleLinkListNextCell(pCell);

        if (pData->Id / 1000 == Id)                             /* 3948 */
        {
            SingleLinkListRemove(&ModelAlphaChangeList, pCell);  /* 3950 */
        }
    }
}                                                               /* 3953 */

/* 3961 -- step every ramp.  Time == 0 means "not ramping", and the ramp stops
 * itself by clearing both Counter and Time once Counter reaches Time -- so the
 * model is left at the *interpolated* value of the last step, not snapped to
 * AlphaEnd.  With Counter == Time the interpolation is exact anyway, but only
 * because the final frame is evaluated before the reset. */
void EffectModelAlphaChangeExec(void)
{
    SLL_CELL *pCell;
    MODEL_ALPHA_CAHNGE_DATA *pData;

    for (pCell = SingleLinkListBeginCell(&ModelAlphaChangeList); /* 3963 */
         pCell != (SLL_CELL *)nullptr;
         pCell = SingleLinkListNextCell(pCell))
    {
        pData = (MODEL_ALPHA_CAHNGE_DATA *)SingleLinkListCellBodyPtr(pCell);

        if (pData->Time != 0)                                   /* 3967 */
        {
            ManmdlSetAlpha(pData->pSgdTop, (u_char)
                (int)((float)(pData->AlphaEnd - pData->AlphaStart)
                        * ((float)pData->Counter / (float)pData->Time)
                      + (float)pData->AlphaStart));             /* 3969 */

            if (pData->Counter < pData->Time)                   /* 3972 */
            {
                pData->Counter++;                               /* 3974 */
            }
            else
            {
                pData->Counter = 0;                             /* 3977 */
                pData->Time    = 0;                             /* 3978 */
            }
        }
    }
}                                                               /* 3979 */

/* 3996 -- arm a ramp.  Applies AlphaStart immediately, so the first frame does
 * not show the model's previous alpha. */
void EffectModelAlphaChangeReq(int Id, int AlphaStart, int AlphaEnd, int Time)
{
    SLL_CELL *pCell;
    MODEL_ALPHA_CAHNGE_DATA *pData;

    for (pCell = SingleLinkListBeginCell(&ModelAlphaChangeList); /* 3998 */
         pCell != (SLL_CELL *)nullptr;
         pCell = SingleLinkListNextCell(pCell))
    {
        pData = (MODEL_ALPHA_CAHNGE_DATA *)SingleLinkListCellBodyPtr(pCell);

        if (pData->Id == Id)                                    /* 4001 */
        {
            pData->AlphaStart = AlphaStart;                     /* 4003 */
            pData->AlphaEnd   = AlphaEnd;                       /* 4004 */
            pData->Time       = Time;                           /* 4005 */
            pData->Counter    = 0;                              /* 4006 */

            ManmdlSetAlpha(pData->pSgdTop, (u_char)pData->AlphaStart); /* 4007 */
        }
    }
}                                                               /* 4008 */

/* ==========================================================================
 *  Lantern -- the flame
 *
 *  Counter runs 0 .. 2*Time and the three getters read it as two halves, so
 *  the flame swells over the first half and shrinks back over the second.
 *  FlameCounter is an independent 0..149 cycle for the texture animation, and
 *  both are seeded at a random phase so a room full of lanterns does not
 *  flicker in step.
 * ======================================================================== */

/* 4026 */
static void EffectTourouFreaInit(void)
{
    SingleLinkListInit(&TourouFreaList, sizeof(TOUROU_FREA_DATA)); /* 4027 */
}

/* 4037 */
void EffectTourouFreaRegist(void *pHandle, int Id)
{
    TOUROU_FREA_DATA TourouFreaData;

    if (pHandle != nullptr)                                     /* 4039 */
    {
        TourouFreaData.pHandle = pHandle;                       /* 4041 */
        TourouFreaData.Id      = Id;                            /* 4042 */
        TourouFreaData.Time    = 34;                            /* 4043 */

        TourouFreaData.Counter      = (int)EffectGetRandom(0.0f, 34.0f);  /* 4045 */
        TourouFreaData.FlameCounter = (int)EffectGetRandom(0.0f, 149.0f); /* 4046 */

        TourouFreaData.ScaleX = EffectTourouFreaGetScaleX(TourouFreaData.Counter,
                                                          TourouFreaData.Time); /* 4048 */
        TourouFreaData.ScaleY = EffectTourouFreaGetScaleY(TourouFreaData.Counter,
                                                          TourouFreaData.Time); /* 4049 */
        TourouFreaData.Alpha  = EffectTourouFreaGetAlpha(TourouFreaData.Counter,
                                                         TourouFreaData.Time);  /* 4050 */

        SingleLinkListAddEnd(&TourouFreaList, &TourouFreaData);  /* 4058 */
    }
}                                                               /* 4060 */

/* 4066 */
void EffectTourouFreaDelete(int Id)
{
    SLL_CELL *pCell;
    SLL_CELL *pNext;
    TOUROU_FREA_DATA *pData;

    for (pCell = SingleLinkListBeginCell(&TourouFreaList);      /* 4068 */
         pCell != (SLL_CELL *)nullptr;
         pCell = pNext)
    {
        pData = (TOUROU_FREA_DATA *)SingleLinkListCellBodyPtr(pCell);
        pNext = SingleLinkListNextCell(pCell);

        if (pData->Id == Id)                                    /* 4074 */
        {
            SingleLinkListRemove(&TourouFreaList, pCell);       /* 4076 */
        }
    }
}                                                               /* 4078 */

/* 4086 / 4118 -- the flame's two scales.  Both are two straight lines meeting
 * at Counter == AllTime: X swells 0.95 -> 0.61 while Y swells 1.16 -> 1.27,
 * so the flame narrows as it stretches.  A zero AllTime returns 1.0 rather
 * than dividing. */
static float EffectTourouFreaGetScaleX(int Counter, int AllTime)
{
    if (AllTime == 0)                                           /* 4088 */
    {
        return 1.0f;
    }

    if (Counter < AllTime)                                      /* 4093 */
    {
        return ((float)Counter / (float)AllTime) * -0.34f + 0.95f;      /* 4095 */
    }

    return ((float)(Counter - AllTime) / (float)AllTime) * 0.34f + 0.61f; /* 4110 */
}                                                               /* 4112 */

static float EffectTourouFreaGetScaleY(int Counter, int AllTime)
{
    if (AllTime == 0)                                           /* 4120 */
    {
        return 1.0f;
    }

    if (Counter < AllTime)                                      /* 4125 */
    {
        return ((float)Counter / (float)AllTime) * 0.11f + 1.16f;        /* 4127 */
    }

    return ((float)(Counter - AllTime) / (float)AllTime) * -0.11f + 1.27f; /* 4142 */
}                                                               /* 4144 */

/* 4150 -- and its alpha, 68 -> 86 and back.  Note the fallback here is 0x80
 * (128), not 1.0's analogue -- a zero AllTime leaves the flame at full alpha. */
static int EffectTourouFreaGetAlpha(int Counter, int AllTime)
{
    if (AllTime == 0)                                           /* 4152 */
    {
        return 128;
    }

    if (Counter < AllTime)                                      /* 4157 */
    {
        return (int)(((float)Counter / (float)AllTime) * 18.0f + 68.0f); /* 4159 */
    }

    return (int)(((float)(Counter - AllTime) / (float)AllTime) * -18.0f + 86.0f); /* 4174 */
}                                                               /* 4176 */

/* 4182 -- step one flame.  Counter wraps at 2*Time, FlameCounter at 150. */
void EffectTourouFreaExecOne(TOUROU_FREA_DATA *pData)
{
    if (pData != (TOUROU_FREA_DATA *)nullptr)                   /* 4184 */
    {
        pData->Counter++;                                       /* 4186 */

        if (pData->Counter >= pData->Time * 2)                  /* 4187 */
        {
            pData->Counter = 0;                                 /* 4189 */
        }

        pData->FlameCounter++;                                  /* 4192 */

        if (pData->FlameCounter > 149)                          /* 4193 */
        {
            pData->FlameCounter = 0;                            /* 4195 */
        }

        pData->ScaleX = EffectTourouFreaGetScaleX(pData->Counter, pData->Time); /* 4198 */
        pData->ScaleY = EffectTourouFreaGetScaleY(pData->Counter, pData->Time); /* 4199 */
        pData->Alpha  = EffectTourouFreaGetAlpha(pData->Counter, pData->Time);  /* 4200 */
    }
}                                                               /* 4202 */

/* 4208 -- find a flame by its model handle. */
TOUROU_FREA_DATA *EffectTourouFreaGetDataPtr(void *pHandle)
{
    SLL_CELL *pCell;
    TOUROU_FREA_DATA *pData;

    for (pCell = SingleLinkListBeginCell(&TourouFreaList);      /* 4210 */
         pCell != (SLL_CELL *)nullptr;
         pCell = SingleLinkListNextCell(pCell))
    {
        pData = (TOUROU_FREA_DATA *)SingleLinkListCellBodyPtr(pCell);

        if (pData->pHandle == pHandle)                          /* 4214 */
        {
            return pData;                                       /* 4216 */
        }
    }

    return (TOUROU_FREA_DATA *)nullptr;                         /* 4222 */
}                                                               /* 4224 */

/* ==========================================================================
 *  Lantern -- the base
 *
 *  The same two-half cycle driving a colour rather than a scale.  Unlike the
 *  flame, whose Time is the constant 34, the base re-randomises Time to
 *  3..37 at the end of every cycle, so successive pulses are not the same
 *  length.
 * ======================================================================== */

/* 4238 */
static void EffectTourouBaseInit(void)
{
    SingleLinkListInit(&TourouBaseList, sizeof(TOUROU_BASE_DATA)); /* 4239 */
}

/* 4249 -- alpha is the fixed 0xba; only rgb pulses. */
void EffectTourouBaseRegist(void *pSgdTop, int Id)
{
    TOUROU_BASE_DATA TourouBaseData;

    if (pSgdTop != nullptr)                                     /* 4251 */
    {
        TourouBaseData.pSgdTop = pSgdTop;                       /* 4253 */
        TourouBaseData.Id      = Id;                            /* 4254 */

        TourouBaseData.Time    = (int)EffectGetRandom(3.0f, 37.0f);      /* 4256 */
        TourouBaseData.Counter =
            (int)EffectGetRandom(0.0f, (float)TourouBaseData.Time);      /* 4257 */

        EffectTourouBaseGetColor(TourouBaseData.Color,
                                 TourouBaseData.Counter,
                                 TourouBaseData.Time);          /* 4259 */
        TourouBaseData.Color[3] = 0xba;                         /* 4260 */

        SingleLinkListAddEnd(&TourouBaseList, &TourouBaseData);  /* 4272 */
    }
}                                                               /* 4274 */

/* 4280 */
void EffectTourouBaseDelete(int Id)
{
    SLL_CELL *pCell;
    SLL_CELL *pNext;
    TOUROU_BASE_DATA *pData;

    for (pCell = SingleLinkListBeginCell(&TourouBaseList);      /* 4282 */
         pCell != (SLL_CELL *)nullptr;
         pCell = pNext)
    {
        pData = (TOUROU_BASE_DATA *)SingleLinkListCellBodyPtr(pCell);
        pNext = SingleLinkListNextCell(pCell);

        if (pData->Id == Id)                                    /* 4288 */
        {
            SingleLinkListRemove(&TourouBaseList, pCell);       /* 4290 */
        }
    }
}                                                               /* 4292 */

/* 4300 -- the base's colour, 0x46/0x17/0x1c up to 0x61/0x2b/0x28 and back.
 *
 * The ROM holds the two endpoints in local ColorMax[4] / ColorMin[4] arrays,
 * but GCC propagated every element into a register and neither array survives
 * into the code -- which is why the constants appear inline here.  Alpha is
 * untouched: only entries 0..2 are written. */
static void EffectTourouBaseGetColor(int *Color, int Counter, int AllTime)
{
    int   Base0;
    int   Base1;
    int   Base2;
    int   Cnt;
    float Step0;
    float Step1;
    float Step2;
    float t;

    if (Counter < AllTime)                                      /* 4302 */
    {
        Base0 = 0x46;  Base1 = 0x17;  Base2 = 0x1c;
        Step0 = 27.0f; Step1 = 20.0f; Step2 = 12.0f;
        Cnt   = Counter;                                        /* 4310 */
    }
    else
    {
        Base0 = 0x61;  Base1 = 0x2b;  Base2 = 0x28;
        Step0 = -27.0f; Step1 = -20.0f; Step2 = -12.0f;
        Cnt   = Counter - AllTime;                              /* 4318 */
    }

    t = (float)Cnt / (float)AllTime;                            /* 4321 */

    Color[0] = (int)(t * Step0 + (float)Base0);                 /* 4323 */
    Color[1] = (int)(t * Step1 + (float)Base1);                 /* 4324 */
    Color[2] = (int)(t * Step2 + (float)Base2);                 /* 4325 */
}                                                               /* 4326 */

/* 4334 -- step one base.  At the end of a cycle Time is re-drawn, so the next
 * pulse is a different length. */
void EffectTourouBaseExecOne(TOUROU_BASE_DATA *pData)
{
    if (pData != (TOUROU_BASE_DATA *)nullptr)                   /* 4336 */
    {
        pData->Counter++;                                       /* 4338 */

        if (pData->Counter >= pData->Time * 2)                  /* 4339 */
        {
            pData->Counter = 0;                                 /* 4341 */
            pData->Time    = (int)EffectGetRandom(3.0f, 37.0f); /* 4342 */
        }

        EffectTourouBaseGetColor(pData->Color, pData->Counter, pData->Time); /* 4345 */
    }
}                                                               /* 4355 */

/* 4361 -- find a base by its model pointer. */
TOUROU_BASE_DATA *EffectTourouBaseGetDataPtr(void *pSgdTop)
{
    SLL_CELL *pCell;
    TOUROU_BASE_DATA *pData;

    for (pCell = SingleLinkListBeginCell(&TourouBaseList);      /* 4363 */
         pCell != (SLL_CELL *)nullptr;
         pCell = SingleLinkListNextCell(pCell))
    {
        pData = (TOUROU_BASE_DATA *)SingleLinkListCellBodyPtr(pCell);

        if (pData->pSgdTop == pSgdTop)                          /* 4367 */
        {
            return pData;                                       /* 4369 */
        }
    }

    return (TOUROU_BASE_DATA *)nullptr;                         /* 4375 */
}                                                               /* 4377 */
