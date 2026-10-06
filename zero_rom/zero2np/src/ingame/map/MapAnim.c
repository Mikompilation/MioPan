// FILE: /home/zero_rom/zero2np/src/ingame/map/MapAnim.c
//
// Map animation controller: everything in a room that moves under its own
// motion data rather than under the player's.
//
// Two systems share the file.  MapAnim* is one animation per registration:
// MapAnimRegistEx() takes a model, its motion, and the matrix the caller wants
// the result written into, and hands back a slot id.  MapAnimProc() then walks
// all 32 slots once a frame and MapAnimOne() does the work -- decode the key
// under the play cursor, build scale/rotation/translation from it, fold in the
// registration's own offset and rotation, and step the cursor on.
//
// MapManim* is the multi-instance variant.  A stand of trees is one model with
// many coordinate blocks, so one MAPMANIM_HEAD owns a linked list of
// MAPMANIM_MATRIX nodes, each with its own MapAnim slot and its own start
// frame; MapManimProc() copies the finished matrices into the model's SGD
// coordinate array so the whole stand draws from a single mesh.
//
// Three details are worth knowing before touching MapAnimOne():
//
//   * The play cursor is in hundredths of a frame.  Dividing by 100 gives the
//     key index and the remainder is the interpolation weight -- which is why
//     PAL steps 120 per field where NTSC steps 100.
//   * Key interpolation only happens on PAL.  On NTSC the cursor always lands
//     exactly on a key, so the ROM takes a cheaper path that reads the decoded
//     RST_DATA directly instead of blending two of them.
//   * The two shared ANI_CTRLs (MapAnimMotCtl) exist so the door transition
//     can always get a control block: motGetANI_CTRL() draws from the global
//     motion pool and can legitimately come back empty mid-room-load.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), MapAnim.o
// 0x001035c0..0x001052e0.

#include "MapAnim.h"

#include "MapGeom.h"                            /* sceVu0UnitMatrix & co  */
#include "../../common/utility.h"               /* _SetVector             */
#include "../../common/utility2.h"              /* PRINT_ASSERT / _ERROR  */
#include "../../common/packfile.h"              /* GetPakTaleAddr         */
#include "../../graphics/graph3d/ctl/fixed_array.h"
#include "../../graphics/graph3d/g3dxVu0.h"     /* g3dxVu0MixVectorXYZ    */
#include "../../graphics/graph3d/gra3dConst.h"  /* g_matUnit              */
#include "../../graphics/motion/mim.h"          /* mimInitMimeCtrl        */
#include "../../graphics/motion/motion.h"       /* mot* / sceRotMatrixXYZ */
#include "../../ingame/plyr/unit_ctl.h"         /* RotLimitChk            */
#include "../../system/os/system.h"             /* GetPALMode             */

#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* The ROM spells pi at full double precision here (lit8 39e0d0), unlike
 * MapGeom.h's shortened 3.141592 -- keep them apart. */
#define MAPANIM_PI      3.14159265358979323846

/* MapAnimOne() builds its local matrix at the map's placement scale, with Y
 * and Z negated for the map's handedness.  Same convention as MapGeom.h. */
#define MAPANIM_SCALE   25.0f

/* PORT: the ROM clears a flat 0x80 bytes of the ANI_CTRL -- all of
 * ANI_CODE_CTRL plus the first five MOT_CTRL words.  ANI_CODE_CTRL holds
 * pointers, so that byte count covers different fields on a 64-bit host; the
 * same *fields* are named instead. */
#define MAPANIM_CTRL_CLEAR_SIZE \
    (offsetof(ANI_CTRL, mot) + offsetof(MOT_CTRL, inp_allcnt))

/* MapAnimRegstMim() carves its scratch out of the pak tail for this many
 * morph parts before handing the rest to mimInitMimeCtrl(). */
#define MAPANIM_MIM_MAX 50

static fixed_array<MAP_ANIM_CTL, MAP_ANIM_CTL_MAX>       MapAnimCtl;         /* bss 3f65a0 */
static fixed_array<ANI_CTRL, MAP_ANIM_MOT_CTL_MAX>       MapAnimMotCtl;      /* bss 3f75a0 */
static fixed_array<MAPMANIM_MATRIX, MAPMANIM_MATRIX_MAX> MapManimMatrxList;  /* bss 3f7a20 */
static fixed_array<MAPMANIM_HEAD, MAPMANIM_HEAD_MAX>     MapManimList;       /* bss 3fc9d0 */

static MAP_ANIM_CTL    *MapAnimGetCtlPtr(u_int *mdl_p, float (*pMat)[4][4]);
static MAP_ANIM_CTL    *MapAnimGetFreeCtlEx(int iFlg);
static MAP_ANIM_CTL    *MapAnimGetFreeCtl(void);
static MAP_ANIM_CTL    *MapAnimGetCtlArea(int id);
static MAP_ANIM_CTL    *MapAnimGetUseHeader(int id);
static void             MapAnimOne(int id, float (*mat)[4], int a_flg);

static MAPMANIM_MATRIX *MapManimGetFreeMatrix(void);
static int              MapManimDeleteMatrix(MAPMANIM_MATRIX *mat);
static int              MapManimGetMatrixNumSgd(char *addr);
static MAPMANIM_HEAD   *MapManimGetHeadPtr(int id);
static MAPMANIM_HEAD   *MapManimGetFreeSpace(int buff_id);
static MAPMANIM_MATRIX *MapManimAddMatrix(MAPMANIM_HEAD *hp);
static int              MapManimSetAnimSub(int buff_id, int mat_num,
                                           char *mdl_addr, char *mot_addr,
                                           int *anim_id, float *offset,
                                           float *rot, int st_frame, int e_flg);
static void             MapManimSetMatrixSgdOneSub(SGDFILEHEADER *pSGDHead,
                                                   MAPMANIM_HEAD *hp);
static void             MapManimSetMatrixSgdOne(MAPMANIM_HEAD *hp);


/* ==========================================================================
 *  Slot lookup
 * ======================================================================== */

