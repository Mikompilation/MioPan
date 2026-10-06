// FILE: /home/zero_rom/zero2np/src/graphics/motion/morph.c
//
// Model morphing: a cross-dissolve between two sub-objects of the same model
// pak, used when a character changes shape (clothing states, damage, the
// ghosts' transformations).
//
// Each morphing model names one or more object pairs in morph_dat[].  For every
// active pair MorphSetCtrl() takes a MORPH_CTRL slot, snapshots both objects'
// PHEADs and allocates a vertex buffer sized to object 1's vertex array.  Per
// frame MorphRun() blends obj1's and obj2's vertices into that buffer at the
// current rate and re-points both objects' VUVN units at it, so the pair is
// drawn from one interpolated mesh.  MorphReset() puts the packets and PHEADs
// back the way they were.
//
// The rate is driven by a little bytecode: anm_tbl[anm_no].morph indexes a
// per-animation script of MORPH_CODEs that MorphDevCode() steps through -- see
// morph_dat.c for the encoding.  DrawGirlSubObj()/DrawEneSubObj() ask
// MorphCheckId1/2() whether a sub-object is taking part in a morph and, if so,
// take its alpha from MorphGetAlpha1/2() rather than the caller's.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
// Trailing /* NNN */ comments are the ROM's source line numbers.  Members read
// through the ROM's fixed_array<> wrapper all report as line 124/125, so those
// statements carry no usable line of their own.

#include "Morph.h"

#include "mdldat.h"                          /* anm_tbl */
#include "morph_dat.h"
#include "../../common/ol_load.h"            /* ol_loadGetHeap / ol_loadFreeHeap */
#include "../../common/packfile.h"           /* GetFileInPak */
#include "../graph3d/sgd_types.h"            /* SGDFILEHEADER / _VECTORDATA */

#define MORPH_CTRL_MAX  15

MORPH_DAT ch017_morph[2] = { { 9, 10 }, { -1, -1 } };       /* data 32e9a8 */
MORPH_DAT ch020_morph[2] = { { 8,  9 }, { -1, -1 } };       /* data 32e9b8 */
MORPH_DAT ch030_morph[2] = { { 1,  2 }, { -1, -1 } };       /* data 32e9c8 */

MORPH_DAT ch041_morph[14] = {                               /* data 32e9d8 */
    {  1,  2 }, {  3,  4 }, {  5,  6 }, {  7,  8 },
    {  9, 10 }, { 11, 12 }, { 13, 14 }, { 15, 16 },
    { 17, 18 }, { 19, 20 }, { 21, 22 }, { 23, 24 },
    { 25, 26 }, { -1, -1 }
};

/* Indexed by model number; only these four models morph. */
MORPH_DAT *morph_dat[78] = {                                /* data 32ea48 */
    0,            0,            0,            0,            0,            0,    /*  0 */
    0,            0,            0,            0,            0,            0,    /*  6 */
    0,            0,            0,            0,            0,            ch017_morph,    /* 12 */
    0,            0,            ch020_morph,  0,            0,            0,    /* 18 */
    0,            0,            0,            0,            0,            0,    /* 24 */
    ch030_morph,  0,            0,            0,            0,            0,    /* 30 */
    0,            0,            0,            0,            0,            ch041_morph,    /* 36 */
    0,            0,            0,            0,            0,            0,    /* 42 */
    0,            0,            0,            0,            0,            0,    /* 48 */
    0,            0,            0,            0,            0,            0,    /* 54 */
    0,            0,            0,            0,            0,            0,    /* 60 */
    0,            0,            0,            0,            0,            0,    /* 66 */
    0,            0,            0,            0,            0,            0,    /* 72 */
};

static MORPH_CTRL morph_ctrl[MORPH_CTRL_MAX];               /* bss  4b6990 */
static ANI_CTRL  *now_work;                                 /* sbss 3f4e6c */

static int MorphDevCode(ANI_CTRL *ani_ctrl);
static int MorphSetNewCode(MORPH_CODE_CTRL *morph);
static int MorphSetRateM(MORPH_CTRL *m_ctrl, float rate);
static int MorphSetP(MORPH_CTRL *m_ctrl, u_int *mpk_p, float *morph_v);
static int MorphResetP(MORPH_CTRL *m_ctrl, u_int *mpk_p, float *morph_v);
static int MorphSetWorkNo(ANI_CTRL *ani_ctrl);

