// FILE: /home/zero_rom/zero2np/src/ingame/map/map_height.c
//
// Height-mesh loading and queries.  A room's ".high" file is a triangle mesh
// plus a uniform X/Z grid index over it: MhIsInArea() picks the grid cell a
// position falls in, and only the triangles registered to that cell are tested.
//
// Everything in here works in the PS2's own units -- positions coming in are
// scaled by mh_l_scale_vec (1/25) on the way in and the answer is scaled back
// by 25 on the way out.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), map_height.o.

#include "map_height.h"

#include "hit_check_base.h"                         /* HcBaseIs*           */
#include "../../graphics/graph3d/ctl/fixed_array.h" /* fixed_array         */
#include "../../graphics/graph3d/g3dxVu0.h"         /* g3dxVu0MulVector    */
#include "../../graphics/graph3d/gra3dConst.h"      /* g_vConvertPS2SI     */
#include "../../graphics/graph3d/sgd_types.h"       /* SGDFILE32           */
#include "../../graphics/graphics.h"                /* Draw3DSquare        */
#include "../../sdk/libvu0.h"

#include <stdint.h>

/* --------------------------------------------------------------------------
 *  On-disc layout.
 *
 *  The five pointer-shaped fields of MhCtrl, and SgmAREA::e_idx, are 32-bit
 *  offsets from the start of the MhCtrl blob as the file is read off disc.
 *  MhInitMapHeight() turned them into absolute pointers in place by adding the
 *  load address -- see the preserved block there for why the port cannot.
 *
 *  Keeping them 32-bit is not optional: widening them would move every field
 *  from 0x20 on and the header would no longer describe the file.  SGDFILE32
 *  resolves each one against the blob base, which it recovers as
 *  (field address - field offset) -- correct precisely because MhSetCtrlP()
 *  stores the blob base itself as the MhCtrl pointer.
 * ------------------------------------------------------------------------ */
typedef struct
{
    /* 0x0 */ sceVu0FVECTOR v;
} SgmVERTEX;                                                    /* 0x10 */

typedef struct
{
    /* 0x0 */ sceVu0IVECTOR n;
} SgmIDX;                                                       /* 0x10 */

typedef struct
{
    /* 0x0 */ int   num;
    /* 0x4 */ u_int e_idx;      /* -> int[num]; resolve with MhAreaEIdx()   */
} SgmAREA;                                                      /* 0x8 */

typedef struct
{
    /* 0x00 */ int                        inf[4];
    /* 0x10 */ int                        w;         /* X grid divisions    */
    /* 0x14 */ int                        h;         /* Z grid divisions    */
    /* 0x18 */ int                        v_count;
    /* 0x1c */ int                        f_count;
    /* 0x20 */ SGDFILE32<float,     0x20> x;         /* w + 1 grid lines    */
    /* 0x24 */ SGDFILE32<float,     0x24> z;         /* h + 1 grid lines    */
    /* 0x28 */ SGDFILE32<SgmAREA,   0x28> area;      /* w * h cells         */
    /* 0x2c */ SGDFILE32<SgmIDX,    0x2c> v_idx;     /* f_count triangles   */
    /* 0x30 */ SGDFILE32<SgmVERTEX, 0x30> V;         /* v_count vertices    */
    /* 0x34 */ int                        dmy[3];
} MhCtrl;                                                       /* 0x40 */

static_assert(sizeof(SgmVERTEX) == 0x10, "SgmVERTEX must stay PS2-sized");
static_assert(sizeof(SgmIDX) == 0x10, "SgmIDX must stay PS2-sized");
static_assert(sizeof(SgmAREA) == 0x08, "SgmAREA must stay PS2-sized");
static_assert(sizeof(MhCtrl) == 0x40, "MhCtrl must stay PS2-sized");

/* SgmAREA::e_idx is a blob-relative offset like the MhCtrl fields, but an
 * SgmAREA does not sit at the blob base, so SGDFILE32 cannot recover the base
 * from the field's own address -- it has to be handed the MhCtrl. */
