/* ==========================================================================
 *  graphics/effect/effect.h
 *
 *  Main effect-system interface: the EFFECT_CONT control tables, the
 *  SetEffects_* front doors, the EFF_WRK switch accessors, the
 *  screen-effect parameter sets and the private effect heap.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRAPHICS_EFFECT_EFFECT_H
#define _GRAPHICS_EFFECT_EFFECT_H

#include "eetypes.h"
#include "../graph2d/g2d_draw.h"    /* Q_WORDDATA */
#include "../graph3d/ctl/fixed_array.h"

/* One entry of the effect system's control table.  The per-effect handlers
 * keep their own state in `dat` (which is why it is a raw quadword union) and
 * share the flow / in / keep / out countdown fields.
 *
 * enemy.c only ever reads efcnt[12].dat.uc8[0], the "an all-screen effect owns
 * the frame" gate that suppresses the per-ghost aura. */
typedef struct                      /* 0x70 */
{
    /* 0x00 */ Q_WORDDATA dat;
    /* 0x10 */ float  Pos[4];
    /* 0x20 */ void  *pnt[6];
    /* 0x38 */ fixed_array<float, 3> fw;
    /* 0x44 */ u_int  z;
    /* 0x48 */ u_int  flow;
    /* 0x4c */ u_int  cnt;
    /* 0x50 */ u_int  in;
    /* 0x54 */ u_int  keep;
    /* 0x58 */ u_int  out;
    /* 0x5c */ u_int  max;
    /* 0x60 */ u_char r;
    /* 0x61 */ u_char g;
    /* 0x62 */ u_char b;
    /* 0x63 */ u_char a;
} EFFECT_CONT;

/* --------------------------------------------------------------------------
 *  The shared effect particle.                                types.txt 0x50
 *
 *  Every particle system in the effect layer -- torch flame, sparks, ghost
 *  damage motes -- uses this record, and draw_distortion_particles2()
 *  (effect_oth.o) is the one drawing routine they all share.  Its home header
 *  is not recoverable from the debug info (no object carries a SOL for it), so
 *  it lives here, alongside EFFECT_CONT, as the most-included effect header.
 *
 *  `position` and `velocity` are quadwords so the VU0 vector helpers can be
 *  applied to them in place; `alp_step` is written by every producer and read
 *  by none in this build.
 * ------------------------------------------------------------------------ */
typedef struct                      /* 0x50 */
{
    /* 0x00 */ float position[4];
    /* 0x10 */ float color[4];      /* rgb 0..255, a is the live alpha */
    /* 0x20 */ float velocity[4];
    /* 0x30 */ float acceleration[4];
    /* 0x40 */ float alp_step;
    /* 0x44 */ int   lifetime;      /* counts down; 0 = dead */
    /* 0x48 */ int   BaseLifeTime;  /* what it started at */
    /* 0x4c */ float Scale;
} PARTICLE;

/* One canned whole-screen look; six live in effect.c and the story script
 * picks one with EffectSetScreenEffectNo(). */
typedef struct                      /* 0x60, types.txt */
{
    /* 0x00 */ int Z_Dep;
    /* 0x04 */ int Dither;
    /* 0x08 */ int DitherSpeed;
    /* 0x0c */ int DitherAlpha;
    /* 0x10 */ int DitherAlphaMax;
    /* 0x14 */ int DitherColorMax;
    /* 0x18 */ int Blur;
    /* 0x1c */ int BlurAlpha;
    /* 0x20 */ int BlurScale;
    /* 0x24 */ int BlrrRot;
    /* 0x28 */ int Deform;
    /* 0x2c */ int DeformRate;
    /* 0x30 */ int Focus;
    /* 0x34 */ int ColorFilter;
    /* 0x38 */ int BlackFilter;
    /* 0x3c */ int Contrast;
    /* 0x40 */ int ContrastColor;
    /* 0x44 */ int ContrastAlpha;
    /* 0x48 */ int NegaColor;
    /* 0x4c */ int NegaAlpha;
    /* 0x50 */ int NegaAlpha2;
    /* 0x54 */ int FadeFrame;
    /* 0x58 */ int FadeFrameAlpha;
    /* 0x5c */ int Overlap;
} SCREEN_EFFECT_PARAMETER;

