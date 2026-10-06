/* ==========================================================================
 *  graphics/effect/effect_obj.h
 *
 *  Placed-object effects (effect_obj.o, .text 0x00149d40..0x00151cd8).
 *
 *  Seven sub-systems share the file, all attached to a model a room registers
 *  from its furniture list rather than to a free-standing particle:
 *
 *    parts deform    the 17x17 vertex grid that ripples, bounces or dissolves
 *                    a model -- CallPartsDeform2..6 arm it, SetPartsDeform()
 *                    draws it, and the PartsDeform*CalcWave* family are the
 *                    per-vertex displacement kernels;
 *    lens flare      SetRenzFlare() / SetStarRay();
 *    light shafts    EffectLightComeIn*, with three authored vertex-colour
 *                    variants (normal / f607 / f609);
 *    water flow      EffectWaterFlow*, eight move types cycling A->B->C;
 *    camera flash    EffectCameraFlash*;
 *    model alpha     EffectModelAlphaChange*, a per-model alpha ramp keyed by
 *                    label so the event macros can drive it;
 *    lantern         EffectTourouFrea* (the flame) and EffectTourouBase*.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRAPHICS_EFFECT_EFFECT_OBJ_H
#define _GRAPHICS_EFFECT_EFFECT_OBJ_H

#include "eetypes.h"

#include "effect.h"                             /* EFFECT_CONT */
#include "../graph3d/ctl/fixed_array.h"
#include "../../common/SingleLinkList.h"
#include "../../sdk/libvu0.h"
#include "../../miopan/rendering/miopan_renderer.h"  /* GetViewBounds */

/* ==========================================================================
 *  Parts deform
 * ======================================================================== */

/* One vertex of the deform grid.  `r`/`add` are its phase and per-frame phase
 * step, `h` its amplitude; x/y/z is the displaced position and ox/oy/oz the
 * rest position the kernels displace from. */
typedef struct                          /* 0x28 */
{
    /* 0x00 */ float r;
    /* 0x04 */ float add;
    /* 0x08 */ float h;
    /* 0x0c */ float x;
    /* 0x10 */ float y;
    /* 0x14 */ float z;
    /* 0x18 */ float ox;
    /* 0x1c */ float oy;
    /* 0x20 */ float oz;
    /* 0x24 */ float lng;
} EFFPOS;

/* The grid is 17 x 17 -- which is also the length of the two authored alpha
 * maps pdeform_alpha1/2 in .rodata. */
#define PDEFORM_VERTEX_W    17
#define PDEFORM_VERTEX_H    17
#define PDEFORM_VERTEX_MAX  (PDEFORM_VERTEX_W * PDEFORM_VERTEX_H)   /* 289 */

/* One deform work slot.  Eight exist (`efi`), which is the hard limit on how
 * many models can be deforming at once. */
typedef struct                          /* 0x2d30 */
{
    /* 0x0000 */ fixed_array<EFFPOS, PDEFORM_VERTEX_MAX> ep;
    /* 0x2d28 */ short use;
    /* 0x2d2a */ short pass;
    /* 0x2d2c */ float r;
} EFFINFO2;

/* The two wave-parameter pairs a deform kernel is driven by. */
typedef struct                          /* 0x10 */
{
    /* 0x0 */ float pr11;
    /* 0x4 */ float pr12;
    /* 0x8 */ float pr21;
    /* 0xc */ float pr22;
} PDEFORM_PARA;

/* The motion-blur trail a deform can leave: ten slots, each on/off with its
 * own flag, over one in/keep/out ramp. */
typedef struct                          /* 0x6c */
{
    /* 0x00 */ fixed_array<int, 10> on;
    /* 0x28 */ fixed_array<int, 10> flg;
    /* 0x50 */ u_int flow;
    /* 0x54 */ u_int cnt;
    /* 0x58 */ u_int in;
    /* 0x5c */ u_int keep;
    /* 0x60 */ u_int out;
    /* 0x64 */ u_int max;
    /* 0x68 */ float *vol;
} EFF_PARTSBLUR;