static inline int *MhAreaEIdx(const MhCtrl *mh_ctrl, const SgmAREA *area)
{
    return (int *)((uintptr_t)mh_ctrl + (uintptr_t)area->e_idx);
}

/* data 31c680 / 31c690 -- read out of the ELF, not guessed. */
static float mh_l_scale_vec[4] = {  0.04f,  -0.04f,  -0.04f, 1.0f };
static float mh_b_scale_vec[4] = { 25.0f,  -25.0f,  -25.0f,  1.0f };

/* sbss 3f4d90 */
static fixed_array<MhCtrl *, 2> map_height_ctrl;

static int     MhSetCtrlP(uintptr_t addr, int id);
static MhCtrl *MhGetCtrlP(int id);
static int     MhSetOffset(MhCtrl *mh_ctrl, float *in_offset);
static int     MhIsInArea(MhCtrl *mh_ctrl, float *pos);
static int     MhIsInTriXZ(MhCtrl *mh_ctrl, float *pos, int idx);
static float   MhIsHeightTri(MhCtrl *mh_ctrl, float *pos, int idx);
static int     MhIsHitHeightTri(MhCtrl *mh_ctrl, float *pos1, float *pos2,
                                int idx);

/* --------------------------------------------------------------------------
 *  MhFirstInit
 *
 *  Drop both room buffers.  Called once from ingame init, not per room.
 * ------------------------------------------------------------------------ */
void MhFirstInit(void)
{                                                                       /* 44 */
    int i;

    for (i = 0; i < 2; i++)                                             /* 47 */
    {
        map_height_ctrl[i] = (MhCtrl *)0;                               /* 49 */
    }
}

/* --------------------------------------------------------------------------
 *  MhInitMapHeight
 *
 *  Bind the height file at `addr` to room buffer `id` and shift it to the
 *  room's world position.
 * ------------------------------------------------------------------------ */
int MhInitMapHeight(uintptr_t addr, float *offset, int id)
{                                                                       /* 63 */
    MhCtrl *mh_ctrl;

    if (MhSetCtrlP(addr, id) == 0)                                      /* 67 */
    {
        return 0;
    }

    mh_ctrl = MhGetCtrlP(id);                                           /* 70 */

    /* The ROM relocated the file here, turning each 32-bit offset into an
     * absolute pointer by adding the load address.  On the host `addr` is a
     * 64-bit pointer that does not fit in those fields, so the port leaves
     * them relative and resolves on access instead (SGDFILE32 / MhAreaEIdx).
     * The original is kept because it is the authority on what the offsets
     * are relative to -- it must not run.
     *
     * The ROM's `i`, `w` and `h` locals existed only for this block. */
#if 0
    mh_ctrl->x     = (float *)((int)mh_ctrl->x + addr);                 /* 73 */
    mh_ctrl->z     = (float *)((int)mh_ctrl->z + addr);                 /* 74 */
    mh_ctrl->area  = (SgmAREA *)((int)mh_ctrl->area + addr);            /* 75 */
    mh_ctrl->v_idx = (SgmIDX *)((int)mh_ctrl->v_idx + addr);            /* 76 */
    mh_ctrl->V     = (SgmVERTEX *)((int)mh_ctrl->V + addr);             /* 77 */

    w = mh_ctrl->w;                                                     /* 79 */
    h = mh_ctrl->h;                                                     /* 80 */
    for (i = 0; i < w * h; i++)                                         /* 81 */
    {
        mh_ctrl->area[i].e_idx =
            (int *)((int)mh_ctrl->area[i].e_idx + addr);                /* 82 */
    }
#endif

    MhSetOffset(mh_ctrl, offset);                                       /* 86 */

    return 1;                                                           /* 87 */
}

