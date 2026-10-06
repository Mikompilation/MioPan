// FILE: /home/zero_rom/zero2np/src/ingame/enemy/fly_ctrl.c
//
// The flying sub-creatures.
//
// Forty resident FLY_WRK slots serve the whole field.  A hostile ghost that
// declares a fly type in ENE_DAT::fly_type[] claims five of them per type when
// its own model is requested, and the action script fires them off one at a
// time with EneFlyAct().  Each creature homes on a unit for trace_time frames,
// then flies straight until its lifetime runs out; touching the target's head
// damages it.  The camera kills them: PhotoFlyChk() fades out everything in
// shot.
//
// Two creature types have no model at all -- mdl_no bit 0x8000 -- and are just
// a torch effect flying around.  CheckEffectFly() is that test, and it forks
// almost every function in the file.
//
// FlyInit() and FlyAct(), the ownerless API, are exported but have no call
// site anywhere in the ROM (checked by scanning every jal in the loadable
// segments, not by Ghidra xrefs).  They are reconstructed because the object
// file defines them.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), fly_ctrl.o.
// Trailing /* NNN */ comments are the original source line numbers.

#include "fly_ctrl.h"

#include <stdio.h>                              /* printf                      */
#include <string.h>                             /* memset                      */
#include <math.h>                               /* sinf / cosf / fabsf         */

#include "eetypes.h"
#include "libvu0.h"
#include "enemy.h"                              /* ENE_WRK / ENE_STATUS_ACT    */

#include "../../common/utility.h"               /* GetDistV / _SetVector       */
#include "../../common/variable.h"              /* plyr_wrk / sis_wrk          */
#include "../../graphics/mmanage.h"
#include "../../graphics/graph3d/ctl/fixed_array.h"
#include "../../graphics/graph3d/gra3d.h"       /* gra3dGetCamera / lights     */
#include "../../graphics/graph3d/g3dLight.h"    /* G3DLIGHT                    */
#include "../../graphics/motion/mdlwork.h"      /* ANI_CTRL                    */
#include "../../graphics/motion/motion.h"       /* motInitOneEnemyAnm          */
#include "../../graphics/effect/effect_torch.h" /* EffectSetTorch2             */
#include "../../system/os/system.h"             /* GetPALMode                  */
#include "../ingame.h"                          /* SetIngameDamageMode         */
#include "../plyr/ChrSort.h"
#include "../plyr/player.h"                     /* SetPlyrAnime / FrameInsideChk */
#include "../plyr/sister.h"                     /* IsSisWrk / SetSisterAnime   */
#include "../plyr/unit_ctl.h"                   /* GetTrgtRot / RotLimitChk    */
#include "../../graphics/graph3d/g3dxVu0.h" /* g3dxVu0CopyVector */

/* --------------------------------------------------------------------------
 *  Module state
 * ----------------------------------------------------------------------- */

/* The six creature types.  Types 1 and 4 carry mdl_no 0x8000 -- effect-only,
 * no model file, and their anm_no is never read.  attr bit 1 (types 1 and 2's
 * neighbours) suppresses the X rotation so the creature stays level; bit 0
 * (type 3) keeps it out of the photo check. */
FLY_DATA fly_dat[6] =                                       /* data 312948 */
{
    /* 0 */ { 0x00,  64, 0x000e, 0x0010, 1500, 100, 0, 120,   0,
              20.0f, 3.0f,          1200.0f, 20.0f, 3.0f, 700.0f, 35.0f, 1.0f },
    /* 1 */ { 0x02, 128, 0x8000, 0x0000, 1500, 100, 0, 300,   0,
              15.0f, 4.0f,          1000.0f, 15.0f, 4.0f, 400.0f, 15.0f, 1.0f },
    /* 2 */ { 0x00,  64, 0x000e, 0x0010, 1500, 100, 0, 400,  30,
              30.0f, 0.19999999f,   1000.0f, 20.0f, 5.0f, 500.0f, 10.0f, 1.1999999f },
    /* 3 */ { 0x01,  64, 0x000e, 0x0010, 1500, 100, 0, 400,   0,
              12.0f, 1.0f,          1500.0f, 12.0f, 1.0f, 700.0f, 10.0f, 0.29999998f },
    /* 4 */ { 0x00, 128, 0x8000, 0x0000, 1800, 100, 0, 400,  30,
              30.0f, 0.19999999f,   1000.0f, 20.0f, 5.0f, 500.0f, 10.0f, 1.1999999f },
    /* 5 */ { 0x00,  64, 0x000e, 0x0010, 1800, 100, 0, 400,  90,
              30.0f, 0.19999999f,   1000.0f, 16.0f, 5.0f, 500.0f,  8.0f, 1.1999999f }
};

#define FLY_WRK_MAX 40

static fixed_array<FLY_WRK, FLY_WRK_MAX> fly_wrk;           /* bss 47c0a0 */

/* The ROM's own float constants, exact.  GCC 2.96-ee truncated rather than
 * rounded its literals, so PI here is one ulp below (float)M_PI and the PAL
 * rate one ulp below 1.2 -- written out in full so the bits match. */
#define FLY_PI          3.1415925f      /* lit4 3ee2b8 .. 3ee2f4  */
#define FLY_PAL_RATE    1.1999999f      /* lit4 3ee2cc / 3ee2d0 / 3ee2f0 */
#define FLY_ADJP_STEP   0.0087266453f   /* lit4 3ee2f8, half a degree   */

