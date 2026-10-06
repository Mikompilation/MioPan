/* ==========================================================================
 *  ingame/map/MapLoad.c
 *
 *  Room streaming.  Two room buffers (MapLoadBuff) split the map heap between
 *  them: the player stands in one while the neighbouring room loads into the
 *  other, so walking through a door is a buffer swap and a draw-flag change
 *  rather than a stall.  A room is addressed by its "label", a file id derived
 *  straight from the room number (MapLoadGetLabel), and each room contributes
 *  up to four registered sections (MLOAD_HEAD::reg_id).
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "MapLoad.h"
#include "FurnCtl.h"
#include "FurnLoad.h"
#include "MapAnim.h"
#include "MapDoor.h"
#include "MapDraw.h"
#include "MapHit.h"
#include "MapObj.h"
#include "MapObjReg.h"
#include "MapPut.h"
#include "MapSky.h"
#include "MapSp.h"
#include "map_height.h"
#include "RegDat.h"
#include "foot_se.h"
#include "../../graphics/graph3d/gra3d.h"           /* gra3dSetLightData / gra3dIsMonotoneDrawEnable */
#include "../../graphics/graph3d/ctl/fixed_array.h" /* per-TU _fixed_array_* helpers */
#include "../../common/utility2.h"                  /* PRINT_ASSERT */
#include "../../sdk/libvu0.h"                       /* sceVu0CopyVector */
#include "../../miopan/miopan_memory.h"             /* MioPan_GetHostPointer */
#include "../../miopan/miopan_profiler.h"
#include "../../system/os/system.h"                /* EE memory map (ROOM1_DATA_ADDR ...) */
#include "../../system/eeiop/cddat.h"              /* RY00_PK2 (room label base) */
#include "../../system/os/eecdvd.h"                 /* LoadReqGetAddr / IsLoadEnd / IsLoadEndAll */
#include "../../system/eeiop/fileload.h"            /* FileLoadCancel */

#include <stddef.h>                                  /* ptrdiff_t */
#include <stdint.h>                                  /* uintptr_t */
#include <stdio.h>
#include <string.h>
#include "../../graphics/graph3d/g3dxVu0.h" /* g3dxVu0CopyVector */

/* --------------------------------------------------------------------------
 *  File-scope state
 * ------------------------------------------------------------------------ */

static int    MapLoadDrawID;                    /* sdata 0x3eeeb0 */
static int    MapLoadMyPosLabel;                /* sdata 0x3eeeb4 */
static int    MapLoadStat;                      /* sdata 0x3eeeb8 */
static void  *MapLoadDoorBuff;                  /* sdata 0x3eeebc */
static char  *MapLoadFreeArea[2];               /* sdata 0x3eeec0 */
static fixed_array<MLOAD_HEAD, 2> MapLoadBuff;  /* bss   0x407ab0 */
static int    MapLoadFileIDList[5];             /* bss   0x40a290 */

static int    MapLoadCheckFreeAreaLabelID(int label);
static int    MapLoadCheckSwitch(int buff_id, int sw);
static u_int *MapLoadRegistReq(int label1, char *addr);
static int    MapLoadCallFurn(int buff_id);

/* --------------------------------------------------------------------------
 *  MapLoadGetLabel
 *
 *  Room number -> file label.  Five files per room, the first room's block
 *  starting at 0x22c; the inverse is spelled out in MapLoadGetRoomNo4BuffID.
 * ------------------------------------------------------------------------ */
int MapLoadGetLabel(int _room_no)
{
    return _room_no * 5 + RY00_PK2;
}

/* --------------------------------------------------------------------------
 *  MapLoadCheckDrawFlg
 *
 *  Non-zero while the buffer is drawing, i.e. bit 0 of stat is clear.
 * ------------------------------------------------------------------------ */
int MapLoadCheckDrawFlg(int buff_id)
{
    if ((u_int)buff_id < 2)
    {
        return (MapLoadBuff[buff_id].stat ^ 1) & 1;
    }

    return 0;
}

/* --------------------------------------------------------------------------
 *  MapLoadCheckLoadNow
 * ------------------------------------------------------------------------ */
int MapLoadCheckLoadNow(void)
{
    return MapLoadStat & 1;
}

/* --------------------------------------------------------------------------
 *  MapLoadInitFreeMem
 * ------------------------------------------------------------------------ */
