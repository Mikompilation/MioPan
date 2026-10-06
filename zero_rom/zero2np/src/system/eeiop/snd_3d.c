/* ==========================================================================
 *  system/eeiop/snd_3d.c
 *
 *  3D sound placement.  Thirty emitter slots, one listener.  Snd3DCalc() is
 *  the whole model:
 *
 *    - distance drives a single scalar `fVolRate`, linear between
 *      nearest_dist and farthest_dist and clamped at vol_min;
 *    - the angle to the listener's forward axis picks the front or back
 *      level and cross-fades it against `side` to give the two channels;
 *    - the closing speeds of listener and emitter along the line between
 *      them give a doppler pitch, if a velocity was supplied.
 *
 *  All distances are in metres and scaled to world units by snd3DSet1Meter();
 *  the environment record itself is authored in metres and never rescaled,
 *  which is why nearest_dist_calc / farthest_dist_calc exist alongside it.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include <math.h>
#include <stdio.h>

#include "snd3d.h"

#include "../../common/utility2.h"  /* PRINT_ASSERT */
#include "../../sdk/libvu0.h"

/* --------------------------------------------------------------------------
 *  State
 * ------------------------------------------------------------------------ */

/* .data -- the authored defaults, verbatim from the ROM at 0x35e980. */
static SND_3D_ENV snd_3d_env =                                               /* 35e980 */
{
    0.79999995f,                    /* front         */
    0.099999994f,                   /* side          */
    0.69999999f,                    /* back          */
    0.29999998f,                    /* vol_min       */
    30.0f,                          /* farthest_dist */
    2.0f                            /* nearest_dist  */
};

static char             listner_set;                                          /* sdata 3f4992 */
static float            farthest_dist_calc;                                   /* sbss 3f5050 */
static float            nearest_dist_calc;                                    /* sbss 3f5054 */
static float            sound_speed;                                          /* sbss 3f5058 */
static float            dist_unit;                                            /* sbss 3f505c */
static SND_3D_WRK       snd_3d_wrk[30];                                        /* bss 4c00f0 */
static SND_3D_LISTENER  snd_3d_listner;                                        /* bss 4c0690 */

static void Snd3DCalc(SND_3D_WRK *s3d);

/* --------------------------------------------------------------------------
 *  Environment
 * ------------------------------------------------------------------------ */

/* 77 */
static void CheckEnvValue(void)
{
    if (snd_3d_env.front < 0.f || 1.f < snd_3d_env.front)                    /* 78 */
        PRINT_ASSERT("Snd3DEnv.front Is between 0.f and 1.f");               /* 79 */

    if (snd_3d_env.side < 0.f || 1.f < snd_3d_env.side)                      /* 81 */
        PRINT_ASSERT("Snd3DEnv.side Is between 0.f and 1.f");                /* 82 */

    if (snd_3d_env.back < 0.f || 1.f < snd_3d_env.back)                      /* 84 */
        PRINT_ASSERT("Snd3DEnv.back Is between 0.f and 1.f");                /* 85 */

    if (snd_3d_env.vol_min < 0.f || 1.f < snd_3d_env.vol_min)                /* 88 */
        PRINT_ASSERT("Snd3DEnv.vol_min Is between 0.f and 1.f");             /* 89 */

    if (snd_3d_env.farthest_dist <= snd_3d_env.nearest_dist)                 /* 91 */
        PRINT_ASSERT("Snd3DEnv.farthest_dist have to be longer nearest_dist");
}                                                                            /* 94 */

/* 99 */
void Snd3DInit(void)
{
    int i;

    snd3DSet1Meter(1.f);                                                     /* 103 */

    for (i = 0; i < 30; i++)                                                 /* 106 */
        snd_3d_wrk[i].status = 0;                                            /* 107 */

    CheckEnvValue();                                                         /* 110 */
}

/* One world unit per `unit` metres.  Everything derived from the environment
 * record has to be re-derived here, so the two _calc mirrors are rebuilt. */
void snd3DSet1Meter(float unit)
{
    dist_unit          = unit;                                               /* 116 */
    sound_speed        = 340.f * unit;                                       /* 117 */
    nearest_dist_calc  = snd_3d_env.nearest_dist * unit;                     /* 118 */
    farthest_dist_calc = snd_3d_env.farthest_dist * unit;                    /* 119 */
}

void snd3DSetEnvironment(SND_3D_ENV *env)
{
    snd_3d_env = *env;                                                       /* 125 */

    nearest_dist_calc  = snd_3d_env.nearest_dist * dist_unit;                /* 127 */
    farthest_dist_calc = snd_3d_env.farthest_dist * dist_unit;               /* 128 */

    CheckEnvValue();                                                         /* 129 */
}

/* --------------------------------------------------------------------------
 *  Listener
 * ------------------------------------------------------------------------ */

