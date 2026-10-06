// FILE: /home/zero_rom/zero2np/src/graphics/effect/effect_rdr.c
//
// COMPLETE.  All 12 ZERO2.MAP .text symbols plus the four statics
// (GetBurnFireWork, SearchBurnFireFurnID, RDPFireCalcMovePos,
// RDPFireCheckTorchTypeExist) and both file-scope tables.
//
// A NOTE ON THE /* NNN */ ANNOTATIONS.  Both tables are fixed_array<>, and
// fixed_array::operator[] swallows the line note of any statement whose only
// memory access goes through a subscript -- those come out tagged
// fixed_array.h 124/125 instead.  In this file that is most of the body of
// SetRDPFire(), SetRDPFireMove(), ResetRDPFire() and RDPFireMoveCtrl(), so
// their per-statement numbers are interpolated into the measured gap rather
// than read off a $LM.  Everything else is measured.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), 0x0015d960.

#include "effect_rdr.h"

#include <math.h>
#include <stdio.h>
#include <string.h>                             /* memset */

#include "effect.h"                             /* SetEffects / ResetEffects */
#include "effect_oth.h"                         /* EffOthCandleFlameFlareUpReq */
#include "effect_torch.h"                       /* EffectSetTorch2 family */

#include "../graph3d/ctl/fixed_array.h"
#include "../graph3d/g3dxVu0.h"                 /* g3dxVu0CopyVector */
#include "../../sdk/libvu0.h"

/* Both tables are plain file-scope objects in the ROM, not statics -- the
 * global constructor the object file carries is keyed to burn_fire. */
fixed_array<BURN_FIRE, 30>  burn_fire;                          /* data 2fc740 */
fixed_array<EFFRDR_RSV, 10> pfire_rsv;                          /* data 2fd280 */

/* The ROM defines these below their first use, so it must have carried the
 * same forward declarations. */
static void RDPFireCalcMovePos(float *Position, const float *CenterPos,
                               float Radius, float RotY);
static int  RDPFireCheckTorchTypeExist(int Type);

/* --------------------------------------------------------------------------
 *  Module init.                                                     ROM 45
 *
 *  `tex_id` is accepted and never used; the effect textures this module draws
 *  through are resolved by effect_torch and the EFFECT_CONT handlers instead.
 * ------------------------------------------------------------------------ */
void InitEffectRdr(int tex_id)
{
    int i;

    (void)tex_id;

    memset(&burn_fire, 0, sizeof(burn_fire));                       /* 48 */

    /* pfire_rsv[] cannot be zeroed the same way: a free slot is furn_id -1,
     * and label 0 is a legitimate furniture id. */
    for (i = 0; i < 10; i++)                                        /* 50 */
    {
        pfire_rsv[i].furn_id = -1;                                  /* 52 */
    }
}

/* Genuinely empty in this build -- 8 bytes of `jr ra`. */
void InitEffectRdrEF(void)                                          /* 56 */
{
}

/* --------------------------------------------------------------------------
 *  Claim a free BURN_FIRE slot, marking it in use.                  ROM 63
 * ------------------------------------------------------------------------ */
static void *GetBurnFireWork(void)
{
    int i;

    for (i = 0; i < 30; i++)                                        /* 65 */
    {
        if (burn_fire[i].usefl == 0)
        {
            burn_fire[i].usefl = 1;
            return &burn_fire[i];
        }
    }

    return NULL;                                                    /* 71 */
}                                                                   /* 72 */

/* --------------------------------------------------------------------------
 *  Find the live BURN_FIRE placed under `furn_id`.                  ROM 76
 *
 *  The usefl test is `== 1`, not `!= 0`, so a slot in any other state is
 *  invisible here even though nothing ever writes one.
 * ------------------------------------------------------------------------ */
static void *SearchBurnFireFurnID(int furn_id)
{
    int i;

    for (i = 0; i < 30; i++)                                        /* 78 */
    {
        if (burn_fire[i].usefl == 1 && burn_fire[i].furn_id == furn_id)
        {
            return &burn_fire[i];
        }
    }

    return NULL;                                                    /* 83 */
}                                                                   /* 84 */

