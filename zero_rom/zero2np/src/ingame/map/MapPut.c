/* ==========================================================================
 *  ingame/map/MapPut.c
 *
 *  Placed-map-object pool, draw filtering, depth/first-pass ordering and
 *  per-object light setup.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), functions
 *  0x00110898..0x0011156f.
 *
 *  The original MAPPUT_OBJ used 32-bit EE pointers and occupied 0x90 bytes.
 *  This pool is runtime state rather than serialized map data, so the PC port
 *  deliberately uses native pointers.  The widened host layout must not be
 *  used as an on-disc structure.
 * ======================================================================== */

#include "MapPut.h"
#include "../../common/utility2.h"            /* PRINT_ERROR */

#include "MapDraw.h"
#include "MapLight.h"
#include "../../common/variable.h"
#include "../../graphics/effect/effect_oth.h"
#include "../../graphics/graph3d/gra3d.h"
#include "../../graphics/graph3d/gra3dConst.h"
#include "../../graphics/graph3d/gra3dSGD.h"
#include "../../graphics/obj_draw_ctrl.h"
#include "../../ingame/photo/photo.h"
#include "../../miopan/miopan_profiler.h"

#include <libvu0.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../../graphics/graph3d/g3dxVu0.h" /* g3dxVu0CopyVector */

enum
{
    MAPPUT_MAX = 320,
    MAPPUT_FUNC_OBJECT = 0x0001,
    MAPPUT_NODRAW = 0x0008,
    MAPPUT_MATRIX_PTR = 0x0010,
    MAPPUT_PLAYER_LIGHT_ONLY = 0x0020,
    MAPPUT_HIDE_FINDER = 0x0040,
    MAPPUT_FINDER_ONLY = 0x0080,
    MAPPUT_HIDE_PHOTO = 0x0100,
    MAPPUT_PHOTO_ONLY = 0x0200,
    MAPPUT_FIRST = 0x0400,
    MAPPUT_ROOM_REAL_LIGHT = 0x0800,
    MAPPUT_PRELIGHT = 0x1000,
};

struct alignas(16) MAPPUT_OBJ
{
    short buff_id;
    short first_id;
    int flg;
    float z_num;
    float z_offset;
    u_int *addr;
    MAPPUT_FUNC func;
    /* PORT DEVIATION: `int` in the ROM (0x18).  It is dual-use -- some
     * registrations park a label id in it and some an SGD address -- and a
     * host pointer does not fit in 32 bits, so it is widened here.  The
     * consumers that want the id cast back. */
    intptr_t work;
    alignas(16) MAPPUT_MATRIX mat;
    MAPPUT_MATRIX *mp;
    GRA3DLIGHTDATA *lit;
    alignas(16) float c_pos[4];
    MAPPUT_OBJ *next;
    MAPPUT_OBJ *befo;
    MAPPUT_OBJ *next_draw;
};

static fixed_array<MAPPUT_OBJ, MAPPUT_MAX> MapPutList;
static MAPPUT_OBJ *MapPutSt;
static MAPPUT_OBJ *MapPutNowDraw;

static float MapPutNormalizeAngle(float angle)
{
    const float pi = 3.141592502593994140625f;
    const float two_pi = 6.28318500518798828125f;

    /*
     * This asymmetric outer test is intentional.  It is the exact inlined
     * g3dMath normalisation emitted at 0x00110c28/cc8/d68.
     */
    if (pi < angle)
    {
        angle = fmodf(angle, two_pi);
        if (pi < angle)
        {
            angle -= two_pi;
        }
        else if (angle < -pi)
        {
            angle += two_pi;
        }
    }

    return angle;
}

void MapPutSetFlg(int buff_id, int flg)
{
    MAPPUT_OBJ *wp = MapPutSt;

    while (wp != nullptr)
    {
        if (wp->buff_id == buff_id)
        {
            wp->flg |= flg;
        }
        wp = wp->next;
    }
}

void MapPutDeleteFlg(int buff_id, int flg)
{
    MAPPUT_OBJ *wp = MapPutSt;

    while (wp != nullptr)
    {
        if (wp->buff_id == buff_id)
        {
            wp->flg &= ~flg;
        }
        wp = wp->next;
    }
}

void MapPutSetFirst(void *obj, short offset)
{
    MAPPUT_OBJ *op = (MAPPUT_OBJ *)obj;

    if (op == nullptr)
    {
        return;
    }

    if (offset < 0)
    {
        op->flg &= ~MAPPUT_FIRST;
        return;
    }

    op->first_id = offset;
    op->flg |= MAPPUT_FIRST;
}