/* --------------------------------------------------------------------------
 *  PHEAD snapshot helper (port-only).
 *
 *  The ROM copied the whole 0x34-byte PHEAD with an unaligned block move, which
 *  worked because sgdRemap() had already turned its offsets into absolute
 *  addresses.  Here the fields are still self-relative, so resolve them on the
 *  way into the snapshot.  See PHEAD_WORK in mdlwork.h.
 * ------------------------------------------------------------------------ */
static void MorphSnapPHead(PHEAD_WORK *dst, const PHEAD *src)
{
    dst->HeaderSections     = src->HeaderSections;
    dst->UniqHeaderSize     = src->UniqHeaderSize;
    dst->pUniqVertex        = src->pUniqVertex.get();
    dst->pUniqNormal        = src->pUniqNormal.get();
    dst->pUniqList          = src->pUniqList.get();
    dst->CommonHeaderSize   = src->CommonHeaderSize;
    dst->pCommonVertex      = src->pCommonVertex.get();
    dst->pCommonNormal      = src->pCommonNormal.get();
    dst->pCommonList        = src->pCommonList.get();
    dst->WeightedHeaderSize = src->WeightedHeaderSize;
    dst->pWeightedVertex    = src->pWeightedVertex.get();
    dst->pWeightedNormal    = src->pWeightedNormal.get();
    dst->pWeightedList      = src->pWeightedList.get();
}

int MorphInit(void)
{
    int i;

    now_work = 0;                                                   /* 196 */

    for (i = 0; i < MORPH_CTRL_MAX; i++)                            /* 198 */
    {
        morph_ctrl[i].ani_ctrl     = 0;
        morph_ctrl[i].old_anm      = -1;
        morph_ctrl[i].morph.code   = 0;
        morph_ctrl[i].morph.cnt    = 0.0f;
        morph_ctrl[i].morph.target = 0;
        morph_ctrl[i].morph.sta    = 0;
        morph_ctrl[i].morph_buf[0] = 0;
    }

    return 1;                                                       /* 208 */
}

/* --------------------------------------------------------------------------
 *  MorphDevCode
 *
 *  Advance the bytecode for whichever slot owns ani_ctrl and walk `rate`
 *  towards the script's target.  The step is value * reso / 200, so the morph
 *  runs at the animation's own playback resolution.
 * ------------------------------------------------------------------------ */
static int MorphDevCode(ANI_CTRL *ani_ctrl)
{
    int   i;
    float rate;
    float value;
    float target;

    for (i = 0; i < MORPH_CTRL_MAX; i++)                            /* 220 */
    {
        MORPH_CTRL *m_ctrl = &morph_ctrl[i];

        if (m_ctrl->ani_ctrl != ani_ctrl)
        {
            continue;
        }

        if (anm_tbl[ani_ctrl->anm_no].morph == 0)                   /* 224 */
        {
            m_ctrl->rate = 0.0f;                                    /* 226 */
            return 0;
        }

        if (ani_ctrl->anm.playnum != m_ctrl->old_anm)               /* 229 */
        {
            m_ctrl->morph.code =                                    /* 230 */
                anm_tbl[ani_ctrl->anm_no].morph[ani_ctrl->anm.playnum];

            MorphSetNewCode(&m_ctrl->morph);                        /* 232 */
        }
        else if (m_ctrl->morph.cnt == 0.0f)                         /* 235 */
        {
            m_ctrl->morph.code++;                                   /* 236 */

            MorphSetNewCode(&m_ctrl->morph);                        /* 238 */
        }

        rate   = m_ctrl->rate * 100.0f;                             /* 242 */
        value  = ((float)m_ctrl->morph.value                        /* 243 */
                  * (float)ani_ctrl->mot.reso) / 200.0f;
        target = (float)m_ctrl->morph.target;                       /* 244 */

        if (rate != target)                                         /* 246 */
        {
            if (target < rate)                                      /* 249 */
            {
                rate = rate - value;                                /* 250 */

                if (rate < target)                                  /* 251 */
                {
                    rate = target;                                  /* 252 */
                }
            }
            else if (rate < target)                                 /* 255 */
            {
                rate = rate + value;                                /* 256 */

                if (target < rate)                                  /* 257 */
                {
                    rate = target;
                }
            }
        }

        if (m_ctrl->morph.sta == 1)                                 /* 262 */
        {
            m_ctrl->morph.cnt =                                     /* 268 */
                m_ctrl->morph.cnt - (float)ani_ctrl->mot.reso / 200.0f;
        }

        if (m_ctrl->morph.cnt < 0.0f)                               /* 277 */
        {
            m_ctrl->morph.cnt = 0.0f;
        }

        MorphSetRateM(m_ctrl, rate / 100.0f);                       /* 282 */

        m_ctrl->old_anm = ani_ctrl->anm.playnum;                    /* 283 */
    }

    return 0;                                                       /* 287 */
}

