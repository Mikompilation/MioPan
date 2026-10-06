// FILE: /home/zero_rom/zero2np/src/ingame/camera/finder_camera.c
//
// The Camera Obscura's first-person camera.
//
// While the finder is up the camera stops following the player and becomes the
// player: it sits 709 units up her own local -Y axis (the eye), aims along
// plyr_wrk.frot_x for pitch and mbox.rot[1] for yaw, and takes its FOV from the
// lens fitted to the camera.  Everything else in the file is about how those
// two angles move.
//
// Three things drive them, in order of priority:
//
//   * the scripted swing (mvsta 0x4000 / sta 0x40000).  ReqPointSearchCamera()
//     divides the yaw speed already sitting in mbox.rspd[1] by ten and spends
//     the next ten frames turning; PointSearchCameraCtrl() also serves the
//     quadratic sta-0x40000 variant, which eases in from mbox.trot[] instead.
//     While either is running the player has no control at all.
//   * the paralysis wobble (st.cond == 1), a 0.1-degree twitch that flips
//     direction every four frames.
//   * the player, through SetFinderRot() -- d-pad or stick yaw and pitch, plus
//     the reticle (plyr_wrk.fp) which the stick pushes around within a small
//     box and which springs back to screen centre when the stick is released.
//
// Angles are written as <degrees> * PI / 180.0f throughout.  Every one of the
// five constants reproduces the ROM's stored bits exactly under EE GCC's
// truncate-at-every-operation rounding with PI = 3.1415925f, which is what
// identifies the source form; on the host they land within a couple of ulps.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), finder_camera.o.
// Trailing /* NNN */ comments are the original source line numbers.

#include "finder_camera.h"

#include <math.h>                               /* atan2f / sinf / cosf        */

#include "eetypes.h"
#include "libvu0.h"                             /* sceVu0AddVector             */

#include "../../common/utility.h"               /* RotFvector / GetDist        */
#include "../../common/variable.h"              /* plyr_wrk / opt_wrk / pad     */
#include "../../graphics/graph3d/gra3d.h"       /* gra3dcam*                   */
#include "../../system/os/system.h"             /* GetPALMode                  */
#include "../../system/pad/pad.h"               /* paddat                      */
#include "../photo/freq_camera.h"               /* FreqCamera                  */
#include "../photo/m_plyr_camera.h"             /* m_plyr_camera               */
#include "../plyr/player.h"                     /* IsFinderLocked              */
#include "../plyr/unit_ctl.h"                   /* RotLimitChk                 */

/* The ROM's own PI.  EE GCC truncates float literals toward zero, so this is
 * one ulp below the libm constant -- see [[ee-gcc-truncates-float-literals]]
 * in the surrounding modules. */
#define FC_PI               3.1415925f

/* Finder screen geometry, in 640x448 pixels.  The reticle rests at the screen
 * centre; the stick may push it around inside a 40x30 box centred there, and
 * anything else springs it straight back. */
#define FINDER_CENTER_X     320
#define FINDER_CENTER_Y     224
#define FINDER_RETICLE_X0   300
#define FINDER_RETICLE_X1   340
#define FINDER_RETICLE_Y0   209
#define FINDER_RETICLE_Y1   239

/* Pitch is clamped to +/-60 degrees; the reticle spring uses 1.9 units a frame
 * and the stick 2.0. */
#define FINDER_PITCH_LIMIT  (60.0f * FC_PI / 180.0f)

/* The finder's vertical FOV, 44.374401 degrees in radians.  The ROM folds this
 * to a double in .rodata (3ae3a0) and does the scale in double precision --
 * the float-to-double and double-to-float calls around the multiply are
 * libgcc's, not something the source asked for. */
#define FINDER_FOV          0.7744794026652051

/* paddat[] index of START.  Holding it halves the look speed. */
#define FC_PAD_START        12

/* pad[0].now bits.  Note the layout is remapped from the raw pad report --
 * 0x1000 is UP, not TRIANGLE. */
#define FC_PAD_UP           0x1000
#define FC_PAD_RIGHT        0x2000
#define FC_PAD_DOWN         0x4000
#define FC_PAD_LEFT         0x8000
#define FC_PAD_DPAD         0xf000