void MapPutSetZoffset(void *obj, float offset)
{
    if (obj != nullptr)
    {
        ((MAPPUT_OBJ *)obj)->z_offset = offset;
    }
}

void MapPutSetMatrix(void *obj, MAPPUT_MATRIX mat)
{
    if (obj != nullptr)
    {
        sceVu0CopyMatrix(((MAPPUT_OBJ *)obj)->mat, mat);
    }
}

void MapPutSetMatrixPtr(void *obj, MAPPUT_MATRIX *mp)
{
    if (obj != nullptr)
    {
        ((MAPPUT_OBJ *)obj)->mp = mp;
    }
}

void MapPutSetBuffID(void *obj, int buff_id)
{
    if (obj != nullptr)
    {
        ((MAPPUT_OBJ *)obj)->buff_id = (short)buff_id;
    }
}

void MapPutSetLitPtr(void *obj, GRA3DLIGHTDATA *light_ptr)
{
    if (obj != nullptr)
    {
        ((MAPPUT_OBJ *)obj)->lit = light_ptr;
    }
}

void MapPutSetWork(void *obj, intptr_t work)
{
    if (obj != nullptr)
    {
        ((MAPPUT_OBJ *)obj)->work = work;
    }
}

void MapPutSetModelPtr(void *obj, u_int *addr)
{
    ((MAPPUT_OBJ *)obj)->addr = addr;
}

MAPPUT_MATRIX *MapPutGetMatrixPtr(void *obj)
{
    return &((MAPPUT_OBJ *)obj)->mat;
}

int *MapPutGetFlgPtr(void *obj)
{
    return &((MAPPUT_OBJ *)obj)->flg;
}

GRA3DLIGHTDATA *MapPutGetLitPtr(void *obj)
{
    return ((MAPPUT_OBJ *)obj)->lit;
}

u_int *MapPutGetModelPtr(void *obj)
{
    return ((MAPPUT_OBJ *)obj)->addr;
}

intptr_t MapPutGetWork(void *obj)
{
    return ((MAPPUT_OBJ *)obj)->work;
}

void MapPutSetFuncAddr(void *obj, MAPPUT_FUNC func)
{
    if (obj != nullptr)
    {
        ((MAPPUT_OBJ *)obj)->func = func;
    }
}

static MAPPUT_OBJ *MapPutGetFreeArea(void)
{
    int i;

    for (i = 0; i < MAPPUT_MAX; i++)
    {
        MAPPUT_OBJ *op = &MapPutList[i];
        if (op->buff_id < 0)
        {
            op->addr = nullptr;
            return op;
        }
    }

    PRINT_ERROR("NO_FREE_AREA\n");
    return nullptr;
}

static void MapPutAddList(MAPPUT_OBJ *op)
{
    MAPPUT_OBJ *wp;

    if (MapPutSt == nullptr)
    {
        MapPutSt = op;
        op->befo = nullptr;
    }
    else
    {
        wp = MapPutSt;
        while (wp->next != nullptr)
        {
            wp = wp->next;
        }
        wp->next = op;
        op->befo = wp;
    }

    op->next = nullptr;
}

void *MapPutSetObj(int buff_id, u_int *addr, float *pos, float *rot,
                   float *scale, GRA3DLIGHTDATA *lit, int flg)
{
    const float deg_to_rad = 0.01745329052209854126f;
    MAPPUT_OBJ *op = MapPutGetFreeArea();

    if (op != nullptr)
    {
        op->buff_id = (short)buff_id;
        if ((flg & MAPPUT_FUNC_OBJECT) == 0)
        {
            op->addr = addr;
        }
        else
        {
            op->func = (MAPPUT_FUNC)addr;
        }

        op->lit = lit;
        op->flg = flg;
        op->z_offset = 0.0f;
        op->mp = nullptr;

        if (op->addr != nullptr)
        {
            MapDrawGetCenPos(op->addr, (float (*)[4])op->c_pos);
            op->c_pos[0] += pos[0];
            op->c_pos[1] += pos[1];
            op->c_pos[2] += pos[2];
            op->c_pos[3] = 1.0f;
        }

        if ((flg & MAPPUT_MATRIX_PTR) == 0)
        {
            sceVu0UnitMatrix(op->mat);
            op->mat[0][0] = scale[0] * 25.0f;
            op->mat[1][1] = scale[1] * -25.0f;
            op->mat[2][2] = scale[2] * -25.0f;

            float rx = MapPutNormalizeAngle(-(rot[0] * deg_to_rad));
            sceVu0RotMatrixX(op->mat, op->mat, rx);

            float ry = MapPutNormalizeAngle(-(rot[1] * deg_to_rad));
            sceVu0RotMatrixY(op->mat, op->mat, ry);

            float rz = MapPutNormalizeAngle(rot[2] * deg_to_rad);
            sceVu0RotMatrixZ(op->mat, op->mat, rz);

            g3dxVu0CopyVector(op->mat[3], pos);
            op->mat[3][3] = 1.0f;
        }

        MapPutAddList(op);
    }

    return op;
}