char *MapLoadInitFreeMem(int buff_id)
{
    MLOAD_HEAD *hp;

    hp = MapLoadGetHeadPtr(buff_id);
    MapLoadFreeArea[buff_id] = (char *)hp->free_mem_top;

    return MapLoadFreeArea[buff_id];
}

/* --------------------------------------------------------------------------
 *  MapLoadGetFreeMemAddr
 * ------------------------------------------------------------------------ */
char *MapLoadGetFreeMemAddr(int buff_id)
{
    if ((u_int)buff_id < 2)
    {
        return MapLoadFreeArea[buff_id];
    }

    return nullptr;
}

/* --------------------------------------------------------------------------
 *  MapLoadSetFreeMemAddr
 *
 *  The two room buffers sit back to back, so the gap between their base
 *  addresses is exactly one buffer's worth of heap.
 * ------------------------------------------------------------------------ */
void MapLoadSetFreeMemAddr(int buff_id, char *addr)
{
    if ((u_int)buff_id < 2)
    {
        MapLoadFreeArea[buff_id] = addr;

        ptrdiff_t buff_size = (ptrdiff_t) MapLoadBuff[1].addr - (ptrdiff_t) MapLoadBuff[0].addr;
        if (buff_size <= (ptrdiff_t)addr - (ptrdiff_t)MapLoadBuff[buff_id].addr)
        {
            //PRINT_ASSERT("ERR ROOM HEAP MAX OVER\n");
        }
    }
}

/* --------------------------------------------------------------------------
 *  MapLoadGetDoorBuffPtr
 * ------------------------------------------------------------------------ */
void *MapLoadGetDoorBuffPtr(void)
{
    return MapLoadDoorBuff;
}

/* --------------------------------------------------------------------------
 *  MapLoadGetFileID
 * ------------------------------------------------------------------------ */
int MapLoadGetFileID(int stat)
{
    if ((u_int)stat < 5)
    {
        return MapLoadFileIDList[stat];
    }

    return -1;
}

/* --------------------------------------------------------------------------
 *  MapLoadGetHeadPtr
 * ------------------------------------------------------------------------ */
MLOAD_HEAD *MapLoadGetHeadPtr(int id)
{
    if ((u_int)id < 2)
    {
        return &MapLoadBuff[id];
    }

    return nullptr;
}

/* --------------------------------------------------------------------------
 *  MapLoadCheckFreeAreaLabelID
 *
 *  Buffer currently holding `label`, or -1.
 * ------------------------------------------------------------------------ */
static int MapLoadCheckFreeAreaLabelID(int label)
{
    for (int i = 0; i < 2; i++)
    {
        if (MapLoadBuff[i].labelID == label)
        {
            return i;
        }
    }

    return -1;
}

/* --------------------------------------------------------------------------
 *  MapLoadGetBuffID4Label
 *
 *  Buffer owning a registered section that carries `label`.
 * ------------------------------------------------------------------------ */
int MapLoadGetBuffID4Label(int label)
{
    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            if (MapLoadBuff[i].reg_id[j] != -1 && RegDatGetStPtr4Label((int)MapLoadBuff[i].reg_id[j], label) != nullptr)
            {
                return i;
            }
        }
    }

    return -1;
}

/* --------------------------------------------------------------------------
 *  MapLoadSetNoRegList
 *
 *  Rebuild RegDat's "do not register" list from every buffer that is not
 *  currently drawing.
 * ------------------------------------------------------------------------ */
void MapLoadSetNoRegList(void)
{
    RegDatResetNoRegistList();

    for (int i = 0; i < 2; i++)
    {
        if (MapLoadCheckDrawFlg(i) == 0)
        {
            MLOAD_HEAD *hp = MapLoadGetHeadPtr(i);
            for (int j = 0; j < 4; j++)
            {
                if (hp->reg_id[j] != -1)
                {
                    RegDatAddNoRegistList((int)hp->reg_id[j]);
                }
            }
        }
    }
}

/* --------------------------------------------------------------------------
 *  MapLoadGetHeader
 * ------------------------------------------------------------------------ */
MLOAD_HEAD *MapLoadGetHeader(void)
{
    int b_id = MapLoadCheckFreeAreaLabelID(MapLoadMyPosLabel);
    if ((u_int)b_id > 1)
    {
        PRINT_ERROR(" NOT_B_ID b_id[%d]\n", b_id);
        return nullptr;
    }

    return MapLoadGetHeadPtr(b_id);
}