/* DualShock analog-pad id.  Without one, only the d-pad path runs. */
#define FC_PAD_ID_ANALOG    0x79

static void SetFinderRot(PLYR_WRK *pPlyrWrk, MOVE_BOX *pMoveBox);
static int  FinderModePadChk(char *pad_x, char *pad_y, float *ax, float *ay,
                             u_char *jyuji_on);
static void PlyrCamCondChk(PLYR_WRK *pPlyrWrk, MOVE_BOX *pMoveBox);
static void PconMahiCameraCtrl(MOVE_BOX *pMoveBox);

/* --------------------------------------------------------------------------
 *  FinderModeCameraCtrl
 *
 *  Place the camera at the eye, decide this frame's aim, then look 1000 units
 *  down it.  The scripted swing and the player controls are mutually exclusive
 *  -- mvsta 0x4000 means a turn is being played out and the pad is ignored
 *  until mloop runs down.
 * ------------------------------------------------------------------------ */
void FinderModeCameraCtrl(void)
{                                                                       /* 50 */
    PLYR_WRK *pPlyrWrk = &plyr_wrk;                                     /* 51 */
    float     Position[4];
    float     Target[4];
    float     RotVec[4];
    float     SetCamPos[4];

    if (IsFinderLocked() != 0)                                          /* 58 */
    {
        return;
    }

    /* The eye, as an offset in the player's own frame.  -709 on Y is up: the
     * model's Y axis points down. */
    Position[0] = 0.0f; Position[1] = -709.0f;                          /* 65 */
    Position[2] = 0.0f; Position[3] = 1.0f;

    RotFvector(pPlyrWrk->cmn_wrk.mbox.rot, Position);                   /* 67 */
    sceVu0AddVector(SetCamPos, pPlyrWrk->cmn_wrk.mbox.pos, Position);   /* 68 */
    SetCamPos[3] = 1.0f;
    gra3dcamSetPosition(SetCamPos);                                     /* 70 */

    if ((pPlyrWrk->cmn_wrk.st.mvsta & 0x4000) != 0)                     /* 73 */
    {
        if (pPlyrWrk->cmn_wrk.mbox.mloop == 0.0f)                       /* 75 */
        {
            pPlyrWrk->cmn_wrk.st.mvsta &= ~0x4000;                      /* 76 */
            /* One source line in the ROM: four stores, no helper call. */
            pPlyrWrk->cmn_wrk.mbox.rspd[0] = 0.0f;                      /* 77 */
            pPlyrWrk->cmn_wrk.mbox.rspd[1] = 0.0f;
            pPlyrWrk->cmn_wrk.mbox.rspd[2] = 0.0f;
            pPlyrWrk->cmn_wrk.mbox.rspd[3] = 0.0f;
        }
        else
        {
            pPlyrWrk->cmn_wrk.mbox.mloop -= 1.0f;                       /* 80 */
            sceVu0AddVector(pPlyrWrk->cmn_wrk.mbox.rot,                 /* 81 */
                            pPlyrWrk->cmn_wrk.mbox.rot,
                            pPlyrWrk->cmn_wrk.mbox.rspd);
            RotLimitChk(&pPlyrWrk->cmn_wrk.mbox.rot[1]);                /* 82 */
        }
    }
    else
    {
        PointSearchCameraCtrl();                                        /* 86 */
        PlyrCamCondChk(pPlyrWrk, &pPlyrWrk->cmn_wrk.mbox);              /* 87 */
        SetFinderRot(pPlyrWrk, &pPlyrWrk->cmn_wrk.mbox);                /* 88 */
    }

    /* Look 1000 units along the aim.  Pitch comes from frot_x, which is the
     * finder's own and separate from the body's mbox.rot[0]. */
    Target[0] = 0.0f; Target[1] = 0.0f;                                 /* 91 */
    Target[2] = 1000.0f; Target[3] = 1.0f;
    RotVec[0] = pPlyrWrk->frot_x;                                       /* 92 */
    RotVec[1] = pPlyrWrk->cmn_wrk.mbox.rot[1];
    RotVec[2] = 0.0f; RotVec[3] = 0.0f;

    RotFvector(RotVec, Target);                                         /* 93 */
    sceVu0AddVector(Target, SetCamPos, Target);                         /* 94 */
    Target[3] = 1.0f;
    gra3dcamSetTarget(Target, 1);                                       /* 96 */

    FreqCamera();                                                       /* 98 */

    gra3dcamSetFov((float)(m_plyr_camera.GetFOVRate() * FINDER_FOV));   /* 100 */
    gra3dApplyCamera(NULL, 1);                                          /* 101 */
}                                                                       /* 109 */