/* Decode one MORPH_CODE into the control block.  See morph_dat.c. */
static int MorphSetNewCode(MORPH_CODE_CTRL *morph)
{
    int kind   = (int)((*morph->code >> 28) & 0xff);                /* 294 */
    int arg[4] = { (int)((*morph->code >> 24) & 0x0f),               /* 295 */
                   (int)((*morph->code >> 16) & 0xff),               /* 296 */
                   (int)((*morph->code >>  8) & 0xff),               /* 297 */
                   (int)( *morph->code        & 0xff) };             /* 298 */

    switch (kind)                                                   /* 300 */
    {
    case 0:
        if (arg[0] != 0)                                            /* 302 */
        {
            return 0;
        }

        morph->cnt    = 1.0f;                                       /* 304 */
        morph->target = 0;                                          /* 305 */
        morph->value  = 0;                                          /* 306 */
        morph->sta    = 0;                                          /* 307 */
        /* fall through -- kind 0 then re-runs the kind 1 decode */

    case 1:
        switch (arg[0])                                             /* 314 */
        {
        case 0:
            morph->target = arg[1];                                 /* 316 */
            morph->cnt    = (float)arg[2];                          /* 317 */
            morph->value  = arg[3];                                 /* 318 */

            if (arg[2] == 0)                                        /* 319 */
            {
                morph->cnt = 1.0f;
                morph->sta = 0;
            }
            else
            {
                morph->sta = 1;                                     /* 324 */
            }
            break;                                                  /* 326 */

        case 1:
            morph->target = 0;                                      /* 328 */
            morph->cnt    = (float)arg[1];                          /* 329 */
            morph->value  = 0;                                      /* 330 */

            if (arg[1] == 0)                                        /* 331 */
            {
                morph->cnt = 1.0f;                                  /* 332 */
                morph->sta = 0;                                     /* 333 */
            }
            else
            {
                morph->sta = 1;                                     /* 336 */
            }
            break;

        default:
            break;                                                  /* 340 */
        }
        break;

    default:
        break;
    }

    return 0;                                                       /* 344 */
}

int MorphSetRate(ANI_CTRL *ani_ctrl, float rate)
{
    int i;

    for (i = 0; i < MORPH_CTRL_MAX; i++)                            /* 359 */
    {
        if (morph_ctrl[i].ani_ctrl == ani_ctrl)
        {
            /* the ROM clamps against double literals here, unlike
             * MorphSetRateM below which uses float ones */
            if (rate > 1.0)                                         /* 362 */
            {
                rate = 1.0f;                                        /* 363 */
            }
            else if (rate < 0.0)                                    /* 365 */
            {
                rate = 0.0f;                                        /* 366 */
            }

            morph_ctrl[i].rate = rate;
        }
    }

    return 1;                                                       /* 372 */
}

static int MorphSetRateM(MORPH_CTRL *m_ctrl, float rate)
{
    if (1.0f < rate)                                                /* 380 */
    {
        rate = 1.0f;
    }
    else if (rate < 0.0f)                                           /* 383 */
    {
        rate = 0.0f;                                                /* 384 */
    }

    m_ctrl->rate = rate;                                            /* 386 */

    return 1;                                                       /* 388 */
}

float MorphGetRate(ANI_CTRL *ani_ctrl)
{
    int i;

    for (i = 0; i < MORPH_CTRL_MAX; i++)                            /* 397 */
    {
        if (morph_ctrl[i].ani_ctrl == ani_ctrl)
        {
            return morph_ctrl[i].rate;
        }
    }

    return -1.0f;                                                   /* 402 */
}

int IsMorphEnable(ANI_CTRL *ani_ctrl)
{
    int i;

    for (i = 0; i < MORPH_CTRL_MAX; i++)                            /* 411 */
    {
        if (morph_ctrl[i].ani_ctrl == ani_ctrl)
        {
            if (morph_ctrl[i].obj1_id == -1)
            {
                return 0;
            }

            if (morph_ctrl[i].obj2_id != -1)
            {
                return 1;
            }

            return 0;                                               /* 417 */
        }
    }

    return 0;                                                       /* 421 */
}