/* ==========================================================================
 *  Light shafts
 * ======================================================================== */

enum EFF_LIGHT_COMEIN_ALPHA_MODE
{
    EFF_LIGHT_COMEIN_ALPHA_CALC_DIRECTION = 0,
    EFF_LIGHT_COMEIN_ALPHA_CALC_DISTANCE  = 1
};

enum EFF_LIGHT_COMEIN_TYPE
{
    EFF_LIGHT_COMEIN_TYPE_NORMAL = 0,
    EFF_LIGHT_COMEIN_TYPE_F607   = 1,
    EFF_LIGHT_COMEIN_TYPE_F609   = 2
};

/* TotalS/TotalT are the accumulated texture scroll, wrapped by
 * EffectLightComeInAddSTScrollCtrl(). */
typedef struct                          /* 0x18 */
{
    /* 0x00 */ void *pSgdTop;
    /* 0x04 */ float TotalS;
    /* 0x08 */ float TotalT;
    /* 0x0c */ int   MapBuffId;
    /* 0x10 */ int   AlphaMax;
    /* 0x14 */ int   Type;
} LIGHT_COME_IN_DATA;

typedef struct                          /* 0x10 */
{
    /* 0x0 */ SINGLE_LINK_LIST LightList;
} LIGHT_COME_IN_CTRL;

/* ==========================================================================
 *  Water flow
 * ======================================================================== */

enum EFF_WATER_FLOW_MOVE_TYPE
{
    EFF_WATER_MOVE_RIVER0           = 0,
    EFF_WATER_MOVE_RIVER1           = 1,
    EFF_WATER_MOVE_LAKE0            = 2,
    EFF_WATER_MOVE_LAKE1            = 3,
    EFF_WATER_MOVE_RIVER_MINAKAMI0  = 4,
    EFF_WATER_MOVE_RIVER_MINAKAMI1  = 5,
    EFF_WATER_MOVE_LAKE_FUKAMICHI0  = 6,
    EFF_WATER_MOVE_LAKE_FUKAMICHI1  = 7
};

/* The flow cycles A -> A_TO_B -> B -> B_TO_C -> C -> C_TO_A, so a surface
 * never repeats exactly; MoveDataNo is which leg it is on. */
enum EFF_WATER_FLOW_MOVE_STATUS
{
    EFF_WATER_MOVE_TYPE_A      = 0,
    EFF_WATER_MOVE_TYPE_A_TO_B = 1,
    EFF_WATER_MOVE_TYPE_B      = 2,
    EFF_WATER_MOVE_TYPE_B_TO_C = 3,
    EFF_WATER_MOVE_TYPE_C      = 4,
    EFF_WATER_MOVE_TYPE_C_TO_A = 5
};

typedef struct                          /* 0x1c */
{
    /* 0x00 */ void *pSgdTop;
    /* 0x04 */ float TotalS;
    /* 0x08 */ float TotalT;
    /* 0x0c */ int   Id;
    /* 0x10 */ int   MoveType;
    /* 0x14 */ int   Count;
    /* 0x18 */ int   MoveDataNo;
} WATER_FLOW_DATA;

/* ==========================================================================
 *  Model alpha ramp
 * ======================================================================== */

/* NOTE the ROM's own spelling -- CAHNGE, not CHANGE.  Kept, because it is what
 * types.txt carries and the functions around it are spelled correctly.
 *
 * Id is the object's label; EffectModelAlphaChangeDeleteGroup() matches on
 * Id / 1000, so the thousands digit is a group number. */
typedef struct                          /* 0x18 */
{
    /* 0x00 */ void *pSgdTop;
    /* 0x04 */ int   Id;
    /* 0x08 */ int   AlphaStart;
    /* 0x0c */ int   AlphaEnd;
    /* 0x10 */ int   Time;
    /* 0x14 */ int   Counter;
} MODEL_ALPHA_CAHNGE_DATA;

/* ==========================================================================
 *  Lantern
 * ======================================================================== */

