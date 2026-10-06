/* ==========================================================================
 *  graphics/effect/effect_ene.h
 *
 *  The ghost-damage effects (effect_ene.o, .text 0x00142020..0x00149d40).
 *
 *  Three separate effects share this file, all raised while the camera is
 *  doing damage to a ghost:
 *
 *    - the "large hit" flash        one of 38 authored ENE_DMG_LARGE_HIT_
 *                                   PARAMETER presets, drawn as a ring of
 *                                   camera-facing quads, with a matching
 *                                   ENE_DMG_BLUR_CONTRAST_PARAMETER driving
 *                                   the screen blur, contrast, camera shake
 *                                   and pad rumble;
 *    - the spirit particles         ENEDMG_P_WRK, a burst of up to 48
 *                                   ENEDMG_PARTICLE_ONE that either disperse
 *                                   or fly home to the equip tray;
 *    - the damage overlay           ENDMG1 / ENDMG2 / ENE_DMG_EFF, the
 *                                   sprite passes drawn over the ghost.
 *
 *  Every struct here is effect_ene.o's own -- no other object file in
 *  ZERO2.MAP names any of them.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRAPHICS_EFFECT_EFFECT_ENE_H
#define _GRAPHICS_EFFECT_EFFECT_ENE_H

#include "eetypes.h"

#include "../graph3d/ctl/fixed_array.h"
#include "../../sdk/libvu0.h"

/* ==========================================================================
 *  The large-hit flash
 * ======================================================================== */

/* One authored preset for the flash.  Colours are 0..255; the geometry fields
 * are fixed-point and EneDmgLargeHitInit() is where each is scaled down, with
 * a *different* divisor per field rather than one shared one:
 *
 *      Size, LastScale      / 100.0f
 *      MoveDist, Distance   /  10.0f
 *      RotVal               (RotVal - 3600) * PI / 180.0f
 *
 * so 971 reads as 9.71 for a Size and as -262.9 degrees for a RotVal.  Do not
 * assume a single scale across the record.
 *
 * The five AlphaBlend fields are the GS ALPHA register's A/B/C/D selectors and
 * its FIX value; EneDmgLargeHitInit() packs them into the 64-bit AlphaBlend the
 * packet carries, at bits 0-1 / 2-3 / 4-5 / 6-7 and 32-39. */
typedef struct                          /* 0x5c */
{
    /* 0x00 */ int CenterRgba[4];       /* colour at the middle of the ring   */
    /* 0x10 */ int OutsideRgba[4];      /* colour at its rim                  */
    /* 0x20 */ int VertexNumW;          /* ring subdivision, across           */
    /* 0x24 */ int VertexNumH;          /* ring subdivision, around           */
    /* 0x28 */ int Size;                /* /100 -> starting scale             */
    /* 0x2c */ int LastScale;           /* /100 -> scale at AllFrame          */
    /* 0x30 */ int AllFrame;            /* lifetime in frames, 1..83          */
    /* 0x34 */ int MoveDist;            /* /10 -> drift along the camera Z    */
    /* 0x38 */ int Distance;            /* /10 -> offset towards the camera   */
    /* 0x3c */ int RotVal;              /* degrees + 3600; 3600 means no spin */
    /* 0x40 */ int CaptureInterval;     /* frames between trailing copies     */
    /* 0x44 */ int CaptureNumber;       /* how many trailing copies           */
    /* 0x48 */ int AlphaBlendA;
    /* 0x4c */ int AlphaBlendB;
    /* 0x50 */ int AlphaBlendC;
    /* 0x54 */ int AlphaBlendD;
    /* 0x58 */ int AlphaBlendFIX;
} ENE_DMG_LARGE_HIT_PARAMETER;

/* The screen-wide half of the same hit: a blur ramp, a contrast ramp, a
 * camera shake and a pad rumble, all keyed off one counter.
 *
 * The ramp is in / keep / out, starting StartFrame frames after the hit.
 * Scales and rotations are thousandths again; colours and alphas are 0..255.
 * BlurOnFlg / ContrastOnFlg / CameraShakeOnFlg / PadVibrateOnFlg gate each
 * quarter independently, so a preset can shake without blurring. */
