/* ==========================================================================
 *  ingame/plyr/ChrSort.c
 *
 *  Character registration in MapPut's depth-sorted draw list.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), functions
 *  0x00100e88..0x001013d4.
 *
 *  The original work records contained 32-bit EE pointers.  They are runtime
 *  state, not serialized data, so the PC port deliberately stores native
 *  pointers here.  SGD coordinate references remain 32-bit file-relative
 *  fields and are resolved by SGDFILE32.
 * ======================================================================== */

#include "ChrSort.h"

#include "plyr_mdl.h"
#include "sis_mdl.h"
#include "../enemy/enemy.h"
#include "../map/MapPut.h"

enum
{
    CHR_SORT_MAX = 32,
    CHR_SORT_SISTER_ID = 2000,
    CHR_SORT_PLAYER_ID = 2001,
    CHR_SORT_ENEMY_ID = 3000,
    CHR_SORT_FLY_ID = 4000,
    CHR_SORT_MATRIX_PTR = 0x10
};

struct CHR_SORT_WORK
{
    int put_id;
    void *hdl;
    ENE_WRK *ene;
};

struct CHR_SORT_FLY_WORK
{
    int put_id;
    void *hdl;
    FLY_WRK *fly;
};

static int ChrSortFlg;
static void *ChrSortHdl[2];
static CHR_SORT_WORK ChrSortEnemList[CHR_SORT_MAX];
static CHR_SORT_FLY_WORK ChrSortFlyList[CHR_SORT_MAX];

void ChrSortSetFlg(int flg)
{
    ChrSortFlg |= flg;
}

void ChrSortDelFlg(int flg)
{
    ChrSortFlg &= ~flg;
}

void ChrSortEnemCallback(void)
{
    int id = MapPutGetNowBuffID() - CHR_SORT_ENEMY_ID;
    CHR_SORT_WORK *ep = &ChrSortEnemList[id];

    if (ep->hdl != nullptr && ep->ene->status == ENE_STATUS_ACT &&
        (ChrSortFlg & 1) == 0)
    {
        EnemyDrawOne(ep->ene);
        MapPutSetMatrix(ep->hdl, *EnemyGetMatrix(ep->ene));
    }
}

void ChrSortFlyCallback(void)
{
    int id = MapPutGetNowBuffID() - CHR_SORT_FLY_ID;
    CHR_SORT_FLY_WORK *fp = &ChrSortFlyList[id];

    if (fp->hdl != nullptr)
    {
        FlyDrawOne(fp->fly);
        MapPutSetMatrix(fp->hdl, *FlyGetMatrix(fp->fly));
    }
}

static CHR_SORT_WORK *ChrSortGetEnemWork(void)
{
    for (int i = 0; i < CHR_SORT_MAX; i++)
    {
        if (ChrSortEnemList[i].hdl == nullptr)
        {
            ChrSortEnemList[i].put_id = i + CHR_SORT_ENEMY_ID;
            return &ChrSortEnemList[i];
        }
    }

    return nullptr;
}

static CHR_SORT_FLY_WORK *ChrSortGetFlyWork(void)
{
    for (int i = 0; i < CHR_SORT_MAX; i++)
    {
        if (ChrSortFlyList[i].hdl == nullptr)
        {
            ChrSortFlyList[i].put_id = i + CHR_SORT_FLY_ID;
            return &ChrSortFlyList[i];
        }
    }

    return nullptr;
}

int ChrSortRegistEnem(ENE_WRK *ene)
{
    CHR_SORT_WORK *ep = ChrSortGetEnemWork();
    int ret = -1;

    if (ep != nullptr)
    {
        ep->hdl = MapPutSetFunc(ep->put_id, (u_int *)ChrSortEnemCallback, 0);
        ep->ene = ene;
        *MapPutGetFlgPtr(ep->hdl) &= ~CHR_SORT_MATRIX_PTR;
        MapPutSetMatrix(ep->hdl, *EnemyGetMatrix(ene));
        ret = ep->put_id;
    }

    return ret;
}

int ChrSortRegistFly(FLY_WRK *fly)
{
    CHR_SORT_FLY_WORK *fp = ChrSortGetFlyWork();
    int ret = -1;

    if (fp != nullptr)
    {
        fp->hdl = MapPutSetFunc(fp->put_id, (u_int *)ChrSortFlyCallback, 0);
        fp->fly = fly;
        *MapPutGetFlgPtr(fp->hdl) &= ~CHR_SORT_MATRIX_PTR;
        MapPutSetMatrix(fp->hdl, *FlyGetMatrix(fly));
        ret = fp->put_id;
    }

    return ret;
}

int ChrSortDeleteEnem(ENE_WRK *ene)
{
    for (int i = 0; i < CHR_SORT_MAX; i++)
    {
        CHR_SORT_WORK *ep = &ChrSortEnemList[i];
        if (ep->hdl != nullptr && ep->ene == ene)
        {
            MpaPutDeleteOneObj(ep->hdl);
            ep->hdl = nullptr;
            return 0;
        }
    }

    return -1;
}

int ChrSortDeleteFly(FLY_WRK *fly)
{
    for (int i = 0; i < CHR_SORT_MAX; i++)
    {
        CHR_SORT_FLY_WORK *fp = &ChrSortFlyList[i];
        if (fp->hdl != nullptr && fp->fly == fly)
        {
            MpaPutDeleteOneObj(fp->hdl);
            fp->hdl = nullptr;
            return 0;
        }
    }

    return -1;
}

CHR_SORT_MATRIX *ChrSortGetSgdMatrix(HeaderSection *hs)
{
    return &hs->coordp->matCoord;
}

CHR_SORT_MATRIX *ChrSortGetPlayrMatrix(void)
{
    ANI_CTRL *ani_ctrl = plyr_mdlGetANI_CTRL();
    return ChrSortGetSgdMatrix(ani_ctrl->base_p);
}

CHR_SORT_MATRIX *ChrSortGetSisMatrix(void)
{
    ANI_CTRL *ani_ctrl = sis_mdlGetANI_CTRL();
    return ChrSortGetSgdMatrix(ani_ctrl->base_p);
}

int ChrSortRegistSis(void)
{
    ChrSortHdl[0] = MapPutSetFunc(CHR_SORT_SISTER_ID, (u_int *)DrawSister, 0);
    MapPutSetMatrixPtr(ChrSortHdl[0], ChrSortGetSisMatrix());
    return 0;
}

int ChrSortRegistPlayr(void)
{
    ChrSortHdl[1] = MapPutSetFunc(CHR_SORT_PLAYER_ID, (u_int *)DrawGirl, 0);
    MapPutSetMatrixPtr(ChrSortHdl[1], ChrSortGetPlayrMatrix());
    return 0;
}

int ChrSortDelete(int id)
{
    int ret = 0;

    if ((unsigned int)id < 2)
    {
        void *hdl = ChrSortHdl[id];
        if (hdl != nullptr)
        {
            MpaPutDeleteOneObj(hdl);
            ChrSortHdl[id] = nullptr;
        }
        ret = hdl != nullptr;
    }

    return ret;
}

void ChrSortInit(void)
{
    int i;

    ChrSortHdl[0] = nullptr;
    ChrSortHdl[1] = nullptr;

    for (i = 0; i < CHR_SORT_MAX; i++)
    {
        ChrSortEnemList[i].hdl = nullptr;
    }

    for (i = 0; i < CHR_SORT_MAX; i++)
    {
        ChrSortFlyList[i].hdl = nullptr;
    }

    ChrSortFlg = 0;
}
