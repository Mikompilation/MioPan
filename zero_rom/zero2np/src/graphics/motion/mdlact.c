// FILE: /home/zero_rom/zero2np/src/graphics/motion/mdlact.c
//
// Motion "action" helpers: the look-at / neck-aim solvers that bend a
// character's chest, head and eyes toward a world position on top of whatever
// the motion data already produced, plus the interpolation utilities the
// motion player leans on.
//
// The solvers all work the same way: sgdClearCoordCalcFlgParents() invalidates
// a bone and its ancestors, sgdCalcCoordinateMatrix() rebuilds the chain so the
// current world matrices are up to date, then a local rotation is folded into
// the bone's matCoord and the chain is rebuilt again for the next joint down.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
// Trailing /* NNN */ comments are the ROM's source line numbers.

#include "mdlact.h"

#include "motion.h"                          /* LocalRotMatrixX / Y / Z */

#include <math.h>                            /* sinf / fabsf */
#include <string.h>                          /* memset */

#include "mdldat.h"                          /* manmdl_dat / anm_tbl */
#include "../../common/utility2.h"            /* GetClampValF */
#include "../graph3d/g3dMath.h"              /* g3dAcosf */
#include "../graph3d/gra3dSGDData.h"         /* sgdCalcCoordinateMatrix etc. */

static void  motAddRotOffsetZX(SGDCOORDINATE *cp, float roty, float rotz,
                               float (*lwm2)[4]);
static float GetClampValAddOffsetF(float val, float target_val, float spd_val);
static int   motGetTargetRot(float *rot, SGDCOORDINATE *cp, float *target, int xyz);
static int   motCheckRotLimit(float *rot, float limit, float new_rot);
static int   GetPlyrSpd(float *spd, float *new_spd, float *old_spd, ANI_CTRL *ani_ctrl);
static u_int movGetFrameNum(u_int *mov_p);
static float movGetMaxval(u_int *mov_p);

float motGetRandom(float upper, float lower)
{
    return (float)MioPan_Rand() / MIOPAN_RAND_MAXF * (upper - lower) + lower; /* 46 */
}

/* --------------------------------------------------------------------------
 *  motLinearSupValue
 *
 *  Interpolate moto -> saki over all_cnt steps.  mode picks the easing:
 *    0  linear
 *    1  ease-out  (sin ramped from -PI/2, so it decelerates into saki)
 *    2  ease-in   (sin ramped from 0, so it accelerates out of moto)
 *  Anything else leaves the value at moto.
 * ------------------------------------------------------------------------ */
float motLinearSupValue(float moto, float saki, u_char mode, u_int cnt, u_int all_cnt)
{
    u_int now_cnt;
    float val;
    float cnt_rate;
    float dv;

    now_cnt = all_cnt < cnt ? all_cnt : cnt;                            /* 51 */

    if (all_cnt == 0)                                                   /* 57 */
    {
        all_cnt = 1;
    }

    cnt_rate = (float)now_cnt / (float)all_cnt;                         /* 58 */
    dv       = saki - moto;

    switch (mode)                                                       /* 61 */
    {
    case 0:
        val = dv * cnt_rate;                                            /* 65 */
        break;

    case 1:
        val = sinf(cnt_rate * 1.570796251296997f                        /* 68 */
                   + -1.570796251296997f) * dv;
        val = val + dv;                                                 /* 69 */
        break;

    case 2:
        val = dv * sinf(cnt_rate * 1.570796251296997f);                 /* 72 */
        break;

    default:
        return moto;
    }

    return moto + val;                                                  /* 75 */
}

/* --------------------------------------------------------------------------
 *  motGetEneNeckRot
 *
 *  Enemy head aim.  cp2 is the target's coordinate array; bone 2 of it is the
 *  point being looked at.  The yaw comes from the angle between the chest's
 *  (negated) side axis and the direction to that point, the pitch from the
 *  angle between the body-space offset and its horizontal projection.  Both
 *  are clamped, eased into e_rot at 1/neck_spd per call, and finally baked
 *  into trot_m as a rotation in the neck parent's (unscaled) local space.
 *
 *  Returns 0 when the animation has no neck-speed table, i.e. no head aim.
 * ------------------------------------------------------------------------ */