void *MapPutSetFunc(int buff_id, u_int *addr, int flg)
{
    return MapPutSetObj(buff_id, addr, nullptr, nullptr, nullptr, nullptr,
                        flg | MAPPUT_FUNC_OBJECT | MAPPUT_MATRIX_PTR);
}

void MapPutChangeFunc(void *pHdl, MAPPUT_FUNC func)
{
    MAPPUT_OBJ *op = (MAPPUT_OBJ *)pHdl;
    op->func = func;
    op->flg |= MAPPUT_FUNC_OBJECT;
}

void MapPutChangeObj(void *pHdl)
{
    MAPPUT_OBJ *op = (MAPPUT_OBJ *)pHdl;
    op->func = nullptr;
    op->flg &= ~MAPPUT_FUNC_OBJECT;
}

static void MpaPutDeleteOne(MAPPUT_OBJ *op)
{
    MAPPUT_OBJ *np;

    if (op == nullptr)
    {
        return;
    }

    np = op->next;
    if (op->befo != nullptr)
    {
        op->befo->next = np;
    }
    else
    {
        MapPutSt = np;
    }

    if (np != nullptr)
    {
        np->befo = op->befo;
    }

    op->addr = nullptr;
    op->func = nullptr;
    op->buff_id = -1;
}

void MpaPutDeleteOneObj(void *hdl)
{
    MpaPutDeleteOne((MAPPUT_OBJ *)hdl);
}

void MapPutDelete(int buff_id)
{
    int i;

    for (i = 0; i < MAPPUT_MAX; i++)
    {
        MAPPUT_OBJ *op = &MapPutList[i];
        if (op->buff_id == buff_id)
        {
            MpaPutDeleteOne(op);
        }
    }
}

static int MapPutDrawOK(MAPPUT_OBJ *lp)
{
    if ((lp->flg & MAPPUT_NODRAW) != 0)
    {
        return -1;
    }
    if ((lp->flg & MAPPUT_HIDE_FINDER) != 0 && plyr_wrk.cmn_wrk.mode == 6)
    {
        return -2;
    }
    if ((lp->flg & MAPPUT_FINDER_ONLY) != 0 && plyr_wrk.cmn_wrk.mode != 6)
    {
        return -3;
    }
    if ((lp->flg & MAPPUT_HIDE_PHOTO) != 0 && PhotoFlgIsUp() != 0)
    {
        return -4;
    }
    if ((lp->flg & MAPPUT_PHOTO_ONLY) != 0 && PhotoFlgIsUp() == 0)
    {
        return -5;
    }

    return 1;
}

static int MapPutSortBefore(const MAPPUT_OBJ *current, const MAPPUT_OBJ *item)
{
    int mode = ((current->flg & MAPPUT_FIRST) != 0 ? 2 : 0) |
               ((item->flg & MAPPUT_FIRST) != 0 ? 1 : 0);

    switch (mode)
    {
    case 0:
        return current->z_num < item->z_num;
    case 1:
        return 1;
    case 2:
        return 0;
    case 3:
        return current->first_id < item->first_id;
    default:
        return 0;
    }
}