typedef struct                          /* 0x50 */
{
    /* 0x00 */ int StartFrame;
    /* 0x04 */ int InTime;
    /* 0x08 */ int KeepTime;
    /* 0x0c */ int OutTime;
    /* 0x10 */ int MinBlurScale;
    /* 0x14 */ int MaxBlurScale;
    /* 0x18 */ int MinBlurAlpha;
    /* 0x1c */ int MaxBlurAlpha;
    /* 0x20 */ int MinBlurRot;
    /* 0x24 */ int MaxBlurRot;
    /* 0x28 */ int BlurOnFlg;
    /* 0x2c */ int MinContrastColor;
    /* 0x30 */ int MaxContrastColor;
    /* 0x34 */ int MinContrastAlpha;
    /* 0x38 */ int MaxContrastAlpha;
    /* 0x3c */ int ContrastOnFlg;
    /* 0x40 */ int CameraShakeOnFlg;
    /* 0x44 */ int CameraShakeFrame;
    /* 0x48 */ int PadVibrateOnFlg;
    /* 0x4c */ int PadVibrateFrame;
} ENE_DMG_BLUR_CONTRAST_PARAMETER;

/* The live flash, unpacked from a preset by EneDmgLargeHitInit().
 *
 * AlphaBlend is the GS ALPHA register value.  types.txt types it `long`,
 * which is 8 bytes on the EE and 4 on the host -- but sdk/scetypes.h already
 * resolves that by typedef'ing u_long to uint64_t, so the ROM's own spelling
 * keeps all 64 bits and the FIX field above bit 31 survives. */
typedef struct                          /* 0x60 */
{
    /* 0x00 */ sceVu0FVECTOR CenterPos;
    /* 0x10 */ u_long AlphaBlend;
    /* 0x18 */ float Scale;
    /* 0x1c */ float LastScale;
    /* 0x20 */ float MoveDist;
    /* 0x24 */ float Distance;
    /* 0x28 */ float RotVal;
    /* 0x2c */ u_int VertexNumW;
    /* 0x30 */ u_int VertexNumH;
    /* 0x34 */ u_int NowFrame;
    /* 0x38 */ u_int AllFrame;
    /* 0x3c */ u_int CaptureInterval;
    /* 0x40 */ u_int CaptureNumber;
    /* 0x44 */ fixed_array<u_char, 4> CenterRgba;
    /* 0x48 */ fixed_array<u_char, 4> OutsideRgba;
    /* 0x4c */ u_int InitCenterAlpha;
    /* 0x50 */ u_int InitOutsideAlpha;
    /* 0x54 */ int Delay;
} ENE_DMG_LARGE_HIT;

/* Two flash slots, so an A/B preset pair can run at once -- every sub-function
 * hit is authored as two presets drawn together. */
typedef struct                          /* 0xd0 */
{
    /* 0x00 */ fixed_array<ENE_DMG_LARGE_HIT, 2> Work;
    /* 0xc0 */ fixed_array<int, 2> Status;
} ENE_DMG_LARGE_HIT_CTRL;

/* ==========================================================================
 *  The spirit particles
 * ======================================================================== */

/* One particle.  It spirals: rpos is its offset from Center, rrad the radius
 * of that spiral and rrot the angle, with racc/rbrk accelerating and braking
 * the radius so a burst blooms and then closes again. */
typedef struct                          /* 0x50 */
{
    /* 0x00 */ sceVu0FVECTOR Center;
    /* 0x10 */ sceVu0FVECTOR rpos;
    /* 0x20 */ float rrad;
    /* 0x24 */ float rrot;
    /* 0x28 */ float racc;
    /* 0x2c */ float rbrk;
    /* 0x30 */ float ralp;
    /* 0x34 */ float rrad_max;
    /* 0x38 */ float cnt_f;
    /* 0x3c */ float cnt_spd;
    /* 0x40 */ float rot_spd;
    /* 0x44 */ int   anm_count;
    /* 0x48 */ float AlphaRate;
} ENEDMG_PARTICLE_ONE;

/* The comet tail behind a particle that is flying home: a ring buffer of the
 * last ten world matrices and positions.  Top is the newest slot. */