int motGetEneNeckRot(float (*trot_m)[4], ANI_CTRL *ani_ctrl, SGDCOORDINATE *cp2,
                     float *e_rot)
{
    SGDCOORDINATE  *cp;
    NECK_CTRL_WORK *nw;
    float           p[4];
    float           d0[4];
    float           d1[4];
    float           d2[4];
    float           n[4];
    float           v0[4];
    float           v1[4];
    float           trot[4];
    float           inm[4][4];
    float           rotm[4][4];
    float           tmpm[4][4];
    float           lwm[4][4];
    float           scale;
    int             m_no;
    u_short         neck_id;
    u_short         mdl_no;
    u_short         anm_no;
    float           neck_offset;
    float           neck_spd;
    float           lr_limit;

    cp = ani_ctrl->base_p->coordp;                                      /* 82 */
    nw = &ani_ctrl->neck_work;                                          /* 83 */

    memset(trot, 0, sizeof(trot));                                      /* 87 */

    mdl_no = (u_short)ani_ctrl->mdl_no;                                 /* 95 */
    anm_no = (u_short)ani_ctrl->anm_no;                                 /* 96 */
    m_no   = ani_ctrl->anm.playnum;                                     /* 97 */

    if (anm_tbl[anm_no].neck_spd == 0)                                  /* 98 */
    {
        return 0;
    }

    neck_id  = manmdl_dat[mdl_no].neck_id;                              /* 102 */

    neck_spd = anm_tbl[anm_no].neck_spd[m_no];                          /* 104 */
    lr_limit = anm_tbl[anm_no].neck_lr_limit;                           /* 105 */

    sceVu0CopyVector(p, cp2[2].matLocalWorld[3]);                       /* 106 */
    p[3] = 1.0f;                                                        /* 107 */

    sceVu0SubVector(d0, p, nw->chest_lw[3]);                            /* 110 */
    sceVu0CopyVector(d1, nw->chest_lw[1]);                              /* 111 */
    sceVu0SubVector(d2, cp2->matLocalWorld[3], cp->matCoord[3]);        /* 112 */

    /* the ROM negates through double here -- the literals are not "f" */
    d1[0] = d1[0] * -1.0;                                               /* 115 */
    d1[2] = d1[2] * -1.0;                                               /* 116 */

    /* ---- yaw: chest side axis vs. direction to the target, flattened -- */
    sceVu0CopyVector(v0, d0);                                           /* 118 */
    sceVu0CopyVector(v1, d1);                                           /* 119 */
    v0[1] = v1[1] = 0.0f;                                               /* 120 */
    sceVu0Normalize(v0, v0);                                            /* 121 */
    sceVu0ScaleVector(v1, v1, -1.0f);                                   /* 122 */
    sceVu0Normalize(v1, v1);                                            /* 123 */

    trot[1] = g3dAcosf(sceVu0InnerProduct(v0, v1));                     /* 124, 126 */

    neck_offset = 0.5235987305641174f;                                  /* PI/6 */

    if (lr_limit + neck_offset < trot[1])                               /* 127 */
    {
        /* target is behind the shoulder line -- do not aim at all */
        trot[1] = 0.0f;                                                 /* 128 */
    }
    else
    {
        sceVu0OuterProduct(n, v0, v1);                                  /* 131 */

        if (n[1] < 0.0f)                                               /* 132 */
        {
            trot[1] = -trot[1];
        }

        if (lr_limit < trot[1])                                        /* 133 */
        {
            trot[1] = lr_limit;
        }
        else if (trot[1] < -lr_limit)                                  /* 134 */
        {
            trot[1] = -lr_limit;
        }

        /* ---- pitch: body-space offset vs. its horizontal projection -- */
        sceVu0CopyVector(v0, d2);                                      /* 137 */
        sceVu0CopyVector(v1, d2);                                      /* 138 */
        v1[1] = 0.0f;
        sceVu0Normalize(v0, v0);                                       /* 141 */
        sceVu0Normalize(v1, v1);                                       /* 142 */

        trot[2] = g3dAcosf(sceVu0InnerProduct(v0, v1));                 /* 143, 144 */

        if (p[1] < nw->neck_lw[3][1])                                  /* 145 */
        {
            trot[2] = -trot[2];
        }

        if (0.31415924429893494f < trot[2])                            /* 146 */
        {
            trot[2] = 0.31415924429893494f;
        }
        else if (trot[2] < -0.6283184885978699f)                       /* 148 */
        {
            trot[2] = -0.6283184885978699f;
        }
    }

    if (neck_spd == 0.0f)                                               /* 154 */
    {
        trot[1] = 0.0f;                                                 /* 155 */
        trot[2] = 0.0f;                                                 /* 156 */

        neck_spd = 25.0f;                                               /* 157 */
    }

    e_rot[1] = e_rot[1] + (trot[1] - e_rot[1]) / neck_spd;              /* 159 */
    e_rot[2] = e_rot[2] + (trot[2] - e_rot[2]) / neck_spd;              /* 160 */

    /*
     * Rebuild the neck parent's world matrix without its translation and with
     * the 1/25 unit scale divided back out, so the rotations below are applied
     * in a clean orthonormal frame.
     */
    sceVu0CopyMatrix(lwm, nw->neck_p_lw);                               /* 161 */
    sceVu0MulMatrix(lwm, lwm, cp[neck_id].matCoord);                    /* 162 */

    scale = 0.03999999910593033f;                                       /* 163 */

    sceVu0ScaleVectorXYZ(lwm[0], lwm[0], scale);                        /* 164 */
    sceVu0ScaleVectorXYZ(lwm[1], lwm[1], scale);                        /* 165 */
    sceVu0ScaleVectorXYZ(lwm[2], lwm[2], scale);                        /* 166 */

    lwm[3][0] = 0.0f;                                                   /* 167 */
    lwm[3][1] = 0.0f;                                                   /* 168 */
    lwm[3][2] = 0.0f;                                                   /* 169 */

    sceVu0InversMatrix(inm, lwm);                                       /* 170 */

    LocalRotMatrixY(rotm, inm, e_rot[1]);                               /* 172 */
    sceVu0MulMatrix(tmpm, rotm, lwm);                                   /* 174 */
    LocalRotMatrixZ(trot_m, tmpm, e_rot[2]);                            /* 175 */

    return 1;                                                           /* 178 */
}