/* --------------------------------------------------------------------------
 *  MapLoadGetRegBuffID
 *
 *  Registered-section id for `room_no` on floor `kai`.  -1 when the room is
 *  not resident, -2 when it is but no section sits on that floor.
 * ------------------------------------------------------------------------ */
int MapLoadGetRegBuffID(int room_no, int kai)
{
    MB_OUT_HEAD *mp;
    int          b_id;
    int          i;

    b_id = MapLoadCheckFreeAreaLabelID(MapLoadGetLabel(room_no));
    if (b_id < 0)
    {
        PRINT_ERROR(" NO_MODEL_AREA :label[%d] room_no[%d] floor[%d]\n",
                    MapLoadGetLabel(room_no), room_no, kai);
        return -1;
    }

    if (kai < 0)
    {
        kai = 0xb;
    }

    for (i = 0; i < 4; i++)
    {
        if (MapLoadBuff[b_id].reg_id[i] != -1)
        {
            mp = RegDatGetHead((int)MapLoadBuff[b_id].reg_id[i]);
            if (mp != nullptr && mp->kai == kai)
            {
                return (int)MapLoadBuff[b_id].reg_id[i];
            }
        }
    }

    return -2;
}

/* --------------------------------------------------------------------------
 *  MapLoadSetNowRoom
 * ------------------------------------------------------------------------ */
void MapLoadSetNowRoom(int room_no)
{
    MapLoadMyPosLabel = MapLoadGetLabel(room_no);
}

/* --------------------------------------------------------------------------
 *  MapLoadSetDrawFlgSub
 *
 *  sw == 1 turns the buffer on (clear bit 0, build its furniture); any other
 *  value turns it off and drops its lights and put-objects.
 * ------------------------------------------------------------------------ */
void MapLoadSetDrawFlgSub(int buff_id, int sw)
{
    MLOAD_HEAD *hp = MapLoadGetHeadPtr(buff_id);

    if (sw == 1)
    {
        if (hp->labelID < 0)
        {
            PRINT_ERROR("DONOT_MODEL\n");
        }
        else
        {
            MapLoadBuff[buff_id].stat &= ~1;
            MapDrawInitFurn(hp);
        }
    }
    else
    {
        MapLoadBuff[buff_id].stat |= 1;
        gra3dSetLightData(&hp->lit_dat, nullptr);
        MapDrawDeleteNoDraw(buff_id);
        MapPutSetFlg(buff_id, 8);
    }

    MapLoadSetNoRegList();
}

/* --------------------------------------------------------------------------
 *  MapLoadCheckSwitch
 *
 *  Returns 0 when the requested state differs from the current one, i.e. when
 *  the caller still has work to do.
 * ------------------------------------------------------------------------ */
static int MapLoadCheckSwitch(int buff_id, int sw)
{
    if (MapLoadBuff[buff_id].labelID < 0 && sw == 1)
    {
        return 1;
    }

    int work = (MapLoadBuff[buff_id].stat ^ (u_int) (sw == 2) ^ 1) & 1;
    if (work == 0)
    {
        printf("SAISETTEI HITUYOU[%d]:%s/////\n", buff_id, (sw == 1) ? "ON" : "OFF");
        return 0;
    }

    return 1;
}

/* --------------------------------------------------------------------------
 *  MapLoadSetDrawFlg
 *
 *  flg bit 0 acts on the buffer the player is in, bit 1 on the other one.
 * ------------------------------------------------------------------------ */
void MapLoadSetDrawFlg(int flg, int sw)
{
    int buff_id = MapLoadCheckFreeAreaLabelID(MapLoadMyPosLabel);

    if ((u_int)buff_id > 1)
    {
        PRINT_ERROR(" NO_BUFF_ID[%d]\n", buff_id);
        return;
    }

    if ((flg & 2) != 0 && MapLoadCheckSwitch(buff_id ^ 1, sw) == 0)
    {
        MapLoadSetDrawFlgSub(buff_id ^ 1, sw);
    }

    if ((flg & 1) != 0 && MapLoadCheckSwitch(buff_id, sw) == 0)
    {
        MapLoadSetDrawFlgSub(buff_id, sw);
    }
}