/* The already-registered test.  A model can be registered more than once, so
 * the matrix has to match too -- that is what separates two instances of the
 * same tree. */
static MAP_ANIM_CTL *MapAnimGetCtlPtr(u_int *mdl_p, float (*pMat)[4][4])
{                                                                       /* 58 */
    int i;

    for (i = 0; i < MAP_ANIM_CTL_MAX; i++)                              /* 61 */
    {
        if ((MapAnimCtl[i].flg & MAPANIM_FLG_USE) != 0 &&
            MapAnimCtl[i].ctl != (ANI_CTRL *)0 &&
            MapAnimCtl[i].ctl->base_p == (HeaderSection *)mdl_p &&
            MapAnimCtl[i].mat == pMat)
        {
            return &MapAnimCtl[i];
        }
    }                                                                   /* 70 */

    return (MAP_ANIM_CTL *)0;                                           /* 72 */
}                                                                       /* 73 */


/* Claims a slot.  0x400 / 0x800 borrow one of the two shared ANI_CTRLs --
 * they are recycled rather than allocated, so motFreeANI_CTRL() is called on
 * the way in to release whatever held them last. */
static MAP_ANIM_CTL *MapAnimGetFreeCtlEx(int iFlg)
{                                                                       /* 77 */
    int i;

    for (i = 0; i < MAP_ANIM_CTL_MAX; i++)                              /* 80 */
    {
        if ((MapAnimCtl[i].flg & MAPANIM_FLG_USE) == 0)
        {
            MapAnimCtl[i].id    = (short)i;
            MapAnimCtl[i].frame = 0;
            MapAnimCtl[i].flg   = MAPANIM_FLG_USE;

            if ((iFlg & MAPANIM_FLG_MOTCTL0) != 0)                      /* 86 */
            {
                motFreeANI_CTRL(&MapAnimMotCtl[0]);
                MapAnimCtl[i].ctl = &MapAnimMotCtl[0];
            }
            else if ((iFlg & MAPANIM_FLG_MOTCTL1) != 0)                 /* 89 */
            {
                motFreeANI_CTRL(&MapAnimMotCtl[1]);
                MapAnimCtl[i].ctl = &MapAnimMotCtl[1];
            }
            else
            {
                MapAnimCtl[i].ctl = motGetANI_CTRL();                   /* 93 */
            }

            return &MapAnimCtl[i];
        }
    }                                                                   /* 97 */

    PRINT_ASSERT("MANIM_MAX_OVER\n");                                   /* 98 */

    return (MAP_ANIM_CTL *)0;                                           /* 99 */
}                                                                       /* 100 */


static MAP_ANIM_CTL *MapAnimGetFreeCtl(void)
{
    return MapAnimGetFreeCtlEx(0);                                      /* 106 */
}


static MAP_ANIM_CTL *MapAnimGetCtlArea(int id)
{                                                                       /* 111 */
    if ((u_int)id >= MAP_ANIM_CTL_MAX)                                  /* 112 */
    {
        return (MAP_ANIM_CTL *)0;
    }

    return &MapAnimCtl[id];
}                                                                       /* 114 */


int MapAnimCheckPlay(int id)
{
    MAP_ANIM_CTL *hp = MapAnimGetCtlArea(id);                           /* 119 */

    if (hp == (MAP_ANIM_CTL *)0)                                        /* 121 */
    {
        return -1;
    }

    return (hp->flg & MAPANIM_FLG_USE) != 0;                            /* 122 */
}                                                                       /* 124 */


/* As MapAnimGetCtlArea(), but rejects a slot that is not in use. */
static MAP_ANIM_CTL *MapAnimGetUseHeader(int id)
{
    MAP_ANIM_CTL *cp = MapAnimGetCtlArea(id);                           /* 133 */

    if (cp == (MAP_ANIM_CTL *)0)
    {
        return (MAP_ANIM_CTL *)0;
    }

    return ((cp->flg & MAPANIM_FLG_USE) != 0) ? cp : (MAP_ANIM_CTL *)0; /* 135 */
}                                                                       /* 137 */


/* ==========================================================================
 *  Flags and callback
 * ======================================================================== */

void MapAnimSetFlg(int id, int flg)
{
    MAP_ANIM_CTL *cp = MapAnimGetUseHeader(id);                         /* 142 */

    if (cp != (MAP_ANIM_CTL *)0)
    {
        cp->flg |= (short)flg;                                          /* 143 */
    }
}


void MapAnimDeleteFlg(int id, int flg)
{
    MAP_ANIM_CTL *cp = MapAnimGetUseHeader(id);                         /* 149 */

    if (cp != (MAP_ANIM_CTL *)0)
    {
        cp->flg &= (short)~flg;                                         /* 150 */
    }
}


short MapAnimGetFlg(int id)
{
    MAP_ANIM_CTL *cp = MapAnimGetUseHeader(id);                         /* 156 */

    return (cp != (MAP_ANIM_CTL *)0) ? cp->flg : (short)-1;             /* 157 */
}


void MapAnimSetCallback(int anim_id, MAPANIM_FUNC func, void *dat, void *dat2)
{
    MAP_ANIM_CTL *cp = MapAnimGetUseHeader(anim_id);                    /* 165 */

    if (cp == (MAP_ANIM_CTL *)0)                                        /* 166 */
    {
        return;
    }

    cp->func_dat[0] = dat;                                              /* 168 */
    cp->func_dat[1] = dat2;                                             /* 169 */
    cp->func        = func;

    if (func != (MAPANIM_FUNC)0)                                        /* 170 */
    {
        cp->flg |= MAPANIM_FLG_CALLBACK;                                /* 171 */
    }
    else
    {
        cp->flg &= ~MAPANIM_FLG_CALLBACK;
    }
}


/* ==========================================================================
 *  Morph (MIME) attachment
 * ======================================================================== */