/* --------------------------------------------------------------------------
 *  motSetNeckWork
 *
 *  Snapshot the neck, neck-parent and chest world matrices so the aim solver
 *  above can work against the pose the motion produced this frame.
 * ------------------------------------------------------------------------ */
int motSetNeckWork(ANI_CTRL *ani_ctrl)
{
    SGDCOORDINATE  *cp;
    NECK_CTRL_WORK *nw;
    u_short         mdl_no;
    u_char          neck_id;
    u_char          chest_id;

    cp = ani_ctrl->base_p->coordp;                                      /* 184 */
    nw = &ani_ctrl->neck_work;                                          /* 185 */

    mdl_no   = (u_short)ani_ctrl->mdl_no;                               /* 186 */
    neck_id  = manmdl_dat[mdl_no].neck_id;                              /* 187 */
    chest_id = manmdl_dat[mdl_no].chest_id;                             /* 188 */

    sceVu0CopyMatrix(nw->neck_lw,   cp[neck_id].matLocalWorld);         /* 190 */
    sceVu0CopyMatrix(nw->neck_p_lw, cp[neck_id].pParent->matLocalWorld); /* 191 */
    sceVu0CopyMatrix(nw->chest_lw,  cp[chest_id].matLocalWorld);        /* 192 */

    nw->flg = 1;                                                        /* 193 */

    return 1;                                                           /* 195 */
}