/* --------------------------------------------------------------------------
 *  MhGetMapHeight
 *
 *  Floor height under `pos`.  Writes `pos` through to `pos_h` with only Y
 *  replaced, and returns non-zero when a triangle was found.
 *
 *  With several floors stacked over the same spot the pick is deliberately not
 *  "highest wins": candidates more than 20 units above the query point are
 *  treated as a separate, worse class than those below it, so a ceiling-side
 *  surface never beats the floor you are standing on -- but if every candidate
 *  is far above, the last such one is taken rather than nothing.
 *
 *  limit_flg clamps the result to +/-2 units of the query height, which is how
 *  the walk code keeps a step from teleporting the character.
 * ------------------------------------------------------------------------ */
int MhGetMapHeight(float *pos_h, float *pos, int id, int limit_flg)
{                                                                       /* 100 */
    MhCtrl *mh_ctrl;
    int     i;
    int     p_area;
    int     idx;
    int     flg;
    float   height;
    float   max_height;
    float   scale_pos[4];

    flg        = 0;                                                     /* 102 */
    max_height = 0.0f;                                                  /* 104 */

    mh_ctrl = MhGetCtrlP(id);                                           /* 107 */
    if (mh_ctrl != (MhCtrl *)0)                                         /* 108 */
    {
        sceVu0MulVector(scale_pos, pos, mh_l_scale_vec);                /* 114 */

        p_area = MhIsInArea(mh_ctrl, scale_pos);                        /* 116 */
        if (p_area == -1)                                               /* 117 */
        {
            sceVu0CopyVector(pos_h, pos);                               /* 122 */
            return 0;                                                   /* 123 */
        }

        for (i = 0; i < mh_ctrl->area[p_area].num; i++)                 /* 126 */
        {
            idx = MhAreaEIdx(mh_ctrl, &mh_ctrl->area[p_area])[i];       /* 127 */
            if (MhIsInTriXZ(mh_ctrl, scale_pos, idx) != 0)              /* 128 */
            {
                height = MhIsHeightTri(mh_ctrl, scale_pos, idx);        /* 129 */

                if (flg == 0)                                           /* 130 */
                {
                    max_height = height;                                /* 131 */
                    flg        = 1;                                     /* 132 */
                }
                else if (max_height - scale_pos[1] < 20.0f)             /* 134 */
                {
                    /* Both near: highest wins.  A far candidate cannot
                     * displace a near one. */
                    if (height - scale_pos[1] < 20.0f)                  /* 135 */
                    {
                        if (max_height < height)                        /* 137 */
                        {
                            max_height = height;
                        }
                    }
                }
                else if (20.0f < max_height - scale_pos[1])             /* 141 */
                {
                    /* Current pick is far above; a near candidate replaces it
                     * outright, another far one only if it is higher. */
                    if (20.0f < height - scale_pos[1])                  /* 142 */
                    {
                        if (max_height < height)                        /* 143 */
                        {
                            max_height = height;                        /* 148 */
                        }
                    }
                    else
                    {
                        max_height = height;
                    }
                }
            }
        }

        sceVu0CopyVector(pos_h, pos);                                   /* 155 */
        if (flg != 0)                                                   /* 156 */
        {
            if (limit_flg != 0)                                         /* 157 */
            {
                if (2.0f < max_height - scale_pos[1])                   /* 158 */
                {
                    max_height = scale_pos[1] + 2.0f;                   /* 159 */
                }
                else if (max_height - scale_pos[1] < -2.0f)             /* 161 */
                {
                    max_height = scale_pos[1] - 2.0f;
                }
            }

            pos_h[1] = -(max_height * 25.0f);                           /* 165 */
        }
    }

    return flg;                                                         /* 173 */
}

/* --------------------------------------------------------------------------
 *  MhDrawHeight
 *
 *  Debug overlay of the whole height mesh, one translucent green quad per
 *  triangle (the fourth corner repeats the third).
 *
 *  No null check on mh_ctrl: that is the ROM's, and MhCtl.c reaches this only
 *  behind the mhdb.draw_hight debug flag.  It is a live hazard on the host
 *  though -- a room with no ".high" file leaves map_height_ctrl[] null, which
 *  read back as garbage on the EE but faults here.
 * ------------------------------------------------------------------------ */