/* --------------------------------------------------------------------------
 *  MorphGetAlpha2 / MorphGetAlpha1
 *
 *  Split the caller's alpha between the two halves of the dissolve.  Object 2
 *  simply gets rate * alpha; object 1 gets the coverage that, composited over
 *  the already-drawn object 2, lands back on the requested alpha -- hence the
 *  ((1-rate)*a2) / (rate*(1-a2)) form.
 *
 *  Both search morph_ctrl for now_work, which MorphRun() publishes.  The ROM's
 *  fixed_array<> bounds check would assert if no slot matched; here the search
 *  just runs off the end.  Neither is reachable from the draw path, which only
 *  calls these after MorphCheckId1/2() has already found a slot.
 * ------------------------------------------------------------------------ */
float MorphGetAlpha2(float alpha)
{
    float m_alpha;
    int   i;

    m_alpha = 0.0f;                                                 /* 427 */

    if (now_work == 0)                                              /* 430 */
    {
        return m_alpha;
    }

    for (i = 0; i < MORPH_CTRL_MAX; i++)                            /* 434 */
    {
        if (morph_ctrl[i].ani_ctrl == now_work)
        {
            break;                                                  /* 436 */
        }
    }

    if (morph_ctrl[i].flg != 0)
    {
        m_alpha = morph_ctrl[i].rate * alpha;
    }
    else
    {
        m_alpha = 0.0f;                                             /* 444 */
    }

    return m_alpha;                                                 /* 448 */
}

float MorphGetAlpha1(float alpha)
{
    MORPH_CTRL *m_ctrl;
    float       m_alpha1;
    float       m_alpha2;
    float       rate;
    int         i;

    m_ctrl   = 0;                                                   /* 453 */
    m_alpha1 = 0.0f;                                                /* 454 */
    rate     = 0.0f;                                                /* 455 */

    if (now_work == 0)
    {
        return m_alpha1;                                            /* 459 */
    }

    for (i = 0; i < MORPH_CTRL_MAX; i++)                            /* 462 */
    {
        if (morph_ctrl[i].ani_ctrl == now_work)
        {
            m_ctrl = &morph_ctrl[i];                                /* 464 */
            rate   = morph_ctrl[i].rate;                            /* 465 */
            break;                                                  /* 466 */
        }
    }

    if (m_ctrl->flg != 0)                                           /* 470 */
    {
        m_alpha2 = MorphGetAlpha2(alpha);                            /* 471 */

        if (m_alpha2 == 1.0f)                                       /* 472 */
        {
            return 0.0f;
        }

        if (rate == 0.0f)                                           /* 475 */
        {
            return alpha;
        }

        return ((1.0f - rate) * m_alpha2)                           /* 479 */
               / (rate * (1.0f - m_alpha2));
    }

    return alpha;                                                   /* 487 */
}

int MorphCheckId1(int id)
{
    int i;

    if (now_work == 0)                                              /* 494 */
    {
        return 0;                                                   /* 495 */
    }

    for (i = 0; i < MORPH_CTRL_MAX; i++)                            /* 497 */
    {
        if (morph_ctrl[i].ani_ctrl == now_work)
        {
            if (morph_ctrl[i].obj1_id == id)
            {
                return 1;
            }
        }
    }

    return 0;                                                       /* 505 */
}

int MorphCheckId2(int id)
{
    int i;

    if (now_work == 0)                                              /* 513 */
    {
        return 0;                                                   /* 514 */
    }

    for (i = 0; i < MORPH_CTRL_MAX; i++)                            /* 516 */
    {
        if (morph_ctrl[i].ani_ctrl == now_work)
        {
            if (morph_ctrl[i].obj2_id == id)
            {
                return 1;
            }
        }
    }

    return 0;                                                       /* 524 */
}

/* --------------------------------------------------------------------------
 *  MorphSetCtrl
 *
 *  Claim a slot per object pair listed for this model and allocate the blend
 *  buffer.  Its size is the byte span from object 1's first vertex to its first
 *  normal: the normals follow the vertices in the file, so that gap is exactly
 *  the vertex array.
 * ------------------------------------------------------------------------ */