void GetNeckPos(float *pos, void *ani_hndl)
{
    HeaderSection *hs2;
    SGDCOORDINATE *cp2;
    int            mdl_no;

    hs2    = ((ANI_CTRL *)ani_hndl)->base_p;                            /* 202 */
    cp2    = hs2->coordp;                                               /* 203 */
    mdl_no = ((ANI_CTRL *)ani_hndl)->mdl_no;                            /* 204 */

    sceVu0CopyVector(pos, cp2[manmdl_dat[mdl_no].neck_id].matLocalWorld[3]); /* 206 */
}

/* --------------------------------------------------------------------------
 *  IsTargetInSight
 *
 *  True when pos falls inside the animation's horizontal neck limit widened by
 *  PI/4, measured from bone 25 (the chest).  motCheckRotLimit returns 1 only
 *  when the yaw needed no clamping.
 * ------------------------------------------------------------------------ */
int IsTargetInSight(ANI_CTRL *ani_ctrl, float *pos)
{
    float          neck_offset;
    float          lr_limit;
    HeaderSection *hs;
    SGDCOORDINATE *cp;
    float          rot[4];
    float          limit;

    neck_offset = 0.7853981256484985f;                                  /* PI/4 */
    lr_limit    = anm_tbl[ani_ctrl->anm_no].neck_lr_limit;              /* 214 */
    hs          = ani_ctrl->base_p;                                     /* 215 */
    cp          = hs->coordp;                                           /* 216 */

    limit = lr_limit + neck_offset;                                     /* 218 */

    motGetTargetRot(rot, &cp[25], pos, 1);                              /* 221 */

    return motCheckRotLimit(&rot[1], limit, limit) != 0;                /* 222 */
}

/* --------------------------------------------------------------------------
 *  motAddRotOffsetZX
 *
 *  Fold a Y-then-Z rotation into cp's local matrix, expressed in the parent's
 *  world frame (lwm2).  As in motGetEneNeckRot the frame is stripped of its
 *  translation and rescaled to unit length first, so the inverse is a plain
 *  rotation transpose.
 * ------------------------------------------------------------------------ */
static void motAddRotOffsetZX(SGDCOORDINATE *cp, float roty, float rotz,
                              float (*lwm2)[4])
{
    float scale;
    float inm[4][4];
    float lwm[4][4];
    float tmpm[4][4];

    scale = 0.03999999910593033f;                                       /* 232 */

    sceVu0CopyMatrix(lwm, lwm2);                                        /* 236 */
    sceVu0MulMatrix(lwm, lwm, cp->matCoord);                            /* 237 */

    sceVu0ScaleVectorXYZ(lwm[0], lwm[0], scale);                        /* 238 */
    sceVu0ScaleVectorXYZ(lwm[1], lwm[1], scale);                        /* 239 */
    sceVu0ScaleVectorXYZ(lwm[2], lwm[2], scale);                        /* 240 */

    lwm[3][0] = 0.0f;                                                   /* 241 */
    lwm[3][1] = 0.0f;                                                   /* 242 */
    lwm[3][2] = 0.0f;

    sceVu0InversMatrix(inm, lwm);                                       /* 244 */

    LocalRotMatrixY(tmpm, inm, roty);                                   /* 254 */
    sceVu0MulMatrix(tmpm, tmpm, lwm);                                   /* 255 */
    LocalRotMatrixZ(tmpm, tmpm, rotz);                                  /* 256 */
    sceVu0MulMatrix(cp->matCoord, cp->matCoord, tmpm);                  /* 257 */
}

