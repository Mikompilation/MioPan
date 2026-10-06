/* ==========================================================================
 *  graphics/effect/effect_sub.h
 *
 *  Shared helpers of the effect system: the screen-colour fade, the effect
 *  billboard writers, point-visibility queries, the positional effect SE
 *  layer, and the falling-leaves particle system.
 *
 *  The ROM's own effect_sub.c opens with a block that declares every function
 *  in the file, exports and statics alike, one per odd line 9..119 -- that is
 *  where every PROC/STATICPROC record's declared-line number comes from.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), 0x00165950.
 * ======================================================================== */

#ifndef _GRAPHICS_EFFECT_EFFECT_SUB_H
#define _GRAPHICS_EFFECT_EFFECT_SUB_H

#include "../draw_env.h"                /* DRAW_ENV */
#include "../graph3d/ctl/fixed_array.h" /* PP_JUDGE's two containers */
#include "../../common/SingleLinkList.h"

/* A batch of "is this world point in front of the geometry the camera sees?"
 * queries.  A caller fills p[0..num-1] and raises its own check flag; the
 * effect system resolves the whole batch once per frame in CheckPointDepth()
 * and writes result[i] back.  ENE_WRK embeds one, which is how a ghost knows
 * it is not hidden behind a wall.
 *
 * Declared outside the extern "C" block: the two members are C++ templates. */
typedef struct                          /* 0xb0 */
{
    /* 0x00 */ u_char num;
    /* 0x01 */ fixed_array<u_char, 10>  result;
    /* The ROM aligns the quadword array to 0x10; a fixed_array<float[4],N> is
     * only 4-aligned on the host, so the gap is explicit. */
    /* 0x0b */ u_char _pad0b[5];
    /* 0x10 */ fixed_array<float[4], 10> p;
} PP_JUDGE;

/* --------------------------------------------------------------------------
 *  Falling leaves.  One LEAVES_FALL_CTRL per placed "eff_ha_0" volume, a
 *  private EFFECT_MALLOC'd array of LEAVES_PARTICLE under it, and the whole
 *  set linked into LeavesList, keyed by the placing record's label.
 *
 *  Host layouts: both structs end short of the ROM's quadword-aligned sizes
 *  (the leading float[4] gives them 16-byte alignment on the EE, 4-byte here),
 *  and LeavesList's element size must be the ROM's 0x60, so the tails are
 *  spelled out.  See float4-members-lose-quadword-alignment.
 * ------------------------------------------------------------------------ */
typedef struct                          /* 0x50 */
{
    /* 0x00 */ float Position[4];
    /* 0x10 */ float Aim[4];            /* horizontal sway target, X/Z used  */
    /* 0x20 */ float Accel[4];          /* sway progress ([0]/[2]) + fall speed ([1]) */
    /* 0x30 */ float Rots[4];           /* billboard rotation, radians       */
    /* 0x40 */ short at_ground;         /* frames spent lying on the ground  */
    /* 0x42 */ fixed_array<short, 4> rgba;
    /* 0x4a */ short InCount;           /* frames since (re)spawn, fade-in   */
    /* 0x4c */ u_char _pad4c[4];        /* ROM tail padding to 0x50          */
} LEAVES_PARTICLE;

typedef struct                          /* 0x60 */
{
    /* 0x00 */ LEAVES_PARTICLE *pLeavesParticle;
    /* The EE pads its 4-byte pointer out to the quadword FallSpeed needs;
     * the host pointer is 8, so half that gap survives explicitly. */
    u_char _pad08[8];
    /* 0x10 */ float FallSpeed[4];      /* [0]/[2] sway, [1] fall, [3] sway target range */
    /* 0x20 */ float CenterPos[4];      /* volume centre; [1] is the ground  */
    /* 0x30 */ int   ParticleMax;
    /* 0x34 */ int   ParticleNum;       /* how many are live so far          */
    /* 0x38 */ float AppearRate;        /* frames between spawns             */
    /* 0x3c */ float AppearRateCount;
    /* 0x40 */ short FallDistance;      /* spawn height above the ground     */
    /* 0x44 */ int   StopTime;          /* frames a leaf lies before recycling */
    /* 0x48 */ int   Id;                /* placing record's label            */
    /* 0x4c */ short Area;              /* square spawn area edge, world units */
    /* 0x4e */ fixed_array<short, 4> Color;
    /* 0x56 */ u_char _pad56[10];       /* ROM tail padding to 0x60          */
} LEAVES_FALL_CTRL;