/* --------------------------------------------------------------------------
 *  Light a tall candle flame at `pos`.                              ROM 98
 *
 *  Placing the same label twice is a no-op rather than an error, which is what
 *  lets a room be re-registered without stacking duplicate flames.
 *
 *  `room` is taken and never stored.  BURN_FIRE has no field for it and the
 *  ROM's SetRDLongFire() still passes one through, so the parameter is dead in
 *  this build -- kept because both entry points are exported with it.
 * ------------------------------------------------------------------------ */
void SetRDLongFire2(float *pos, u_char sta, float szw, float szh,
                    float sw, float sh, float r, float g, float b,
                    float room, int furn_id)
{
    /* The stab types this `void *` -- GetBurnFireWork() returns void * -- but
     * every use below is a BURN_FIRE member, so it is typed here instead of
     * casting at each one.  The emitted code is the same. */
    BURN_FIRE   *ret;
    EFFECT_CONT *ecw;

    (void)room;

    if (SearchBurnFireFurnID(furn_id) != NULL)                      /* 104 */
    {
        return;
    }

    ret = (BURN_FIRE *)GetBurnFireWork();                           /* 108 */
    if (ret == NULL)
    {
        printf("BurnFire Work Is Full!!\n");                        /* 109 */
        return;                                                     /* 110 */
    }

    /* The call site's own line is swallowed by the g3dxVu0.h inline. */
    g3dxVu0CopyVector(ret->fpos, pos);

    ret->ebuf = SetEffects_FIRE(2, 0, ret->fpos, 0x80, 0x75, 0x70, /* 116 */
                                1.0f, 0xf0, 0xd0, 0xa0, 3.0f);
    if (ret->ebuf != NULL)
    {
        ret->furn_id    = furn_id;                                  /* 119 */
        ret->ligdiff[0] = r;                                        /* 120 */
        ret->ligdiff[1] = g;                                        /* 121 */
        ret->ligdiff[2] = b;                                        /* 122 */
        ret->ligdiff[3] = 1.0f;                                     /* 123 */
        ret->szw        = szw;                                      /* 124 */
        ret->szh        = szh;                                      /* 125 */
        ret->sw         = sw;                                       /* 126 */
        ret->sh         = sh;                                       /* 127 */

        ret->sta = sta;                                             /* 129 */
        if ((sta & 1) != 0)                                         /* 130 */
        {
            ret->pat = 0x20;                                        /* 132 */
        }

        printf("addr:%x\n", (u_int)(uintptr_t)ret->ebuf);           /* 136 */

        /* Hand the work block back to the effect, which reads its parameters
         * out of pnt[5] every frame. */
        ecw = (EFFECT_CONT *)ret->ebuf;                             /* 137 */
        ecw->pnt[5] = ret;                                          /* 138 */
    }
}                                                                   /* 139 */

/* Default candle: unit sprite, unit scroll, "lit from cold" start. */
void SetRDLongFire(float *pos, float r, float g, float b,           /* 142 */
                   float room, int furn_id)
{
    SetRDLongFire2(pos, 3, 1.0f, 1.0f, 1.0f, 1.0f,                  /* 143 */
                   r, g, b, room, furn_id);
}

/* --------------------------------------------------------------------------
 *  Put a candle flame out.                                         ROM 147
 * ------------------------------------------------------------------------ */
void ResetRDLongFire(int furn_id)
{
    BURN_FIRE *ret;

    ret = (BURN_FIRE *)SearchBurnFireFurnID(furn_id);               /* 151 */
    if (ret == NULL)
    {
        printf("Not Find BurnFire Work!!\n");                       /* 152 */
        return;
    }

    ResetEffects(ret->ebuf);                                        /* 163 */
    ret->usefl = 0;                                                 /* 164 */
}

/* --------------------------------------------------------------------------
 *  Ask a live candle flame to flare up.                            ROM 170
 * ------------------------------------------------------------------------ */
void RDLongFireFlareUpReq(int furn_id)
{
    BURN_FIRE *ret;

    ret = (BURN_FIRE *)SearchBurnFireFurnID(furn_id);               /* 174 */
    if (ret == NULL)
    {
        printf("Not Find BurnFire Work!!\n");                       /* 175 */
        return;
    }

    EffOthCandleFlameFlareUpReq((EFFECT_CONT *)ret->ebuf);          /* 179 */
}

/* --------------------------------------------------------------------------
 *  Torch reservation slot lookup.                             ROM 186 / 195
 *
 *  Both return a short, so the -1 miss narrows to 0xffff and is sign-extended
 *  back by the callers' `< 0` test.
 * ------------------------------------------------------------------------ */