/* --------------------------------------------------------------------------
 *  GetClampValAddOffsetF
 *
 *  Step val toward target_val by spd_val without overshooting.  The sign of
 *  target_val picks which of the four symmetric arms runs; the ROM's compiler
 *  cross-jumped two of them, which is why the disassembly looks lopsided.
 * ------------------------------------------------------------------------ */
static float GetClampValAddOffsetF(float val, float target_val, float spd_val)
{
    if (0.0f < target_val)                                              /* 264 */
    {
        if (val < target_val)                                           /* 265 */
        {
            val += spd_val;                                             /* 266 */

            if (target_val < val)                                       /* 267 */
            {
                val = target_val;
            }
        }
        else
        {
            val -= spd_val;

            if (val < target_val)
            {
                val = target_val;
            }
        }
    }
    else
    {
        if (target_val < val)                                           /* 277 */
        {
            val -= spd_val;                                             /* 278 */

            if (val < target_val)                                       /* 279 */
            {
                val = target_val;
            }
        }
        else
        {
            val += spd_val;

            if (target_val < val)                                       /* 284 */
            {
                val = target_val;
            }
        }
    }

    return val;                                                         /* 291 */
}

/* --------------------------------------------------------------------------
 *  motLookAtCtrl
 *
 *  The player's look-at chain, solved outward from the body: chest (bones 25
 *  and 1), then head (bones 14 and 2), then each eye (bones 4 and 15).  Every
 *  stage re-derives the world matrices it needs, because the stage before it
 *  has just changed them.
 *
 *  param->enable == 0 does not skip the chain: it drives every target angle to
 *  zero instead, so the pose eases back to the motion's own orientation at the
 *  same speeds.  That is what work_flg carries.
 * ------------------------------------------------------------------------ */