int MhDrawHeight(int id)
{                                                                       /* 180 */
    MhCtrl                  *mh_ctrl;
    fixed_array<float[4], 3> v;
    int                      i;
    int                      j;
    int                      f;

    mh_ctrl = MhGetCtrlP(id);                                           /* 186 */
    f       = mh_ctrl->f_count;                                         /* 187 */

    for (i = 0; i < f; i++)                                             /* 189 */
    {
        for (j = 0; j < 3; j++)                                         /* 190 */
        {
            sceVu0CopyVector(v[j], mh_ctrl->V[mh_ctrl->v_idx[i].n[j]].v);
            sceVu0MulVector(v[j], v[j], mh_b_scale_vec);
        }                                                               /* 196 */

        Draw3DSquare(v[0], v[1], v[2], v[2], 0x20, 0x80, 0x20, 0x40);
    }                                                                   /* 198 */

    return 1;                                                           /* 199 */
}

/* --------------------------------------------------------------------------
 *  MhHitLineCheck
 *
 *  Non-zero when the segment pos1..pos2 crosses the floor.  Brute force over
 *  every triangle -- the grid index is not used here.
 * ------------------------------------------------------------------------ */
int MhHitLineCheck(float *pos1, float *pos2, int id)
{                                                                       /* 211 */
    MhCtrl *mh_ctrl;
    int     i;
    float   scale_pos1[4];
    float   scale_pos2[4];

    mh_ctrl = MhGetCtrlP(id);                                           /* 216 */
    if (mh_ctrl != (MhCtrl *)0)                                         /* 217 */
    {
        /* Inline COP2 in the ROM (g3dxVu0.h:208-209), not the out-of-line
         * sceVu0MulVector() the rest of this file calls -- and against
         * g_vConvertPS2SI rather than the file-local copy, though the two
         * hold the same four constants. */
        g3dxVu0MulVector(scale_pos1, pos1, g_vConvertPS2SI);
        g3dxVu0MulVector(scale_pos2, pos2, g_vConvertPS2SI);

        for (i = 0; i < mh_ctrl->f_count; i++)                          /* 229 */
        {
            if (MhIsHitHeightTri(mh_ctrl, scale_pos1, scale_pos2, i) != 0)
            {                                                           /* 231 */
                return 1;                                               /* 232 */
            }
        }                                                               /* 234 */
    }

    return 0;                                                           /* 236 */
}

/* --------------------------------------------------------------------------
 *  MhSetCtrlP / MhGetCtrlP
 *
 *  The blob base doubles as the MhCtrl pointer -- see the SGDFILE32 note at
 *  the top of the file, which depends on exactly that.
 * ------------------------------------------------------------------------ */
static int MhSetCtrlP(uintptr_t addr, int id)
{                                                                       /* 245 */
    if ((u_int)id < 2)                                                  /* 246 */
    {
        map_height_ctrl[id] = (MhCtrl *)addr;                           /* 248 */
        return 1;
    }

    return 0;                                                           /* 251 */
}

static MhCtrl *MhGetCtrlP(int id)
{                                                                       /* 258 */
    if ((u_int)id < 2)
    {
        return map_height_ctrl[id];                                     /* 262 */
    }

    return (MhCtrl *)0;                                                 /* 264 */
}

/* --------------------------------------------------------------------------
 *  MhSetOffset
 *
 *  Shift the whole mesh by `in_offset` (world units, scaled down on the way
 *  in).  The grid lines move with it, so MhIsInArea() stays valid.
 *
 *  No null check on mh_ctrl -- the ROM's.  MhSetOffset2() feeds it whatever
 *  MhGetCtrlP() returns, so a room with no height file would fault here; its
 *  only caller, MapLoadSetOffSet(), is currently unreferenced.
 * ------------------------------------------------------------------------ */