static int      FlyModelDataInit(FLY_WRK *fw);
static int      FlyEffectDataInit(FLY_WRK *fw);
static PLCMN_WRK *FlyPlyrHitChk(FLY_WRK *fw);
static void     FlyAtkHit(FLY_WRK *fw, PLCMN_WRK *target);
static void     FlyMove(FLY_WRK *fw);
static FLY_WRK *GetFlyWork(void);
static void     ClearCheckFlyWork(void);

/* Bit 0x8000 of the table's model number: this creature is a torch effect, not
 * a model.  Note the inverted sense against the rest of the file -- non-zero
 * means "no model to load". */
static int CheckEffectFly(FLY_WRK *fw)                                  /* 158 */
{
    return (fw->mdl_no & 0x8000) != 0;                                  /* 160 */
}

/* --------------------------------------------------------------------------
 *  Allocation
 * ----------------------------------------------------------------------- */

FLY_WRK *FlyInit(u_char type)                                           /* 169 */
{
    FLY_WRK *fw;

    if ((fw = GetFlyWork()) == nullptr)                                 /* 172 */
    {
        printf("FLY_BUFFER is FULL!!\n");                               /* 173 */
        return nullptr;                                                 /* 174 */
    }

    fw->init_flow = 0;                                                  /* 177 */
    fw->dat       = &fly_dat[type];                                     /* 178 */
    fw->mdl_no    = fly_dat[type].mdl_no;                               /* 179 */
    fw->anm_no    = fly_dat[type].anm_no;                               /* 180 */
    fw->adjp_cnt  = 0.0f;                                               /* 181 */
    fw->adjp_add  = ((float)(GetRndSP(0, 70) + 20) * FLY_PI) / 1800.0f; /* 182 */
    fw->adjp_span = (float)GetRndSP(0, 70) + 10.0f;                     /* 183 */
    fw->adjp_ang  = ((float)GetRndSP(0, 360) * FLY_PI) / 1800.0f;       /* 184 */

    if (CheckEffectFly(fw) == 0)                                        /* 186 */
    {
        FlyModelDataInit(fw);                                           /* 187 */
    }
    else
    {
        /* Nothing to load, so the slot is ready the moment it is claimed. */
        fw->sta |= FLY_STA_USE;                                         /* 190 */
    }

    return fw;                                                          /* 193 */
}                                                                       /* 194 */

int FlyAct(FLY_WRK *fw, u_char type, float *pos, float *rot,
           PLCMN_WRK *target)                                           /* 197 */
{
    if ((fw->sta & FLY_STA_USE) == 0)  { return 0; }                    /* 198 */
    if ((fw->sta & FLY_STA_MOVE) != 0) { return 0; }                    /* 198 */

    if (CheckEffectFly(fw) == 0)                                        /* 199 */
    {
        /* Still streaming: the caller retries next frame. */
        if (FlyModelDataInit(fw) != 1) { return 0; }                    /* 200 */

        SetFlyOne(fw, nullptr, type, pos, rot, target);                 /* 201 */
    }
    else
    {
        FlyEffectDataInit(fw);                                          /* 205 */
        SetFlyOne(fw, nullptr, type, pos, rot, target);                 /* 206 */
    }

    return 1;                                                           /* 207 */
}                                                                       /* 211 */

void EneFlyInit(ENE_WRK *ew)                                            /* 220 */
{
    int      i;
    int      j;
    FLY_WRK *fw;
    short    type;

    /* Passive ghosts never carry a swarm. */
    if (ew->type == 2) { return; }                                      /* 226 */

    for (i = 0; i < 3; i++)                                             /* 230 */
    {
        type = ew->dat->fly_type[i];                                    /* 231 */

        if (type < 0) { continue; }                                     /* 233 */

        for (j = 0; j < 5; j++)                                         /* 234 */
        {
            if ((fw = GetFlyWork()) == nullptr)                         /* 236 */
            {
                printf("FLY_BUFFER is FULL!!\n");                       /* 237 */
                return;                                                 /* 238 */
            }

            fw->init_flow = 0;                                          /* 240 */
            fw->dat       = &fly_dat[type];                             /* 241 */
            fw->mdl_no    = fly_dat[type].mdl_no;                       /* 242 */
            fw->anm_no    = fly_dat[type].anm_no;                       /* 243 */
            fw->adjp_cnt  = 0.0f;
            fw->adjp_add  = ((float)(GetRndSP(0, 70) + 20) * FLY_PI) / 1800.0f;
                                                                        /* 245 */
            fw->adjp_span = (float)GetRndSP(0, 70) + 10.0f;             /* 246 */
            fw->adjp_ang  = ((float)GetRndSP(0, 360) * FLY_PI) / 1800.0f;
                                                                        /* 247 */
            ew->fw[i][j]  = fw;                                         /* 248 */

            if (CheckEffectFly(fw) == 0)                                /* 250 */
            {
                FlyModelDataInit(fw);                                   /* 251 */
            }
            else
            {
                fw->sta |= FLY_STA_USE;                                 /* 254 */
            }
        }                                                               /* 256 */
    }                                                                   /* 258 */
}                                                                       /* 259 */

