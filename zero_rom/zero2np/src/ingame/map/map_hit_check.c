// FILE: /home/zero_rom/zero2np/src/ingame/map/map_hit_check.c
//
// Wall collision: resolves a moved position out of the map's hit rectangles.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), map_hit_check.o
// 0x001dc548..0x001dd0a7.

#include "map_hit_check.h"

#include "MapHit.h"
#include "hit_check_base.h"
#include "map_rectangle.h"

#include <libvu0.h>
#include <math.h>

#include "graphics/graph3d/g3dxVu0.h"

/* DrawMapHitRect / DrawMapHitRectOne were stubbed here; ZERO2.MAP puts them in
 * map_rectangle.o, so the reconstructions live in map_rectangle.c and reach
 * this file through map_rectangle.h. */

/* Pushes v1 out to `r` from the corner at `a`, along the corner->point
 * direction flattened to the floor plane.  The extra 0.01 stops the next
 * frame starting exactly on the boundary and re-triggering. */
int MapHitCollisionPoint(float *v0, float *v1, float *a, float len, float r)     /* 433 */
{
    float av[4];

    sceVu0SubVector(av, v1, a);
    av[1] = 0.0f;

    float av_len = g3dxVu0Sqrt(sceVu0InnerProduct(av, av));

    sceVu0ScaleVector(av, av, ((r + 0.01f) - len) / av_len);
    sceVu0AddVector(v0, av, v1);

    return 1;
}

/* Pushes v1 out to `r` from the edge a->b, along the edge normal.  The
 * normal is taken in XZ and flipped to whichever side v1 is on, so the push
 * is always outward regardless of the rectangle's winding. */
int MapHitCollisionLine(float *v0, float *v1, float *a, float *b,       /* 398 */
                        float len, float r)
{
    float ab[4];
    float av[4];
    float ver[4];

    sceVu0SubVector(ab, b, a);
    sceVu0SubVector(av, v1, a);

    ver[0] =  ab[2];
    ver[1] =  0.0f;
    ver[2] = -ab[0];
    ver[3] =  0.0f;

    /* Cross product sign in XZ says which side of the edge v1 is on. */
    if ((ab[2] * av[0] - ab[0] * av[2]) < 0.0f)
    {
        sceVu0ScaleVector(ver, ver, -1.0f);
    }

    float ver_len = g3dxVu0Sqrt(sceVu0InnerProduct(ver, ver));

    sceVu0ScaleVector(ver, ver, ((r + 0.01f) - len) / ver_len);
    sceVu0AddVector(v0, ver, v1);

    return 1;
}

/* Resolves `now` out of every wall rectangle it ended up inside.
 *
 * Iterates because pushing out of one wall can push into another (inside
 * corners); after 4 passes it gives up and falls back to `old`, which is the
 * last position known to be clear.  Return: 0 = position unchanged (no hit),
 * 1 = v0 was moved. */
int MapHitCheck(float *v0, float *now, float *old, float r, int kai)     /* 46 */
{
    float tmp[4];
    float a[4];
    float b[4];
    float len;
    int  *num_list;
    int   list_num;
    int   door_rec_num;
    int   hit_num = 0;
    int   ret = 0;
    int   i;
    int   j;

    sceVu0CopyVector(tmp, now);                                         /* 58 */

    MrecSetRegBuffID(kai, tmp, 0);                                      /* 60 */
    MrecSetHitRectInfo(0);                                              /* 61 */

    list_num     = MrecGetHitInfoIdNum();                               /* 63 */
    num_list     = MrecGetHitInfoRecNumList();
    door_rec_num = MrecIsDoorRectangleNum();

    DrawMapHitRect(tmp);                                                /* 67 */

    for (;;)
    {
        int flg = (MapHitCheckCol(tmp, r, 0, kai) > 0);                  /* 71 */

        /* Static map rectangles. */
        for (i = 0; i < list_num; i++)                                  /* 75 */
        {
            int rec_num = num_list[i];

            for (j = 0; j < rec_num; j++)                               /* 79 */
            {
                float (*vec)[4] = MrecGetHitInfoRecVecter(i, j);
                int    k;

                if (vec == 0)
                {
                    continue;
                }

                k = HcBasePointRectangle(&len, tmp, a, b, vec, r);       /* 83 */
                if (k == 0)
                {
                    continue;
                }

                flg = 1;
                if (k == 1)
                {
                    MapHitCollisionPoint(tmp, tmp, a, len, r);          /* 87 */
                }
                else
                {
                    MapHitCollisionLine(tmp, tmp, a, b, len, r);        /* 91 */
                }
                break;          /* one push per rectangle list per pass */
            }
        }

        /* Door rectangles, which come and go with the door state. */
        for (i = 0; i < door_rec_num; i++)                              /* 100 */
        {
            int k = MrecPointDoorRectangle(&len, tmp, a, b, i, r);      /* 102 */

            if (k == 0)
            {
                continue;
            }

            flg = 1;
            if (k == 1)
            {
                MapHitCollisionPoint(tmp, tmp, a, len, r);              /* 106 */
            }
            else
            {
                MapHitCollisionLine(tmp, tmp, a, b, len, r);            /* 110 */
            }
            DrawMapHitRectOne(i, tmp);
            break;
        }

        if (flg == 0)
        {
            break;
        }
        if (++hit_num > 3)                                              /* 120 */
        {
            break;
        }
    }

    if (hit_num == 0)                                                   /* 128 */
    {
        sceVu0CopyVector(v0, now);      /* nothing hit               */
        ret = 0;
    }
    else if (hit_num == 4)
    {
        sceVu0CopyVector(v0, old);      /* wedged -- give up, rewind */
        ret = 1;
    }
    else
    {
        sceVu0CopyVector(v0, tmp);      /* resolved                  */
        ret = 1;
    }

    return ret;                                                         /* 147 */
}