int MorphSetCtrl(void *ani_hndl, int mdl_no)
{
    int            i;
    int            j;
    int            obj1;
    int            obj2;
    HeaderSection *my_hs1;
    HeaderSection *my_hs2;
    u_int         *mpk_p;
    float         *v1;
    float         *n;

    if (morph_dat[mdl_no] == 0)                                     /* 537 */
    {
        return 1;                                                   /* 538 */
    }

    j = 0;                                                          /* 541 */

    for (i = 0; i < MORPH_CTRL_MAX; i++)                            /* 542 */
    {
        obj1 = morph_dat[mdl_no][j].obj1_id;                        /* 543 */

        if (obj1 == -1)
        {
            return 1;
        }

        if (morph_ctrl[i].ani_ctrl == 0)
        {
            morph_ctrl[i].ani_ctrl = (ANI_CTRL *)ani_hndl;          /* 548 */

            mpk_p = ((ANI_CTRL *)ani_hndl)->mpk_p;                  /* 550 */
            obj1  = morph_dat[mdl_no][j].obj1_id;                   /* 551 */
            obj2  = morph_dat[mdl_no][j].obj2_id;                   /* 552 */

            morph_ctrl[i].rate         = 0.0f;                      /* 554 */
            morph_ctrl[i].model_no     = mdl_no;                    /* 555 */
            morph_ctrl[i].obj1_id      = obj1;                      /* 556 */
            morph_ctrl[i].obj2_id      = obj2;                      /* 557 */
            morph_ctrl[i].flg          = 0;                         /* 558 */
            morph_ctrl[i].old_anm      = -1;                        /* 560 */
            morph_ctrl[i].morph.code   = 0;                         /* 561 */
            morph_ctrl[i].morph.cnt    = 0.0f;                      /* 562 */
            morph_ctrl[i].morph.target = 0;                         /* 563 */
            morph_ctrl[i].morph.sta    = 0;

            my_hs1 = (HeaderSection *)GetFileInPak(mpk_p, obj1);     /* 566 */
            my_hs2 = (HeaderSection *)GetFileInPak(mpk_p, obj2);     /* 567 */

            MorphSnapPHead(&morph_ctrl[i].ph1,                      /* 568 */
                           (const PHEAD *)my_hs1->phead.get());
            MorphSnapPHead(&morph_ctrl[i].ph2,                      /* 569 */
                           (const PHEAD *)my_hs2->phead.get());

            if (morph_ctrl[i].ph1.pUniqVertex == 0 ||               /* 572 */
                morph_ctrl[i].ph2.pUniqVertex == 0)
            {
                v1 = morph_ctrl[i].ph1.pWeightedVertex;             /* 573 */
                n  = morph_ctrl[i].ph1.pWeightedNormal;             /* 574 */
            }
            else
            {
                v1 = morph_ctrl[i].ph1.pUniqVertex;                 /* 577 */
                n  = morph_ctrl[i].ph1.pUniqNormal;
            }

            morph_ctrl[i].morph_buf[0] = (float *)ol_loadGetHeap(   /* 580 */
                (int)((((char *)n - (char *)v1) >> 4) << 4));        /* 581 */

            j++;                                                    /* 583 */
        }
    }

    return 0;                                                       /* 587 */
}

int MorphDell(void *ani_hndl)
{
    ANI_CTRL *ani_ctrl;
    int       i;

    ani_ctrl = (ANI_CTRL *)ani_hndl;

    if (morph_dat[ani_ctrl->mdl_no] == 0)                           /* 597 */
    {
        return 1;
    }

    /* the ROM tests this only after the dereference above -- kept as found */
    if (ani_ctrl == 0)                                              /* 601 */
    {
        return 1;
    }

    for (i = 0; i < MORPH_CTRL_MAX; i++)                            /* 604 */
    {
        if (morph_ctrl[i].ani_ctrl == ani_ctrl)
        {
            morph_ctrl[i].ani_ctrl = 0;
            ol_loadFreeHeap(morph_ctrl[i].morph_buf[0]);
        }
    }

    return 1;                                                       /* 611 */
}

/* --------------------------------------------------------------------------
 *  MorphRun
 *
 *  Blend the pair's vertices into the slot's buffer and redirect both objects
 *  at it.  anm_no == -1 means the model is not animating: the slot is only
 *  published as the current work, so the alpha queries answer for it, and
 *  nothing is blended.
 * ------------------------------------------------------------------------ */