int EneFlyModelInitWait(ENE_WRK *ew)                                    /* 262 */
{
    int ret = 1;
    int i;
    int j;

    if (ew->type == 2) { return 1; }                                    /* 267 */

    for (i = 0; i < 3; i++)                                             /* 271 */
    {
        if (ew->dat->fly_type[i] < 0) { continue; }                     /* 273/275 */

        /* An effect-only swarm has nothing to stream, so it is skipped whole
         * -- the type is uniform across the five slots. */
        if (CheckEffectFly(ew->fw[i][0]) != 0) { continue; }            /* 277 */

        for (j = 0; j < 5; j++)                                         /* 279 */
        {
            if ((ew->fw[i][j]->sta & FLY_STA_USE) != 0)                 /* 281 */
            {
                ret &= FlyModelDataInit(ew->fw[i][j]);                  /* 283 */
            }
        }                                                               /* 286 */
    }                                                                   /* 289 */

    return ret;                                                         /* 290 */
}                                                                       /* 291 */

void EneFlyAct(ENE_WRK *ew, u_char type, float *pos, float *rot,
               PLCMN_WRK *target)                                       /* 294 */
{
    int i;
    int j;
    int m;
    int n;

    if (ew->type == 2) { return; }                                      /* 297 */

    /* Which of the parent's three swarms is this type? */
    if ((u_short)type == ew->dat->fly_type[0])                          /* 302 */
    {
        n = 0;                                                          /* 301 */
    }
    else
    {
        n = -1;

        for (i = 1; i < 3; i++)                                         /* 306 */
        {
            if ((u_short)type == ew->dat->fly_type[i])                  /* 302 */
            {
                n = i;                                                  /* 303 */
                break;
            }
        }
    }

    if (n == -1) { return; }                                            /* 304 */

    /* The first idle creature in it. */
    m = -1;

    for (j = 0; j < 5; j++)                                             /* 311 */
    {
        if ((ew->fw[n][j]->sta & FLY_STA_USE) != 0 &&                   /* 312 */
            (ew->fw[n][j]->sta & FLY_STA_MOVE) == 0)
        {
            m = j;
            break;
        }
    }                                                                   /* 316 */

    if (m < 0)                                                          /* 317 */
    {
        printf("ENE_FLY_BUFFER OVER!!\n");                              /* 323 */
        return;
    }

    if (CheckEffectFly(ew->fw[n][m]) != 0)                              /* 318 */
    {
        FlyEffectDataInit(ew->fw[n][m]);                                /* 319 */
    }

    SetFlyOne(ew->fw[n][m], ew, type, pos, rot, target);                /* 321 */
}                                                                       /* 325 */

void EneFlyAnmctrlRelease(ENE_WRK *ew)                                  /* 328 */
{
    int i;
    int j;

    if (ew->type == 2) { return; }                                      /* 331 */

    for (i = 0; i < 3; i++)                                             /* 335 */
    {
        if (ew->dat->fly_type[i] < 0) { continue; }                     /* 336 */

        for (j = 0; j < 5; j++)                                         /* 337 */
        {
            FlyRelease(ew->fw[i][j]);                                   /* 338 */
        }                                                               /* 339 */
    }                                                                   /* 341 */
}                                                                       /* 342 */

/* --------------------------------------------------------------------------
 *  Streaming
 *
 *  init_flow is a four-state cursor: 0 request, 1 wait, 2 ready, 3 failed.
 *  Only state 2 returns 1, which is what EneFlyModelInitWait() gates on; state
 *  3 clears FLY_STA_USE so the slot is handed back to GetFlyWork().
 * ----------------------------------------------------------------------- */

static int FlyModelDataInit(FLY_WRK *fw)                                /* 350 */
{
    int ret = 0;
    int LoadOK;

    switch (fw->init_flow)                                              /* 353 */
    {
    case 0:
        if (mmanageReqAnm(fw->anm_no) == OL_LOAD_ERR_WORK_LACK)         /* 355 */
        {
            fw->init_flow = 3;                                          /* 356 */
            printf("mmanageReqAnm() > FLY_INIT_FAILURE\n");             /* 357 */
        }
        if (mmanageReqMdl(fw->mdl_no) == OL_LOAD_ERR_WORK_LACK)         /* 359 */
        {
            fw->init_flow = 3;                                          /* 360 */
            printf("mmanageReqMdl() > FLY_INIT_FAILURE\n");             /* 361 */
        }
        fw->sta |= FLY_STA_USE;                                         /* 363 */
        fw->init_flow = 1;                                              /* 364 */
        break;

    case 1:
        /* Bitwise & on purpose: both halves are polled every frame, because
         * each is what hands back its own base address. */
        LoadOK = mmanageIsReadyMdl(fw->mdl_no, &fw->mdl_p, 0) &         /* 369 */
                 mmanageIsReadyAnm(fw->anm_no, &fw->anm_p, 0);          /* 370 */

        if (LoadOK == 0) { break; }                                     /* 371 */

        fw->ani_ctrl = (ANI_CTRL *)motInitOneEnemyAnm(                  /* 372 */
                           (u_int *)fw->anm_p, (u_int *)fw->mdl_p,
                           fw->mdl_no, fw->anm_no);
        if (fw->ani_ctrl == nullptr)
        {
            printf("ANI_CTRL assign failure!!\n");                      /* 373 */
            fw->init_flow = 3;                                          /* 374 */
            break;
        }

        printf("ANI_CTRL addr: %x\n", fw->ani_ctrl);                    /* 376 */
        ChrSortRegistFly(fw);                                           /* 377 */
        fw->init_flow = 2;                                              /* 378 */
        break;                                                          /* 381 */

    case 2:
        ret = 1;                                                        /* 386 */
        break;

    case 3:
        /* Give the slot back; nothing else will ever finish loading it. */
        fw->sta &= ~FLY_STA_USE;                                        /* 389 */
        break;
    }

    return ret;                                                         /* 390 */
}                                                                       /* 392 */