static MAPPUT_OBJ *MapPutSort(void)
{
    MioPanProfileScope profile(MIOPAN_PROFILE_MAPPUT_SORT);
    MAPPUT_OBJ *lp;
    MAPPUT_OBJ *st = nullptr;
    uint64_t candidates = 0;
    uint64_t drawable = 0;
    uint64_t comparisons = 0;

    if (MapPutSt == nullptr)
    {
        return nullptr;
    }

    for (lp = MapPutSt; lp != nullptr; lp = lp->next)
    {
        candidates++;
        if (MapPutDrawOK(lp) >= 0)
        {
            drawable++;
            float mat_world_screen[4][4];
            float o_pos[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            float (*mat)[4];

            if ((lp->flg & MAPPUT_MATRIX_PTR) == 0)
            {
                mat = lp->mat;
            }
            else
            {
                mat = *lp->mp;
            }

            sceVu0MulMatrix(mat_world_screen, gra3dGetCamera()->matWorldScreen, mat);
            sceVu0ApplyMatrix(o_pos, mat_world_screen, o_pos);

            lp->next_draw = nullptr;
            lp->z_num = -(o_pos[2] + lp->z_offset);

            MAPPUT_OBJ *wp = st;
            MAPPUT_OBJ *bp = nullptr;
            while (wp != nullptr)
            {
                comparisons++;
                if (MapPutSortBefore(wp, lp) != 0)
                {
                    break;
                }
                bp = wp;
                wp = wp->next_draw;
            }

            if (bp == nullptr)
            {
                lp->next_draw = st;
                st = lp;
            }
            else
            {
                lp->next_draw = wp;
                bp->next_draw = lp;
            }
        }
    }

    MioPan_ProfilerAddCounter(MIOPAN_PROFILER_COUNTER_MAPPUT_CANDIDATES,
                              candidates);
    MioPan_ProfilerAddCounter(MIOPAN_PROFILER_COUNTER_MAPPUT_DRAWABLE,
                              drawable);
    MioPan_ProfilerAddCounter(MIOPAN_PROFILER_COUNTER_MAPPUT_COMPARISONS,
                              comparisons);
    return st;
}

void *MapPutGetNowHdl(void)
{
    return MapPutNowDraw;
}

int MapPutGetNowBuffID(void)
{
    return MapPutNowDraw->buff_id;
}

void MapPutDraw(void)
{
    MAPPUT_OBJ *lp = MapPutSort();
    GRA3DLIGHTDATA mld;
    GRA3DLIGHTDATA object_light;
    uint64_t callbacks = 0;
    uint64_t object_draws = 0;

    MapLightSetPlayerOnly();

    while (lp != nullptr)
    {
        int l_flg = 0;
        MapPutNowDraw = lp;

        if ((lp->flg & MAPPUT_PLAYER_LIGHT_ONLY) == 0)
        {
            if ((lp->flg & MAPPUT_PRELIGHT) != 0)
            {
                gra3dSetLightData(lp->lit, nullptr);
                gra3dExecPrelight((SGDFILEHEADER *)lp->addr, lp->mat);
                MapLightSetPlayerOnly();
            }
            else if ((lp->flg & MAPPUT_ROOM_REAL_LIGHT) != 0)
            {
                if (MapDrawIsEnableFlashlightOnly() != 0)
                {
                    l_flg = 1;
                    gra3dLightEnablePush();
                    MapLightMakeRoomReal(&mld, lp->lit);
                    gra3dEmulateLightDataObj(&object_light, &mld, lp->c_pos, 1.0f);
                    gra3dSetLightData(&object_light, nullptr);
                }
            }
            else if (lp->lit != nullptr && MapDrawIsEnableFlashlightOnly() != 0)
            {
                l_flg = 1;
                gra3dLightEnablePush();
                gra3dSetLightData(lp->lit, nullptr);
                MapLightSetPlayerReal();
                gra3dApplyLight();
            }
        }
        else
        {
            MapLightSetPlayerOnly();
        }

        EffectThunderLightSetRoomLight();

        if ((lp->flg & MAPPUT_FUNC_OBJECT) == 0)
        {
            if (GetObjDrawFLG() != 0)
            {
                MapDrawObj(lp->addr, lp->mat);
                object_draws++;
            }
        }
        else if (lp->func != nullptr)
        {
            {
                MioPanProfileScope profile(MIOPAN_PROFILE_MAPPUT_CALLBACK);
                lp->func();
            }
            callbacks++;
        }

        if (l_flg != 0)
        {
            gra3dLightEnablePop();
            gra3dApplyLight();
        }

        lp = lp->next_draw;
    }

    MioPan_ProfilerAddCounter(MIOPAN_PROFILER_COUNTER_MAPPUT_CALLBACKS,
                              callbacks);
    MioPan_ProfilerAddCounter(MIOPAN_PROFILER_COUNTER_MAPPUT_OBJECT_DRAWS,
                              object_draws);
}

void MapPutResetAll(void)
{
    gra3dLightEnableAll(0);
    memset(MapPutList.data(), 0, sizeof(MAPPUT_OBJ) * MAPPUT_MAX);

    for (int i = 0; i < MAPPUT_MAX; i++)
    {
        MapPutList[i].buff_id = -1;
        MapPutList[i].addr = nullptr;
        MapPutList[i].func = nullptr;
    }

    MapPutSt = nullptr;
    MapPutNowDraw = nullptr;
}