/* Carves the morph control blocks, their data blocks and a scratch buffer out
 * of the pak tail, then hands them to mimInitMimeCtrl() and marks every part
 * live.  Returns the new tail so the caller can keep allocating past it.
 *
 * DEAD CODE in this build: nothing calls it (ZERO2.MAP has no reference and
 * Ghidra finds no caller), which is presumably why the guard below survives.
 * The ROM tests `mim_p == NULL` and then dereferences mim_p and passes it to
 * GetPakTaleAddr() -- GCC even constant-folded the argument to zero, so it is
 * the compiler's own reading, not a decompiler artifact.  Preserved as-is;
 * the sense is almost certainly inverted in the original source. */
u_int *MapAnimRegstMim(int id, u_int *mim_p, u_int *pkt_p)
{
    u_int         i;
    MAP_ANIM_CTL *cp;
    ANI_CTRL     *ap;
    MIME_DAT     *mim_dat;

    cp = MapAnimGetCtlArea(id);                                         /* 188 */
    if (cp == (MAP_ANIM_CTL *)0)
    {
        return (u_int *)0;
    }

    if (mim_p != (u_int *)0)                                            /* 190 */
    {
        return (u_int *)0;
    }

    ap    = cp->ctl;                                                    /* 193 */
    pkt_p = (u_int *)GetPakTaleAddr(mim_p);

    ap->mim_num = *mim_p;                                               /* 194 */
    ap->mim     = (MIME_CTRL *)motAlign128(pkt_p);                      /* 195 */

    mim_dat = (MIME_DAT *)motAlign128((u_int *)(ap->mim + MAPANIM_MIM_MAX));    /* 197 */
    pkt_p   = motAlign128((u_int *)(mim_dat + MAPANIM_MIM_MAX));                /* 198 */

    pkt_p = mimInitMimeCtrl(ap->mim, mim_dat, (u_int *)0, ap->mpk_p,    /* 199 */
                            pkt_p, &ap->mim_num);

    for (i = 0; i < ap->mim_num; i++)                                   /* 202 */
    {
        ap->mim[i].stat = 2;                                            /* 203 */
    }                                                                   /* 204 */

    return pkt_p;                                                       /* 206 */
}                                                                       /* 207 */


/* ==========================================================================
 *  Registration
 * ======================================================================== */

int MapAnimRegistEx(int play_id, u_int *mdl_p, u_int *mot_p,
                    float (*mat)[4][4], float *offset, float *rot,
                    int iFlg, int iFflg)
{                                                                       /* 212 */
    MAP_ANIM_CTL *cp;
    ANI_CTRL     *ap;

    cp = MapAnimGetCtlPtr(mdl_p, mat);                                  /* 217 */
    if (cp == (MAP_ANIM_CTL *)0)
    {
        if ((iFlg & MAPANIM_FLG_MOTCTL_MASK) != 0)                      /* 220 */
        {
            cp = MapAnimGetFreeCtlEx(iFlg);                             /* 221 */
            if (cp == (MAP_ANIM_CTL *)0)
            {
                PRINT_ERROR("NO_ANIM_SPECE\n");                         /* 222 */
                return -1;                                              /* 223 */
            }
        }
        else
        {
            cp = MapAnimGetFreeCtl();                                   /* 227 */
            if (cp == (MAP_ANIM_CTL *)0)
            {
                PRINT_ERROR("NO_ANIM_SPECE\n");                         /* 228 */
                return -1;                                              /* 229 */
            }
        }
    }

    ap = cp->ctl;                                                       /* 232 */
    memset(ap, 0, MAPANIM_CTRL_CLEAR_SIZE);                             /* 233 */

    ap->base_p = (HeaderSection *)mdl_p;                                /* 234 */
    cp->mat    = mat;                                                   /* 235 */
    cp->mot_p  = mot_p;                                                 /* 236 */

    g3dxVu0CopyVector(cp->offset, offset);

    /* Y and Z come in with the map's handedness; flip them once here so the
     * rest of the file can treat cp->rot as plain radians. */
    _SetVector(cp->rot, rot[0], -rot[1], -rot[2], 1.0f);                /* 238 */
    RotLimitChk(&cp->rot[0]);                                           /* 239 */
    RotLimitChk(&cp->rot[1]);                                           /* 240 */
    RotLimitChk(&cp->rot[2]);                                           /* 241 */

    cp->flg |= (short)iFlg;                                             /* 243 */

    if (mot_p != (u_int *)0)                                            /* 246 */
    {
        motInitMotCtrlEx(&ap->mot, mot_p, (u_int *)0, play_id);         /* 247 */
        ap->mot_num = *mot_p;                                           /* 248 */
    }

    /* Evaluate frame 0 straight away so the matrix is valid before the first
     * MapAnimProc(); a freshly placed object must not draw at the origin. */
    MapAnimOne((int)cp->id, *cp->mat, iFflg);                           /* 252 */

    return (int)cp->id;                                                 /* 253 */
}                                                                       /* 254 */


int MapAnimRegist(int play_id, u_int *mdl_p, u_int *mot_p,
                  float (*mat)[4][4], float *offset, float *rot)
{
    return MapAnimRegistEx(play_id, mdl_p, mot_p, mat, offset, rot, 0, 3);  /* 262 */
}


/* Jumps an existing registration onto another sub-sequence.  The ANI_CTRL is
 * wiped and rebuilt but base_p is carried across, so the model binding
 * survives -- this is a re-cue, not a re-registration. */
int MapAnimCall(int anim_id, int play_id)
{
    MAP_ANIM_CTL  *cp;
    ANI_CTRL      *ap;
    HeaderSection *wp;

    cp = MapAnimGetCtlArea(anim_id);                                    /* 273 */
    if (cp == (MAP_ANIM_CTL *)0)
    {
        return -1;
    }

    cp->frame = 0;                                                      /* 274 */

    ap = cp->ctl;                                                       /* 276 */
    wp = ap->base_p;
    memset(ap, 0, MAPANIM_CTRL_CLEAR_SIZE);                             /* 278 */
    ap->base_p = wp;                                                    /* 279 */

    if (cp->mot_p != (u_int *)0)                                        /* 282 */
    {
        motInitMotCtrlEx(&ap->mot, cp->mot_p, (u_int *)0, play_id);     /* 283 */
        ap->mot_num = *cp->mot_p;                                       /* 284 */
    }

    cp->flg &= ~MAPANIM_FLG_STOP;                                       /* 288 */

    return (int)cp->id;                                                 /* 290 */
}                                                                       /* 291 */