/* The flame.  Counter runs 0 .. 2*Time and the scale/alpha getters read it as
 * two halves, so the flame breathes up and back down; FlameCounter is a
 * separate 0..149 cycle for the texture animation. */
typedef struct                          /* 0x20 */
{
    /* 0x00 */ void *pHandle;
    /* 0x04 */ int   Id;
    /* 0x08 */ float ScaleX;
    /* 0x0c */ float ScaleY;
    /* 0x10 */ int   Alpha;
    /* 0x14 */ int   Time;
    /* 0x18 */ int   Counter;
    /* 0x1c */ int   FlameCounter;
} TOUROU_FREA_DATA;

/* The base it stands on -- the same two-half cycle driving a colour instead of
 * a scale, and re-randomising Time at the end of each cycle. */
typedef struct                          /* 0x20 */
{
    /* 0x00 */ sceVu0IVECTOR Color;
    /* 0x10 */ void *pSgdTop;
    /* 0x14 */ int   Id;
    /* 0x18 */ int   Time;
    /* 0x1c */ int   Counter;
} TOUROU_BASE_DATA;

/* ==========================================================================
 *  Exports (all 38 ZERO2.MAP .text symbols)
 * ======================================================================== */

void InitEffectObj(void);
void InitEffectObjEF(void);

/* ---- parts deform -------------------------------------------------------
 * The arming calls.  Every one of them is a single SetEffects_PDEFORM() with
 * the slots the caller does not care about defaulted, so the only real
 * difference between them is which of the four drive pointers and which colour
 * they let through.  `in`/`keep`/`out` are the flow countdown (fl = 4); the
 * pDrive form (fl = 2) hands the alpha over to a float the caller updates.
 *
 * Parameter names are the ROM's own, from the stabs. */
void *CallPartsDeform2(int type, float scale, void *pos,
                       u_int in, u_int keep, u_int out);
void *CallPartsDeform3(int type, float scale, void *pos,
                       u_int in, u_int keep, u_int out, int alp);
void *CallPartsDeform3_2(int type, float sclx, float scly, void *pos,
                         u_int in, u_int keep, u_int out, int alp);
void *CallPartsDeform4(int type, float scale, void *pos, float *vol);
void *CallPartsDeform5(int type, float sclx, float scly, void *pos,
                       float *vol);
void *CallPartsDeform5_2(int type, float sclx, float scly, void *pos,
                         float *vol, float *pSpd, float *pRate);
void *CallPartsDeform6(int type, float scale, void *pos,
                       u_int in, u_int keep, u_int out, int alp,
                       float *pSpd, float *pRate, int r, int g, int b);

void SetPartsDeform(EFFECT_CONT *ec);
int  EffectObjPartsDeformBlurCheck(EFFECT_CONT *ec);
void SetVURand(float x);

/* PORT DEVIATION: the ROM is CalcPartsDeformXYZ(int *, float *) and read the two
   local->clip matrices out of vf4..vf11, where an earlier gra3dVu0ApplyMatrix2()
   had left them.  There is no host VU0 register file, so they are parameters --
   the same deviation gra3dVu0.h:189 documents.  See the definition.

   PORT ADDITION: pNdcZ, which the ROM had no use for.  The GS took this
   vertex's depth from the packet's XYZF2; the host renderer wants it in clip
   space, so the clip-space z/w already computed here is handed back rather
   than recomputed.  NULL when the caller's draw env does not test depth. */
int  CalcPartsDeformXYZ(int *vi, float *vf,
                        float (*matLocalScreen)[4], float (*matLocalClipPolygon)[4],
                        float *pNdcZ);