short GetRDPFireWork(void)
{
    int i;

    for (i = 0; i < 10; i++)                                        /* 188 */
    {
        if (pfire_rsv[i].furn_id == -1)
        {
            return (short)i;                                        /* 189 */
        }
    }                                                               /* 190 */

    return -1;                                                      /* 191 */
}                                                                   /* 192 */

short SearchRDPFireWork(int furn_id)
{
    int i;

    for (i = 0; i < 10; i++)                                        /* 197 */
    {
        if (pfire_rsv[i].furn_id == furn_id)
        {
            return (short)i;                                        /* 198 */
        }
    }                                                               /* 199 */

    return -1;                                                      /* 200 */
}                                                                   /* 201 */

/* --------------------------------------------------------------------------
 *  Place a static torch.                                          ROM 206
 *
 *  Type 8 is the silent variant.  GCC cross-jumped the two identical `.adr =`
 *  tails into one store, so the ROM emits a single `sw` for both arms; the
 *  source below is the pair that produced it.
 * ------------------------------------------------------------------------ */
void SetRDPFire(float *pos, int furn_id, int Type)
{
    int ret;

    ret = GetRDPFireWork();                                         /* 209 */
    if (ret < 0)
    {
        printf("PFire Work Is Full!!:%d\n", ret);                   /* 210 */
        return;
    }

    g3dxVu0CopyVector(pfire_rsv[ret].Position, pos);

    if (Type == 8)                                                  /* 214 */
    {
        pfire_rsv[ret].adr = EffectSetTorch2NoSE(pfire_rsv[ret].Position, 8);
    }
    else
    {
        pfire_rsv[ret].adr = EffectSetTorch2(pfire_rsv[ret].Position, Type);
    }

    pfire_rsv[ret].furn_id = furn_id;
    pfire_rsv[ret].Type    = Type;
    pfire_rsv[ret].RotY    = 0.0f;
    pfire_rsv[ret].WavePos = 0.0f;
}

/* --------------------------------------------------------------------------
 *  Place the moving torch ("eff_torch_6").                        ROM 233
 *
 *  The placing rotation arrives in degrees and is the *initial* bearing; from
 *  here RDPFireMoveCtrl() carries it round the 1161-unit orbit.  WavePos is
 *  seeded from the same value, so two torches placed at different bearings
 *  bob out of phase.
 *
 *  Only the first of this type gets a sound cue: RDPFireCheckTorchTypeExist()
 *  is the test, and note the inverted sense -- "one already exists" selects
 *  the *silent* constructor.
 * ------------------------------------------------------------------------ */
void SetRDPFireMove(float *pos, float *rot, int furn_id)
{
    float RotY;
    int   ret;

    RotY = rot[1] * 0.017453290f;                                   /* 241 */

    ret = GetRDPFireWork();                                         /* 244 */
    if (ret < 0)
    {
        printf("PFire Work Is Full!!:%d\n", ret);                   /* 245 */
        return;
    }

    g3dxVu0CopyVector(pfire_rsv[ret].CenterPos, pos);
    RDPFireCalcMovePos(pfire_rsv[ret].Position, pos, 1161.0f, RotY);

    if (RDPFireCheckTorchTypeExist(6) == 0)                         /* 250 */
    {
        pfire_rsv[ret].adr = EffectSetTorch2(pfire_rsv[ret].Position, 6);
    }
    else
    {
        pfire_rsv[ret].adr = EffectSetTorch2NoSE(pfire_rsv[ret].Position, 6);
    }

    pfire_rsv[ret].furn_id = furn_id;
    pfire_rsv[ret].Type    = 6;
    pfire_rsv[ret].RotY    = RotY;
    pfire_rsv[ret].WavePos = RotY;

    while (pfire_rsv[ret].RotY > 3.1415925f)                        /* 260 */
    {
        pfire_rsv[ret].RotY -= 6.283185f;
    }
    while (pfire_rsv[ret].RotY < -3.1415925f)                       /* 261 */
    {
        pfire_rsv[ret].RotY += 6.283185f;
    }
}

/* --------------------------------------------------------------------------
 *  Take a torch away.                                             ROM 265
 * ------------------------------------------------------------------------ */