/* ==========================================================================
 *  Teardown
 * ======================================================================== */

void MapAnimDelete(int id)
{
    MAP_ANIM_CTL *cp = MapAnimGetCtlArea(id);                           /* 299 */

    if (cp == (MAP_ANIM_CTL *)0)
    {
        return;
    }

    if (cp->ctl != (ANI_CTRL *)0)                                       /* 300 */
    {
        printf("MapAnim free ANI_CTRL[%x]\n", (u_int)(uintptr_t)cp->ctl);    /* 302 */
        motFreeANI_CTRL(cp->ctl);                                       /* 303 */
        cp->ctl   = (ANI_CTRL *)0;                                      /* 304 */
        cp->frame = 0;                                                  /* 305 */
        cp->flg   = 0;                                                  /* 306 */
    }
}                                                                       /* 307 */


/* Drops everything a room buffer owns.  MapDrawDeleteNoDraw() calls this after
 * MapManimDelete(), so the heads are already gone by the time the individual
 * slots are released. */
void MapAnimDeleteBuffID(int buff_id)
{                                                                       /* 312 */
    int i;
    int flg;

    flg = (buff_id == 0) ? MAPANIM_FLG_BUFF0 : MAPANIM_FLG_BUFF1;       /* 315 */

    for (i = 0; i < MAP_ANIM_CTL_MAX; i++)                              /* 318 */
    {
        if ((MapAnimCtl[i].flg & MAPANIM_FLG_USE) != 0 &&
            (MapAnimCtl[i].flg & flg) != 0)
        {
            MapAnimDelete(i);                                           /* 322 */
        }
    }                                                                   /* 323 */
}


void MapAnimDeleteAll(void)
{                                                                       /* 328 */
    int i;

    for (i = 0; i < MAP_ANIM_CTL_MAX; i++)                              /* 331 */
    {
        if ((MapAnimCtl[i].flg & MAPANIM_FLG_USE) != 0)
        {
            MapAnimDelete(i);                                           /* 334 */
        }
    }
}                                                                       /* 335 */


/* ==========================================================================
 *  Key evaluation
 * ======================================================================== */

/* Decodes the key under the play cursor into pMapAniCtl->rst, blending
 * towards the next key when the cursor sits between two.
 *
 * DEAD CODE in this build, like MapAnimRegstMim(): MapAnimOne() carries its
 * own copy of the same logic.  Kept because ZERO2.MAP exports it. */
void MapAnimGetRstMix(MAP_ANIM_CTL *pMapAniCtl, MOT_CTRL *pMotCtl)
{                                                                       /* 353 */
    RST_DATA  aRst;
    float     fWDat;
    u_int     frame = pMapAniCtl->frame;                                /* 354 */
    u_int     key   = frame / MAPANIM_FRAME_SCALE;                      /* 355 */
    u_int     rem   = frame % MAPANIM_FRAME_SCALE;                      /* 356 */

    motGetFrameDataRT(&pMapAniCtl->rst, pMotCtl->dat, key,              /* 360 */
                      (u_int)(frame == 0));

    if (rem != 0 && (int)key < pMotCtl->all_cnt - 1)                    /* 363 */
    {
        fWDat = (float)rem / (float)MAPANIM_FRAME_SCALE;                /* 365 */

        aRst = pMapAniCtl->rst;                                         /* 367 */
        motGetFrameDataRT(&aRst, pMotCtl->dat, key + 1, 0);             /* 368 */

        /* Rotation and translation blend; scale is left on the earlier key. */
        g3dxVu0MixVectorXYZ(pMapAniCtl->rst.rot,   aRst.rot,   fWDat);
        g3dxVu0MixVectorXYZ(pMapAniCtl->rst.trans, aRst.trans, fWDat);
    }
}


/* Builds the matrix for a point `fRate` of the way from pStartRst to pEndRst.
 *
 * The rotations go through motInterpMatrix() rather than a per-axis lerp: the
 * two key rotations are turned into orthonormal bases first, so what is
 * interpolated is the rotation itself and not three Euler angles that would
 * gimbal past each other. */
void MapAnimMixRst(float (*aRetMatrix)[4], RST_DATA *pStartRst,
                   RST_DATA *pEndRst, float fRate)
{                                                                       /* 377 */
    float vTrans[4];
    float vScale[4];
    float aStMat[4][4];
    float aEndMat[4][4];
    float aInterp[4][4];

    sceRotMatrixXYZ(aStMat, g_matUnit, pStartRst->rot);                 /* 383 */
    sceVu0Normalize(aStMat[0], aStMat[0]);                              /* 384 */
    sceVu0Normalize(aStMat[1], aStMat[1]);                              /* 385 */
    sceVu0Normalize(aStMat[2], aStMat[2]);                              /* 386 */

    sceRotMatrixXYZ(aEndMat, g_matUnit, pEndRst->rot);                  /* 387 */
    sceVu0Normalize(aEndMat[0], aEndMat[0]);                            /* 388 */
    sceVu0Normalize(aEndMat[1], aEndMat[1]);                            /* 389 */
    sceVu0Normalize(aEndMat[2], aEndMat[2]);                            /* 390 */

    sceVu0UnitMatrix(aInterp);                                          /* 392 */
    motInterpMatrix(aInterp, aStMat, aEndMat, fRate);                   /* 393 */

    sceVu0InterVector(vTrans, pEndRst->trans, pStartRst->trans, fRate); /* 399 */
    sceVu0InterVector(vScale, pEndRst->scale, pStartRst->scale, fRate); /* 404 */

    sceVu0UnitMatrix(aRetMatrix);                                       /* 407 */
    aRetMatrix[0][0] = vScale[0];                                       /* 408 */
    aRetMatrix[1][1] = vScale[1];                                       /* 409 */
    aRetMatrix[2][2] = vScale[2];                                       /* 410 */
    aRetMatrix[3][3] = 1.0f;                                            /* 411 */

    sceVu0MulMatrix(aRetMatrix, aRetMatrix, aInterp);                   /* 413 */
    sceVu0TransMatrix(aRetMatrix, aRetMatrix, vTrans);                  /* 414 */
}