static int FlyEffectDataInit(FLY_WRK *fw)                               /* 399 */
{
    u_short type = (u_short)fw->mdl_no & 0x7fff;                        /* 401 */

    /* The low bits of a 0x8000 model number pick the torch flavour. */
    if (type == 1)                                                      /* 403 */
    {
        fw->efpw = EffectSetTorch2(fw->npos, 2);                        /* 409 */
    }
    else if (type == 0 || type == 2)                                    /* 406 */
    {
        fw->efpw = EffectSetTorch2(fw->npos, 5);                        /* 407/409 */
    }

    return 1;                                                           /* 416 */
}

void FlyModelRelease(FLY_WRK *fw)                                       /* 425 */
{
    if (fw->ani_ctrl != nullptr)                                        /* 426 */
    {
        motReleaseOneAnm(fw->ani_ctrl);                                 /* 427 */
        fw->ani_ctrl = nullptr;                                         /* 428 */
        printf("FlyWrk [%x] Heap Release!!\n", 0);                      /* 429 */
        ChrSortDeleteFly(fw);                                           /* 431 */
    }

    mmanageClearAnm(fw->anm_no);                                        /* 433 */
    mmanageClearMdl(fw->mdl_no);                                        /* 434 */
    printf("FlyWrk [Model & Anime] Heap Release!!\n");                  /* 435 */
}

void FlyEffectRelease(FLY_WRK *fw)                                      /* 441 */
{
    if (fw->efpw != nullptr && ((u_short)fw->mdl_no & 0x7fff) < 3)      /* 442/444 */
    {
        EffectResetTorch2(fw->efpw);                                    /* 450 */
        fw->efpw = nullptr;                                             /* 451 */
    }
}                                                                       /* 452 */

void FlyRelease(FLY_WRK *fw)                                            /* 459 */
{
    if (CheckEffectFly(fw) != 0)                                        /* 460 */
    {
        FlyEffectRelease(fw);                                           /* 461 */
    }
    else
    {
        FlyModelRelease(fw);                                            /* 463 */
    }

    fw->sta = 0;                                                        /* 465 */
}

/* --------------------------------------------------------------------------
 *  Player interaction
 * ----------------------------------------------------------------------- */

/* Everything the camera has in shot starts fading.  attr bit 0 exempts a
 * creature -- it cannot be shot down. */
void PhotoFlyChk(void)                                                  /* 476 */
{
    FLY_WRK *fw;
    float    tx;
    float    ty;
    u_char   i;

    for (i = 0; i < FLY_WRK_MAX; i++)                                   /* 482 */
    {
        fw = &fly_wrk[i];

        if ((fw->sta & FLY_STA_MOVE) == 0)  { continue; }               /* 484 */
        if ((fw->dat->attr & 1) != 0)       { continue; }               /* 485 */

        if (GetDistV(plyr_wrk.cmn_wrk.mbox.pos, fw->npos) <= 5000.0f)   /* 487/488 */
        {
            if (OutSightChk(fw->npos, gra3dGetCamera()->matCoord[3],    /* 489 */
                            plyr_wrk.cmn_wrk.mbox.rot[1],
                            2.0943949f, 5000.0f) == 0 &&
                FrameInsideChk(fw->npos, &tx, &ty) != 0)
            {
                fw->sta |= FLY_STA_FADE;                                /* 492 */
            }
        }
    }                                                                   /* 495 */
}

/* Which unit, if any, this creature is touching.  The box is hit_rng on X and
 * Z; on Y it is hit_rng upwards but hit_rng + 600 downwards, so a creature
 * above head height still connects. */