/* --------------------------------------------------------------------------
 *  MapLoadSetDrawFlg2
 * ------------------------------------------------------------------------ */
void MapLoadSetDrawFlg2(int buff_id, int sw)
{
    if (MapLoadCheckSwitch(buff_id, sw) == 0)
    {
        MapLoadSetDrawFlgSub(buff_id, sw);
    }
}

/* --------------------------------------------------------------------------
 *  MapLoadSetDrawFlg3
 * ------------------------------------------------------------------------ */
void MapLoadSetDrawFlg3(int room_no, int sw)
{
    MapLoadSetDrawFlg2(MapLoadCheckFreeAreaLabelID(MapLoadGetLabel(room_no)), sw);
}

/* --------------------------------------------------------------------------
 *  MapLoadSetDrawOnly
 *
 *  Draw only `room_no`; when it is not resident, turn both buffers off.
 * ------------------------------------------------------------------------ */
void MapLoadSetDrawOnly(int room_no)
{
    int buff_id = MapLoadCheckFreeAreaLabelID(MapLoadGetLabel(room_no));
    if (buff_id == -1)
    {
        MapLoadSetDrawFlg2(0, 2);
        MapLoadSetDrawFlg2(1, 2);
        return;
    }

    MapLoadSetDrawFlg2(buff_id, 1);
    MapLoadSetDrawFlg2(buff_id ^ 1, 2);
}

/* --------------------------------------------------------------------------
 *  MapLoadGetFreeArea
 *
 *  Release the buffer the player is NOT standing in and hand back its base.
 * ------------------------------------------------------------------------ */
int *MapLoadGetFreeArea(void)
{
    if ((MapLoadStat & 1) != 0)
    {
        PRINT_ERROR("NO_FREE_SPACE_LOADING_NOW\n");
        return nullptr;
    }

    int b_id = MapLoadCheckFreeAreaLabelID(MapLoadMyPosLabel);
    if (b_id < 0)
    {
        PRINT_ERROR("CANNOT_GET_NOW_AREA\n");
        return nullptr;
    }

    b_id ^= 1;

    if (MapLoadCheckDrawFlg(b_id) != 0)
    {
        PRINT_ERROR("ERR? NOW_DORWING_NEXT_ROOM\n");
        MapLoadSetDrawFlg(2, 2);
    }

    MapDrawDeleteRoom(&MapLoadBuff[b_id]);
    MapLoadBuff[b_id].labelID = -1;

    return (int *)MapLoadBuff[b_id].addr;
}

/* --------------------------------------------------------------------------
 *  MapLoadSetOffSet
 *
 *  Move a resident room.  NOTE: the delta handed to MhSetOffset2 subtracts
 *  every axis from x -- three `sub.S` against f20 in the original, not a
 *  transcription slip.  y and z are only right when x == y == z.
 * ------------------------------------------------------------------------ */
void MapLoadSetOffSet(int room_no, float x, float y, float z)
{
    float       work[4];

    int buff_id = MapLoadGetBuffID(room_no);
    if (buff_id < 0)
    {
        return;
    }

    MLOAD_HEAD *hp = MapLoadGetHeadPtr(buff_id);
    if (hp == nullptr)
    {
        return;
    }

    for (int j = 0; j < 4; j++)
    {
        if (hp->reg_id[j] != -1)
        {
            RegDatSetOffset((int)hp->reg_id[j], x, y, z);
        }
    }

    work[0] = x - hp->pos[0];
    work[1] = x - hp->pos[1];
    work[2] = x - hp->pos[2];
    MhSetOffset2(buff_id, work);

    hp->pos[0] = x;
    hp->pos[1] = y;
    hp->pos[2] = z;
    MapDrawSetUpRoomCoordinate(hp);
}

/* --------------------------------------------------------------------------
 *  MapLoadGetOffset
 *
 *  The room's world offset lives in its first registered section's header.
 * ------------------------------------------------------------------------ */
float *MapLoadGetOffset(int room_no)
{
    int b_id = MapLoadCheckFreeAreaLabelID(MapLoadGetLabel(room_no));
    if (b_id < 0)
    {
        return nullptr;
    }

    MB_OUT_HEAD *mp = RegDatGetHead((int) MapLoadBuff[b_id].reg_id[0]);
    if (mp == nullptr)
    {
        PRINT_ERROR("CAN_NOT_READ_OFFSET\n");
        return nullptr;
    }

    return mp->Pos;
}