/* --------------------------------------------------------------------------
 *  PORT DEVIATION -- the frame-buffer sample clamp
 *
 *  Every CalcStqXYZ() caller pins the sample position to the frame it is
 *  sampling before normalising it: stqparam[1] is { 0, 639, 0, 447 }, the
 *  640x448 frame, on all four of them.
 *
 *  On the host that box is too small.  The frustum is widened to fill an
 *  output that is not 4:3 while gra3d's matWorldScreen is deliberately NOT
 *  widened (only the clip matrices get fExtend), so a point at the right edge
 *  of a 16:9 window projects to about x = 706 in these units -- and the ROM's
 *  box pins every sample over the outer ~9% of each side onto a single column
 *  of the capture.  An effect that reaches the edge of the screen freezes into
 *  a streak there, which is what a refraction sampling the wrong scenery looks
 *  like.
 *
 *  MioPan_RendererGetViewBounds() is that visible frame in the same units, and
 *  it is exactly (0,0)-(640,448) on a 4:3 output -- so this reproduces the
 *  ROM's own 639/447 bit-for-bit there, and widens only where the port already
 *  deviates.
 * ------------------------------------------------------------------------ */
static inline void EffectSetScreenSampleClamp(float *pClamp)
{
    float x0, y0, x1, y1;

    MioPan_RendererGetViewBounds(&x0, &y0, &x1, &y1);

    pClamp[0] = x0;
    pClamp[1] = x1 - 1.0f;      /* the ROM's 639: the last pixel, not the edge */
    pClamp[2] = y0;
    pClamp[3] = y1 - 1.0f;      /* the ROM's 447 */
}

/* --------------------------------------------------------------------------
 *  Screen-projected STQ -- effect_obj.h 199/200 and 210/211
 *
 *  The pair of VU0 macro-mode inlines that turn a local-space vertex into the
 *  texture coordinate of the point of the *frame buffer* it lands on.  That is
 *  what makes an effect refract: the caller has already copied the back buffer
 *  into local memory (LocalCopyBtoL) and installed it as the texture, so a mesh
 *  drawn with these coordinates re-displays the screen behind it, warped by
 *  whatever the mesh's geometry does.
 *
 *  Four expansions in the ROM -- one in effect_ene.o (EneDmgLargeHitEffectDisp)
 *  and three in effect_obj.o -- and they always come as a pair: 199/200 loads
 *  stqparam[0..2] into vf8/vf9/vf10 once, 210/211 runs per vertex.  199/200 has
 *  nothing to do on the host (there is no register file to prime), so only the
 *  per-vertex half survives, with the two things the ROM had left in registers
 *  passed explicitly -- exactly the deviation CalcPartsDeformXYZ above and
 *  gra3dVu0.h:189 already carry.
 *
 *  stqparam is the caller's own 3x4 block and is the same in every expansion:
 *
 *      [0] = { XYOFFSET.x - 0.5, XYOFFSET.y - 0.5, 1.0, --- }
 *      [1] = { 0, screen width - 1, 0, screen height - 1 }
 *      [2] = { 1/texture width, 1/texture height, 1.0, 1/16 }
 *
 *  [0].xy takes the GS's 2048-centred origin back off, [1] clamps the result to
 *  the visible screen so a vertex off the side samples the edge rather than
 *  wrapping, and [2] normalises into the texture.  [0].z is a broadcast scale
 *  applied to the vertex's own x/y before the transform -- 1.0 everywhere in
 *  this build.  The final 1/16 scales s, t and q together and so cancels in the
 *  GS's s/q, t/q; it is there to keep the magnitudes in range.
 * ------------------------------------------------------------------------ */