static PLCMN_WRK *FlyPlyrHitChk(FLY_WRK *fw)                            /* 503 */
{
    float      pf[4];
    float      pp[4];
    PLCMN_WRK *p1;
    PLCMN_WRK *p2;
    PLCMN_WRK *ret = nullptr;

    if (IsSisWrk() == 0)                                                /* 510 */
    {
        /* Alone: only the player can be hit. */
        g3dxVu0CopyVector(pf, fw->npos);
        g3dxVu0CopyVector(pp, plyr_wrk.cmn_wrk.headpos);                /* 512 */

        if (fabsf(pf[0] - pp[0]) > (float)fw->dat->hit_rng) { return nullptr; }
                                                                        /* 517 */
        if (fabsf(pf[2] - pp[2]) > (float)fw->dat->hit_rng) { return nullptr; }
                                                                        /* 519 */

        if (pf[1] < pp[1])                                              /* 520 */
        {
            if (fabsf(pf[1] - pp[1]) <= (float)fw->dat->hit_rng)        /* 521 */
            {
                ret = &plyr_wrk.cmn_wrk;
            }
        }
        else if (fabsf(pf[1] - pp[1]) <= (float)(fw->dat->hit_rng + 600))
        {                                                               /* 524 */
            ret = &plyr_wrk.cmn_wrk;
        }
    }
    else
    {
        /* With the companion present the creature's own target is checked
         * first, then the other unit. */
        p1 = (fw->target == &plyr_wrk.cmn_wrk) ? &plyr_wrk.cmn_wrk
                                               : &sis_wrk.cmn_wrk;      /* 511 */
        p2 = (fw->target == &plyr_wrk.cmn_wrk) ? &sis_wrk.cmn_wrk
                                               : &plyr_wrk.cmn_wrk;

        g3dxVu0CopyVector(pf, fw->npos);
        g3dxVu0CopyVector(pp, p1->headpos);

        if (fabsf(pf[0] - pp[0]) <= (float)fw->dat->hit_rng &&          /* 531 */
            fabsf(pf[2] - pp[2]) <= (float)fw->dat->hit_rng)            /* 533 */
        {
            if (pf[1] < pp[1])                                          /* 534 */
            {
                if (fabsf(pf[1] - pp[1]) <= (float)fw->dat->hit_rng)    /* 535 */
                {
                    ret = p1;
                }
            }
            else if (fabsf(pf[1] - pp[1]) <= (float)(fw->dat->hit_rng + 600))
            {                                                           /* 538 */
                ret = p1;
            }
        }

        if (ret != nullptr) { return ret; }                             /* 539 */

        g3dxVu0CopyVector(pf, fw->npos);
        g3dxVu0CopyVector(pp, p2->headpos);                             /* 545 */

        if (fabsf(pf[0] - pp[0]) > (float)fw->dat->hit_rng) { return nullptr; }
                                                                        /* 550 */
        if (fabsf(pf[2] - pp[2]) > (float)fw->dat->hit_rng) { return nullptr; }
                                                                        /* 552 */

        if (pf[1] < pp[1])                                              /* 553 */
        {
            if (fabsf(pf[1] - pp[1]) > (float)fw->dat->hit_rng) { return nullptr; }
                                                                        /* 554 */
        }
        else if (fabsf(pf[1] - pp[1]) > (float)(fw->dat->hit_rng + 600))
        {                                                               /* 557 */
            return nullptr;
        }

        ret = p2;
    }

    return ret;                                                         /* 598 */
}

/* Land a hit.  The creature is spent either way -- FLY_STA_KILL goes up before
 * the invulnerability check, so a hit during i-frames still consumes it. */
static void FlyAtkHit(FLY_WRK *fw, PLCMN_WRK *target)                   /* 608 */
{
    float       rv[4];
    STATUS_DAT *st = &target->st;                                       /* 618 */

    fw->sta |= FLY_STA_KILL;                                            /* 616 */

    if (st->invisible_timer != 0) { return; }                           /* 621 */

    /* Mode 6 is the viewfinder: drop out of it before reacting. */
    if (target == &plyr_wrk.cmn_wrk && plyr_wrk.cmn_wrk.mode == 6)      /* 626 */
    {
        SetPlyrFinderQEnd();                                            /* 627 */
    }

    st->cond_tm      = 0;                                               /* 630 */
    st->dmg_cam_flag = 0;                                               /* 631 */
    st->dmg_type     = 4;                                               /* 632 */
    st->dmg          = st->dmg + fw->dat->dmg;                          /* 633 */

    /* Mode 8 is the damage-immune scripted state; the hit still registers but
     * no reaction animation is played. */
    if (plyr_wrk.cmn_wrk.mode == 8) { return; }                         /* 636 */

    GetTrgtRot(target->mbox.pos, fw->npos, rv, 2);                      /* 640 */
    rv[1] = rv[1] - target->mbox.rot[1];                                /* 641 */
    RotLimitChk(&rv[1]);                                                /* 642 */

    if (target == &plyr_wrk.cmn_wrk)                                    /* 643 */
    {
        SetPlyrAnime(ConvertRot2Dir(rv[1], 2) != 0 ? 0x39 : 0x38, 1);   /* 644/649 */
        plyr_wrk.cmn_wrk.st.mvsta &= ~0xfL;                             /* 650 */
        PlayerChangeMode(2);                                            /* 651 */
        SetIngameDamageMode(1);                                         /* 652 */
    }
    else
    {
        SetSisterAnime(ConvertRot2Dir(rv[1], 2) != 0 ? 0x23 : 0x22, 1); /* 654/659 */
        sis_wrk.cmn_wrk.mode = 2;                                       /* 660 */
        sis_wrk.cmn_wrk.st.mvsta &= ~0xfL;                              /* 661 */
    }

    st->dmg_type        = 4;                                            /* 664 */
    st->invisible_timer = 0x21;                                         /* 665 */

    if (fw->dat->cond != 0)                                             /* 668 */
    {
        st->cond    = fw->dat->cond;                                    /* 669 */
        st->cond_tm = 0xf0;                                             /* 670 */
    }
}                                                                       /* 675 */

/* --------------------------------------------------------------------------
 *  Movement
 * ----------------------------------------------------------------------- */

