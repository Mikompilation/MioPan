/* ==========================================================================
 *  graphics/effect/effect_oth.h
 *
 *  Miscellaneous effect interface used by graph3d.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRAPHICS_EFFECT_EFFECT_OTH_H
#define _GRAPHICS_EFFECT_EFFECT_OTH_H

#include "graphics/graph3d/g3dLight.h"
#include "effect.h"                             /* EFFECT_CONT / PARTICLE */
#include "../graph3d/ctl/fixed_array.h"
#include "../../sdk/libvu0.h"

/* ==========================================================================
 *  Types
 * ======================================================================== */

/* The storm.  Lightning and thunder run as two independent state machines off
 * one request, so the flash leads the crack by DelayTime frames.
 *
 * Both use the same status vocabulary: 4 = waiting out the delay, 1 = first
 * flash, 2 = the gap between the double flash, 3 = the long second flash,
 * 0 = idle.  LightningFlg is what EffectThunderLightSetRoomLight() gates the
 * extra room light on. */
typedef struct                          /* 0x40 */
{
    /* 0x00 */ sceVu0FVECTOR LightningDirection;
    /* 0x10 */ sceVu0FVECTOR ThunderPosition;
    /* 0x20 */ int LightningStatus;
    /* 0x24 */ int LightningTime;
    /* 0x28 */ int LightningFlg;
    /* 0x2c */ int ThunderStatus;
    /* 0x30 */ int ThunderTime;
} THUNDER_LIGHT_CTRL;

/* The camera state the door-seal dissolve borrows and puts back. */
typedef struct                          /* 0x30 */
{
    /* 0x00 */ sceVu0FVECTOR Position;
    /* 0x10 */ sceVu0FVECTOR Target;
    /* 0x20 */ float Fov;
    /* 0x24 */ float Roll;
} DOOR_SEAL_CAMERA_BACKUP;

/* The door-seal dissolve.  A singleton: Status 0 is idle, and
 * DoorSealDisappearIsEnd() is exactly "Status == 0".  It carries its own two
 * EFFECT_CONTs -- one for the parts deform and one for the blur pass -- plus
 * the stream id of the cue Req() preloads. */
typedef struct                          /* 0x110 */
{
    /* 0x000 */ int   Counter;
    /* 0x004 */ int   Status;
    /* 0x008 */ float AlphaRate;
    /* 0x010 */ EFFECT_CONT EffectCont;
    /* 0x080 */ EFFECT_CONT EffectContBlur;
    /* 0x0f0 */ sceVu0FVECTOR BasePos;
    /* 0x100 */ float DeformSpeed;
    /* 0x104 */ float DeformRate;
    /* 0x108 */ float DeformAlphaRate;
    /* 0x10c */ int   StreamId;
} DOOR_SEAL_DISAPPEAR_CTRL;

/* One authored haze volume.  Three exist -- the room haze, and one each for
 * the two ghosts that wear one.  Speeds and offsets are whole units; the
 * AllSpeed triples with their AllSpeedTime are a three-leg drift the whole
 * volume follows, and the five AlphaBlend fields are the GS ALPHA register's
 * selectors and FIX. */
