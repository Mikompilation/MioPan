/* ==========================================================================
 *  system/eeiop/snd3d.h
 *
 *  3D sound placement (snd_3d.c).  A caller hands in a SND_3D_SET -- pointers
 *  to the emitter's position, velocity and (unused) direction -- and gets back
 *  an opaque handle into snd_3d_wrk[].  Once a frame Snd3DMain() turns each
 *  live handle into a stereo VOLSET plus a pitch, which the voice owners
 *  (snd_buffer.c, snd_stream.c) read out with Snd3DGetVal().
 *
 *  Layouts are from types.txt; signatures are from functions.txt / ZERO2.MAP.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_EEIOP_SND3D_H
#define _SYSTEM_EEIOP_SND3D_H

#include "../../sdk/libvu0.h"       /* sceVu0FVECTOR */
#include "snd_def.h"                /* VOLSET */

typedef struct _SND_3D_ENV          /* 0x18 */
{
    /* 0x00 */ float front;         /* level kept dead ahead                 */
    /* 0x04 */ float side;          /* level of the far ear at 90 degrees    */
    /* 0x08 */ float back;          /* level kept dead behind                */
    /* 0x0c */ float vol_min;       /* floor of the distance curve           */
    /* 0x10 */ float farthest_dist; /* in metres; silence (vol_min) beyond   */
    /* 0x14 */ float nearest_dist;  /* in metres; full volume within         */
} SND_3D_ENV;

typedef struct _SND_3D_SET          /* 0x0c */
{
    /* 0x0 */ sceVu0FVECTOR *pos;
    /* 0x4 */ sceVu0FVECTOR *vel;   /* NULL disables the doppler shift       */
    /* 0x8 */ sceVu0FVECTOR *dir;
} SND_3D_SET;

/* One emitter.  `status` is a plain int in the ROM's debug info, so the three
 * bits below were masks in the source rather than a bitfield; the names are
 * the port's.  SND_3D_ST_CALC is the dirty flag Snd3DSetPosition() raises and
 * Snd3DCalc() clears. */
typedef struct _SND_3D_WRK          /* 0x30 */
{
    /* 0x00 */ sceVu0FVECTOR pos;
    /* 0x10 */ sceVu0FVECTOR velocity;
    /* 0x20 */ int           status;
    /* 0x24 */ int           voll;
    /* 0x28 */ int           volr;
    /* 0x2c */ int           pitch;
} SND_3D_WRK;

#define SND_3D_ST_USE   0x1         /* slot allocated                        */
#define SND_3D_ST_CALC  0x2         /* position moved since the last Calc    */
#define SND_3D_ST_VEL   0x4         /* velocity supplied -> doppler is live  */

typedef struct _SND_3D_LISTENER     /* 0x50 */
{
    /* 0x00 */ sceVu0FVECTOR pos;
    /* 0x10 */ sceVu0FVECTOR velocity;
    /* 0x20 */ sceVu0FVECTOR dir;
    /* 0x30 */ sceVu0FVECTOR top;
    /* 0x40 */ int           defer;  /* listener moved: recalc every slot     */
    /* 0x44 */ char          padding[12];
    /* The leading sceVu0FVECTOR gives this quadword alignment on the EE, so
     * the ROM's sizeof is 0x50 rather than 0x44.  float[4] only needs 4-byte
     * alignment on the host, so the tail is spelled out. */
} SND_3D_LISTENER;

void   Snd3DInit(void);
void   Snd3DMain(void);

void   snd3DSet1Meter(float unit);
void   snd3DSetEnvironment(SND_3D_ENV *env);
void   snd3DSetListner(SND_3D_SET *set, float *top);

void  *Snd3DCreateWrk(SND_3D_SET *s3s);
void   Snd3DFreeWrk(void *hndl);
void   snd3DSetSET(void *hndl, SND_3D_SET *set);
void   Snd3DSetPosition(void *hndl, float *pos);
void   Snd3DGetVal(void *hndl, VOLSET *volset, short *pitch);

/* Signed length of `velocity` projected onto the unit vector `unit_dir`. */
float  GetLenDirection(float *velocity, float *unit_dir);

#endif /* _SYSTEM_EEIOP_SND3D_H */