/* ==========================================================================
 *  Per-animation tick
 * ======================================================================== */

/* Evaluates animation `id` into `mat` and advances its cursor.
 *
 * a_flg is the caller's per-tick override: bit 0 drops the key's rotation
 * (the door uses it so the hinge angle comes from the registration instead of
 * the motion), bit 1 evaluates without stepping, bit 8 leaves the local
 * offset/rotation matrix out of the result.
 *
 * Note that aMstRst is filled and then zeroed but never read -- the ROM passes
 * &hp->rst to MapAnimMixRst(), not the copy.  Left in place: it costs a stack
 * copy per PAL tick and removing it would be a deviation, not a fix. */
static void MapAnimOne(int id, float (*mat)[4], int a_flg)
{                                                                       /* 429 */
    MOT_CTRL      *m_ctrl;
    MAP_ANIM_CTL  *hp;
    ANI_CTRL      *ap;
    float          lmat[4][4];
    float          rot_y = 0.0f;
    SGDCOORDINATE *cp;
    float          fWDat;
    RST_DATA       aMstRst;
    RST_DATA       aRst;
    float          w_mat[4][4];

    hp = MapAnimGetCtlArea(id);
    if (hp == (MAP_ANIM_CTL *)0)
    {
        return;
    }

    ap     = hp->ctl;                                                   /* 432 */
    m_ctrl = &ap->mot;                                                  /* 433 */

    if (m_ctrl->dat == (u_int *)0)                                      /* 435 */
    {
        return;
    }

    /* Force the model's coordinate cache to recompute: the matrix this
     * function is about to write is one of its inputs. */
    if (ap->base_p != (HeaderSection *)0)                               /* 446 */
    {
        cp = ap->base_p->coordp;                                        /* 448 */
        cp->bCalc = 0;                                                  /* 450 */
    }

    motGetFrameDataRT(&hp->rst, m_ctrl->dat,                            /* 461, 464 */
                      hp->frame / MAPANIM_FRAME_SCALE,
                      (u_int)(hp->frame == 0));

    /* The local matrix: placement scale, the registration's own rotation, and
     * its offset.  Skipped for a slot that is flagged no-local without also
     * being flagged callback-only. */
    if ((hp->flg & (MAPANIM_FLG_NOLOCAL | MAPANIM_FLG_CALLBACK))        /* 468 */
        != MAPANIM_FLG_NOLOCAL)
    {
        if ((hp->flg & MAPANIM_FLG_REVERSE) != 0)                       /* 471 */
        {
            rot_y = (float)((double)hp->rot[1] + MAPANIM_PI);           /* 472 */
        }
        else
        {
            rot_y = hp->rot[1];                                         /* 474 */
        }
        RotLimitChk(&rot_y);                                            /* 476 */

        sceVu0UnitMatrix(lmat);                                         /* 479 */
        /* MapGeom.h 50-52: scale on the diagonal, Y and Z negated. */
        lmat[0][0] =  MAPANIM_SCALE;
        lmat[1][1] = -MAPANIM_SCALE;
        lmat[2][2] = -MAPANIM_SCALE;

        sceVu0RotMatrixX(lmat, lmat, hp->rot[0]);                       /* 481 */
        sceVu0RotMatrixY(lmat, lmat, rot_y);                            /* 482 */
        sceVu0RotMatrixZ(lmat, lmat, hp->rot[2]);                       /* 483 */

        g3dxVu0CopyVector(lmat[3], hp->offset);
        lmat[3][3] = 1.0f;                                              /* 485 */
    }
    else
    {
        /* PORT: the ROM leaves lmat uninitialised on this path and can still
         * fold it into `mat` below -- MapManimSetAnimSub() reaches exactly
         * that combination (flg carries 0x100 without 0x200, a_flg is 3), so
         * every tree and every door instance multiplied by stack garbage on
         * its first tick.  MapAnimProc() overwrites the result the next frame,
         * which is why it was never visible on hardware; here it would be a
         * genuine uninitialised read and can produce NaNs.  Identity is the
         * only reading that leaves `mat` alone, which is what the skip was
         * asking for. */
        sceVu0UnitMatrix(lmat);
    }

    sceVu0UnitMatrix(mat);                                              /* 492 */

    if (GetPALMode() != 0)                                              /* 495 */
    {
        u_int key = hp->frame / MAPANIM_FRAME_SCALE;                    /* 496 */
        u_int rem = hp->frame % MAPANIM_FRAME_SCALE;                    /* 497 */

        fWDat   = 0.0f;                                                 /* 498 */
        aMstRst = hp->rst;                                              /* 502 */
        aRst    = hp->rst;                                              /* 503 */

        if (rem != 0 && (int)key < m_ctrl->all_cnt - 1)                 /* 506 */
        {
            motGetFrameDataRT(&aRst, m_ctrl->dat, key + 1, 0);          /* 507 */
            fWDat = (float)rem / (float)MAPANIM_FRAME_SCALE;            /* 509 */
        }

        if ((a_flg & MAPANIM_ONE_NOROT) != 0)                           /* 513 */
        {
            aMstRst.rot[0] = aMstRst.rot[1] = aMstRst.rot[2] =          /* 515 */
                aRst.rot[0] = aRst.rot[1] = aRst.rot[2] = 0.0f;
        }

        if (rem != 0)                                                   /* 518 */
        {
            MapAnimMixRst(mat, &hp->rst, &aRst, fWDat);                 /* 520 */
        }
        else
        {
            /* Same block as the NTSC path below.  GCC tail-merged the two
             * copies, so only the second copy's line numbers survive. */
            mat[0][0] = hp->rst.scale[0];
            mat[1][1] = hp->rst.scale[1];
            mat[2][2] = hp->rst.scale[2];

            if ((a_flg & MAPANIM_ONE_NOROT) == 0)
            {
                sceVu0RotMatrixX(mat, mat, hp->rst.rot[0]);
                sceVu0RotMatrixY(mat, mat, hp->rst.rot[1]);
                sceVu0RotMatrixZ(mat, mat, hp->rst.rot[2]);
            }

            g3dxVu0CopyVector(mat[3], hp->rst.trans);
            mat[3][3] = 1.0f;
        }
    }
    else
    {
        /* NTSC: the cursor always lands exactly on a key, so the decoded
         * RST_DATA goes straight into the matrix with no blend. */
        mat[0][0] = hp->rst.scale[0];
        mat[1][1] = hp->rst.scale[1];
        mat[2][2] = hp->rst.scale[2];

        if ((a_flg & MAPANIM_ONE_NOROT) == 0)                           /* 542 */
        {
            sceVu0RotMatrixX(mat, mat, hp->rst.rot[0]);                 /* 543 */
            sceVu0RotMatrixY(mat, mat, hp->rst.rot[1]);                 /* 544 */
            sceVu0RotMatrixZ(mat, mat, hp->rst.rot[2]);                 /* 545 */
        }

        g3dxVu0CopyVector(mat[3], hp->rst.trans);
        mat[3][3] = 1.0f;                                               /* 548 */
    }

    if ((a_flg & MAPANIM_ONE_NOLOCAL) == 0)                             /* 554 */
    {
        sceVu0MulMatrix(mat, lmat, mat);
    }

    if ((a_flg & MAPANIM_ONE_NOADVANCE) == 0)                           /* 575 */
    {
        if (GetPALMode() != 0)                                          /* 577 */
        {
            hp->frame += MAPANIM_FRAME_STEP_PAL;                        /* 578 */
        }
        else
        {
            hp->frame += MAPANIM_FRAME_STEP_NTSC;                       /* 580 */
        }
    }

    if ((hp->flg & MAPANIM_FLG_CALLBACK) != 0)                          /* 585 */
    {
        if (hp->func != (MAPANIM_FUNC)0)
        {
            /* The callback gets the registration's own matrix, not `mat` --
             * they differ whenever the caller asked for a bare result. */
            sceVu0CopyMatrix(w_mat, *hp->mat);

            if ((hp->flg & MAPANIM_FLG_NOLOCAL) != 0)                   /* 593 */
            {
                sceVu0MulMatrix(w_mat, lmat, w_mat);
            }

            hp->func((hp->frame / MAPANIM_FRAME_SCALE                   /* 598 */
                          < (u_int)m_ctrl->all_cnt)
                         ? (int)hp->id : -1,
                     w_mat, hp->func_dat[0], hp->func_dat[1]);          /* 599 */
        }
    }

    /* Ran past the last key. */
    if (hp->frame / MAPANIM_FRAME_SCALE >= (u_int)m_ctrl->all_cnt)      /* 603 */
    {
        if ((hp->flg & MAPANIM_FLG_LOOP) != 0)                          /* 604 */
        {
            hp->frame = 0;                                              /* 605 */
        }
        else if ((hp->flg & MAPANIM_FLG_KEEP) != 0)                     /* 606 */
        {
            hp->flg |= MAPANIM_FLG_STOP;                                /* 607 */
        }
        else
        {
            MapAnimDelete(id);                                          /* 609 */
        }
    }
}                                                                       /* 612 */