static inline void CalcStqXYZ(int i, const sceVu0FVECTOR *pVtx, sceVu0FVECTOR *pStq,
                              float (*matLocalScreen)[4], float (*stqparam)[4])
{
    const float *v = pVtx[i];
    float       *o = pStq[i];
    float        vScreen[4];
    float        q, x, y;
    int          c;

    /* vmulz.xy vf12,vf12,vf8 -- x and y only; z and w go in untouched. */
    const float vx = v[0] * stqparam[0][2];
    const float vy = v[1] * stqparam[0][2];

    for (c = 0; c < 4; c++)                     /* vmulax / vmadda* / vmaddw   */
        vScreen[c] = matLocalScreen[0][c] * vx
                   + matLocalScreen[1][c] * vy
                   + matLocalScreen[2][c] * v[2]
                   + matLocalScreen[3][c];

    q = 1.0f / vScreen[3];                      /* vdiv Q,vf0w,vf13w           */

    x = vScreen[0] * q - stqparam[0][0];        /* vmulq.xyz then vsub.xy      */
    y = vScreen[1] * q - stqparam[0][1];

    if (x < stqparam[1][0]) { x = stqparam[1][0]; }   /* vmaxx.x    / vminibcy */
    if (x > stqparam[1][1]) { x = stqparam[1][1]; }
    if (y < stqparam[1][2]) { y = stqparam[1][2]; }   /* vmaxz.y    / vminibcw */
    if (y > stqparam[1][3]) { y = stqparam[1][3]; }

    o[0] = x * (stqparam[2][0] * q) * stqparam[2][3]; /* vmulq.xy / vmul / vmulw */
    o[1] = y * (stqparam[2][1] * q) * stqparam[2][3];
    o[2] =      stqparam[2][2] * q  * stqparam[2][3]; /* vmulq.z   / vmulw     */

    /* Nothing after vmaddx touched w, and sqc2 writes the whole quadword, so
       the fourth word keeps the pre-divide w.  No caller reads it -- the packet
       builders take only s/t/q -- but the store is the ROM's. */
    o[3] = vScreen[3];
}

/* ---- lens flare ---------------------------------------------------------
 *
 * One ghost of the flare.  `type` is the effect-sprite number SetEffSQTex()
 * draws, `lscl` its position along the light-to-screen-centre line and `tscl`
 * its size in tens of pixels; rgba[3] is scaled by the run-time brightness.
 * SetRenzFlare()'s nine-entry table is a local, not a file-scope one. */
typedef struct                          /* types.txt 0x10 */
{
    /* 0x0 */ u_int  type;
    /* 0x4 */ u_char rgba[4];
    /* 0x8 */ float  lscl;
    /* 0xc */ float  tscl;
} EFRENZ;

int  GetCornHitCheck(float *bpos, float power);
int  GetCornHitCheck2(float *bpos, float power, float *rrate, float *lrate);
void SetRenzFlare(EFFECT_CONT *ec);

/* ---- light shafts ------------------------------------------------------- */
void EffectLightComeInRegist(void *pSgdTop, int MapBuffId, int Type);
void EffectLightComeInDeleteMapBuffId(int MapBuffId);
void EffectLightComeInExecOne(LIGHT_COME_IN_DATA *pData, int AlphaCalcMode);
LIGHT_COME_IN_DATA *EffectLightComeInGetDataPtr(void *pSgdTop);

/* ---- water flow --------------------------------------------------------- */
void EffectWaterFlowRegist(void *pSgdTop, int Id, int MoveType);
void EffectWaterFlowDelete(int Id);
void EffectWaterFlowExec(void);

/* ---- camera flash ------------------------------------------------------- */
void *EffectCameraFlashReq(void);
void  SetCameraFlash(EFFECT_CONT *ec);

/* ---- model alpha ramp --------------------------------------------------- */
void EffectModelAlphaChangeRegist(void *pSgdTop, int Id);
void EffectModelAlphaChangeDelete(int Id);
void EffectModelAlphaChangeDeleteGroup(int Id);
void EffectModelAlphaChangeExec(void);
void EffectModelAlphaChangeReq(int Id, int AlphaStart, int AlphaEnd, int Time);

/* ---- lantern ------------------------------------------------------------ */
void EffectTourouFreaRegist(void *pHandle, int Id);
void EffectTourouFreaDelete(int Id);
void EffectTourouFreaExecOne(TOUROU_FREA_DATA *pData);
TOUROU_FREA_DATA *EffectTourouFreaGetDataPtr(void *pHandle);

void EffectTourouBaseRegist(void *pSgdTop, int Id);
void EffectTourouBaseDelete(int Id);
void EffectTourouBaseExecOne(TOUROU_BASE_DATA *pData);
TOUROU_BASE_DATA *EffectTourouBaseGetDataPtr(void *pSgdTop);

#endif /* _GRAPHICS_EFFECT_EFFECT_OBJ_H */