/* On the ROM's 32-bit pointers the three pointer members make this 0xc and it
 * is memset with that literal; the host uses sizeof throughout. */

/* ---- the effect SE bookkeeping records ---------------------------------- */

typedef struct                          /* 0xc */
{
    /* 0x0 */ int FileNo;
    /* 0x4 */ int BankNo;               /* SndBankNew() handle, -1 = none    */
    /* 0x8 */ int IsReady;
} EFFECT_SOUNDFILE_DATA;

typedef struct                          /* 0x4 */
{
    /* 0x0 */ int FileNo;
} EFFECT_FILEDEL_DATA;

typedef struct                          /* 0x30 */
{
    /* 0x00 */ float Position[4];
    /* 0x10 */ int   SetPositionFlg;
    /* 0x14 */ int   FileNo;
    /* 0x18 */ int   No;
    /* 0x1c */ int   Effect;
    /* 0x20 */ int   FadeTime;          /* stored, never consumed            */
    /* 0x24 */ int   PlayId;            /* SndBankPlay() voice, -1 = pending */
    /* 0x28 */ u_int DeleteKey;
    /* 0x2c */ u_char _pad2c[4];        /* ROM tail padding to 0x30          */
} EFFECT_SOUNDPLAY_DATA;

/* ROM offsets; on the host SINGLE_LINK_LIST is pointer-widened (0x18, not
 * 0x10), so the members sit at 0x18/0x30 here.  Nothing indexes past them. */
typedef struct                          /* 0x30 */
{
    /* 0x00 */ SINGLE_LINK_LIST FileReadyList;   /* EFFECT_SOUNDFILE_DATA    */
    /* 0x10 */ SINGLE_LINK_LIST FileDeleteList;  /* EFFECT_FILEDEL_DATA      */
    /* 0x20 */ SINGLE_LINK_LIST PlayList;        /* EFFECT_SOUNDPLAY_DATA    */
} EFFECT_SOUND_CTRL;