static int MhSetOffset(MhCtrl *mh_ctrl, float *in_offset)
{                                                                       /* 268 */
    int   i;
    int   j;
    int   w;
    int   h;
    float offset[4];

    sceVu0MulVector(offset, in_offset, mh_l_scale_vec);                 /* 277 */

    w = mh_ctrl->w;                                                     /* 279 */
    h = mh_ctrl->h;                                                     /* 280 */

    for (i = 0; i <= w; i++)                                            /* 282 */
    {
        mh_ctrl->x[i] += offset[0];                                     /* 283 */
    }

    for (i = 0; i <= h; i++)                                            /* 285 */
    {
        mh_ctrl->z[i] += offset[2];                                     /* 286 */
    }

    for (i = 0; i < mh_ctrl->v_count; i++)                              /* 289 */
    {
        for (j = 0; j < 3; j++)                                         /* 290 */
        {
            mh_ctrl->V[i].v[j] += offset[j];                            /* 291 */
        }
    }

    return 1;                                                           /* 294 */
}

/* --------------------------------------------------------------------------
 *  MhSetOffset2
 *
 *  Re-offset a room already resident in buffer `id`.
 * ------------------------------------------------------------------------ */
int MhSetOffset2(int id, float *in_offset)
{                                                                       /* 300 */
    return MhSetOffset(MhGetCtrlP(id), in_offset);                      /* 303 */
}

/* --------------------------------------------------------------------------
 *  MhIsInArea
 *
 *  Grid cell containing `pos`, or -1 when it falls outside the mesh.
 *
 *  Each axis walks its grid lines until one overshoots.  The extra `i == w`
 *  term makes the last line exclusive: sitting exactly on the far edge lands
 *  on index w + 1, which the range test below rejects.  Landing on 0 means the
 *  position is before the first line.
 * ------------------------------------------------------------------------ */
static int MhIsInArea(MhCtrl *mh_ctrl, float *pos)
{                                                                       /* 311 */
    int w;
    int h;
    int i;
    int j;

    w = mh_ctrl->w;                                                     /* 312 */
    h = mh_ctrl->h;                                                     /* 313 */

    for (i = 0; i <= w; i++)                                            /* 316 */
    {
        if (pos[0] < mh_ctrl->x[i])                                     /* 317 */
        {
            break;
        }

        if (i == w && pos[0] <= mh_ctrl->x[i])                          /* 320 */
        {
            break;
        }
    }                                                                   /* 323 */

    for (j = 0; j <= h; j++)                                            /* 325 */
    {
        if (pos[2] < mh_ctrl->z[j])                                     /* 326 */
        {
            break;
        }

        if (j == h && pos[2] <= mh_ctrl->z[j])                          /* 329 */
        {
            break;
        }
    }                                                                   /* 332 */

    if (i == 0 || i == w + 1 || j == 0 || j == h + 1)                   /* 334 */
    {
        return -1;                                                      /* 336 */
    }

    return (j - 1) * w + i - 1;                                         /* 339 */
}

/* --------------------------------------------------------------------------
 *  MhIsInTriXZ
 *
 *  Non-zero when `pos` is inside triangle `idx` seen from above.  Triangles
 *  that are not clearly upward-facing are rejected first, which is what keeps
 *  walls and ceilings out of the floor query.
 * ------------------------------------------------------------------------ */
static int MhIsInTriXZ(MhCtrl *mh_ctrl, float *pos, int idx)
{                                                                       /* 347 */
    float y_vec[4] = { 0.0f, 1.0f, 0.0f, 0.0f };                        /* 348 */
    float tri[3][4];
    float outer[4];
    float tmp1[4];
    float tmp2[4];
    int   idx_num[3];
    int   i;

    for (i = 0; i < 3; i++)                                             /* 354 */
    {
        idx_num[i] = mh_ctrl->v_idx[idx].n[i];                          /* 355 */
        sceVu0CopyVector(tri[i], mh_ctrl->V[idx_num[i]].v);             /* 356 */
    }                                                                   /* 357 */

    sceVu0SubVector(tmp1, tri[1], tri[0]);                              /* 360 */
    sceVu0SubVector(tmp2, tri[2], tri[0]);                              /* 361 */
    sceVu0OuterProduct(outer, tmp1, tmp2);                              /* 362 */

    /* Face normal against +Y.  The threshold is a plain double in the ROM
     * (0x3f50624dd2f1a9fb in .rodata), so the compare promotes -- writing
     * 0.001f here would not be the same test. */
    if (sceVu0InnerProduct(outer, y_vec) < 0.001)                       /* 363 */
    {
        return 0;                                                       /* 365 */
    }

    return HcBaseIsInTriXZ(pos, tri[0], tri[1], tri[2]);                /* 369 */
}