typedef struct                          /* 0x330 */
{
    /* 0x000 */ fixed_array<float[4][4], 10> LwMatrix;
    /* 0x280 */ fixed_array<sceVu0FVECTOR, 10> OldPos;
    /* 0x320 */ int NumMax;
    /* 0x324 */ int Num;
    /* 0x328 */ int Top;
} ENEDMG_P_TAIL_WRK;

#define ENEDMG_PARTICLE_MAX 48

/* One burst.  These are heap blocks chained through pNext and walked by
 * EneDmgMain(); a burst whose particles have all landed is unlinked and
 * freed by EneDmgParticleEffectCut().
 *
 * PORT NOTE: pEndPos and pTailWrk are 4 bytes on target and 8 here, so every
 * offset from 0xf20 on drifts.  The offsets below are the ROM's. */
typedef struct _ENEDMG_P_WRK            /* 0xf50 on target */
{
    /* 0x000 */ fixed_array<ENEDMG_PARTICLE_ONE, ENEDMG_PARTICLE_MAX> particle;
    /* 0xf00 */ sceVu0FVECTOR StartPos;
    /* 0xf10 */ sceVu0FVECTOR EndPos;
    /* 0xf20 -- where the particles are being pulled to, refreshed every frame
     * by EneDmgParticleEndPosSet() while the finder is up.  NULL means the
     * burst just disperses. */
    /* 0xf20 */ float (*pEndPos)[4];
    /* 0xf24 */ ENEDMG_P_TAIL_WRK *pTailWrk;
    /* 0xf28 */ int Num;
    /* 0xf2c */ int flow;
    /* 0xf30 */ int cnt;
    /* 0xf34 */ float SpeedRate;
    /* 0xf38 */ int SuctionFlg;
    /* 0xf3c */ int Type;
    /* 0xf40 */ int EffectType;
    /* 0xf44 */ struct _ENEDMG_P_WRK *pNext;
} ENEDMG_P_WRK;

/* One burst of spirit particles pulled off a photographed ghost.
 *
 * StartPos is the ghost, pEndPos where they are pulled to -- the finder's
 * suction point while the camera is up, NULL for a burst that just disperses.
 * ParticleNum is the shot's suction power divided by 45, so each particle is
 * worth 45 units of banked power; DistPE is the player-to-ghost distance the
 * effect times its flight against.
 *
 * SuctionFlg selects the two behaviours: 0 disperses, 1 flies the particles
 * home to the tray.  PhotoDmgChk2() picks by whether the shot came from a
 * sub-function.
 *
 * PORT NOTE: the ROM's is 0x30 -- pEndPos is 4 bytes there and StartPos is
 * quadword-aligned, which rounds the tail up.  Here the pointer is 8 and the
 * vector only 4-aligned, so it comes out 0x28 with everything after pEndPos
 * shifted by four.  It is only ever a stack local handed straight to
 * EneDmgParticleEffectReq(), so the offsets below are the ROM's. */
typedef struct                          /* 0x30 on target, 0x28 here */
{
    /* 0x00 */ sceVu0FVECTOR StartPos;
    /* 0x10 */ float (*pEndPos)[4];
    /* 0x14 */ int   ParticleNum;
    /* 0x18 */ float DistPE;
    /* 0x1c */ int   SuctionFlg;
    /* 0x20 */ int   EffectType;
} ENE_DMG_PARTICLE_REQ;

/* ==========================================================================
 *  The damage overlay
 * ======================================================================== */

/* The "slash" overlay: up to four camera-facing quads per ghost, each with
 * its own scale/rotation/alpha ramp driven by cnt.  flow is the per-quad
 * state and enedmg1_flg the whole record's. */
typedef struct                          /* 0x90 */
{
    /* 0x00 */ fixed_array<sceVu0FVECTOR, 2> wbpos;
    /* 0x20 */ fixed_array<float, 4> scw;
    /* 0x30 */ fixed_array<float, 4> sch;
    /* 0x40 */ fixed_array<float, 4> rot_z;
    /* 0x50 */ fixed_array<float, 4> alp;
    /* 0x60 */ fixed_array<int, 4> cnt;
    /* 0x70 */ fixed_array<int, 4> flow;
    /* 0x80 */ int enedmg_no;
    /* 0x84 */ int enedmg1_flg;
    /* 0x88 */ int enedmg_chance;
    /* 0x8c */ int dummy;
} ENDMG1;