void SetFlyOne(FLY_WRK *fw, ENE_WRK *ew, u_char type, float *pos, float *rot,
               PLCMN_WRK *target)                                       /* 679 */
{
    float wvec[4];
    float mvv[4] = { 0.0f, 0.0f, 20.0f, 1.0f };                         /* 683 */
    float wmx[4][4];
    float xrw;
    float yrw;

    fw->ew     = ew;                                                    /* 687 */
    fw->dat    = &fly_dat[type];                                        /* 688 */
    fw->target = target;                                                /* 689 */

    fw->life_time  = GetRndSP(0, 100) + fw->dat->blifetime;             /* 690 */
    fw->trace_time = fw->life_time;

    /* PAL runs at 50Hz, so every frame count shrinks by 60/50. */
    if (GetPALMode() != 0)                                              /* 691 */
    {
        fw->life_time  = (u_int)((float)fw->life_time  / FLY_PAL_RATE); /* 692 */
        fw->trace_time = (u_int)((float)fw->trace_time / FLY_PAL_RATE); /* 693 */
    }
    fw->now_cnt = 0;                                                    /* 695 */

    fw->alp = fw->dat->alp;                                             /* 696 */

    if (CheckEffectFly(fw) != 0)                                        /* 697 */
    {
        EffectTorch2SetAlphaRate(fw->efpw, (float)fw->alp * 0.0078125f); /* 698 */
    }

    /* The launch spread, in tenths of a degree.  Types 2/4/5 are thrown out
     * wide to either side; the rest leave almost straight ahead. */
    switch (type)                                                       /* 701 */
    {
    case 2:
    case 4:
    case 5:
        fw->nrot[0] = ((float)(GetRndSP(0, 200) + 250) * FLY_PI * 0.1f) / 180.0f;
                                                                        /* 706 */
        fw->nrot[1] = ((float)(GetRndSP(0, 400) + 450) * FLY_PI * 0.1f) / 180.0f;
                                                                        /* 707 */
        if (GetRndSP(0, 2) != 0) { fw->nrot[0] = -fw->nrot[0]; }        /* 708 */
        if (GetRndSP(0, 2) != 0) { fw->nrot[1] = -fw->nrot[1]; }        /* 709 */
        break;                                                          /* 710 */

    case 0:
    case 1:
    case 3:
        fw->nrot[0] = ((float)(GetRndSP(0, 100) -  50) * FLY_PI * 0.1f) / 180.0f;
                                                                        /* 715 */
        fw->nrot[1] = ((float)(GetRndSP(0, 200) - 100) * FLY_PI * 0.1f) / 180.0f;
                                                                        /* 716 */
        break;
    }

    g3dxVu0CopyVector(fw->npos, pos);
    fw->npos[3] = 1.0f;                                                 /* 722 */
    g3dxVu0CopyVector(fw->tpos, fw->target->headpos);
    fw->tpos[3] = 1.0f;                                                 /* 725 */

    xrw = rot[0] + fw->nrot[0];                                         /* 727 */
    yrw = rot[1] + fw->nrot[1];                                         /* 728 */
    RotLimitChk(&xrw);                                                  /* 729 */
    RotLimitChk(&yrw);                                                  /* 730 */

    sceVu0UnitMatrix(wmx);                                              /* 732 */

    /* attr bit 1 keeps the creature level. */
    if ((fw->dat->attr & 2) == 0)                                       /* 733 */
    {
        sceVu0RotMatrixX(wmx, wmx, xrw);                                /* 734 */
    }
    sceVu0RotMatrixY(wmx, wmx, yrw);                                    /* 735 */
    sceVu0TransMatrix(wmx, wmx, fw->npos);                              /* 736 */
    sceVu0ApplyMatrix(wvec, wmx, mvv);                                  /* 737 */

    g3dxVu0CopyVector(fw->opos, fw->npos);
    g3dxVu0CopyVector(fw->npos, wvec);

    fw->sta |= FLY_STA_MOVE;                                            /* 742 */
}