/* --------------------------------------------------------------------------
 *  MapLoadGetOffsetVector
 * ------------------------------------------------------------------------ */
void MapLoadGetOffsetVector(float *Center, int RoomNo)
{
    float tmp_vec[4] = {};

    float *pFloat = nullptr;
    if (RoomNo >= 0)
    {
        pFloat = MapLoadGetOffset(RoomNo);
    }

    if (pFloat != nullptr)
    {
        tmp_vec[0] = pFloat[0];
        tmp_vec[1] = pFloat[1];
        tmp_vec[2] = pFloat[2];
        tmp_vec[3] = pFloat[3];
    }

    g3dxVu0CopyVector(Center, tmp_vec);
}

/* --------------------------------------------------------------------------
 *  MapLoadGetBuffID4Pos
 *
 *  Buffer whose registered sections contain vPos.  -1 when RegDat matches
 *  nothing, -2 when both buffers claim the point, -3 when neither does.
 * ------------------------------------------------------------------------ */
int MapLoadGetBuffID4Pos(int kai, float *vPos)
{
    int ret_id = -3;

    if (RegDatGetBuffIDG(kai, vPos) == -1)
    {
        return -1;
    }

    int *rec_list = RegDatGetHitList();

    for (int i = 0; i < 2; i++)
    {
        if (MapLoadCheckDrawFlg(i) == 0)
        {
            continue;
        }

        MLOAD_HEAD *hp = MapLoadGetHeadPtr(i);

        for (int j = 0; j < 4; j++)
        {
            for (int l = 0; l < RegDatGetHitNum(); l++)
            {
                if (hp->reg_id[j] == (char)rec_list[l])
                {
                    if (ret_id >= 0)
                    {
                        return -2;
                    }
                    ret_id = i;
                }
            }
        }
    }

    return ret_id;
}

/* --------------------------------------------------------------------------
 *  MapLoadGetBuffID
 * ------------------------------------------------------------------------ */
int MapLoadGetBuffID(int room_no)
{
    int label = MapLoadGetLabel(room_no);
    if (label == -1)
    {
        return -1;
    }

    return MapLoadCheckFreeAreaLabelID(label);
}

/* --------------------------------------------------------------------------
 *  MapLoadGetRoomNo4BuffID
 * ------------------------------------------------------------------------ */
int MapLoadGetRoomNo4BuffID(int buff_id)
{
    if ((u_int)buff_id < 2)
    {
        return (MapLoadGetHeadPtr(buff_id)->labelID - RY00_PK2) / 5;
    }

    return -1;
}

/* --------------------------------------------------------------------------
 *  MapLoadGetRoomNoNow
 * ------------------------------------------------------------------------ */
int MapLoadGetRoomNoNow(void)
{
    return MapLoadGetRoomNo4BuffID(MapLoadCheckFreeAreaLabelID(MapLoadMyPosLabel));
}

/* --------------------------------------------------------------------------
 *  MapLoadSwitch
 *
 *  Finish a pending room change once its load has completed.
 * ------------------------------------------------------------------------ */
int MapLoadSwitch(void)
{
    MLOAD_HEAD *hp = MapLoadGetHeadPtr(MapLoadDrawID);

    if (MapLoadCheckLoadNow() != 0)
    {
        return 1;
    }

    if ((hp->stat & 2) == 0)
    {
        int buff_id = MapLoadCheckFreeAreaLabelID(MapLoadMyPosLabel);
        MapLoadDrawID = buff_id ^ 1;
        MapLoadSetDrawFlg2(buff_id, 2);
        MapLoadSetDrawFlg2(MapLoadDrawID, 1);
    }
    else
    {
        MapLoadSetDrawFlg2(MapLoadDrawID ^ 1, 2);
        MapLoadInitRoom();
    }

    hp = MapLoadGetHeadPtr(MapLoadDrawID);
    MapLoadSetNowRoom((hp->labelID - RY00_PK2) / 5);

    return 0;
}

/* --------------------------------------------------------------------------
 *  MapLoadBg
 *
 *  Background load of a room that is not resident: drop any in-flight
 *  requests first, then queue the move.
 * ------------------------------------------------------------------------ */