/* --------------------------------------------------------------------------
 *  SetFinderRot
 *
 *  The player's own look control, plus the reticle.
 *
 *  The reticle (plyr_wrk.fp) and the yaw/pitch are steered from the same pad
 *  read but on different rules.  `delta` is the direction the reticle should
 *  travel this frame -- from the stick when there is stick input, otherwise
 *  from the reticle back towards screen centre -- and 10.0f stands in for "it
 *  has nowhere to go", which is why the sentinel is outside atan2f's range.
 * ------------------------------------------------------------------------ */
static void SetFinderRot(PLYR_WRK *pPlyrWrk, MOVE_BOX *pMoveBox)
{                                                                       /* 138 */
    float  delta = 10.0f;                                               /* 141 */
    float  ax;
    float  ay;
    float  dist = 0.0f;                                                 /* 140 */
    float  spd  = 0.0f;
    float  rot;
    char   pad_x;
    char   pad_y;
    u_char jyuji_on;

    /* Paralysed or dead: PconMahiCameraCtrl() already owns the yaw. */
    if (pPlyrWrk->cmn_wrk.st.cond == 1)                                 /* 146 */
    {
        return;
    }

    if (pPlyrWrk->cmn_wrk.st.cond == 3)
    {
        return;
    }

    if (FinderModePadChk(&pad_x, &pad_y, &ax, &ay, &jyuji_on) != 0)     /* 157 */
    {
        delta = atan2f(ax, ay);                                         /* 159 */
        dist  = GetDist(ax, ay);                                        /* 160 */
        spd   = 2.0f;                                                   /* 161 */
    }
    else
    {
        /* No input: spring the reticle back to centre.  The ROM tests both
         * halves at once by punning fp[] to an int. */
        if (*(int *)pPlyrWrk->fp !=                                     /* 165 */
            ((FINDER_CENTER_Y << 16) | FINDER_CENTER_X))
        {
            delta = atan2f((float)(FINDER_CENTER_X - pPlyrWrk->fp[0]),  /* 167 */
                           (float)(FINDER_CENTER_Y - pPlyrWrk->fp[1]));
            spd   = 1.9f;                                               /* 168 */
        }
    }

    if (delta != 10.0f)                                                 /* 173 */
    {
        ax = spd * sinf(delta);                                         /* 174 */
        ay = spd * cosf(delta);                                         /* 175 */

        pPlyrWrk->fp[0] = (short)(pPlyrWrk->fp[0] + (int)ax);           /* 177 */

        if (spd == 2.0f)                                                /* 178 */
        {
            /* Stick input: free inside the box. */
            if (pPlyrWrk->fp[0] < FINDER_RETICLE_X0)                    /* 179 */
            {
                pPlyrWrk->fp[0] = FINDER_RETICLE_X0;                    /* 180 */
            }
            else if (FINDER_RETICLE_X1 < pPlyrWrk->fp[0])               /* 182 */
            {
                pPlyrWrk->fp[0] = FINDER_RETICLE_X1;                    /* 183 */
            }
        }
        /* Springing back: stop dead on the centre rather than overshoot it. */
        else if (0.0f < ax)                                             /* 187 */
        {
            if (FINDER_CENTER_X < pPlyrWrk->fp[0])
            {
                pPlyrWrk->fp[0] = FINDER_CENTER_X;
            }
        }
        else if (ax < 0.0f)                                             /* 191 */
        {
            if (pPlyrWrk->fp[0] < FINDER_CENTER_X)
            {
                pPlyrWrk->fp[0] = FINDER_CENTER_X;                      /* 193 */
            }
        }

        pPlyrWrk->fp[1] = (short)(pPlyrWrk->fp[1] + (int)ay);           /* 197 */

        if (spd == 2.0f)                                                /* 198 */
        {
            if (pPlyrWrk->fp[1] < FINDER_RETICLE_Y0)                    /* 199 */
            {
                pPlyrWrk->fp[1] = FINDER_RETICLE_Y0;                    /* 200 */
            }
            else if (FINDER_RETICLE_Y1 < pPlyrWrk->fp[1])               /* 202 */
            {
                pPlyrWrk->fp[1] = FINDER_RETICLE_Y1;                    /* 203 */
            }
        }
        else if (0.0f < ay)                                             /* 207 */
        {
            if (FINDER_CENTER_Y < pPlyrWrk->fp[1])
            {
                pPlyrWrk->fp[1] = FINDER_CENTER_Y;
            }
        }
        else if (ay < 0.0f)                                             /* 211 */
        {
            if (pPlyrWrk->fp[1] < FINDER_CENTER_Y)
            {
                pPlyrWrk->fp[1] = FINDER_CENTER_Y;                      /* 213 */
            }
        }
    }

    /* Look speed.  Full tilt normally; holding START (or pushing the stick
     * past 125, or using the d-pad) drops it to a slow crawl for aiming. */
    rot = 4.7f * FC_PI / 180.0f;                                        /* 234 */

    if (*paddat[FC_PAD_START] == 0)
    {
        if (125.0f < dist || jyuji_on != 0)                             /* 238 */
        {
            rot = 1.676f * FC_PI / 180.0f;                              /* 240 */
        }
        else
        {
            rot = 0.4f * FC_PI / 180.0f;                                /* 244 */
        }
    }

    if (GetPALMode() != 0)                                              /* 252 */
    {
        rot *= 1.2f;                                                    /* 253 */
    }

    if ((pad[0].now & FC_PAD_LEFT) != 0 || pad_x < 0)                   /* 256 */
    {
        pMoveBox->rot[1] -= rot;                                        /* 257 */
    }
    else if ((pad[0].now & FC_PAD_RIGHT) != 0 || 0 < pad_x)             /* 260 */
    {
        pMoveBox->rot[1] += rot;                                        /* 261 */
    }

    RotLimitChk(&pMoveBox->rot[1]);                                     /* 263 */

    /* Pitch runs at 0.78 of the yaw speed and is hard-clamped rather than
     * wrapped -- RotLimitChk() below only folds it back into range. */
    if (pad_y < 0)                                                      /* 266 */
    {
        pPlyrWrk->frot_x += rot * 0.78f;                                /* 268 */

        if (FINDER_PITCH_LIMIT < pPlyrWrk->frot_x)                      /* 269 */
        {
            pPlyrWrk->frot_x = FINDER_PITCH_LIMIT;                      /* 270 */
        }
    }
    else if (0 < pad_y)                                                 /* 275 */
    {
        pPlyrWrk->frot_x -= rot * 0.78f;                                /* 277 */

        if (pPlyrWrk->frot_x < -FINDER_PITCH_LIMIT)                     /* 278 */
        {
            pPlyrWrk->frot_x = -FINDER_PITCH_LIMIT;
        }
    }

    RotLimitChk(&pPlyrWrk->frot_x);                                     /* 282 */
}                                                                       /* 283 */