static void FlyMove(FLY_WRK *fw)                                        /* 747 */
{
    float     wvec[4];
    float     mvv[4];
    float     wmx[4][4];
    float     tr_ang;
    float     xrw;
    float     yrw;
    float     f;
    FLY_DATA *fd;

    memset(mvv, 0, sizeof(mvv));                                        /* 749 */
    mvv[3] = 1.0f;

    if ((fw->sta & FLY_STA_HOLD) == 0)                                  /* 755 */
    {
        fw->life_time--;                                                /* 756 */

        if (fw->life_time == 0)
        {
            fw->sta |= FLY_STA_FADE;                                    /* 757 */
        }
    }

    fd = fw->dat;                                                       /* 762 */
    f  = GetDistV(fw->npos, fw->tpos);                                  /* 763 */

    /* Speed and turn rate by distance band: flat outside fdist and inside
     * ndist, interpolated between them. */
    if (fd->fdist < f)                                                  /* 764 */
    {
        mvv[2] = fd->fmove;                                             /* 765 */
        tr_ang = (fd->frot * FLY_PI) / 180.0f;                          /* 766 */
    }
    else if (f < fd->ndist)                                             /* 767 */
    {
        mvv[2] = fd->nmove;                                             /* 768 */
        tr_ang = (fd->nrot * FLY_PI) / 180.0f;                          /* 769 */
    }
    else
    {
        mvv[2] = (1.0f - (f - fd->ndist) / (fd->fdist - fd->ndist)) *   /* 771 */
                 (fd->nmove - fd->fmove) + fd->fmove;
        tr_ang = ((((f - fd->ndist) / (fd->fdist - fd->ndist)) *        /* 772 */
                   (fd->frot - fd->nrot) + fd->nrot) * FLY_PI) / 180.0f;
    }

    if (GetPALMode() != 0)                                              /* 775 */
    {
        mvv[2] = mvv[2] * FLY_PAL_RATE;                                 /* 776 */
        tr_ang = tr_ang * FLY_PAL_RATE;                                 /* 777 */
    }

    /* Blend out of the launch values over the first chg_cnt frames. */
    if (fw->now_cnt < fd->chg_cnt)                                      /* 780 */
    {
        f      = (fd->fstrot * FLY_PI) / 180.0f;                        /* 781 */
        mvv[2] = ((mvv[2] - fd->fstmove) * (float)fw->now_cnt) /        /* 782 */
                 (float)fd->chg_cnt + fd->fstmove;
        tr_ang = ((tr_ang - f) * (float)fw->now_cnt) /                  /* 783 */
                 (float)fd->chg_cnt + f;
        fw->now_cnt++;                                                  /* 784 */
    }

    if ((fw->sta & FLY_STA_HOLD) != 0) { return; }                      /* 789 */

    fw->trace_time--;                                                   /* 790 */

    /* Out of homing time: keep the heading and fly straight on. */
    if (fw->trace_time == 0)
    {
        tr_ang = 0.0f;                                                  /* 791 */
    }

    g3dxVu0CopyVector(fw->tpos, fw->target->headpos);

    GetTrgtRot(fw->npos, fw->tpos, fw->trot, 3);                        /* 799 */
    GetTrgtRot(fw->opos, fw->npos, fw->nrot, 3);                        /* 800 */

    /* Turn towards the target, at most tr_ang this frame.  RotLimitChk() wraps
     * the difference into +-PI so the creature takes the short way round. */
    xrw = fw->trot[0];                                                  /* 802 */

    if (fabsf(fw->nrot[0] - xrw) > tr_ang)
    {
        f = fw->nrot[0] - xrw;
        RotLimitChk(&f);                                                /* 805 */

        if (f <= 0.0f)                                                  /* 806 */
        {
            xrw = fw->nrot[0] + tr_ang;                                 /* 809 */
        }
        else
        {
            xrw = fw->nrot[0] - tr_ang;                                 /* 807 */
        }
    }

    yrw = fw->trot[1];                                                  /* 814 */

    if (fabsf(fw->nrot[1] - yrw) > tr_ang)
    {
        f = fw->nrot[1] - yrw;
        RotLimitChk(&f);                                                /* 817 */

        if (f <= 0.0f)                                                  /* 818 */
        {
            yrw = fw->nrot[1] + tr_ang;                                 /* 821 */
        }
        else
        {
            yrw = fw->nrot[1] - tr_ang;                                 /* 819 */
        }
    }

    RotLimitChk(&xrw);                                                  /* 826 */
    RotLimitChk(&yrw);                                                  /* 827 */

    sceVu0UnitMatrix(wmx);                                              /* 829 */

    if ((fw->dat->attr & 2) == 0)                                       /* 830 */
    {
        sceVu0RotMatrixX(wmx, wmx, xrw);                                /* 831 */
    }
    sceVu0RotMatrixY(wmx, wmx, yrw);                                    /* 832 */
    sceVu0TransMatrix(wmx, wmx, fw->npos);                              /* 833 */
    sceVu0ApplyMatrix(wvec, wmx, mvv);                                  /* 834 */

    /* The wobble.  adjv is where the model is actually drawn, so the creature
     * corkscrews around the path npos follows. */
    f      = sinf(fw->adjp_cnt) * fw->adjp_span;                        /* 837 */
    mvv[0] = cosf(fw->adjp_ang) * f;                                    /* 838 */
    mvv[1] = sinf(fw->adjp_ang) * f;                                    /* 839 */

    fw->adjp_cnt += fw->adjp_add;                                       /* 840 */
    fw->adjp_ang += FLY_ADJP_STEP;                                      /* 841 */
    RotLimitChk(&fw->adjp_cnt);                                         /* 842 */
    RotLimitChk(&fw->adjp_ang);                                         /* 843 */

    sceVu0ApplyMatrix(fw->adjv, wmx, mvv);                              /* 844 */

    g3dxVu0CopyVector(fw->opos, fw->npos);
    g3dxVu0CopyVector(fw->npos, wvec);
}

/* --------------------------------------------------------------------------
 *  Slot management and the per-frame rule
 * ----------------------------------------------------------------------- */

static FLY_WRK *GetFlyWork(void)                                        /* 859 */
{
    int i;

    for (i = 0; i < FLY_WRK_MAX; i++)                                   /* 861 */
    {
        if ((fly_wrk[i].sta & FLY_STA_USE) == 0)
        {
            printf("ASSIGN FLY BUFFER fly_wrk[%d]\n", i);               /* 863 */
            return &fly_wrk[i];
        }
    }

    printf("FLY BUFFER is FULL!!\n");                                   /* 867 */
    return nullptr;                                                     /* 868 */
}                                                                       /* 869 */