typedef struct                          /* 0xa8 */
{
    /* 0x00 */ int AreaRadius;
    /* 0x04 */ int MaxY;
    /* 0x08 */ int MinY;
    /* 0x0c */ int Frequency;
    /* 0x10 */ int SpeedXMax;
    /* 0x14 */ int SpeedXMin;
    /* 0x18 */ int SpeedYMax;
    /* 0x1c */ int SpeedYMin;
    /* 0x20 */ int SpeedZMax;
    /* 0x24 */ int SpeedZMin;
    /* 0x28 */ int AllSpeedX_1;
    /* 0x2c */ int AllSpeedY_1;
    /* 0x30 */ int AllSpeedZ_1;
    /* 0x34 */ int AllSpeedX_2;
    /* 0x38 */ int AllSpeedY_2;
    /* 0x3c */ int AllSpeedZ_2;
    /* 0x40 */ int AllSpeedX_3;
    /* 0x44 */ int AllSpeedY_3;
    /* 0x48 */ int AllSpeedZ_3;
    /* 0x4c */ int AllSpeedTime_1;
    /* 0x50 */ int AllSpeedTime_2;
    /* 0x54 */ int AllSpeedTime_3;
    /* 0x58 */ int Alpha;
    /* 0x5c */ int AlphaInTime;
    /* 0x60 */ int AlphaKeepTime;
    /* 0x64 */ int AlphaOutTime;
    /* 0x68 */ int StartScale;
    /* 0x6c */ int EndScale;
    /* 0x70 */ int R;
    /* 0x74 */ int G;
    /* 0x78 */ int B;
    /* 0x7c */ int RotZMax;
    /* 0x80 */ int RotZMin;
    /* 0x84 */ int RotZTime;
    /* 0x88 */ int AlphaBlendA;
    /* 0x8c */ int AlphaBlendB;
    /* 0x90 */ int AlphaBlendC;
    /* 0x94 */ int AlphaBlendD;
    /* 0x98 */ int AlphaBlendFIX;
    /* 0x9c */ int OffsetX;
    /* 0xa0 */ int OffsetY;
    /* 0xa4 */ int OffsetZ;
} HAZE_PARAMETER;

HAZE_PARAMETER *EffectHazeGetParameterPtr(int Type);
HAZE_PARAMETER *EffectHazeGetParameterPtrOrg(int Type);

/* ---- the heat haze / distortion pool ------------------------------------
 * A ring of 200 shared PARTICLEs.  `head` is where the next one is written
 * and `cnt` how many are live; `blife` is the lifetime a newly added particle
 * gets.  Four pools exist for ghosts, one for the amulet and five for
 * torches, and GetEnePartAddr/GetAmuPartAddr/GetTorchPartAddr are what hand
 * one out. */
typedef struct                          /* 0x3e90 */
{
    /* 0x0000 */ fixed_array<PARTICLE, 200> particles;
    /* 0x3e80 */ int   head;
    /* 0x3e84 */ int   cnt;
    /* 0x3e88 */ short flag;
    /* 0x3e8a */ short max;
    /* 0x3e8c */ short disp;
    /* 0x3e8e */ short blife;
} HEAT_HAZE;

/* ---- falling leaves ----------------------------------------------------- */

typedef struct                          /* 0x50 */
{
    /* 0x00 */ int    fl;
    /* 0x04 */ u_char r;
    /* 0x05 */ u_char g;
    /* 0x06 */ u_char b;
    /* 0x07 */ u_char a;
    /* 0x08 */ float  mang;
    /* 0x0c */ float  cnt;
    /* 0x10 */ sceVu0FVECTOR pos;
    /* 0x20 */ sceVu0FVECTOR opos;
    /* 0x30 */ sceVu0FVECTOR vel;
    /* 0x40 */ sceVu0FVECTOR ang;
} EFF_LEAF_ONE;

/* One drift of leaves: sixteen of them about a base position.  Six drifts
 * can be up at once. */
typedef struct                          /* 0x520 */
{
    /* 0x000 */ int flag;
    /* 0x004 */ int type;
    /* 0x010 */ sceVu0FVECTOR bpos;
    /* 0x020 */ fixed_array<EFF_LEAF_ONE, 16> lo;
} EFF_LEAF;

/* ---- haze volumes ------------------------------------------------------- */

typedef struct                          /* 0x40 */
{
    /* 0x00 */ sceVu0FVECTOR Position;
    /* 0x10 */ sceVu0FVECTOR Velocity;
    /* 0x20 */ int   Lifetime;
    /* 0x24 */ int   Alpha;
    /* 0x28 */ float Scale;
    /* 0x2c */ float RotZ;
    /* 0x30 */ float RotZSpeed;
} HAZE_PARTICLE;