void MapLoadBg(int room_no)
{
    if (MapLoadCheckFreeAreaLabelID(MapLoadGetLabel(room_no)) >= 0)
    {
        return;
    }

    if (MapLoadCheckLoadNow() != 0) {
        int i;
        for (i = 0; i < 5; i++)
        {
            if (MapLoadFileIDList[i] != -1)
            {
                FileLoadCancel(MapLoadFileIDList[i], nullptr, nullptr);
                MapLoadFileIDList[i] = -1;
            }
        }
    }

    MapLoadMoveRoom(room_no);
    MapLoadStat |= 4;
}

/* --------------------------------------------------------------------------
 *  MapLoadDrawRoomOne
 * ------------------------------------------------------------------------ */
int MapLoadDrawRoomOne(int room_no)
{
    int buff_id = MapLoadGetBuffID(room_no);
    if (buff_id < 0)
    {
        return -1;
    }

    MLOAD_HEAD *hp = MapLoadGetHeadPtr(buff_id);
    if (hp == nullptr)
    {
        return -2;
    }

    MapDrawRoomOne(hp, nullptr);

    return 0;
}

/* --------------------------------------------------------------------------
 *  MapLoadInitRoom
 * ------------------------------------------------------------------------ */
int MapLoadInitRoom(void)
{
    MLOAD_HEAD *hp;

    hp = MapLoadGetHeadPtr(MapLoadDrawID);
    MapDrawInitRoom(hp);
    hp->stat &= ~2;
    MapLoadStat &= ~1;
    MapLoadSetDrawFlg2(MapLoadDrawID, 1);

    return MapLoadDrawID;
}

/* --------------------------------------------------------------------------
 *  MapLoadDeleteRoom
 * ------------------------------------------------------------------------ */
void MapLoadDeleteRoom(int b_id)
{
    MapDrawDeleteRoom(&MapLoadBuff[b_id]);
    MapLoadBuff[b_id].labelID = -1;
}

/* --------------------------------------------------------------------------
 *  MapLoadDeleteRoomAll
 * ------------------------------------------------------------------------ */
void MapLoadDeleteRoomAll(void)
{
    MapLoadDeleteRoom(0);
    MapLoadDeleteRoom(1);
}

/* --------------------------------------------------------------------------
 *  MapLoadRegistReq
 *
 *  Queue a room's files into `addr`, packed end to end: model pack, lighting
 *  (the mono variant when monotone draw is on), high model, shadow.  Returns
 *  the first free byte past the room.
 * ------------------------------------------------------------------------ */
static u_int *MapLoadRegistReq(int label1, char *addr)
{
    MLOAD_HEAD *hp = MapLoadGetHeadPtr(MapLoadDrawID);
    MapLoadDrawID &= 1;

    foot_seSetRoom((MapLoadGetHeadPtr(MapLoadDrawID ^ 1)->labelID - RY00_PK2) / 5, (label1 - RY00_PK2) / 5);

    MapDrawDeleteRoom(hp);

    MapLoadStat |= 1;
    hp->stat = 3;
    hp->reg_dat_top = (uintptr_t)hp->addr;

    hp->model_pak_addr = LoadReqGetAddr(label1 + 2, (uintptr_t)hp->addr, &MapLoadFileIDList[2]);

    uintptr_t   w_addr;

    if (gra3dIsMonotoneDrawEnable() == 0)
    {
        w_addr = LoadReqGetAddr(label1, hp->model_pak_addr, &MapLoadFileIDList[0]);
    }
    else
    {
        w_addr = LoadReqGetAddr(label1 + 4, hp->model_pak_addr, &MapLoadFileIDList[0]);
    }

    hp->lit_addr = w_addr;

    hp->high_addr = LoadReqGetAddr(label1 + 1, w_addr, &MapLoadFileIDList[1]);

    w_addr = LoadReqGetAddr(label1 + 3, hp->high_addr, &MapLoadFileIDList[3]);
    if (hp->high_addr == w_addr)
    {
        hp->high_addr = 0;
    }

    for (int i = 0; i < 5; i++)
    {
        printf("IsLoadEnd[%d] id = %x : flg = %d\n", i, MapLoadFileIDList[i], IsLoadEnd(MapLoadFileIDList[i]));
    }

    hp->free_mem_top = w_addr;

    return (u_int *)w_addr;
}