int MorphRun(ANI_CTRL *ani_ctrl, u_int *mpk_p)
{
    float *v1;
    float *v2;
    float *n;
    float  rate;
    int    i;

    if (ani_ctrl->anm_no == -1)                                     /* 622 */
    {
        for (i = 0; i < MORPH_CTRL_MAX; i++)                        /* 623 */
        {
            if (morph_ctrl[i].ani_ctrl == ani_ctrl)
            {
                MorphSetWorkNo(ani_ctrl);                           /* 625 */
            }
        }

        return 0;                                                   /* 628 */
    }

    MorphDevCode(ani_ctrl);                                         /* 632 */

    for (i = 0; i < MORPH_CTRL_MAX; i++)                            /* 634 */
    {
        MORPH_CTRL *m_ctrl = &morph_ctrl[i];

        if (m_ctrl->ani_ctrl != ani_ctrl)
        {
            continue;
        }

        if (m_ctrl->obj1_id == -1)                                  /* 638 */
        {
            m_ctrl->flg = 0;
            return 0;
        }

        if (m_ctrl->obj2_id == -1)
        {
            m_ctrl->flg = 0;                                        /* 640 */
            return 0;
        }

        if (m_ctrl->ph1.pUniqVertex == 0 ||                         /* 642 */
            m_ctrl->ph2.pUniqVertex == 0)
        {
            if (m_ctrl->ph1.pWeightedVertex == 0)                   /* 643 */
            {
                m_ctrl->flg = 0;
                return 0;
            }

            if (m_ctrl->ph2.pWeightedVertex == 0)
            {
                m_ctrl->flg = 0;                                    /* 645 */
                return 0;
            }

            rate = m_ctrl->rate;                                    /* 648 */

            v1 = m_ctrl->ph1.pWeightedVertex;                       /* 650 */
            v2 = m_ctrl->ph2.pWeightedVertex;
            n  = m_ctrl->ph1.pWeightedNormal;                       /* 651 */
        }
        else
        {
            rate = m_ctrl->rate;

            v1 = m_ctrl->ph1.pUniqVertex;                           /* 656 */
            v2 = m_ctrl->ph2.pUniqVertex;                           /* 657 */
            n  = m_ctrl->ph1.pUniqNormal;                           /* 658 */
        }

        /*
         * The ROM runs this in VU0 macro mode: vf6.x = rate, then
         * vsub.x vf8, vf1, vf6 followed by vmulax.xyz / vmaddx.xyz per
         * quadword.  vf1 is not a hardwired register -- g3dInitialize() loads
         * it once with g_v1110 (1,1,1,0), so vf8.x is (1 - rate).  Only the
         * xyz fields are written, so w stays as object 1's.  Spelled out here
         * because the host has no VU0.                              662 - 677
         */
        if (v1 < n)                                                 /* 668 */
        {
            float *buf = m_ctrl->morph_buf[0];
            int    k;

            for (k = 0; (v1 + k) < n; k += 4)                       /* 669, 677 */
            {
                buf[k + 0] = v2[k + 0] * rate + v1[k + 0] * (1.0f - rate);
                buf[k + 1] = v2[k + 1] * rate + v1[k + 1] * (1.0f - rate);
                buf[k + 2] = v2[k + 2] * rate + v1[k + 2] * (1.0f - rate);
                buf[k + 3] = v1[k + 3];
            }
        }

        m_ctrl->flg = 1;                                            /* 688 */

        MorphSetWorkNo(ani_ctrl);                                   /* 689 */
        MorphSetP(m_ctrl, mpk_p, m_ctrl->morph_buf[0]);             /* 691 */
    }

    return 0;                                                       /* 695 */
}

int MorphReset(ANI_CTRL *ani_ctrl, u_int *mpk_p)
{
    int i;

    now_work = 0;                                                   /* 704 */

    for (i = 0; i < MORPH_CTRL_MAX; i++)                            /* 706 */
    {
        if (morph_ctrl[i].ani_ctrl == ani_ctrl)
        {
            if (morph_ctrl[i].flg != 0)                             /* 709 */
            {
                MorphResetP(&morph_ctrl[i], mpk_p,                  /* 714 */
                            morph_ctrl[i].morph_buf[0]);

                morph_ctrl[i].flg = 0;                              /* 716 */
            }
        }
    }

    return 0;                                                       /* 719 */
}

/* --------------------------------------------------------------------------
 *  MorphSetP / MorphResetP
 *
 *  Point both objects' vertex references at the blend buffer, and put them
 *  back.  Two things get redirected: the per-vertex addresses inside every
 *  vector-type-0 VUVN unit (the _VECTORDATA array at unit + 0x30, which is what
 *  the host mesh bridge follows as well), and the PHEAD pointers that record
 *  where each vertex block starts.
 *
 *  The ROM did the first as flat arithmetic on the packet's 32-bit address
 *  fields -- "field += morph_v - old_base".  Those fields are SGDSELF32 in this
 *  port, so the same move goes through the accessors; the model file and the
 *  ol_load heap both live in the emulated EE block, so the offsets fit.
 * ------------------------------------------------------------------------ */