/* --------------------------------------------------------------------------
 *  FinderModePadChk
 *
 *  Read the look input into a digital pair (pad_x / pad_y, each -1/0/+1) and
 *  the raw analog pair (ax / ay, centred on zero, +/-128 full scale).  The
 *  d-pad is folded into the analog pair as a fixed +/-40 so both paths score
 *  the same way, and `jyuji_on` ("juuji" = the cross key) records that the
 *  d-pad rather than the stick produced it -- SetFinderRot() reads that back
 *  to pick the slow aiming speed.
 *
 *  Returns non-zero when there is any look input at all.
 * ------------------------------------------------------------------------ */
static int FinderModePadChk(char *pad_x, char *pad_y, float *ax, float *ay,
                            u_char *jyuji_on)
{
    *pad_x = 0; *pad_y = 0; *jyuji_on = 0;                              /* 300 */
    *ax = 0.0f; *ay = 0.0f;                                             /* 301 */

    if ((pad[0].now & FC_PAD_DPAD) != 0)                                /* 303 */
    {
        *jyuji_on = 1;                                                  /* 304 */
    }

    /* Without an analog pad the stick half is skipped entirely, so only the
     * d-pad can steer. */
    if (pad[0].id == FC_PAD_ID_ANALOG || *jyuji_on != 0)                /* 307 */
    {
        if ((pad[0].now & FC_PAD_UP) != 0)                              /* 310 */
        {
            *ay = -40.0f;                                               /* 311 */
        }
        else if ((pad[0].now & FC_PAD_DOWN) != 0)                       /* 314 */
        {
            *ay = 40.0f;                                                /* 315 */
        }

        if ((pad[0].now & FC_PAD_RIGHT) != 0)                           /* 317 */
        {
            *ax = 40.0f;                                                /* 318 */
        }
        else if ((pad[0].now & FC_PAD_LEFT) != 0)                       /* 321 */
        {
            *ax = -40.0f;                                               /* 322 */
        }

        if (*ax == 0.0f && *ay == 0.0f)                                 /* 326 */
        {
            /* analog[0..1] is the right stick, [2..3] the left; ana_replace
             * swaps which one aims. */
            if (opt_wrk.ana_replace == 1)                               /* 327 */
            {
                *ax = (float)pad[0].analog[0] - 128.0f;                 /* 333 */
                *ay = (float)pad[0].analog[1] - 128.0f;                 /* 335 */
            }
            else
            {
                *ax = (float)pad[0].analog[2] - 128.0f;                 /* 337 */
                *ay = (float)pad[0].analog[3] - 128.0f;                 /* 338 */
            }
        }

        if (40.0f <= *ax)                                               /* 342 */
        {
            *pad_x = 1;
        }
        else if (*ax <= -40.0f)                                         /* 346 */
        {
            *pad_x = -1;                                                /* 347 */
        }

        if (40.0f <= *ay)                                               /* 349 */
        {
            *pad_y = 1;
        }
        else if (*ay <= -40.0f)                                         /* 352 */
        {
            *pad_y = -1;                                                /* 353 */
        }

        if (*pad_y != 0)                                                /* 358 */
        {
            if (opt_wrk.view_vertical == 1)                             /* 359 */
            {
                *pad_y = -*pad_y;                                       /* 363 */
            }
        }
    }

    /* Lines 366..381 produce no code -- a commented-out block in the ROM. */

    return (*pad_x != 0 || *pad_y != 0);                                /* 382 */
}