/* --------------------------------------------------------------------------
 *  MapLoadMoveRoom
 *
 *  Make `room_no` current.  Already resident -> flip the draw flags; else
 *  stream it into the other buffer.
 * ------------------------------------------------------------------------ */
int MapLoadMoveRoom(int room_no)
{
    int label1 = MapLoadGetLabel(room_no);
    printf("room_no[%d]label1 = %d : label2 = %d\n", room_no, label1, MapLoadMyPosLabel);

    if (label1 == MapLoadMyPosLabel)
    {
        return MapLoadDrawID;
    }

    int id = MapLoadCheckFreeAreaLabelID(label1);
    if (id < 0)
    {
        id = MapLoadCheckFreeAreaLabelID(MapLoadMyPosLabel);

        if (id < 0)
        {
            id = 0;
        }

        MapLoadDrawID = id ^ 1;

        MLOAD_HEAD *hp = MapLoadGetHeadPtr(MapLoadDrawID);
        hp->labelID = label1;
        MapLoadRegistReq(label1, hp->addr);
    }
    else
    {
        MapLoadSetDrawFlg(2, 1);
        MapLoadDrawID = id;
    }

    return MapLoadDrawID;
}

/* --------------------------------------------------------------------------
 *  MapLoadReload
 * ------------------------------------------------------------------------ */
int MapLoadReload(int room_no)
{
    int label = MapLoadGetLabel(room_no);
    int id = MapLoadCheckFreeAreaLabelID(label);
    if (id < 0)
    {
        printf("ERR NO_LABEL_ID[%d] label[%d]\n", id, label);
        return -1;
    }

    MapLoadDrawID = id;
    MLOAD_HEAD *hp = MapLoadGetHeadPtr(id);
    MapDrawDeleteRoom(hp);
    MapLoadMyPosLabel = label;
    MapLoadRegistReq(label, hp->addr);

    return 0;
}

/* --------------------------------------------------------------------------
 *  MapLoadUpdatRoomDat
 *
 *  Step through a door: pick whichever of its two rooms is not the current
 *  one and move there.  Returns the new room's label.
 * ------------------------------------------------------------------------ */
int MapLoadUpdatRoomDat(int door_id)
{
    int             id;

    MLOAD_DOOR_DAT *dp = MapDoorGetDatListPtr(door_id);
    if (dp == nullptr)
    {
        PRINT_ERROR("NO_ROOM_DAT\n");
        return -1;
    }

    if (MapLoadGetLabel(dp->room_no[0]) == MapLoadMyPosLabel)
    {
        id = 1;
    }
    else if (MapLoadGetLabel(dp->room_no[1]) == MapLoadMyPosLabel)
    {
        id = 0;
    }
    else
    {
        PRINT_ERROR("NO_NOW_ROOM_ID\n");
        return -2;
    }

    int buff_id = MapLoadMoveRoom(dp->room_no[id]);

    return MapLoadBuff[buff_id].labelID;
}

/* --------------------------------------------------------------------------
 *  MapLoadCallFurn
 *
 *  Once the model pack has landed, hand each registered section to the
 *  furniture loader and advance the room's free-memory watermark.
 * ------------------------------------------------------------------------ */
static int MapLoadCallFurn(int buff_id)
{
    MLOAD_HEAD *hp = MapLoadGetHeadPtr(buff_id);

    if ((hp->stat & 8) != 0)
    {
        return 0;
    }

    if (IsLoadEnd(MapLoadFileIDList[2]) == 0)
    {
        return -1;
    }

    MapDrawInitRegDat(hp);
    MapLoadSetNoRegList();

    for (int i = 0; i < 4; i++)
    {
        if (hp->reg_id[i] >= 0)
        {
            hp->free_mem_top = (uintptr_t)FurnLoadRegID(buff_id, (int)hp->reg_id[i], (char *)hp->free_mem_top);

            if ((char *)hp->free_mem_top > hp->addr + ((uintptr_t)MapLoadBuff[1].addr - (uintptr_t)MapLoadBuff[0].addr))
            {
                PRINT_ASSERT("ERR ROOM MEM SPACE OVER\n");
            }
        }
    }

    MapLoadFreeArea[MapLoadDrawID] = (char *)hp->free_mem_top;
    CurnCtlSetTopWorkAddr(buff_id, MapLoadFreeArea[buff_id]);
    hp->stat |= 8;

    return 1;
}