/* A live haze volume: 64 particles that follow whatever pPos/pRot point at,
 * so a ghost's haze tracks the ghost without a per-frame update.  The
 * AllVelocity pair is the three-leg drift the whole volume follows. */
typedef struct                          /* 0x1020 */
{
    /* 0x0000 */ fixed_array<HAZE_PARTICLE, 64> Particles;
    /* 0x1000 */ sceVu0FVECTOR *pPos;
    /* 0x1004 */ sceVu0FVECTOR *pRot;
    /* 0x1008 */ float *pAlphaRate;
    /* 0x100c */ int   AllVelocityTime;
    /* 0x1010 */ int   AllVelocityStatus;
    /* 0x1014 */ int   Id;
    /* 0x1018 */ short disp;
    /* 0x101a */ short Type;
} HAZE_CTRL;

/* ---- the massed candles ------------------------------------------------- */

typedef struct                          /* 0x20 */
{
    /* 0x00 */ sceVu0FVECTOR Position;
    /* 0x10 */ int Alpha;
    /* 0x14 */ int FreaAlpha;
    /* 0x18 */ int Count;
} MANY_CANDLE_PARTICLE;

typedef struct                          /* 0xf30 */
{
    /* 0x000 */ fixed_array<MANY_CANDLE_PARTICLE, 120> Particles;
    /* 0xf00 */ sceVu0FVECTOR CenterPos;
    /* 0xf10 */ int Id;
    /* 0xf14 */ int R;
    /* 0xf18 */ int G;
    /* 0xf1c */ int B;
    /* 0xf20 */ int DataNum;
} MANY_CANDLE_CTRL;

/* The loader in front of it -- up to five effect handles, loaded as one pack. */
typedef struct                          /* 0x40 */
{
    /* 0x00 */ sceVu0FVECTOR Offset;
    /* 0x10 */ u_int *pLoadBuf;
    /* 0x14 */ int   Status;
    /* 0x18 */ int   LoadId;
    /* 0x1c */ int   PackNum;
    /* 0x20 */ void *pEffRet[5];
} MANY_CANDLE_LOAD_CTRL;

/* ---- the item glint -----------------------------------------------------
 * One per placed item, keyed by the placing record's label (ItemNo), not by
 * an inventory id.  Four counters rather than one: the two scale ramps and
 * the two alpha ramps run independently, so the pulse and the twinkle are
 * deliberately out of step. */
typedef struct                          /* 0x40 */
{
    /* 0x00 */ sceVu0FVECTOR Position;
    /* 0x10 */ int   Flow;
    /* 0x14 */ float Rot;
    /* 0x18 */ float fCounter;
    /* 0x1c */ int   ScaleCounter0;
    /* 0x20 */ int   ScaleCounter1;
    /* 0x24 */ int   AlphaCounter0;
    /* 0x28 */ int   AlphaCounter1;
    /* 0x2c */ int   ItemNo;
    /* 0x30 */ int   Type;
} ITEM_EFFECT_DATA;

/* ---- the dust cloud a run kicks up -------------------------------------- */

typedef struct                          /* 0x990 */
{
    /* 0x000 */ fixed_array<PARTICLE, 30> particles;
    /* 0x960 */ sceVu0FVECTOR Pos;
    /* 0x970 */ int   head;
    /* 0x974 */ int   cnt;
    /* 0x978 */ short max;
    /* 0x97a */ short disp;
    /* 0x97c */ short blife;
    /* 0x980 */ float BrakeRate;
} CLOUD_OF_DUST_CTRL;

/* The two shared particle drawing routines.  DrawFrea() is the soft flare
 * sprite at the head of a flame; draw_distortion_particles2() draws a whole
 * PARTICLE array as heat-distorted billboards and returns how many of them
 * were actually on screen -- which is what feeds a flame's own brightness. */
void DrawFrea(float *pos, float size, int Depth, int R, int G, int B, int Alpha);
int  draw_distortion_particles2(float (*matLocalScreen)[4],
                                float (*matLocalClip)[4],
                                int num, PARTICLE *pPartTop, float scale,
                                u_long tex0, u_long alpha);

