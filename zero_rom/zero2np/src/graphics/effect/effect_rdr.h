/* ==========================================================================
 *  graphics/effect/effect_rdr.h
 *
 *  The placed fire effects: the tall "burn fire" candle flame that furniture
 *  registration lights (BURN_FIRE) and the torch reservations that wrap
 *  effect_torch's Torch2 (EFFRDR_RSV).  Both are keyed by the placing
 *  record's label (`furn_id`), which is how the matching Reset finds them
 *  again after the room has been rebuilt.
 *
 *  COMPLETE - all 12 ZERO2.MAP .text symbols plus 4 statics and both
 *  file-scope tables.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), 0x0015d960.
 * ======================================================================== */

#ifndef _GRAPHICS_EFFECT_EFFECT_RDR_H
#define _GRAPHICS_EFFECT_EFFECT_RDR_H

#include "eetypes.h"

/* --------------------------------------------------------------------------
 *  One tall candle flame.                                     types.txt 0x60
 *
 *  `ebuf` is the EFFECT_CONT the flame runs on; SetRDLongFire2() hands the
 *  work block back to it through pnt[5], so the effect handler can reach its
 *  own parameters.  `usefl` is the slot's in-use flag and `sta` the start
 *  bitfield -- bit 0 asks for a lit-from-cold start, which seeds `pat`.
 * ------------------------------------------------------------------------ */
typedef struct                      /* 0x60 */
{
    /* 0x00 */ float  ligdiff[4];   /* flame tint, w = 1.0 */
    /* 0x10 */ float  ligpos[4];
    /* 0x20 */ float  fpos[4];      /* world position */
    /* 0x30 */ void  *ebuf;         /* the EFFECT_CONT this flame drives */
    /* 0x34 */ float  ligpow;
    /* 0x38 */ float  wavew;
    /* 0x3c */ float  rate;
    /* 0x40 */ float  szw;          /* sprite width  */
    /* 0x44 */ float  szh;          /* sprite height */
    /* 0x48 */ float  sw;           /* scroll x */
    /* 0x4c */ float  sh;           /* scroll y */
    /* 0x50 */ int    furn_id;
    /* 0x54 */ u_char lignum;
    /* 0x55 */ u_char usefl;
    /* 0x56 */ u_char sta;
    /* 0x57 */ u_char pat;
} BURN_FIRE;

/* --------------------------------------------------------------------------
 *  One torch reservation.                                     types.txt 0x40
 *
 *  A free slot is marked by `furn_id == -1`, which is why InitEffectRdr()
 *  seeds every entry with -1 rather than zeroing the table the way it does
 *  burn_fire[].  Type 6 is the moving torch: CenterPos is the orbit centre,
 *  RotY the current bearing and WavePos the bob phase, and RDPFireMoveCtrl()
 *  is what advances them.
 * ------------------------------------------------------------------------ */
typedef struct                      /* 0x40 */
{
    /* 0x00 */ float  Position[4];  /* where the torch effect actually is */
    /* 0x10 */ float  CenterPos[4]; /* orbit centre, type 6 only */
    /* 0x20 */ void  *adr;          /* the Torch2 handle */
    /* 0x24 */ int    furn_id;      /* -1 = free */
    /* 0x28 */ int    Type;
    /* 0x2c */ float  RotY;
    /* 0x30 */ float  WavePos;
} EFFRDR_RSV;

#ifdef __cplusplus
extern "C" {
#endif

void InitEffectRdr(int tex_id);
void InitEffectRdrEF(void);

/* Torch flames, keyed by the placing record's label so the matching Reset can
 * find them again.  `Type` selects one of nine flame presets: the eight
 * "eff_torch_*" placeholders map onto types 0..5 and 8, and "eff_torch_6" is
 * the moving variant with its own entry point.  Type 8 starts silently. */
void SetRDPFire(float *pos, int furn_id, int Type);
void SetRDPFireMove(float *pos, float *rot, int furn_id);
void ResetRDPFire(int furn_id);

/* Slot lookup; both return -1 when they find nothing.  Exported by the ROM
 * even though only this module calls them. */
short GetRDPFireWork(void);
short SearchRDPFireWork(int furn_id);

/* Advance every type-6 (moving) torch by one frame.  Nothing else in the
 * module has a per-frame pass. */
void RDPFireMoveCtrl(void);

/* The tall candle flame.  sta is a start bitfield; szw/szh are the sprite
 * size, sw/sh its scroll, r/g/b the tint and `room` a room bias.  Note `room`
 * is accepted and never stored -- see the note in the .c. */
void SetRDLongFire2(float *pos, u_char sta, float szw, float szh,
                    float sw, float sh, float r, float g, float b,
                    float room, int furn_id);
void SetRDLongFire(float *pos, float r, float g, float b,
                   float room, int furn_id);
void ResetRDLongFire(int furn_id);
void RDLongFireFlareUpReq(int furn_id);

#ifdef __cplusplus
}
#endif

#endif /* _GRAPHICS_EFFECT_EFFECT_RDR_H */