static int MorphSetP(MORPH_CTRL *m_ctrl, u_int *mpk_p, float *morph_v)
{
    int            i;
    int            k;
    int            obj2;
    int            w_num;
    int            c_num;
    HeaderSection *my_hs1;
    HeaderSection *my_hs2;
    PHEAD         *ph1;
    PHEAD         *ph2;

    obj2   = m_ctrl->obj2_id;
    my_hs1 = (HeaderSection *)GetFileInPak(mpk_p, m_ctrl->obj1_id);  /* 740 */
    my_hs2 = (HeaderSection *)GetFileInPak(mpk_p, obj2);             /* 741 */

    ph1 = (PHEAD *)my_hs1->phead.get();                              /* 742 */
    ph2 = (PHEAD *)my_hs2->phead.get();                              /* 743 */

    for (i = 0; i < (int)my_hs1->blocks; i++)                        /* 748 */
    {
        SGDPROCUNITHEADER *prim;

        prim = ((SGDFILEHEADER *)my_hs1)->apProcUnitHead[i];         /* 750 */

        /* the ROM names this header VUVN_PRIM; it is SGDVUVNDESC here */
        while (prim != 0 && prim->pNext.get() != 0)                  /* 753 */
        {
            if (prim->iCategory == SPC_VUVN)                         /* 759 */
            {
                SGDVUVNDESC *vh = &prim->VUVNDesc;             /* 762 */
                _VECTORDATA *vd = (_VECTORDATA *)&prim[3];

                if (vh->ucVectorType == 0)                                  /* 768 */
                {
                    for (k = 0; k < vh->sNumVertex; k++)                   /* 774, 780 */
                    {
                        float *cur = (float *)vd[k].vAddress.pVertex.get();

                        vd[k].vAddress.pVertex = (sceVu0FVECTOR *)
                            (morph_v + (cur - m_ctrl->ph1.pUniqVertex));
                    }
                }
            }

            prim = prim->pNext;                                      /* 794 */
        }

        prim = ((SGDFILEHEADER *)my_hs2)->apProcUnitHead[i];         /* 798 */

        while (prim != 0 && prim->pNext.get() != 0)                  /* 801 */
        {
            if (prim->iCategory == SPC_VUVN)                         /* 807 */
            {
                SGDVUVNDESC *vh = &prim->VUVNDesc;             /* 810 */
                _VECTORDATA *vd = (_VECTORDATA *)&prim[3];

                if (vh->ucVectorType == 0)                                  /* 817 */
                {
                    for (k = 0; k < vh->sNumVertex; k++)                   /* 823, 829 */
                    {
                        float *cur = (float *)vd[k].vAddress.pVertex.get();

                        vd[k].vAddress.pVertex = (sceVu0FVECTOR *)
                            (morph_v + (cur - m_ctrl->ph2.pUniqVertex));
                    }
                }
            }

            prim = prim->pNext;                                      /* 843 */
        }
    }

    /* Re-point the block starts, keeping the gaps between them intact. */
    if (ph1->pWeightedVertex.get() != 0)                            /* 849 */
    {
        if (ph1->pUniqVertex.get() != 0)                            /* 850 */
        {
            w_num = (int)(((char *)ph1->pWeightedVertex.get()        /* 851 */
                           - (char *)ph1->pUniqVertex.get()) >> 4);
        }
        else
        {
            w_num = 0;                                              /* 854 */
        }

        ph1->pWeightedVertex = morph_v + w_num * 4;                  /* 856 */
        ph2->pWeightedVertex = morph_v + w_num * 4;                  /* 857 */
    }

    if (ph1->pCommonVertex.get() != 0)                              /* 859 */
    {
        if (ph1->pUniqVertex.get() != 0)                            /* 860 */
        {
            c_num = (int)(((char *)ph1->pCommonVertex.get()          /* 861 */
                           - (char *)ph1->pUniqVertex.get()) >> 4);
        }
        else if (ph1->pWeightedVertex.get() != 0)                   /* 864 */
        {
            c_num = (int)(((char *)ph1->pCommonVertex.get()          /* 865 */
                           - (char *)ph1->pWeightedVertex.get()) >> 4);
        }
        else
        {
            c_num = 0;                                              /* 868 */
        }

        ph1->pCommonVertex = morph_v + c_num * 4;                    /* 871 */
        ph2->pCommonVertex = morph_v + c_num * 4;                    /* 872 */
    }

    if (ph1->pUniqVertex.get() != 0 && ph2->pUniqVertex.get() != 0)  /* 874 */
    {
        ph1->pUniqVertex = morph_v;                                  /* 875 */
        ph2->pUniqVertex = morph_v;                                  /* 876 */
    }

    return 1;                                                       /* 878 */
}