void MapAnimProc(void)
{                                                                       /* 616 */
    int i;

    for (i = 0; i < MAP_ANIM_CTL_MAX; i++)                              /* 619 */
    {
        if ((MapAnimCtl[i].flg & MAPANIM_FLG_USE) != 0 &&
            (MapAnimCtl[i].flg & MAPANIM_FLG_STOP) == 0)
        {
            MapAnimOne(i, *MapAnimCtl[i].mat,
                       MapAnimCtl[i].flg & MAPANIM_FLG_NOLOCAL);
        }
    }                                                                   /* 625 */

    MapManimProc();                                                     /* 628 */
}


void MapAnimInit(void)
{                                                                       /* 633 */
    int i;

    for (i = 0; i < MAP_ANIM_CTL_MAX; i++)                              /* 636 */
    {
        memset(&MapAnimCtl[i], 0, sizeof(MAP_ANIM_CTL));
    }

    /* The two shared control blocks are not owned by any slot yet. */
    motFreeANI_CTRL(&MapAnimMotCtl[0]);                                 /* 638 */
    motFreeANI_CTRL(&MapAnimMotCtl[1]);
}


/* ==========================================================================
 *  MapManim: multi-instance
 * ======================================================================== */

static MAPMANIM_MATRIX *MapManimGetFreeMatrix(void)
{                                                                       /* 689 */
    int i;

    for (i = 0; i < MAPMANIM_MATRIX_MAX; i++)                           /* 693 */
    {
        if ((MapManimMatrxList[i].flg & 1) == 0)
        {
            MapManimMatrxList[i].flg     |= 1;
            MapManimMatrxList[i].mot_addr = (char *)0;
            MapManimMatrxList[i].next     = (MAPMANIM_MATRIX *)0;

            return &MapManimMatrxList[i];
        }
    }                                                                   /* 699 */

    PRINT_ERROR("NO_MATRIX_SPACE\n");                                   /* 700 */

    return (MAPMANIM_MATRIX *)0;                                        /* 701 */
}                                                                       /* 702 */


static int MapManimDeleteMatrix(MAPMANIM_MATRIX *mat)
{                                                                       /* 706 */
    int i;

    if (mat == (MAPMANIM_MATRIX *)0)
    {
        return -2;
    }

    for (i = 0; i < MAPMANIM_MATRIX_MAX; i++)                           /* 712 */
    {
        if (&MapManimMatrxList[i] == mat)
        {
            /* Only the in-use bit is cleared -- `next` is left intact so the
             * caller can keep walking the list it is tearing down. */
            MapManimMatrxList[i].flg &= ~1;                             /* 715 */
            return 0;                                                   /* 716 */
        }
    }                                                                   /* 717 */

    return -1;
}                                                                       /* 718 */