/* ---- the screen-effect state block effect_scr.c's machines share -------- */

typedef struct                      /* 0x14 */
{
    /* 0x00 */ u_char sw;
    /* 0x01 */ u_char type;
    /* 0x02 */ u_char alp;
    /* 0x04 */ float  scale;
    /* 0x08 */ int    rot;
    /* 0x0c */ float  x;
    /* 0x10 */ float  y;
} BLUR_STR;

typedef struct                      /* 0x4 */
{
    /* 0x0 */ u_char sw;
    /* 0x1 */ u_char type;
    /* 0x2 */ u_char col;
    /* 0x3 */ u_char alp;
} CONTRAST_STR;

typedef struct                      /* 0x2 */
{
    /* 0x0 */ u_char sw;
    /* 0x1 */ u_char alp;
} FFRAME_STR;

typedef struct                      /* 0x10 */
{
    /* 0x0 */ u_char sw;
    /* 0x1 */ u_char type;
    /* 0x4 */ float  spd;
    /* 0x8 */ float  alp;
    /* 0xc */ u_char amax;
    /* 0xd */ u_char cmax;
} DITHER_STR;

typedef struct                      /* 0x3 */
{
    /* 0x0 */ u_char sw;
    /* 0x1 */ u_char type;
    /* 0x2 */ u_char rate;
} DEFORM_STR;

typedef struct                      /* 0x4 */
{
    /* 0x0 */ u_char sw;
    /* 0x1 */ u_char col;
    /* 0x2 */ u_char alp;
    /* 0x3 */ u_char alp2;
} NEGA_STR;

typedef struct                      /* 0x1 */
{
    /* 0x0 */ u_char sw;
} MONO_STR;

typedef struct                      /* 0x34 */
{
    /* 0x00 */ BLUR_STR     bl;
    /* 0x14 */ CONTRAST_STR cn;
    /* 0x18 */ FFRAME_STR   ff;
    /* 0x1c */ DITHER_STR   dt;
    /* 0x2c */ DEFORM_STR   df;
    /* 0x2f */ NEGA_STR     ng;
    /* 0x33 */ MONO_STR     mn;
} SBTSET;

/* The four control tables (effect.o .data 0x2dbea0 onwards): fixed-slot
 * screen effects and the free-slot modeled bank, two banks of each. */
#define EFCNT_MAX 64
extern fixed_array<EFFECT_CONT, EFCNT_MAX> efcnt;
extern fixed_array<EFFECT_CONT, 48>        efcntm;
extern fixed_array<EFFECT_CONT, EFCNT_MAX> efcnt_cnt;
extern fixed_array<EFFECT_CONT, 48>        efcntm_cnt;

extern SBTSET msbtset;

/* Uniform float in [min, max).  Inline in the original -- the decompiler
 * reports it as "inlined from effect.h" at line 217 across the whole effect
 * system, and the (max - min) / + min pair is constant-folded at every call.
 *
 * The ROM divides by lit4 2147483520.0f, GCC 2.96-ee's truncation of
 * (float)RAND_MAX for the EE's RAND_MAX of 0x7fffffff.  MioPan_Rand() gives
 * back that 31-bit range on the host, so MIOPAN_RAND_MAXF is the ROM's own
 * divisor rather than a substitute for it. */
static inline float EffectGetRandom(float min, float max)           /* 217 */
{
    return (max - min) * ((float)MioPan_Rand() / MIOPAN_RAND_MAXF) + min;
}