/* --------------------------------------------------------------------------
 *  Line-of-sight (map_hit_check.o 0x001dc808).
 *
 *  Both buffers are consulted: the loop below runs once with the registration
 *  buffer selected to 0 and again with it selected to 1, so a door transition
 *  -- where both rooms are resident -- still occludes correctly.
 *
 *  With r == 0 the bare segment pos1..pos2 is tested.  With r != 0 the segment
 *  is widened into the quad box[0..3] (offset +/- r along the segment's left
 *  normal) and all four of its edges are tested; a rectangle that the quad
 *  fully contains but never crosses is caught by MrecIsInHitRectangle().
 * ------------------------------------------------------------------------ */

/* Runs one buffer's worth of rectangles.  Split out of the ROM's single body,
 * which repeats these two loops verbatim for buff 0 and buff 1. */
static int MapHitLineCheckBuff(float *pos1, float *pos2, float r,
                               float (*box)[4], float *boxmin, float *boxmax)
{
    int id_num = MrecGetHitInfoIdNum();
    int *num_list = MrecGetHitInfoRecNumList();
    int *door_list = MrecGetDoorHitInfoRecNumList();
    int id;

    for (id = 0; id < id_num; id++)
    {
        int rec;

        /* --- walls --- */
        for (rec = 0; rec < num_list[id]; rec++)
        {
            if (MrecIsNearRectangle(boxmin, boxmax, id, rec) == 0)
            {
                continue;
            }

            if (r == 0.0f)
            {
                if (MrecLineCross(pos1, pos2, id, rec) != 0)
                {
                    return 1;
                }
                continue;
            }

            int i;
            for (i = 0; i < 4; i++)
            {
                if (MrecLineCross(box[i], box[(i + 1) % 4], id, rec) != 0)
                {
                    break;
                }
            }
            if (i < 4)
            {
                return 1;               /* an edge crossed it        */
            }
            if (MrecIsInHitRectangle(box[0], box[1], box[2], box[3], id, rec) != 0)
            {
                return 1;               /* rectangle inside the quad */
            }
        }

        /* --- doors --- */
        for (rec = 0; rec < door_list[id]; rec++)
        {
            if (MrecCheckDoorHitSta(id, rec) == 0)
            {
                continue;               /* door is open -- see through */
            }
            if (MrecIsNearDoorRectangle(boxmin, boxmax, id, rec) == 0)
            {
                continue;
            }

            if (r == 0.0f)
            {
                if (MrecDoorLineCross(pos1, pos2, id, rec) != 0)
                {
                    return 1;
                }
                continue;
            }

            int i;
            for (i = 0; i < 4; i++)
            {
                if (MrecDoorLineCross(box[i], box[(i + 1) % 4], id, rec) != 0)
                {
                    break;
                }
            }
            if (i < 4)
            {
                return 1;
            }
        }
    }

    return 0;
}

int MapHitLineCheck(float *pos1, int kai1, float *pos2, int kai2, float r)   /* 158 */
{
    float box[4][4];
    float boxmax[4];
    float boxmin[4];

    MrecSetRegBuffID(kai1, pos1, 0);
    MrecSetRegBuffID(kai2, pos2, 1);

    if (r == 0.0f)
    {
        /* box[] is left uninitialised on this path -- with r == 0 only the
         * bare segment is ever tested.  That is the ROM's behaviour. */
        int i;
        for (i = 0; i < 4; i++)
        {
            boxmax[i] = pos1[i] > pos2[i] ? pos1[i] : pos2[i];
            boxmin[i] = pos1[i] < pos2[i] ? pos1[i] : pos2[i];
        }
    }
    else
    {
        float vec12[4];
        float tmp[4];
        int i;

        sceVu0SubVector(vec12, pos2, pos1);
        sceVu0Normalize(vec12, vec12);

        /* Left normal of the segment in the floor plane. */
        tmp[0] = vec12[2];
        tmp[1] = 0.0f;
        tmp[2] = -vec12[0];
        tmp[3] = 0.0f;
        sceVu0ScaleVector(tmp, tmp, r);

        sceVu0AddVector(box[0], pos1, tmp);
        sceVu0AddVector(box[1], pos2, tmp);
        sceVu0ScaleVector(tmp, tmp, -1.0f);
        sceVu0AddVector(box[2], pos2, tmp);
        sceVu0AddVector(box[3], pos1, tmp);

        sceVu0CopyVector(boxmax, box[0]);
        sceVu0CopyVector(boxmin, box[0]);
        for (i = 1; i < 4; i++)
        {
            int c;
            for (c = 0; c < 4; c++)
            {
                if (box[i][c] > boxmax[c]) boxmax[c] = box[i][c];
                if (box[i][c] < boxmin[c]) boxmin[c] = box[i][c];
            }
        }
    }

    /* Both passes always run, even once something has already blocked the
     * line: the ROM only accumulates a flag here, and callers downstream rely
     * on the Mrec buffer selection being left pointing at buffer 1. */
    int hit = 0;

    MrecSetHitRectInfo(0);
    MrecSetDoorHitRectInfo(0);
    hit |= MapHitLineCheckBuff(pos1, pos2, r, box, boxmin, boxmax);

    MrecSetHitRectInfo(1);
    MrecSetDoorHitRectInfo(1);
    hit |= MapHitLineCheckBuff(pos1, pos2, r, box, boxmin, boxmax);

    return hit;                                                          /* 1039 */
}