/* Instance count for a model: the SGD's block count, which is one coordinate
 * block per instance. */
static int MapManimGetMatrixNumSgd(char *addr)
{                                                                       /* 725 */
    if (addr == (char *)0)
    {
        return 0;
    }

    return (int)((SGDFILEHEADER *)addr)->uiNumBlock;                    /* 726 */
}                                                                       /* 727 */


static MAPMANIM_HEAD *MapManimGetHeadPtr(int id)
{                                                                       /* 731 */
    int i;

    for (i = 0; i < MAPMANIM_HEAD_MAX; i++)                             /* 735 */
    {
        if (MapManimList[i].id == id)
        {
            return &MapManimList[i];
        }
    }                                                                   /* 739 */

    PRINT_ERROR("NO_CTL_ID[%d]\n", id);                                 /* 740 */

    return (MAPMANIM_HEAD *)0;
}                                                                       /* 741 */


static MAPMANIM_HEAD *MapManimGetFreeSpace(int buff_id)
{                                                                       /* 745 */
    int i;

    for (i = 0; i < MAPMANIM_HEAD_MAX; i++)                             /* 749 */
    {
        if (MapManimList[i].buff_id == -1)
        {
            MapManimList[i].id       = i;
            MapManimList[i].buff_id  = buff_id;
            MapManimList[i].mat_addr = (MAPMANIM_MATRIX *)0;

            return &MapManimList[i];
        }
    }                                                                   /* 756 */

    PRINT_ERROR("NO_FURN_ANIM_HEAD_SPACE\n");                           /* 757 */

    return (MAPMANIM_HEAD *)0;
}                                                                       /* 758 */


/* Appends a fresh node to the head's list.
 *
 * PORT: the ROM walks with a single MAPMANIM_MATRIX * seeded from the head,
 * because MAPMANIM_HEAD::mat_addr and MAPMANIM_MATRIX::next both sit at +0xc.
 * That pun is not portable, so the tail is tracked as a pointer-to-pointer
 * instead; same list, same result, no aliasing. */
static MAPMANIM_MATRIX *MapManimAddMatrix(MAPMANIM_HEAD *hp)
{                                                                       /* 762 */
    MAPMANIM_MATRIX  *mat_sp;
    MAPMANIM_MATRIX **work_mat;

    if (hp == (MAPMANIM_HEAD *)0)                                       /* 765 */
    {
        return (MAPMANIM_MATRIX *)0;
    }

    work_mat = &hp->mat_addr;
    for (mat_sp = hp->mat_addr; mat_sp != (MAPMANIM_MATRIX *)0;         /* 771 */
         mat_sp = mat_sp->next)
    {
        work_mat = &mat_sp->next;                                       /* 776 */
    }

    *work_mat = MapManimGetFreeMatrix();                                /* 778 */

    return *work_mat;
}                                                                       /* 780 */


void MapManimDeleteOne(int id)
{                                                                       /* 784 */
    MAPMANIM_MATRIX *mat_sp;
    MAPMANIM_HEAD   *hp;

    if ((u_int)id >= MAPMANIM_HEAD_MAX)                                 /* 788 */
    {
        return;
    }

    hp = &MapManimList[id];
    if (hp == (MAPMANIM_HEAD *)0)                                       /* 791 */
    {
        return;
    }

    /* MapManimDeleteMatrix() only clears the in-use bit, so stepping onto
     * ->next after the call is still safe. */
    for (mat_sp = hp->mat_addr; mat_sp != (MAPMANIM_MATRIX *)0;         /* 792 */
         mat_sp = mat_sp->next)                                         /* 793 */
    {
        MapManimDeleteMatrix(mat_sp);                                   /* 799 */
    }

    hp->buff_id = -1;                                                   /* 802 */
}                                                                       /* 803 */


void MapManimDelete(int buff_id)
{                                                                       /* 807 */
    int i;

    for (i = 0; i < MAPMANIM_HEAD_MAX; i++)                             /* 811 */
    {
        if (MapManimList[i].buff_id == buff_id)
        {
            MapManimDeleteOne(MapManimList[i].id);                      /* 815 */
        }
    }
}


/* The shared body of the three MapManimSetAnim entry points.
 *
 * One MapAnim slot is registered per instance, all pointing at the same model
 * and motion but each writing into its own node matrix.  An anim_id[] entry
 * that is negative is a hole -- that coordinate block gets no animation --
 * and MAPMANIM_ID_END stops the walk. */