/* --------------------------------------------------------------------------
 *  MhIsHeightTri
 *
 *  Y of triangle `idx`'s plane above `pos`.  A degenerate triangle (zero area
 *  in XZ) falls back to the first vertex's height.
 * ------------------------------------------------------------------------ */
static float MhIsHeightTri(MhCtrl *mh_ctrl, float *pos, int idx)
{                                                                       /* 376 */
    fixed_array<float[4], 3> v;
    int                      i;
    int                      v_idx;
    float                    height;
    float                    x1, y1, z1;
    float                    x2, y2, z2;
    float                    x3, y3, z3;

    for (i = 0; i < 3; i++)                                             /* 383 */
    {
        v_idx = mh_ctrl->v_idx[idx].n[i];                               /* 384 */
        sceVu0CopyVector(v[i], mh_ctrl->V[v_idx].v);
    }                                                                   /* 386 */

    x1 = v[0][0];  y1 = v[0][1];  z1 = v[0][2];
    x2 = v[1][0];  y2 = v[1][1];  z2 = v[1][2];
    x3 = v[2][0];  y3 = v[2][1];  z3 = v[2][2];

    if ((z2 - z1) * (x3 - x1) - (z3 - z1) * (x2 - x1) != 0.0f)          /* 393 */
    {
        height = (((y2 - y1) * (z3 - z1) - (y3 - y1) * (z2 - z1)) *
                      (x1 - pos[0]) +
                  ((x2 - x1) * (y3 - y1) - (x3 - x1) * (y2 - y1)) *
                      (z1 - pos[2])) /                                  /* 395 */
                     ((z2 - z1) * (x3 - x1) - (z3 - z1) * (x2 - x1)) +  /* 396 */
                 y1;                                                    /* 397 */
    }
    else
    {
        height = y1;
    }

    return height;                                                      /* 403 */
}

/* --------------------------------------------------------------------------
 *  MhIsHitHeightTri
 *
 *  Non-zero when segment pos1..pos2 pierces triangle `idx`.  Cheap proximity
 *  reject, then the plane crossing, then the XZ containment test on the
 *  crossing point -- which also re-applies the upward-facing filter.
 * ------------------------------------------------------------------------ */
static int MhIsHitHeightTri(MhCtrl *mh_ctrl, float *pos1, float *pos2, int idx)
{                                                                       /* 409 */
    float      v0[4];
    SgmVERTEX *raSgmVERTEX;
    int      (&riv)[4] = mh_ctrl->v_idx[idx].n;                         /* 421 */

    raSgmVERTEX = mh_ctrl->V;                                           /* 420 */

    if (HcBaseIsNearSegTri(pos1, pos2,
                           raSgmVERTEX[riv[0]].v, raSgmVERTEX[riv[1]].v,
                           raSgmVERTEX[riv[2]].v) != 0 &&               /* 425 */
        HcBaseIsLineHitFace(v0, pos1, pos2,
                            raSgmVERTEX[riv[0]].v, raSgmVERTEX[riv[1]].v,
                            raSgmVERTEX[riv[2]].v) != 0 &&              /* 433 */
        MhIsInTriXZ(mh_ctrl, v0, idx) != 0)                             /* 436 */
    {
        return 1;
    }

    return 0;                                                           /* 442 */
}