/* --------------------------------------------------------------------------
 *  PlyrCamCondChk
 *
 *  cond 1 is the paralysis ("mahi") status.  It is the only condition that
 *  reaches the finder camera, and it takes the yaw away from the player for
 *  as long as it lasts.
 * ------------------------------------------------------------------------ */
static void PlyrCamCondChk(PLYR_WRK *pPlyrWrk, MOVE_BOX *pMoveBox)
{                                                                       /* 389 */
    if (pPlyrWrk->cmn_wrk.st.cond == 1)                                 /* 390 */
    {
        PconMahiCameraCtrl(pMoveBox);                                   /* 391 */
    }
}

/* --------------------------------------------------------------------------
 *  PconMahiCameraCtrl
 *
 *  The paralysis wobble: a tenth of a degree a frame, reversing every fourth
 *  frame, so the view drifts back and forth over roughly half a degree.
 * ------------------------------------------------------------------------ */
static void PconMahiCameraCtrl(MOVE_BOX *pMoveBox)
{                                                                       /* 401 */
    static u_char time;                             /* sdata 3f08a6 */
    static u_char flag;                             /* sdata 3f08a7 */

    if (time == 0)                                                      /* 404 */
    {
        time = 3;                                                       /* 405 */
        flag ^= 1;                                                      /* 406 */
    }
    else
    {
        time--;                                                         /* 409 */
    }

    if (flag == 0)                                                      /* 412 */
    {
        pMoveBox->rot[1] += 0.1f * FC_PI / 180.0f;                      /* 413 */
    }
    else
    {
        pMoveBox->rot[1] -= 0.1f * FC_PI / 180.0f;                      /* 415 */
    }

    RotLimitChk(&pMoveBox->rot[1]);                                     /* 417 */
}