/* The second overlay pass -- one record for the whole scene rather than one
 * per ghost, because only one ghost can be in a shutter chance at a time. */
typedef struct                          /* 0xc */
{
    /* 0x0 */ int enedmg_no;
    /* 0x4 */ int enedmg2_flg;
    /* 0x8 */ int enedmg_chance;
} ENDMG2;

/* The trailing "sword line" behind the second pass: wmtx/opos are a ring of
 * the last twelve world matrices and positions of one deform point. */
typedef struct                          /* 0x3c0 */
{
    /* 0x000 */ fixed_array<float[4][4], 12> wmtx;
    /* 0x300 */ fixed_array<sceVu0FVECTOR, 12> opos;
} TAIL_DMG2_DAT;

/* One live sword-line particle. */
typedef struct                          /* 0x40 */
{
    /* 0x00 */ sceVu0FVECTOR npos;
    /* 0x10 */ sceVu0FVECTOR *oposp;
    /* 0x14 */ float (*wmtxp)[4][4];
    /* 0x18 */ int time;
    /* 0x1c */ short top;
    /* 0x1e */ short num;
    /* 0x20 */ float xp;
    /* 0x24 */ float xm;
    /* 0x28 */ float rot;
    /* 0x2c */ float rotp;
    /* 0x30 */ float x;
    /* 0x34 */ float y;
    /* 0x38 */ float n;
    /* 0x3c */ float cnt;
} NEW_PERTICLE;

/* Which slot of the ring the sword line is writing, and how many are live. */
typedef struct                          /* 0x10 */
{
    /* 0x0 */ int sw;
    /* 0x4 */ int num;
    /* 0x8 */ int top;
    /* 0xc */ int dummy;
} SWORD_LINE;

/* The screen-space damage flash EneDmgScreenEffectReq() raises: a sprite at
 * the ghost's 2D position whose scale, rotation, colour and alpha each run
 * their own counter up to a matching maximum. */
typedef struct                          /* 0x50 */
{
    /* 0x00 */ sceVu0FVECTOR MposP0;
    /* 0x10 */ u_char flow;
    /* 0x11 */ u_char alp;
    /* 0x12 */ u_char chance;
    /* 0x14 */ int scl;
    /* 0x18 */ int rot;
    /* 0x1c */ int cntcol;
    /* 0x20 */ int cntalp;
    /* 0x24 */ u_int cnt;
    /* 0x28 */ float x;
    /* 0x2c */ float y;
    /* 0x30 */ float dist;
    /* 0x34 */ int almx;
    /* 0x38 */ int scmx;
    /* 0x3c */ int rtmx;
    /* 0x40 */ int ccmx;
    /* 0x44 */ int camx;
    /* 0x48 */ int DmgType;
} ENE_DMG_EFF;

/* The blur/contrast half of a large hit while it runs.  pBlurContrast is the
 * preset it was started from; Counter is the shared ramp clock. */
typedef struct                          /* 0x40 */
{
    /* 0x00 */ sceVu0FVECTOR EneMposP0;
    /* 0x10 */ ENE_DMG_BLUR_CONTRAST_PARAMETER *pBlurContrast;
    /* 0x14 */ int Flow;
    /* 0x18 */ int EneWrkNo;
    /* 0x1c */ int HitEffecType;
    /* 0x20 */ int Counter;
    /* 0x24 */ int ContrastColor;
    /* 0x28 */ int ContrastAlpha;
    /* 0x2c */ int BlurScale;
    /* 0x30 */ int BlurRot;
    /* 0x34 */ u_char BlurAlpha;
} ENE_HIT_EFFECT_CTRL;

/* ==========================================================================
 *  Labels
 * ======================================================================== */

/* Which flash EneHitEffectReq() plays.  The first three are the ordinary shot
 * grades; the rest are one per camera sub-function, and the four attack ones
 * carry the shutter-chance grade in the label as well (`_SC` for a shutter
 * chance, `_SP` for the fatal-frame one).  n_equip_tray.c's SetEffect() is
 * what picks them.
 *
 * This is the index space of pLargeHitBlurParameter (20 entries).  The flash
 * presets are indexed separately: EneDmgLargeHitReq() takes a *preset* index
 * 0..37 into pLargeHitParameter, so EneHitEffectMain() is where a label becomes
 * the A/B pair -- two calls, one per slot. */