/* --------------------------------------------------------------------------
 *  MapLoadMain
 *
 *  Per-frame loader pump; idle unless a load is in flight.
 * ------------------------------------------------------------------------ */
int MapLoadMain(void)
{
    int i;

    if ((MapLoadStat & 1) == 0)
    {
        return 0;
    }

    MioPanProfileScope profile_scope(MIOPAN_PROFILE_ROOM_LOAD);

    if (MapLoadCallFurn(MapLoadDrawID) != 0)
    {
        return -1;
    }

    if (IsLoadEndAll() == 0)
    {
        return -1;
    }

    for (i = 0; i < 5; i++)
    {
        printf("IsLoadEnd[%d] id = %x : flg = %d\n", i, MapLoadFileIDList[i], IsLoadEnd(MapLoadFileIDList[i]));
    }

    MapLoadStat &= ~1;

    for (i = 4; i >= 0; i--)
    {
        MapLoadFileIDList[i] = -1;
    }

    if ((MapLoadStat & 2) != 0)
    {
        MapSkyRegist();
        MapLoadStat &= ~2;
    }

    if ((MapLoadStat & 4) != 0)
    {
        MapLoadStat &= ~4;
    }
    else
    {
        MapLoadInitRoom();
    }

    return 0;
}

/* --------------------------------------------------------------------------
 *  MapLoadSetMemSpace
 *
 *  Midpoint of [st_addr, en_addr), rounded down to 16 bytes.
 * ------------------------------------------------------------------------ */
uintptr_t MapLoadSetMemSpace(uintptr_t st_addr, uintptr_t en_addr)
{
    return (((en_addr - st_addr) >> 1) + st_addr) & ~(uintptr_t)0xf;
}

/* --------------------------------------------------------------------------
 *  MapLoadInit
 *
 *  Reset both room buffers, bring the map subsystems up, carve the room heap
 *  out of the space above the door/sky data, then kick off the first load.
 * ------------------------------------------------------------------------ */
int MapLoadInit(int room_no)
{
    uintptr_t addr;
    uintptr_t buff[2] = { ROOM1_DATA_ADDR, ROOM2_DATA_ADDR };
    int       i;
    int       j;

    memset(&MapLoadBuff, 0, sizeof(MapLoadBuff));

    for (i = 0; i < 2; i++)
    {
        MapLoadBuff[i].labelID = -1;
        MapLoadBuff[i].buff_id = i;
        MapLoadBuff[i].addr    = (char *)buff[i];
        MapLoadBuff[i].stat    = 0;

        for (j = 0; j < 4; j++)
        {
            MapLoadBuff[i].reg_id[j] = -1;
        }

        MapLoadBuff[i].stat |= 1;
    }

    MapLoadDrawID     = 0;
    MapLoadStat       = 0;
    MapLoadMyPosLabel = MapLoadGetLabel(room_no);

    for (i = 4; i >= 0; i--)
    {
        MapLoadFileIDList[i] = -1;
    }

    MapLoadSetDrawFlg(3, 2);
    MapLoadSetNowRoom(room_no);

    MapObjInit();
    MapDoorInit();
    RegDatInit();
    FurnCtlInit();
    MapAnimInit();
    MapManimInit();
    FurnWorkInit();
    MapHitInit();
    MapSpInit();

    addr = MapDoorAnimInit((uintptr_t *)MioPan_GetHostPointer(ROOM1_DATA_ADDR));
    MapLoadDoorBuff = (void *)MapSkyInit(addr);

    /* Both bounds are host pointers into the same contiguous EE image, so the
     * midpoint split still lands where the PS2 one did. */
    MapLoadBuff[0].addr = (char *)((uintptr_t)MapLoadDoorBuff + 0x1ea00);
    MapLoadBuff[1].addr = (char *)MapLoadSetMemSpace(
                              (uintptr_t)MapLoadBuff[0].addr,
                              (uintptr_t)MioPan_GetHostPointer(MEM_UTIL_HEAP_ADDR));

    if (room_no >= 0)
    {
        MapLoadBuff[MapLoadDrawID].labelID = MapLoadGetLabel(room_no);
        MapLoadRegistReq(MapLoadMyPosLabel, MapLoadBuff[MapLoadDrawID].addr);
        MapLoadStat |= 2;
    }

    return 0;
}