/* 134 */
void snd3DSetListner(SND_3D_SET *set, float *top)
{
    float inner;

    sceVu0CopyVector(snd_3d_listner.pos, *set->pos);                         /* 135 */
    sceVu0CopyVector(snd_3d_listner.dir, *set->dir);                         /* 136 */
    sceVu0CopyVector(snd_3d_listner.top, top);                               /* 137 */

    snd_3d_listner.defer = 1;                                                /* 140 */

    if (set->vel != (sceVu0FVECTOR *)0)                                      /* 142 */
    {
        sceVu0CopyVector(snd_3d_listner.velocity, *set->vel);                /* 143 */
    }
    else                                                                     /* 144 */
    {
        snd_3d_listner.velocity[0] = snd_3d_listner.velocity[1] =
        snd_3d_listner.velocity[2] = snd_3d_listner.velocity[3] = 0.f;       /* 145 */
    }

    /* The forward axis has to be a unit vector -- Snd3DCalc() takes its dot
     * with an already-normalized direction and reads the result as a cosine. */
    inner = sceVu0InnerProduct(snd_3d_listner.dir, snd_3d_listner.dir);      /* 155 */
    if (inner < 0.94999999f || 1.0499999f < inner)                           /* 158 */
        printf("snd3D Listner Dir Is Not Normalized!! inner = %f\n", inner); /* 159 */

    listner_set = 1;                                                         /* 162 */
}                                                                            /* 165 */

/* --------------------------------------------------------------------------
 *  Emitters
 * ------------------------------------------------------------------------ */

void Snd3DFreeWrk(void *hndl)
{
    ((SND_3D_WRK *)hndl)->status &= ~SND_3D_ST_USE;                          /* 171 */
}                                                                            /* 172 */

/* 176 */
void *Snd3DCreateWrk(SND_3D_SET *s3s)
{
    int i;

    for (i = 0; i < 30; i++)                                                 /* 180 */
    {
        if ((snd_3d_wrk[i].status & SND_3D_ST_USE) == 0)                     /* 182 */
        {
            snd_3d_wrk[i].status |= SND_3D_ST_USE;                           /* 183 */

            snd3DSetSET(&snd_3d_wrk[i], s3s);                                /* 185 */
            Snd3DCalc(&snd_3d_wrk[i]);                                       /* 186 */
            return &snd_3d_wrk[i];                                           /* 187 */
        }
    }

    return (void *)0;                                                        /* 190 */
}                                                                            /* 191 */

/* Re-seat an existing handle on a new SND_3D_SET.  `dir` is ignored: the
 * emitter is a point source, so only its own motion matters. */
void snd3DSetSET(void *hndl, SND_3D_SET *set)
{
    SND_3D_WRK *s3d = (SND_3D_WRK *)hndl;

    sceVu0CopyVector(s3d->pos, *set->pos);                                   /* 199 */

    if (set->vel != (sceVu0FVECTOR *)0)                                      /* 202 */
    {
        sceVu0CopyVector(s3d->velocity, *set->vel);                          /* 203 */
        s3d->status |= SND_3D_ST_VEL;                                        /* 204 */
    }
    else                                                                     /* 205 */
    {
        s3d->status &= ~SND_3D_ST_VEL;                                       /* 206 */
    }

    s3d->status |= SND_3D_ST_CALC;                                           /* 219 */
}                                                                            /* 220 */

/* 227 */
void Snd3DSetPosition(void *hndl, float *pos)
{
    SND_3D_WRK *s3d = (SND_3D_WRK *)hndl;

    sceVu0CopyVector(s3d->pos, pos);                                         /* 230 */
    s3d->status |= SND_3D_ST_CALC;                                           /* 231 */
}

void Snd3DGetVal(void *hndl, VOLSET *volset, short *pitch)
{
    SND_3D_WRK *s3d = (SND_3D_WRK *)hndl;

    volset->l = s3d->voll;                                                   /* 239 */
    volset->r = s3d->volr;                                                   /* 240 */
    *pitch    = s3d->pitch;                                                  /* 241 */
}

/* --------------------------------------------------------------------------
 *  The model
 * ------------------------------------------------------------------------ */

/* 244 */
float GetLenDirection(float *velocity, float *unit_dir)
{
    float         vel_len;
    sceVu0FVECTOR listner_vel_unit;

    vel_len = sceVu0InnerProduct(velocity, velocity);                        /* 250 */
    vel_len = sqrtf(vel_len);                                                /* 251 */

    sceVu0ScaleVector(listner_vel_unit, velocity, 1.f / vel_len);            /* 253 */

    return vel_len * sceVu0InnerProduct(unit_dir, listner_vel_unit);         /* 255 */
}                                                                            /* 257 */