enum ENE_HIT_EFFECT_LABEL
{
    ENE_HIT_EFFECT_SMALL     =  0,
    ENE_HIT_EFFECT_LARGE     =  1,
    ENE_HIT_EFFECT_SP        =  2,
    ENE_HIT_EFFECT_SLOW      =  3,
    ENE_HIT_EFFECT_ZERO      =  4,
    ENE_HIT_EFFECT_ZERO_SC   =  5,
    ENE_HIT_EFFECT_ZERO_SP   =  6,
    ENE_HIT_EFFECT_KOKU      =  7,
    ENE_HIT_EFFECT_KOKU_SC   =  8,
    ENE_HIT_EFFECT_KOKU_SP   =  9,
    ENE_HIT_EFFECT_PARALYZE  = 10,
    ENE_HIT_EFFECT_VIEW      = 11,
    ENE_HIT_EFFECT_METSU     = 12,
    ENE_HIT_EFFECT_METSU_SC  = 13,
    ENE_HIT_EFFECT_METSU_SP  = 14,
    ENE_HIT_EFFECT_REN       = 15,
    ENE_HIT_EFFECT_REN_SC    = 16,
    ENE_HIT_EFFECT_REN_SP    = 17,
    ENE_HIT_EFFECT_TSUI      = 18,
    ENE_HIT_EFFECT_FUU       = 19
};

/* ==========================================================================
 *  Exports (all 20 ZERO2.MAP .text symbols)
 * ======================================================================== */

/* The two init entry points effect.c calls. */
void InitEffectEne(void);
void InitEffectEneEF(void);

/* The per-frame passes, in the order EffectControl() runs them. */
void EneDmgMain(void);
void EneHitEffectMain(void);
void EffectEndParticleMain(void);
void SetEneDmgEffect1_Sub(void);
void SetEneDmgEffect2_Sub(void);
void SetEneDmgEffect2(void);

/* Requests. */
int  EneDmgParticleEffectReq(const ENE_DMG_PARTICLE_REQ *pEneDmgReq);
int  EffectEndParticleEffectReq(const ENE_DMG_PARTICLE_REQ *pEneDmgReq);
void EneDmgScreenEffectReq(float *EneMposP0, int DmgType, int EneStatus,
                           float DistPE);
void EneHitEffectReq(int EneWrkNo, float *EneMposP0, int HitEffectLabel);
void EneDmgLargeHitReq(int EffectType);
void EneDmgLargeHitAllOff(void);

/* Refreshed every frame while a burst is in flight. */
void EneDmgParticleEndPosSet(const float (*pEndPos)[4]);

/* Non-zero while any burst is still live. */
int  IsActiveEneDmgParticle(void);

/* How many particles reached the tray this frame -- CNEquipTrayWrk::Work()
 * banks 45 units of spirit power for each. */
int  EneDmgParticleSuctionNumGet(void);

/* The two preset lookups, exported for the debug menu. */
ENE_DMG_LARGE_HIT_PARAMETER     *EffEneDmgLargeHitParameterPtrGet(int Label);
ENE_DMG_BLUR_CONTRAST_PARAMETER *EffEneDmgLargeHitBlurParameterPtrGet(int Label);

/* The packet builder, exported because effect_oth.o's haze reuses it.  Its
 * signature is ZERO2.MAP's, with functions.txt's parameter names:
 * EneDmgLargeHitMakePacket(float (*)[3], int, float (*)[3], unsigned char *,
 * unsigned char *, float (*)[3], unsigned long). */
void EneDmgLargeHitMakePacket(sceVu0FVECTOR *pVertexBuf, int VertexNum,
                              float (*LocalWorldMat)[4], u_char *pCenterRgba,
                              u_char *pOutsideRgba, sceVu0FVECTOR *pStq,
                              u_long tex0);

#endif /* _GRAPHICS_EFFECT_EFFECT_ENE_H */