#ifdef __cplusplus
extern "C" {
#endif

void InitEffects(void);
void InitEffectsEF(void);
EFFECT_CONT *EffectGetBufferTopAdrs(void);

/* Re-arms the blur that SetBlurOff() (the boot state) suppresses. */
void EffectEndSet(void);
void SetBlurOff(void);

/* ---- the effect request front doors --------------------------------------
 *
 * PORT DEVIATION.  The ROM has ONE entry point, `void *SetEffects(int id,
 * int fl, ...)`, which looks the control record up by id and then hands the
 * argument tail to a per-effect SetEffects_* unpacker that reads it through a
 * va_list.  That does not survive the move to a 64-bit host: a pointer slot
 * the caller filled with a bare `0` goes out as a 4-byte int and comes back
 * through va_arg(ap, void *) as eight, and the upper half is whatever the
 * register or stack word already held -- so the handler stores a wild pointer
 * and the effect faults the first time the draw pass dereferences it.  The
 * u_char-into-a-double-slot mismatches are the same hazard by another route.
 *
 * So the unpackers are the entry points now: one typed function per effect id,
 * each carrying the gate and the id -> record lookup SetEffects() used to do
 * in front of them, and each returning the EFFECT_CONT the ROM returned.  The
 * names, the argument order and the stores are unchanged -- only the way the
 * arguments arrive is.  A null pointer slot is now spelled `nullptr` and the
 * compiler checks the rest.
 *
 * The `fl` byte is the ROM's: bit 0 "one frame only", bit 1 the modeled bank's
 * marker, bit 2 arms the in/keep/out envelope where one exists.  The ids are
 * in the comment on each declaration. */
void *SetEffects_Z_DEP(int fl);                                     /* 0x01 */
void *SetEffects_DITHER(int fl, int type, float alp, float spd,
                        int amax, int cmax,
                        u_int in, u_int keep, u_int out);           /* 0x02 */
void *SetEffects_BLUR(int type, int fl, void *pAlpha,
                      u_int scale, u_int rot, float cx, float cy);  /* 0x03..05 */
void *SetEffects_DEFORM(int fl, int type, int rate,
                        u_int in, u_int keep, u_int out);           /* 0x06 */
void *SetEffects_FOCUS(int fl, int power);                          /* 0x07 */
void *SetEffects_OVERLAP(int fl, int alpha);                        /* 0x08 */
void *SetEffects_FADEFRAME(int fl, int alpha, u_int pri);           /* 0x09 */
void *SetEffects_RENZFLARE(int fl, int type,
                           void *pPos, void *pRot);                 /* 0x0a */
void *SetEffects_BLACKFILTER(int fl, int alpha);                    /* 0x0b */
void *SetEffects_NEGA(int fl, int col, int alpha,
                      u_int in, u_int keep, u_int out,
                      void *pAlpha2);                               /* 0x0c */
void *SetEffects_NCONTRAST(int id, int fl, int col, int alpha);     /* 0x0d..0f */
void *SetEffects_ENEDMG(int type, int fl, int type2);               /* 0x10/0x11 */
void *SetEffects_HALO(int fl, int type, void *pPos,
                      int r, int g, int b, float scale, int alpha); /* 0x12 */
void *SetEffects_FIRE(int fl, int flow, void *pPos, int size,
                      int r, int g, float scale, int b, int alpha,
                      int type, float rate);                        /* 0x15 */
void *SetEffects_TORCH(int fl, int type, void *pPos,
                       void *pAlpha, void *pScale);                 /* 0x16 */
void *SetEffects_PDEFORM(int fl, int type, u_int max, float sclx, float scly,
                         void *pWrk, u_int in, u_int keep, u_int out,
                         void *pDrive, void *pSpeed, void *pWave, void *pXYZ,
                         int r, int g, int b);                      /* 0x18 */
void *SetEffects_ENEFIRE(int fl, int type, void *pPos, void *pMtx,
                         void *pCol, void *pSize, u_int alpha,
                         void *pRate);                              /* 0x19 */
void *SetEffects_DUST(int fl, void *pPos);                          /* 0x1a */
void *SetEffects_WATERDROP(int fl, void *pTexture, int type, float scale,
                           u_int count, int a, int b, int c);       /* 0x1b */
void *SetEffects_TORCH2(int fl, int type, void *pPos, int depth);   /* 0x1c */
void *SetEffects_SPARK(int fl, void *pPos, int type,
                       float fw0, float fw1);                       /* 0x1d */
void *SetEffects_TORCH_FREA(int fl, void *pPos, int r, int g, int b,
                            float fw0, float fw1, int alpha);       /* 0x1e */
void *SetEffects_CAMERA_FLASH(int fl, void *pPosTex, const float *pParam,
                              int ReqFlg);                          /* 0x1f */
void *SetEffects_MANY_CANDLE(int fl, void *pPos, void *pData,
                             u_int DataNum, u_int Id);              /* 0x20 */
void *SetEffects_HAZE(int fl, void *pPos, void *pRot, u_int Id,
                      u_int mode, void *pAlpha);                    /* 0x21 */
void *SetEffects_DOOR_SEAL(int fl, void *pPos, float size);         /* 0x22 */
void *SetEffects_ENEIN(int fl, int type, int alpha, float rate, void *pWrk,
                       u_int in, u_int keep, u_int out);            /* 0x23 */

/* The one place an effect id is still chosen at run time: puzzle.c asks
 * IgEffectStoryMainContrastTypeGet() which of the story contrast filters is
 * running and gets back 0xd/0xe/0xf (contrast) or 0xc (nega) -- two different
 * handlers off one call site.  See the definition for what the nega arm does
 * about the alpha pointer the ROM never pushed. */
void *SetEffectsStoryContrast(int id, int fl, int col, int alpha);

void  ResetEffects(void *p);
void  CutEffects(int id);
int   EffectDitherIsSet(void);
int   EffectExecCheck(void *pEffRet, int EffectType);

/* The per-frame pass; `no` is the caller's slot in the frame graph
 * (5 main, 7 blur source, 8 final 2D). */
void EffectControl(int no);

/* Steps a record's in/keep/out envelope. */
void EffInKeepOutFlowCtrl(EFFECT_CONT *ec);

/* Player movement makes nearby candle flames gutter. */
void EffectCandleFlameYuramekiCtrl(float *PlayerNowPos, float *PlayerOldPos);

/* The photo phase's effect pass (the 3D set, no screen filters). */
void EffectPhotoPhase(void);

/* Halve a 32-bit image's rows in place. */
void EffImageHalf32(u_int *pImage, u_int Width, u_int Height);

/* ---- the whole-screen effect selection ---------------------------------- */
void EffScreenEffectStatusSet(int Status);
void EffectSetScreenEffectNo(int EffectNo);
int  EffectGetScreenEffectNo(void);
SCREEN_EFFECT_PARAMETER *EffectGetScreenEffectParamPtr(int EffectNo);
SCREEN_EFFECT_PARAMETER *EffectGetNowScreenEffectParamPtr(void);

/* ---- the EFF_WRK switch block ------------------------------------------- */
void SetDebugMenuSwitch(int sw);
int  GetDebugMenuSwitch(void);
int  EffWrkDispFlgGet(void);
void EffWrkDispFlgSet(int flg);
int  EffWrkMonochroModeGet(void);
void EffWrkMonochroModeSet(int flg);
int  EffWrkStopFlgGet(void);
void EffWrkStopFlgSet(int flg);
int  EffWrkEffectBankGet(void);
void EffWrkEffectBankSet(int flg);
int  EffWrkBlurOffGet(void);
void EffWrkBlurOffSet(int flg);
int  EffWrkDithOffGet(void);
void EffWrkDithOffSet(int flg);
int  EffWrkFilamentOffGet(void);
void EffWrkFilamentOffSet(int flg);

/* Original effect-system video-mode flag (effect.o:sdata 0x3efcc8). */
extern u_char g_bInterlace;

/* ---- effect heap ------------------------------------------------------- *
 * Sub-allocations out of the effect system's private 0x90000 region, which
 * InitEffects() lays down.  Everything the effect modules own -- particle
 * arrays, list cells -- comes from here rather than from the system heap. */
void *EFFECT_MALLOC(int size);
void  EFFECT_FREE(void *block);

/* Entry points other modules' code has always pulled from this header; the
 * bodies live with their owners (effect_scr.c and effect_oth.c).
 *
 * IgEffectInit / IgEffectMain / IgEffectRenzFlareDispFlgSet used to be
 * declared here too; they belong to ingame_effect.o and moved to
 * ingame/ingame_effect.h when that module was reconstructed. */
void BrightnessAdjustmentFilterDraw(void);
void InitHeatHaze(void);

#ifdef __cplusplus
}
#endif

#endif /* _GRAPHICS_EFFECT_EFFECT_H */