#ifdef __cplusplus
extern "C" {
#endif

/* The two exported list heads (effect_sub.o .data 2fd570 / 2fd580). */
extern SINGLE_LINK_LIST  LeavesList;
extern EFFECT_SOUND_CTRL EffectSoundCtrl;

/* Fixes the scratch buffer pointers and the leaves list. */
void InitEffectSub(void);

/* ---- whole-screen colour panel ------------------------------------------ *
 * SetParam() arms a fade (flag 1 = alpha 128 -> 0 over `time`, 2 = 0 -> 128,
 * 0 = hold `alp`); ScreenCtrl() draws and steps it once a frame and returns
 * the running flag. */
void SetParam(int alp, int time, u_char r, u_char g, u_char b, int flag);
int  ScreenCtrl(void);

/* One flat-colour quad over x1/y1..x2/y2.  `pri` doubles as the depth value
 * (z = 0xfffff - pri); `z` goes into the ZBUF register's ZMSK bit. */
void SetPanel2(u_int pri, float x1, float y1, float x2, float y2, int z,
               u_char r, u_char g, u_char b, u_char a);

/* Full-screen textured sprite straight out of GS memory at `addr`, drawn at
 * the very back (z 0xfffff). */
void SetScreenZ(int addr);

/* Camera-cut bookkeeping; both are empty shells in this prototype. */
void CamSave(void);
int  CamChangeCheck(void);

/* Resolves one posted batch: for each of the num points in p[], writes
 * result[i] non-zero when nothing occludes it.  On the host the depth
 * comparison degrades to "on screen" -- see the port note on the body. */
void CheckPointDepth(PP_JUDGE *ppj);

/* Projects a world point to interlaced 2D screen coordinates (640x448). */
void GetCamI2DPos(const float *pos, float *tx, float *ty);

/* Euler angles that aim the +Z axis down `dir`, each wrapped to (-PI, PI]. */
void Vector2Rot(const float *dir, float *x, float *y);
/* Vector2Rot() of (v2 - v1): the rotation that turns v1 to face v2. */
void Get2PosRot(const float *v1, const float *v2, float *x, float *y);
/* The same pair computed against the Y difference instead of the XZ plane:
 * *z = atan2(dx, dy), *x = atan2(dz, dy), both wrapped to (-PI, PI]. */
void Get2PosRot2(const float *v1, const float *v2, float *x, float *z);

/* effect_sub.o's own facing helper (0x00166e28), not unit_ctl.o's GetTrgtRot.
 * Writes the rotation that turns p0 to face p1 into rot[]; `id` is a bitfield
 * -- bit 0 = pitch, bit 1 = heading -- and 3 asks for both. */
void GetTrgtRotType2(float *p0, float *p1, float *rot, int id);

/* Full 3D distance between two points.  Unlike GetDistV(), Y counts. */
float Get2PLength(const float *v1, const float *v2);

/* 0x7908 minus the current draw env's XYOFFSET OFY: the interlace field bias
 * in 4.4 fixed point, and the same value as a float in whole pixels. */
int   GetYOffset(void);
float GetYOffsetf(void);

/* ---- GS local <-> EE buffer image copies -------------------------------- *
 * ZtoBZ stashes the 640x224 PSMZ16S depth buffer at GS 0x2300 into the EE
 * buffer bufz points at; BZtoZ loads it back.  LtoBD pulls a 640x224 PSMCT24
 * frame at GS `addr` into `outbuf` (in two strips: 200 rows + 24 rows). */
void LocalCopyZtoBZ(void);
void LocalCopyBZtoZ(void);
void LocalCopyLtoBD(int addr, void *outbuf);

/* ---- positional effect SE ------------------------------------------------ *
 * A sound file becomes a SndBank (its header is always file - 1); cues are
 * played out of it by index, optionally 3D-positioned, and every live cue is
 * tracked on a list so it can be stopped, paused or restarted as a group.
 * EffectSndCtrl() runs once a frame and starts any cue that was requested
 * before its bank had finished loading. */
void EffectSndInit(void);
void EffectSndEnd(void);
void EffectSndCtrl(void);
void EffectSndFileReadyReq(int FileNo);
void EffectSndFileRelease(int FileNo);
void EffectSndPlay(int FileNo, int No, int Effect, int FadeTime,
                   float (*pPosition)[3]);
void EffectSndPlayDeleteKey(int FileNo, int No, int Effect, int FadeTime,
                            float (*pPosition)[3], u_int DeleteKey);
void EffectSndStop(int FileNo, int No, int FadeFlg);
void EffectSndStopDeleteKey(u_int DeleteKey, int FadeFlg);
void EffectSndAllStop(void);
void EffectSndAllPause(void);
void EffectSndAllRestart(void);

/* ---- falling leaves ("eff_ha_0"), keyed by the placing record's label ---- */
void EffectLeavesFallReq(float *CenterPos, int Id);
void EffectLeavesFallCut(int Id);
void EffectLeavesFallExec(void);

#ifdef __cplusplus
}
#endif

/* Textured quad out of the effect texture bank: a `w` x `h` billboard placed
 * by `wlm`, tinted r/g/b/a, drawn under the caller's DRAW_ENV.  `texno`
 * indexes effdat[]; Set3DPosTexure() selects the monochrome twin one slot up
 * automatically, Set3DPosTexure2() is told outright.  Both are two-line
 * wrappers around one inlined body in the ROM. */
void Set3DPosTexure(float (*wlm)[4], DRAW_ENV *de, int texno, float w, float h,
                    u_char r, u_char g, u_char b, u_char a);
void Set3DPosTexure2(float (*wlm)[4], DRAW_ENV *de, int texno, float w, float h,
                     u_char r, u_char g, u_char b, u_char a, int MonochroModeFlg);

#endif /* _GRAPHICS_EFFECT_EFFECT_SUB_H */