static int MapManimSetAnimSub(int buff_id, int mat_num, char *mdl_addr,
                              char *mot_addr, int *anim_id, float *offset,
                              float *rot, int st_frame, int e_flg)
{
    MAPMANIM_MATRIX *mp;
    MAPMANIM_HEAD   *hp;
    MAP_ANIM_CTL    *cp;
    int              i;
    int              flg;
    int              iAniCtlFlg;

    hp = MapManimGetFreeSpace(buff_id);                                 /* 828 */
    if (hp == (MAPMANIM_HEAD *)0)                                       /* 829 */
    {
        return -1;
    }

    flg = (buff_id == 0) ? MAPANIM_FLG_BUFF0 : MAPANIM_FLG_BUFF1;       /* 832 */
    hp->mdl_addr = mdl_addr;

    for (i = 0; i < mat_num && anim_id[i] != MAPMANIM_ID_END; i++)      /* 835, 839 */
    {
        iAniCtlFlg = 0;

        if (anim_id[i] < 0)                                             /* 840 */
        {
            continue;
        }

        mp = MapManimAddMatrix(hp);                                     /* 842 */
        if (mp == (MAPMANIM_MATRIX *)0)
        {
            return -2;
        }

        mp->mat_id   = i;                                               /* 844 */
        mp->mot_addr = mot_addr;

        /* Alternate instances take the two shared ANI_CTRLs so a two-leaf
         * door can animate without touching the motion pool. */
        if ((e_flg & MAPMANIM_EFLG_SPLITCTL) != 0)                      /* 846 */
        {
            iAniCtlFlg = ((i & 1) == 0) ? MAPANIM_FLG_MOTCTL0           /* 847 */
                                        : MAPANIM_FLG_MOTCTL1;
        }

        mp->anim_id = (short)MapAnimRegistEx(anim_id[i],                /* 853 */
                                             (u_int *)mdl_addr,
                                             (u_int *)mot_addr,
                                             &mp->mat, offset, rot,
                                             e_flg | flg | iAniCtlFlg, 3);
        anim_id[i] = mp->anim_id;                                       /* 854 */

        /* No null check here, and none in the ROM: MapAnimRegistEx() only
         * returns -1 after PRINT_ERROR, which halts a debug build. */
        cp = MapAnimGetCtlArea((int)mp->anim_id);                       /* 857 */
        cp->frame = st_frame;                                           /* 858 */
    }                                                                   /* 859 */

    return hp->id;                                                      /* 860 */
}                                                                       /* 861 */


int MapManimSetAnim(int buff_id, char *mdl_addr, char *mot_addr, int *anim_id,
                    float *offset, float *rot, int st_frame)
{
    return MapManimSetAnimEx(buff_id, mdl_addr, mot_addr, anim_id, offset,  /* 872 */
                             rot, st_frame,
                             MAPANIM_FLG_NOLOCAL | MAPANIM_FLG_LOOP
                                 | MAPANIM_FLG_PLAYING);
}


int MapManimSetAnimEx2(int buff_id, int mat_num, char *mot_addr, int *anim_id,
                       float *offset, float *rot, int st_frame, int e_flg)
{
    return MapManimSetAnimSub(buff_id, mat_num, (char *)0, mot_addr,    /* 882 */
                              anim_id, offset, rot, st_frame, e_flg);
}


int MapManimSetAnimEx(int buff_id, char *mdl_addr, char *mot_addr, int *anim_id,
                      float *offset, float *rot, int st_frame, int e_flg)
{
    return MapManimSetAnimSub(buff_id, MapManimGetMatrixNumSgd(mdl_addr),   /* 894 */
                              mdl_addr, mot_addr, anim_id, offset, rot,
                              st_frame, e_flg);
}


/* Pushes the head's instance matrices into the SGD's coordinate array.
 *
 * Blocks and nodes are walked together in one pass, which only works because
 * the nodes are in ascending mat_id order -- MapManimAddMatrix() appends, and
 * MapManimSetAnimSub() adds them in index order.  The last block is the
 * terminator, hence uiNumBlock - 1. */
static void MapManimSetMatrixSgdOneSub(SGDFILEHEADER *pSGDHead, MAPMANIM_HEAD *hp)
{
    int              i;
    MAPMANIM_MATRIX *mp;
    SGDCOORDINATE   *pCoord;

    if (pSGDHead == (SGDFILEHEADER *)0)                                 /* 907 */
    {
        return;
    }

    pCoord = pSGDHead->pCoord;                                          /* 908 */
    if (pCoord == (SGDCOORDINATE *)0)
    {
        return;
    }

    mp = hp->mat_addr;

    for (i = 0; i < (int)(pSGDHead->uiNumBlock - 1)                     /* 912 */
                && mp != (MAPMANIM_MATRIX *)0;                          /* 914 */
         i++, pCoord++)                                                 /* 918 */
    {
        if (mp->mat_id == i)                                            /* 915 */
        {
            sceVu0CopyMatrix(pCoord->matCoord, mp->mat);
            mp = mp->next;                                              /* 917 */
        }
    }
}                                                                       /* 919 */


void MapManimSetMatrixSgdOne2(SGDFILEHEADER *pSGDHead, int id)
{
    MAPMANIM_HEAD *hp = MapManimGetHeadPtr(id);                         /* 923 */

    MapManimSetMatrixSgdOneSub(pSGDHead, hp);                           /* 925 */
}


static void MapManimSetMatrixSgdOne(MAPMANIM_HEAD *hp)
{
    MapManimSetMatrixSgdOneSub((SGDFILEHEADER *)hp->mdl_addr, hp);      /* 932 */
}


void MapManimUnitMatrix(int id)
{                                                                       /* 937 */
    MAPMANIM_MATRIX *mat_sp;
    MAPMANIM_HEAD   *hp;

    if ((u_int)id >= MAPMANIM_HEAD_MAX)                                 /* 941 */
    {
        return;
    }

    hp = &MapManimList[id];
    if (hp == (MAPMANIM_HEAD *)0)                                       /* 944 */
    {
        return;
    }

    for (mat_sp = hp->mat_addr; mat_sp != (MAPMANIM_MATRIX *)0;         /* 946, 947 */
         mat_sp = mat_sp->next)                                         /* 949 */
    {
        sceVu0UnitMatrix(mat_sp->mat);                                  /* 948 */
    }

    MapManimSetMatrixSgdOne(hp);                                        /* 953 */
}                                                                       /* 954 */


void MapManimProc(void)
{                                                                       /* 958 */
    int i;

    for (i = 0; i < MAPMANIM_HEAD_MAX; i++)                             /* 962 */
    {
        if (MapManimList[i].buff_id != -1)
        {
            MapManimSetMatrixSgdOne(&MapManimList[i]);                  /* 968 */
        }
    }
}


void MapManimInit(void)
{                                                                       /* 975 */
    int i;

    for (i = 0; i < MAPMANIM_MATRIX_MAX; i++)                           /* 979 */
    {
        MapManimMatrxList[i].flg = 0;                                   /* 981 */
    }

    for (i = 0; i < MAPMANIM_HEAD_MAX; i++)                             /* 983 */
    {
        MapManimList[i].buff_id = -1;                                   /* 985 */
    }
}