int motLookAtCtrl(ANI_CTRL *ani_ctrl, LOOK_AT_PARAM *param)
{
    float          chest_limit;
    float          rot_diff;
    float          rate;
    float          work_flg;
    float          lr_limit;
    float          rot[4];
    float          dmy[4];
    HeaderSection *hs;
    SGDCOORDINATE *cp;
    int            i;

    chest_limit = 0.6283184885978699f;                                  /* 302 */

    hs = ani_ctrl->base_p;                                              /* 308 */
    cp = hs->coordp;

    work_flg = 0.0f;                                                    /* 313 */

    if (param->enable != 0)
    {
        work_flg = 1.0f;                                                /* 316 */
    }

    lr_limit = anm_tbl[ani_ctrl->anm_no].neck_lr_limit;                 /* 319 */

    /* ---- chest ------------------------------------------------------- */
    sgdClearCoordCalcFlgParents(hs, 1);                                  /* 322 */
    sgdCalcCoordinateMatrix(&cp[1]);                                     /* 324 */

    if (work_flg == 0.0f)                                                /* 325 */
    {
        rot[1] = 0.0f;                                                   /* 326 */
    }
    else
    {
        motGetTargetRot(rot, &cp[1], param->pos, 1);                     /* 329 */
        motCheckRotLimit(&rot[1], 0.6283184885978699f,                   /* 330 */
                         0.6283184885978699f);
    }

    ani_ctrl->chest_rot[1] = GetClampValAddOffsetF(ani_ctrl->chest_rot[1], /* 336 */
                                                  rot[1], param->chest_spd);

    if (chest_limit < ani_ctrl->chest_rot[1])                            /* 339 */
    {
        ani_ctrl->chest_rot[1] = chest_limit;                            /* 340 */
    }
    else if (ani_ctrl->chest_rot[1] < -chest_limit)                      /* 342 */
    {
        ani_ctrl->chest_rot[1] = -chest_limit;
    }

    /* half the yaw at the chest, half at the waist */
    LocalRotMatrixX(cp[25].matCoord, cp[25].matCoord,                    /* 347 */
                    ani_ctrl->chest_rot[1] * 0.5f);
    LocalRotMatrixX(cp[1].matCoord, cp[1].matCoord,                      /* 349 */
                    ani_ctrl->chest_rot[1] * 0.5f);

    /* ---- head -------------------------------------------------------- */
    sgdClearCoordCalcFlgParents(hs, 4);                                  /* 352 */
    sgdCalcCoordinateMatrix(&cp[4]);                                     /* 354 */

    if (work_flg == 0.0f)                                                /* 355 */
    {
        rot[1] = 0.0f;                                                   /* 356 */
        rot[2] = 0.0f;                                                   /* 357 */
        rate   = 0.5f;                                                    /* 358 */
    }
    else
    {
        motGetTargetRot(rot, &cp[2], param->pos, 1);                     /* 361 */
        motCheckRotLimit(&rot[1], lr_limit, lr_limit);                   /* 363 */

        motGetTargetRot(dmy, &cp[4], param->pos, 1);                     /* 366 */

        rot[2] = GetClampValF(dmy[2], 0.31415924429893494f,              /* 369 */
                              -0.6283184885978699f);

        /* small pitch corrections are eased in proportionally, big ones at 0.5 */
        rot_diff = rot[2] - ani_ctrl->neck_rot[2];                       /* 372 */
        rate     = rot_diff / 0.9424777030944824f;                       /* 373 */
        rate     = fabsf(rate);                                          /* 374 */

        if (0.09999999403953552f <= rate)                                /* 376 */
        {
            rate = 0.5f;                                                 /* 378 */
        }
    }

    ani_ctrl->neck_rot[1] = GetClampValAddOffsetF(ani_ctrl->neck_rot[1], /* 385 */
                                                 rot[1], param->head_spd);
    ani_ctrl->neck_rot[2] = GetClampValAddOffsetF(ani_ctrl->neck_rot[2], /* 387 */
                                                 rot[2], param->head_spd * rate);

    /* again split between the two neck joints */
    motAddRotOffsetZX(&cp[14], ani_ctrl->neck_rot[1] * 0.5f,             /* 391 */
                      ani_ctrl->neck_rot[2] * 0.5f,
                      cp[14].pParent->matLocalWorld);
    motAddRotOffsetZX(&cp[2], ani_ctrl->neck_rot[1] * 0.5f,              /* 393 */
                      ani_ctrl->neck_rot[2] * 0.5f,
                      cp[2].pParent->matLocalWorld);

    /* ---- eyes -------------------------------------------------------- */
    sgdClearCoordCalcFlgParents(hs, 4);                                  /* 402 */
    sgdClearCoordCalcFlg(hs, 15);                                        /* 403 */
    sgdCalcCoordinateMatrix(&cp[4]);                                     /* 406 */
    sgdCalcCoordinateMatrix(&cp[15]);                                    /* 407 */

    int   eyes[2]           = { 4, 15 };                                 /* 411 */
    float eye_limits[2][2]  = { {  0.44879892468452454f,                 /* 412 */
                                 -0.3490658104419708f },
                                {  0.3490658104419708f,
                                  -0.44879892468452454f } };

    for (i = 0; i < 2; i++)                                              /* 414 */
    {
        if (work_flg == 0.0f)                                            /* 415 */
        {
            rot[1] = 0.0f;                                               /* 416 */
        }
        else
        {
            motGetTargetRot(rot, &cp[eyes[i]], param->pos, 0);           /* 418 */
        }

        /* an eye turning toward its own nose gets a shorter throw */
        if ((i == 0 && rot[1] < 0.0f) || (i == 1 && 0.0f < rot[1]))      /* 420 */
        {
            rot[1] = rot[1] * 0.4277777075767517f;                       /* 422 */
        }
        else
        {
            rot[1] = rot[1] * 0.75f;                                     /* 424 */
        }

        rot[1] = GetClampValF(rot[1], eye_limits[i][0], eye_limits[i][1]); /* 428 */

        ani_ctrl->eye_rots[i][1] =                                       /* 430 */
            GetClampValAddOffsetF(ani_ctrl->eye_rots[i][1], rot[1], param->eye_spd);

        LocalRotMatrixY(cp[eyes[i]].matCoord, cp[eyes[i]].matCoord,      /* 432 */
                        -ani_ctrl->eye_rots[i][1]);

        rot[2] = GetClampValF(rot[2], 0.18479955196380615f,              /* 438 */
                              -0.1208304762840271f) * 0.699999988079071f;

        ani_ctrl->eye_rots[i][2] =                                       /* 441 */
            GetClampValAddOffsetF(ani_ctrl->eye_rots[i][2], rot[2], param->eye_spd);

        LocalRotMatrixZ(cp[eyes[i]].matCoord, cp[eyes[i]].matCoord,      /* 443 */
                        -ani_ctrl->eye_rots[i][2]);
    }

    return 1;                                                            /* 452 */
}