void ResetRDPFire(int furn_id)
{
    int ret;

    ret = SearchRDPFireWork(furn_id);                               /* 269 */
    if (ret < 0)
    {
        /* No %d here, unlike its three siblings -- the ROM's string really is
         * argument-free and no second register is set up for the call. */
        printf("Not Find PFire Work!!\n");                          /* 270 */
        return;
    }

    EffectResetTorch2(pfire_rsv[ret].adr);                          /* 275 */
    pfire_rsv[ret].furn_id = -1;
}

/* --------------------------------------------------------------------------
 *  Place a moving torch on its orbit.                             ROM 288
 *
 *  A point `Radius` out along local +Z, spun by RotY about the centre, then
 *  lifted by a fixed -456 in Y -- the flame hangs below the pivot rather than
 *  at it.
 * ------------------------------------------------------------------------ */
static void RDPFireCalcMovePos(float *Position, const float *CenterPos,
                               float Radius, float RotY)
{
    float matLocalWorld[4][4];
    float LocalPos[4];
    float Offset[4];

    Offset[0] = 0.0f; Offset[1] = -456.0f; Offset[2] = 0.0f; Offset[3] = 1.0f;   /* 297 */

    LocalPos[0] = 0.0f; LocalPos[1] = 0.0f; LocalPos[2] = Radius; LocalPos[3] = 1.0f; /* 300 */

    sceVu0UnitMatrix(matLocalWorld);                                /* 301 */
    sceVu0RotMatrixY(matLocalWorld, matLocalWorld, RotY);           /* 302 */
    sceVu0TransMatrix(matLocalWorld, matLocalWorld, (float *)CenterPos); /* 303 */
    sceVu0ApplyMatrix(Position, matLocalWorld, LocalPos);           /* 304 */
    sceVu0AddVector(Position, Position, Offset);                    /* 305 */
}

/* --------------------------------------------------------------------------
 *  Per-frame pass for the moving torches.                         ROM 312
 *
 *  Both angles are wrapped after the step, and both wraps are a single `if /
 *  else if` rather than a loop -- one step never overshoots by more than the
 *  period, so that is enough.  Note the two use different periods: RotY is
 *  folded to (-PI, PI] and WavePos to (-2PI, 2PI).
 * ------------------------------------------------------------------------ */
void RDPFireMoveCtrl(void)
{
    float RotSpeed  = 0.0047123879f;                                /* 327 */
    float WaveSize  = 15.0f;                                        /* 328 */
    float WaveSpeed = 0.232f;                                       /* 329 */
    int   i;

    for (i = 0; i < 10; i++)                                        /* 332 */
    {
        if (pfire_rsv[i].furn_id != -1 && pfire_rsv[i].Type == 6)
        {
            RDPFireCalcMovePos(pfire_rsv[i].Position,
                               pfire_rsv[i].CenterPos,
                               1161.0f, pfire_rsv[i].RotY);

            /* The bob rides on top of the orbit, applied straight to Y. */
            pfire_rsv[i].Position[1] += WaveSize * sinf(pfire_rsv[i].WavePos);

            pfire_rsv[i].RotY += RotSpeed;
            if (pfire_rsv[i].RotY > 3.1415925f)
            {
                pfire_rsv[i].RotY -= 6.283185f;
            }
            else if (pfire_rsv[i].RotY < -3.1415925f)
            {
                pfire_rsv[i].RotY += 6.283185f;
            }

            pfire_rsv[i].WavePos += WaveSpeed;
            if (pfire_rsv[i].WavePos > 6.283185f)
            {
                pfire_rsv[i].WavePos -= 6.283185f;
            }
            else if (pfire_rsv[i].WavePos < -6.283185f)
            {
                pfire_rsv[i].WavePos += 6.283185f;
            }
        }
    }                                                               /* 355 */
}

/* --------------------------------------------------------------------------
 *  Is a torch of this type already placed?                        ROM 362
 *
 *  Does not stop at the first hit -- the flag is set and the sweep runs on.
 * ------------------------------------------------------------------------ */
static int RDPFireCheckTorchTypeExist(int Type)
{
    int RetFlg = 0;                                                 /* 363 */
    int i;

    for (i = 0; i < 10; i++)                                        /* 366 */
    {
        if (pfire_rsv[i].furn_id != -1 && pfire_rsv[i].Type == Type)
        {
            RetFlg = 1;
        }
    }                                                               /* 370 */

    return RetFlg;                                                  /* 372 */
}