int  EffectThunderLightGetLightningFlg(void);
void EffectThunderLightSetRoomLight(void);
void EffectThunderLightGetG3dLight(G3DLIGHT *pLight);


/* The storm.  ev_macro.c fires this from the story script. */
void EffectThunderLightReq(float *LightningDirection, int DelayTime, float *ThunderPosition);


/* Ask the candle-flame effect running on `pEffect` to flare up: both colour
 * scales jump to 25 and the record enters flow 1. */
void EffOthCandleFlameFlareUpReq(EFFECT_CONT *pEffect);

/* Draw one candle flame at frame `Count` of its animation, billboarded about
 * Y only.  ingame_effect.c's lantern draw calls it directly rather than
 * through an effect record. */
void EffectCandleFlameDraw(float *Position, int *Color, float Scale, int Count);

/* The item glint.  ItemNo is the placing record's label, not an inventory id;
 * MapObjEffCallback() draws one per put-object as its MapPut draw callback. */
void ItemEffectReq(float *Pos, int ItemNo, int EffectType);
void ItemEffectCut(int ItemNo);
void ItemEffectDrawOne(int ItemNo);

/* Haze / mist volume ("eff_moya"), keyed by label so several can coexist. */
void EffectHazeReqId(float *CenterPos, int Id);
void EffectHazeCutId(int Id);

/* The massed-candle set ("eff_cdl_gather").  A singleton -- the cut takes no
 * id, so only one gathering can be up at a time. */
void EffectManyCandleLoadReq(float *Pos);
void EffectManyCandleLoadCut(void);

/* The two per-ghost haze volumes.  Both are handed the ghost's position and
 * rotation vectors plus its alpha by address, so the effect tracks the ghost
 * without a per-frame update; the cut takes the handle back.  Kusabi wears
 * one for models 3 and 0x12, Sae for data numbers 0x45 and 0xf9. */
void *EffectKusabiHazeReq(float *Pos, float *Rot, float *pAlpha);
void  EffectKusabiHazeCut(void *p);
void *EffectSaeHazeReq(float *Pos, float *Rot, float *pAlpha);
void  EffectSaeHazeCut(void *p);

/* Suppress the haze effect's drawing without tearing it down. */
void  EffectSaeHazSetNoDrawFlg(int iFlg);   /* 0x159a08 */

/* The door-seal dissolve.  A singleton, driven by the photo phase's modes 9
 * and 10: Req() starts it, Draw() runs a frame, IsEnd() reports completion
 * (exactly Status == 0) and EndProc() tears it down.  The seven-stage clock
 * itself runs from InitEffectOthEF(). */
void DoorSealDisappearReq(void);
void DoorSealDisappearDraw(void);
int  DoorSealDisappearIsEnd(void);
void DoorSealDisappearEndProc(void);


/* The modeled-effect draws EffectZSort() dispatches into this module, the
 * per-frame passes, and the init pair.  InitHeatHaze (0x154710) moved here
 * from effect.c -- it is effect_oth.o's export. */
#include "effect.h"                     /* EFFECT_CONT */
void InitEffectOth(void);
void InitEffectOthEF(void);
void InitHeatHaze(void);
void RunLeaf(void);
void EffectThunderLightExec(void);
void EffOthCandleFlameYuramekiReq(EFFECT_CONT *pEffCont, float *PlayerPos);
void SetDoorSeal(EFFECT_CONT *ec);
void SetHalo(EFFECT_CONT *ec);
void SetFire(EFFECT_CONT *ec);
void SetTorch(EFFECT_CONT *ec);
void SetDust(EFFECT_CONT *ec);
void SetHaze(EFFECT_CONT *ec);
void SetManyCandle(EFFECT_CONT *ec);
void SetEneFire(EFFECT_CONT *ec);

#endif /* _GRAPHICS_EFFECT_EFFECT_OTH_H */