/* 263 */
static void Snd3DCalc(SND_3D_WRK *s3d)
{
    sceVu0FVECTOR unit;
    float         dist;
    float         vol;
    float         fVolRate;
    float         temp_dist;
    sceVu0FVECTOR rightvec;
    int           right;
    float         cos;
    float         revcos;
    float         temp;

    if (listner_set == 0)                                                    /* 270 */
        PRINT_ASSERT("3D Sound Cannot Do Without Listner Setting!");         /* 271 */

    sceVu0SubVector(unit, s3d->pos, snd_3d_listner.pos);                     /* 278 */
    dist = sceVu0InnerProduct(unit, unit);                                   /* 279 */
    dist = sqrtf(dist);                                                      /* 280 */
    sceVu0ScaleVector(unit, unit, 1.f / dist);                               /* 281 */

    vol = 16383.f;                                                           /* 286 */

    if (farthest_dist_calc <= dist)                                          /* 288 */
    {
        fVolRate = snd_3d_env.vol_min;                                       /* 289 */
    }
    else                                                                     /* 291 */
    {
        temp_dist = dist - nearest_dist_calc;                                /* 294 */

        if (0.f < temp_dist)                                                 /* 297 */
        {
            fVolRate = 1.f - temp_dist /
                             (farthest_dist_calc - nearest_dist_calc);       /* 298 */
            if (fVolRate < snd_3d_env.vol_min)                               /* 299 */
                fVolRate = snd_3d_env.vol_min;
        }                                                                    /* 303 */
        else
        {
            fVolRate = 1.f;                                                  /* 304 */
        }
    }

    vol = vol * fVolRate;                                                    /* 316 */

    /* `rightvec` is top x dir.  Its sign against the emitter direction says
     * which ear leads; the ROM reads a negative dot as "on the right", so the
     * cross product comes out pointing left in this coordinate system. */
    sceVu0OuterProduct(rightvec, snd_3d_listner.top, snd_3d_listner.dir);    /* 316 */
    right = (sceVu0InnerProduct(rightvec, unit) < 0.f) ? 1 : 0;              /* 317, 319 */

    cos = sceVu0InnerProduct(unit, snd_3d_listner.dir);                      /* 323 */
    if (cos < 0.f)                                                           /* 325 */
    {
        cos    = -cos;                                                       /* 329 */
        revcos = 1.f - cos;                                                  /* 330 */
        temp   = snd_3d_env.back;                                            /* 331 */
    }                                                                        /* 332 */
    else
    {
        revcos = 1.f - cos;                                                  /* 334 */
        temp   = snd_3d_env.front;                                           /* 335 */
    }

    /* The near ear keeps the full off-axis term, the far ear only `side` of
     * it; both keep the same front/back term. */
    if (right)                                                               /* 338 */
    {
        s3d->voll = (short)(vol * (revcos * snd_3d_env.side + cos * temp));  /* 339 */
        s3d->volr = (short)(vol * (revcos + cos * temp));                    /* 340 */
    }                                                                        /* 341 */
    else
    {
        s3d->voll = (short)(vol * (revcos + cos * temp));                    /* 342 */
        s3d->volr = (short)(vol * (revcos * snd_3d_env.side + cos * temp));  /* 343 */
    }

    if (s3d->status & SND_3D_ST_VEL)                                         /* 348 */
    {
        float listner_vel = GetLenDirection(snd_3d_listner.velocity, unit);  /* 367 */
        float vel         = GetLenDirection(s3d->velocity, unit);            /* 370 */

        s3d->pitch = (int)(((sound_speed + listner_vel) * 4096.f) /
                           (sound_speed + vel));                             /* 383 */
    }                                                                        /* 389 */
    else
    {
        s3d->pitch = 0x1000;                                                 /* 390 */
    }

    s3d->status &= ~SND_3D_ST_CALC;                                          /* 394 */
}                                                                            /* 395 */

/* 400 */
void Snd3DMain(void)
{
    int i;

    /* The listener moved, so every live emitter's pan is stale -- the CALC
     * flag only tracks emitter-side movement. */
    if (snd_3d_listner.defer)                                                /* 403 */
    {
        for (i = 0; i < 30; i++)                                             /* 404 */
        {
            if (snd_3d_wrk[i].status & SND_3D_ST_USE)                        /* 406 */
                Snd3DCalc(&snd_3d_wrk[i]);                                   /* 408 */
        }

        snd_3d_listner.defer = 0;                                            /* 411 */
    }
    else                                                                     /* 412 */
    {
        for (i = 0; i < 30; i++)                                             /* 413 */
        {
            if ((snd_3d_wrk[i].status & (SND_3D_ST_USE | SND_3D_ST_CALC)) ==
                (SND_3D_ST_USE | SND_3D_ST_CALC))                            /* 415 */
                Snd3DCalc(&snd_3d_wrk[i]);                                   /* 420 */
        }
    }
}                                                                            /* 425 */