void EraseEneFlyWork(ENE_WRK *ew)                                       /* 873 */
{
    FLY_WRK *fw;
    int      i;

    for (i = 0; i < FLY_WRK_MAX; i++)                                   /* 877 */
    {
        fw = &fly_wrk[i];

        if (fw->ew == ew)                                               /* 878 */
        {
            fw->sta |= FLY_STA_FADE;                                    /* 879 */
        }
    }
}                                                                       /* 881 */

/* Retire whatever finished last frame, and start fading anything whose parent
 * ghost has left the field. */
static void ClearCheckFlyWork(void)                                     /* 886 */
{
    FLY_WRK *fw;
    int      i;

    for (i = 0; i < FLY_WRK_MAX; i++)                                   /* 890 */
    {
        fw = &fly_wrk[i];

        if ((fw->sta & FLY_STA_KILL) != 0)                              /* 891 */
        {
            /* Clear every state bit including FLY_STA_USE, which is what hands
             * the slot back to GetFlyWork(). */
            fw->sta &= 0xfff0;                                          /* 892 */
            fw->alp  = 0;

            if (fw->ew == nullptr)                                      /* 894 */
            {
                /* Ownerless: nothing else will ever free the model. */
                FlyRelease(fw);                                         /* 895 */
            }
            else if (CheckEffectFly(fw) != 0)                           /* 897 */
            {
                /* The parent's EneFlyAnmctrlRelease() frees the models, but an
                 * effect creature has none, so its torch must go here. */
                FlyEffectRelease(fw);                                   /* 898 */
            }
        }
        else if ((fw->sta & FLY_STA_FADE) != 0)                         /* 905 */
        {
            if ((int)(fw->alp - 8) < 1)                                 /* 906/907 */
            {
                fw->alp  = 0;                                           /* 909 */
                fw->sta |= FLY_STA_KILL;                                /* 910 */
            }
            else
            {
                fw->alp -= 8;
            }

            if (CheckEffectFly(fw) != 0)                                /* 912 */
            {
                EffectTorch2SetAlphaRate(fw->efpw,                      /* 913 */
                                         (float)fw->alp * 0.0078125f);
            }
        }
        else if (fw->ew != nullptr &&                                   /* 916 */
                 fw->ew->status != ENE_STATUS_ACT)                      /* 917 */
        {
            fw->sta |= FLY_STA_FADE;                                    /* 918 */
        }
    }                                                                   /* 923 */
}

void FlyRule(void)                                                      /* 930 */
{
    FLY_WRK   *fw;
    int        i;
    PLCMN_WRK *target;

    ClearCheckFlyWork();                                                /* 949 */

    for (i = 0; i < FLY_WRK_MAX; i++)                                   /* 951 */
    {
        fw = &fly_wrk[i];

        if ((fw->sta & FLY_STA_MOVE) == 0) { continue; }                /* 954 */

        FlyMove(&fly_wrk[i]);

        /* Already fading: it has spent its hit and cannot land another. */
        if ((fw->sta & FLY_STA_FADE) != 0) { continue; }                /* 965 */

        target = FlyPlyrHitChk(&fly_wrk[i]);

        if (target != nullptr)
        {
            FlyAtkHit(&fly_wrk[i], target);
        }
    }                                                                   /* 979 */
}

/* One directional light aimed straight down the camera's Z axis, everything
 * else off.  The creature's own set is pushed rather than the room's, which is
 * why FLY_WRK carries a whole GRA3DLIGHTDATA. */
void SetFlyLight(FLY_WRK *fw)                                           /* 995 */
{
    int i;

    fw->light.aLight[0].Type = G3DLIGHT_DIRECTIONAL;                    /* 1002 */

    for (i = 0; i < NUM_GRA3DLIGHTID; i++)                              /* 1004 */
    {
        gra3dLightEnable(i, 0);                                         /* 1005 */
    }

    /* Dead in the ROM: the camera-relative direction computed here is
     * overwritten by the broadcast multiply below before anything reads it.
     * Kept because the calls have side effects on the camera cache. */
    _SetVector(fw->light.aLight[0].vDirection,                          /* 1007 */
               gra3dGetCamera()->matCoord[3][0] - fw->npos[0], -1.0f,
               gra3dGetCamera()->matCoord[3][2] - fw->npos[2], 0.0f);

    _SetVector(fw->light.aLight[0].vDiffuse,  0.5f, 0.5f, 0.5f, 0.0f);  /* 1008 */
    _SetVector(fw->light.aLight[0].vSpecular, 0.0f, 0.0f, 0.0f, 0.0f);  /* 1009 */

    /* A vmulbc against a broadcast -1.0, whole quadword: the light points back
     * along the camera's Z axis, so the creature is always lit from the
     * viewer.  This is what overwrites the direction set above. */
    sceVu0ScaleVector(fw->light.aLight[0].vDirection,
                      gra3dGetCamera()->matCoord[2], -1.0f);

    fw->light.aLight[0].fFalloff = 1.0f;                                /* 1011 */

    gra3dSetLight(0, fw->light.aLight.data());                          /* 1012 */
    gra3dLightEnable(0, 1);                                             /* 1013 */
    gra3dApplyLight();                                                  /* 1014 */
}