static int MorphResetP(MORPH_CTRL *m_ctrl, u_int *mpk_p, float *morph_v)
{
    int            i;
    int            k;
    int            obj2;
    HeaderSection *my_hs1;
    HeaderSection *my_hs2;
    PHEAD         *ph1;
    PHEAD         *ph2;
    PHEAD_WORK    *ori_ph1;
    PHEAD_WORK    *ori_ph2;

    obj2   = m_ctrl->obj2_id;
    my_hs1 = (HeaderSection *)GetFileInPak(mpk_p, m_ctrl->obj1_id);  /* 896 */
    my_hs2 = (HeaderSection *)GetFileInPak(mpk_p, obj2);             /* 897 */

    ph1 = (PHEAD *)my_hs1->phead.get();                              /* 898 */
    ph2 = (PHEAD *)my_hs2->phead.get();                              /* 899 */

    ori_ph1 = &m_ctrl->ph1;                                          /* 900 */
    ori_ph2 = &m_ctrl->ph2;                                          /* 901 */

    for (i = 0; i < (int)my_hs1->blocks; i++)                        /* 906 */
    {
        SGDPROCUNITHEADER *prim;

        prim = ((SGDFILEHEADER *)my_hs1)->apProcUnitHead[i];         /* 908 */

        while (prim != 0 && prim->pNext.get() != 0)                  /* 911 */
        {
            if (prim->iCategory == SPC_VUVN)                         /* 917 */
            {
                SGDVUVNDESC *vh = &prim->VUVNDesc;             /* 920 */
                _VECTORDATA *vd = (_VECTORDATA *)&prim[3];

                if (vh->ucVectorType == 0)                                  /* 926 */
                {
                    for (k = 0; k < vh->sNumVertex; k++)                   /* 932, 938 */
                    {
                        float *cur = (float *)vd[k].vAddress.pVertex.get();

                        vd[k].vAddress.pVertex = (sceVu0FVECTOR *)
                            (ori_ph1->pUniqVertex + (cur - morph_v));
                    }
                }
            }

            prim = prim->pNext;                                      /* 950 */
        }

        prim = ((SGDFILEHEADER *)my_hs2)->apProcUnitHead[i];         /* 954 */

        while (prim != 0 && prim->pNext.get() != 0)                  /* 957 */
        {
            if (prim->iCategory == SPC_VUVN)                         /* 963 */
            {
                SGDVUVNDESC *vh = &prim->VUVNDesc;             /* 966 */
                _VECTORDATA *vd = (_VECTORDATA *)&prim[3];

                if (vh->ucVectorType == 0)                                  /* 973 */
                {
                    for (k = 0; k < vh->sNumVertex; k++)                   /* 979, 985 */
                    {
                        float *cur = (float *)vd[k].vAddress.pVertex.get();

                        vd[k].vAddress.pVertex = (sceVu0FVECTOR *)
                            (ori_ph2->pUniqVertex + (cur - morph_v));
                    }
                }
            }

            prim = prim->pNext;                                      /* 997 */
        }
    }

    ph1->pUniqVertex     = ori_ph1->pUniqVertex;                     /* 1003 */
    ph2->pUniqVertex     = ori_ph2->pUniqVertex;                     /* 1004 */
    ph1->pWeightedVertex = ori_ph1->pWeightedVertex;                 /* 1005 */
    ph2->pWeightedVertex = ori_ph2->pWeightedVertex;                 /* 1006 */
    ph1->pCommonVertex   = ori_ph1->pCommonVertex;                   /* 1007 */
    ph2->pCommonVertex   = ori_ph2->pCommonVertex;                   /* 1008 */

    return 1;                                                       /* 1010 */
}

static int MorphSetWorkNo(ANI_CTRL *ani_ctrl)
{
    now_work = ani_ctrl;                                            /* 1017 */

    return 1;                                                       /* 1018 */
}