/* --------------------------------------------------------------------------
 *  ReqPointSearchCamera
 *
 *  Spread whatever yaw delta is sitting in mbox.rspd[1] over the next ten
 *  frames.  The caller has already put the *whole* remaining turn there, which
 *  is why this divides rather than scales up.
 * ------------------------------------------------------------------------ */
void ReqPointSearchCamera(void)
{
    MOVE_BOX *pMoveBox = &plyr_wrk.cmn_wrk.mbox;                        /* 424 */

    pMoveBox->mloop = 10.0f;                                            /* 427 */

    if (pMoveBox->rspd[1] != 0.0f)                                      /* 428 */
    {
        pMoveBox->rspd[1] /= 10.0f;                                     /* 429 */
    }
}

/* --------------------------------------------------------------------------
 *  PointSearchCameraCtrl
 *
 *  Two swings share this, picked by which flag is up.
 *
 *  st.sta 0x40000 is the eased one: mloop counts *up* and the offset from
 *  mbox.trot[] grows as rspd * 0.5 * t^2, so both pitch and yaw accelerate
 *  away from the stored start angle and the whole thing is torn down after ten
 *  frames.  Otherwise mloop counts *down* and the turn is a flat rspd[1] a
 *  frame, which is what ReqPointSearchCamera() sets up.
 * ------------------------------------------------------------------------ */
void PointSearchCameraCtrl(void)
{                                                                       /* 433 */
    MOVE_BOX *pMoveBox = &plyr_wrk.cmn_wrk.mbox;                        /* 434 */
    float     r;

    if ((plyr_wrk.cmn_wrk.st.sta & 0x40000) != 0)                       /* 438 */
    {
        pMoveBox->mloop += 1.0f;                                        /* 439 */

        r = pMoveBox->rspd[0] * 0.5f * pMoveBox->mloop * pMoveBox->mloop /* 442 */
            + pMoveBox->trot[0];                                        /* 443 */
        RotLimitChk(&r);                                                /* 444 */
        plyr_wrk.frot_x = r;                                            /* 445 */

        r = pMoveBox->rspd[1] * 0.5f * pMoveBox->mloop * pMoveBox->mloop /* 447 */
            + pMoveBox->trot[1];                                        /* 448 */
        RotLimitChk(&r);                                                /* 449 */
        pMoveBox->rot[1] = r;                                           /* 450 */

        if (10.0f <= pMoveBox->mloop)                                   /* 452 */
        {
            plyr_wrk.cmn_wrk.st.sta &= ~0x40000;                        /* 453 */
            pMoveBox->rspd[0] = 0.0f; pMoveBox->rspd[1] = 0.0f;         /* 454 */
            pMoveBox->rspd[2] = 0.0f; pMoveBox->rspd[3] = 0.0f;
            pMoveBox->trot[0] = 0.0f; pMoveBox->trot[1] = 0.0f;         /* 455 */
            pMoveBox->trot[2] = 0.0f; pMoveBox->trot[3] = 0.0f;
        }
    }
    else if (0.0f < pMoveBox->mloop)                                    /* 458 */
    {
        pMoveBox->rot[1] += pMoveBox->rspd[1];                          /* 459 */
        RotLimitChk(&pMoveBox->rot[1]);                                 /* 460 */

        pMoveBox->mloop -= 1.0f;                                        /* 462 */

        if (pMoveBox->mloop <= 0.0f)                                    /* 463 */
        {
            pMoveBox->rspd[0] = 0.0f; pMoveBox->rspd[1] = 0.0f;         /* 464 */
            pMoveBox->rspd[2] = 0.0f; pMoveBox->rspd[3] = 0.0f;
        }
    }
}

void ReqFinderCamera(void)
{
    ReqPointSearchCamera();                                             /* 472 */
}

void FinderInCameraCtrl(void)
{
    PointSearchCameraCtrl();                                            /* 477 */
}