/* --------------------------------------------------------------------------
 *  movGetMoveval
 *
 *  Pull this frame's root displacement out of the motion's mov table and scale
 *  it by the playback resolution.  X and Y come out negated; W is left as the
 *  table stored it.  interp_flg == 1 means the motion is being blended in, so
 *  frame 0 is used and the result is mixed with the previous frame's speed --
 *  except when the new forward speed would drop, in which case the old value
 *  is held instead.
 * ------------------------------------------------------------------------ */
void movGetMoveval(float *spd, float *old_spd, ANI_CTRL *ani_ctrl, u_int frame_num,
                   float frame_f)
{
    float *dist;
    float  reso_val;
    u_int *mov_p;

    (void)frame_f;                          /* unused in the ROM too */

    if (ani_ctrl->mot.reso > 400)                                        /* 467 */
    {
        ani_ctrl->mot.reso = 400;                                        /* 468 */
    }

    mov_p = ani_ctrl->mdat;

    if (mov_p == 0)                                                      /* 471 */
    {
        return;
    }

    (void)movGetMaxval(mov_p);              /* result discarded by the ROM */ /* 474 */

    if (frame_num >= movGetFrameNum(mov_p))                              /* 475 */
    {
        frame_num = movGetFrameNum(mov_p) - 1;                           /* 476 */
    }

    if (ani_ctrl->interp_flg == 1) frame_num = 0;                        /* 478 */

    dist  = (float *)&mov_p[2];                                          /* 482 */
    dist += frame_num * 4;                                               /* 483 */

    spd[0] = dist[0];                                                    /* 484 */
    spd[1] = dist[1];                                                    /* 485 */
    spd[2] = dist[2];                                                    /* 486 */
    spd[3] = dist[3];                                                    /* 487 */

    reso_val = (float)ani_ctrl->mot.reso / 200.0f;                       /* 489 */

    /*
     * The ROM evaluates "1.0 < reso_val" at line 491 and throws the result
     * away -- the body of that if was compiled out, leaving only the libgcc
     * double-compare calls behind.  Nothing observable, so nothing to port.
     */

    spd[0] = -spd[0] * 25.0f * reso_val;                                 /* 498 */
    spd[1] = -spd[1] * 25.0f * reso_val;                                 /* 499 */
    spd[2] =  spd[2] * 25.0f * reso_val;                                 /* 500 */

    if (ani_ctrl->interp_flg == 1)                                       /* 511 */
    {
        if (spd[2] < old_spd[2])                                         /* 512 */
        {
            sceVu0CopyVector(spd, old_spd);                              /* 513 */
        }
        else
        {
            GetPlyrSpd(spd, spd, old_spd, ani_ctrl);                     /* 516 */
        }
    }
    else
    {
        sceVu0CopyVector(old_spd, spd);                                  /* 520 */
    }

    if (ani_ctrl->mot.reso == 0)                                         /* 535 */
    {
        spd[0] = 0.0f;                                                   /* 536 */
        spd[1] = 0.0f;                                                   /* 537 */
        spd[2] = 0.0f;                                                   /* 538 */
        spd[3] = 0.0f;                                                   /* 539 */
    }
}

