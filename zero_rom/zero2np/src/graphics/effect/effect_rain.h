/* ==========================================================================
 *  graphics/effect/effect_rain.h
 *
 *  The three weather effects that make up an "eff_rain" placement:
 *
 *    rain     - a fixed pool of falling line segments, recycled in place.
 *    spray    - camera-facing puffs seeded on quads that outline the roofs
 *               and the ground where the rain lands.
 *    dripping - single droplets released one at a time from 35 fixed points,
 *               falling under gravity until they reach the ground height.
 *
 *  All three are hard-wired to one room: the appearance rectangles, the drip
 *  points and the two hit boxes are that room's geometry baked into .rodata.
 *
 *  MapObjSetEffect() raises all three together and MapObjDeleteEffect() cuts
 *  all three together, so a rain volume is rain plus spray plus drip.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), 0x0015c120.
 *
 *  PORT NOTE 1: the control structs all hold a pointer, so their host offsets
 *  past that pointer drift from the ROM's.  The offset comments are the ROM's
 *  and are documentation; nothing here is shared across a module boundary or
 *  read out of a file, so the drift is harmless.
 *
 *  PORT NOTE 2: the particle and rectangle structs are pointer-free and their
 *  member offsets do match, but each ends just past a float run that the EE
 *  compiler padded out for quadword alignment.  Those tails are spelled out
 *  so that sizeof() -- which sets the array strides and the linked list's
 *  element size -- keeps the ROM's value.
 * ======================================================================== */

#ifndef _GRAPHICS_EFFECT_EFFECT_RAIN_H
#define _GRAPHICS_EFFECT_EFFECT_RAIN_H

#include "eetypes.h"
#include "../../sdk/vu0_intrin.h"       /* sceVu0IVECTOR */
#include "../../common/SingleLinkList.h"

/* ---- rain -------------------------------------------------------------- */

/* One falling line segment.  RotX/RotY aim the segment down its own velocity
 * so the streak leans with the wind; Length is how long that streak is. */
typedef struct                      /* 0x30 */
{
    float Position[4];              /* 0x00 */
    float Velocity[4];              /* 0x10 */
    float Length;                   /* 0x20 */
    float RotX;                     /* 0x24 */
    float RotY;                     /* 0x28 */
    float aPad[1];                  /* 0x2c -- see the note below */
} EFFECT_RAIN_PARTICLE;

/* Color0 is the head of the streak and Color1 the tail; the packet gouraud
 * shades between them. */
typedef struct                      /* 0x40 */
{
    sceVu0IVECTOR Color0;           /* 0x00 */
    sceVu0IVECTOR Color1;           /* 0x10 */
    float Offset[4];                /* 0x20 */
    int   ParticleNum;              /* 0x30 */
    EFFECT_RAIN_PARTICLE *pParticle;/* 0x34 */
} EFFECT_RAIN_CTRL;

/* Rain lives while it is outside this box, or inside it but also inside the
 * open middle -- see EffectRainParticleHitCheck(). */
typedef struct                      /* 0x20 */
{
    float Max[4];                   /* 0x00 */
    float Min[4];                   /* 0x10 */
} RAIN_HIT_BOX;

/* ---- spray ------------------------------------------------------------- */

/* LifeTime -1 marks a free slot, which is how the pool is searched. */
typedef struct                      /* 0x30 */
{
    float Position[4];              /* 0x00 */
    float Velocity[4];              /* 0x10 */
    int   LifeTime;                 /* 0x20 */
    float StartScale;               /* 0x24 */
    float LastScale;                /* 0x28 */
    float aPad[1];                  /* 0x2c */
} EFFECT_SPRAY_PARTICLE;

/* Four corners of a quad, wound 0-1-2-3. */
typedef float RectVECTOR[4][4];

/* One place spray can appear.  Rate is a weight, not a percentage: the picker
 * walks the table accumulating Rate until it passes a 0..10000 roll. */
typedef struct                      /* 0x50 */
{
    RectVECTOR Rect;                /* 0x00 */
    int        Rate;                /* 0x40 */
    int        aPad[3];             /* 0x44 */
} SPRAY_APPEAR_DATA;

typedef struct                      /* 0x60 */
{
    float Offset[4];                /* 0x00 */
    const SPRAY_APPEAR_DATA *pAppearData; /* 0x10 */
    int   RectNum;                  /* 0x14 */
    int   ParticleNum;              /* 0x18 */
    int   R;                        /* 0x1c */
    int   G;                        /* 0x20 */
    int   B;                        /* 0x24 */
    int   Alpha;                    /* 0x28 */
    float SpeedXMax;                /* 0x2c */
    float SpeedXMin;                /* 0x30 */
    float SpeedYMax;                /* 0x34 */
    float SpeedYMin;                /* 0x38 */
    float SpeedZMax;                /* 0x3c */
    float SpeedZMin;                /* 0x40 */
    int   AppearNumMin;             /* 0x44 */
    int   AppearNumMax;             /* 0x48 */
    int   InitLifeTime;             /* 0x4c */
    float StartScale;               /* 0x50 */
    float LastScaleMax;             /* 0x54 */
    float LastScaleMin;             /* 0x58 */
    EFFECT_SPRAY_PARTICLE *pParticle; /* 0x5c */
} EFFECT_SPRAY_CTRL;

/* ---- dripping water ---------------------------------------------------- */

/* Unlike rain and spray this one is a growing list, not a fixed pool: the
 * drips are rare enough that allocating a cell per drop is cheap. */
struct _EFFECT_DROP_PARTICLE        /* 0x30 */
{
    float Position[4];              /* 0x00 */
    float Velocity[4];              /* 0x10 */
    float Gravity;                  /* 0x20 */
    float GroundHeight;             /* 0x24 */
    float aPad[2];                  /* 0x28 */
};

typedef struct _EFFECT_DROP_PARTICLE EFFECT_DROP_PARTICLE;

typedef struct                      /* 0x40 */
{
    float Offset[4];                /* 0x00 */
    SINGLE_LINK_LIST *pList;        /* 0x10 */
    const float (*pAppearPos)[4];   /* 0x14 */
    int   AppearPosNum;             /* 0x18 */
    float ScaleX;                   /* 0x1c */
    float ScaleY;                   /* 0x20 */
    int   R;                        /* 0x24 */
    int   G;                        /* 0x28 */
    int   B;                        /* 0x2c */
    int   Alpha;                    /* 0x30 */
} EFFECT_DROP_CTRL;

/* ---- entry points ------------------------------------------------------ *
 * Init is called once from InitEffects(); Req/Cut are driven by the map's
 * effect placements through MapObjSetEffect()/MapObjDeleteEffect(); Draw is
 * called every frame from EffectInPhoto() (effect.o, not yet reconstructed --
 * nothing calls the three Draw entry points in the port yet). */

void EffectRainInit(void);
void EffectRainReq(float *Offset);
void EffectRainCut(void);
void EffectRainDraw(void);

void EffectSprayInit(void);
void EffectSprayReq(float *Offset);
void EffectSprayAllCut(void);
void EffectSprayAllDraw(void);

void EffectDropOfWaterInit(void);
void EffectDropOfWaterReq(float *Offset);
void EffectDropOfWaterCut(void);
void EffectDropOfWaterDraw(void);

#endif /* _GRAPHICS_EFFECT_EFFECT_RAIN_H */