/* --------------------------------------------------------------------------
 *  motGetTargetRot
 *
 *  Yaw/pitch from cp toward target.  rot[1] is the horizontal angle between
 *  the bone's local axis `xyz` and the direction to the target, signed by the
 *  cross product; rot[2] is the vertical angle of that direction, signed by
 *  its Y component.  rot[0] and rot[3] are untouched.
 * ------------------------------------------------------------------------ */
static int motGetTargetRot(float *rot, SGDCOORDINATE *cp, float *target, int xyz)
{
    float v0[4];
    float v1[4];
    float d0[4];
    float d1[4];
    float n[4];
    float inner;

    sceVu0CopyVector(d0, cp->matLocalWorld[xyz]);                        /* 560 */
    sceVu0SubVector(d1, target, cp->matLocalWorld[3]);                   /* 561 */

    sceVu0CopyVector(v0, d0);                                            /* 564 */
    sceVu0CopyVector(v1, d1);                                            /* 565 */
    v0[1] = v1[1] = 0.0f;                                                /* 566 */
    sceVu0Normalize(v0, v0);                                             /* 567 */
    sceVu0Normalize(v1, v1);                                             /* 568 */

    inner  = sceVu0InnerProduct(v0, v1);                                 /* 570 */
    rot[1] = g3dAcosf(inner);                                            /* 571 */

    sceVu0OuterProduct(n, v1, v0);                                       /* 572 */

    if (n[1] < 0.0f)                                                     /* 573 */
    {
        rot[1] = -rot[1];                                                /* 574 */
    }

    sceVu0CopyVector(v0, d1);                                            /* 578 */
    sceVu0CopyVector(v1, d1);                                            /* 579 */
    v1[1] = 0.0f;
    sceVu0Normalize(v0, v0);                                             /* 582 */
    sceVu0Normalize(v1, v1);                                             /* 583 */

    inner = sceVu0InnerProduct(v0, v1);                                  /* 584 */

    if (1.0f < inner)                                                    /* 585 */
    {
        rot[2] = 0.0f;                                                   /* 586 */
    }
    else
    {
        rot[2] = g3dAcosf(inner);                                        /* 589 */

        if (d1[1] < 0.0f)                                                /* 590 */
        {
            rot[2] = -rot[2];
        }
    }

    return 1;                                                            /* 593 */
}

/* Clamp *rot to +/-limit, substituting new_rot for the magnitude.  Returns 1
 * only when no clamping was needed. */
static int motCheckRotLimit(float *rot, float limit, float new_rot)
{
    if (limit < *rot)                                                    /* 599 */
    {
        *rot = new_rot;                                                  /* 600 */
    }
    else if (*rot < -limit)                                              /* 602 */
    {
        *rot = -new_rot;                                                 /* 603 */
    }
    else
    {
        return 1;
    }

    return 0;                                                            /* 609 */
}

/* Blend new_spd into old_spd by the motion's interpolation progress. */
static int GetPlyrSpd(float *spd, float *new_spd, float *old_spd, ANI_CTRL *ani_ctrl)
{
    float cnt;
    float allcnt;
    float inter_a[4];
    float inter_b[4];

    cnt    = ani_ctrl->mot.inp_cnt;                                      /* 614 */
    allcnt = (float)ani_ctrl->mot.inp_allcnt;                            /* 615 */

    sceVu0ScaleVectorXYZ(inter_a, new_spd, cnt / allcnt);                /* 618 */
    sceVu0ScaleVectorXYZ(inter_b, old_spd, 1.0f - cnt / allcnt);         /* 619 */
    sceVu0AddVector(spd, inter_a, inter_b);                              /* 620 */

    return 1;                                                            /* 621 */
}

static u_int movGetFrameNum(u_int *mov_p)
{
    return mov_p[0];                                                     /* 626 */
}

static float movGetMaxval(u_int *mov_p)
{
    return *(float *)&mov_p[1];                                          /* 632 */
}
