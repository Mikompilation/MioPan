// FILE: /home/zero_rom/zero2np/src/ingame/map/MhCtl.c
//
// Room/model hierarchy controller.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "MhCtl.h"
#include "../../common/utility2.h"            /* PRINT_ERROR */

#include "MapAnim.h"
#include "MapDraw.h"
#include "MapLoad.h"
#include "MapObj.h"
#include "MapHit.h"
#include "MapLight.h"
#include "MapPut.h"
#include "MapSky.h"
#include "MapSp.h"
#include "RegDat.h"
#include "map_height.h"
#include "map_hit_check.h"
#include "map_rectangle.h"                       /* DrawMapHitRect */
#include "../../debug/DbFurnPre.h"
#include "../../debug/debug_menu.h"
#include "../../graphics/graph3d/g3dDma.h"
#include "../../graphics/graph3d/gra3d.h"
#include "../../graphics/graph3d/gra3dMisc.h"
#include "../../graphics/graph3d/gra3dSGD.h"
#include "../../graphics/graph3d/gra3dShadow.h"
#include "../../graphics/motion/accessory.h"
#include "../../graphics/obj_draw_ctrl.h"
#include "../../common/variable.h"                 /* debug_var */
#include "../../main/glob.h"
#include "../plyr/player.h"
#include "../plyr/plyr_mdl.h"
#include "../plyr/sis_mdl.h"

#include <stdio.h>
#include "../../graphics/graph3d/g3dxVu0.h" /* g3dxVu0CopyVector */

static char s_dbg_room_title[] = "ROOM";
static char s_dbg_room_draw[] = "ROOM DRAW";
static char s_dbg_height_draw[] = "HIGH DRAW";
static char s_dbg_obj_prelight[] = "OBJ PRELIGHT";
static char s_dbg_light_scale[] = "LIGHT_SCALE";
static char s_dbg_nlight_intens[] = "NLIGHT_INTENS";
static char s_dbg_nlight_power[] = "NLIGHT_POWER";
static char s_dbg_flight_intens[] = "FLIGHT_INTENS";
static char s_dbg_flight_power[] = "FLIGHT_POWER";
static char s_dbg_door_hit_z[] = "DOOR_HIT_Z";
/* Port additions -- not ROM strings; see the dbg_room_main rows below. */
static char s_dbg_pl_ambient[] = "PL_AMBIENT";
static char s_dbg_steplight_r[] = "STEPLIGHT_R";
static char s_dbg_steplight_g[] = "STEPLIGHT_G";
static char s_dbg_steplight_b[] = "STEPLIGHT_B";
static char s_dbg_simi_end[] = "SIMI_END";
static char s_dbg_simi_time[] = "SIMI_TIME";
static char s_dbg_save[] = "SAVE";
static char s_dbg_obj_sp[] = "OBJ SP";
static char s_dbg_end[] = "_end_";
static char s_maplight_label[] = "MAPLI_";
static char s_maplight_path[] = "host0:../src/ingame/map/";
static char s_maplight_file[] = "MapLightDat.h";

MH_CTL_DB_FLG mhdb = { 1, 0, 0, 0 };

DEBUG_MENU dbg_room_main =                                              /* data 2cc838 */
{
    &dbg_menu_main,
    nullptr,
    s_dbg_room_title,
    {
        { s_dbg_room_draw,     DBM_ATTR_SWITCH, &obj_draw_ctrl.room, 0.0f, 1.0f, 1.0f },
        { s_dbg_height_draw,   DBM_ATTR_SWITCH, &mhdb.draw_hight,    0.0f, 1.0f, 1.0f },
        { s_dbg_obj_prelight,  DBM_ATTR_SWITCH, &mhdb.predb_mode,    0.0f, 1.0f, 1.0f },
        { s_dbg_light_scale,   DBM_ATTR_VALUE | DBM_ATTR_FLOAT,
                               &MapLightPower,      0.0f, 10.0f, 0.01f },
        { s_dbg_nlight_intens, DBM_ATTR_VALUE | DBM_ATTR_FLOAT,
                               &MapLightIntens[0],  0.0f,  1.0f, 0.01f },
        { s_dbg_nlight_power,  DBM_ATTR_VALUE | DBM_ATTR_FLOAT,
                               &MapLightDiff[0],    0.0f,  2.0f, 0.01f },
        { s_dbg_flight_intens, DBM_ATTR_VALUE | DBM_ATTR_FLOAT,
                               &MapLightIntens[1],  0.0f,  1.0f, 0.01f },
        { s_dbg_flight_power,  DBM_ATTR_VALUE | DBM_ATTR_FLOAT,
                               &MapLightDiff[1],    0.0f,  2.0f, 0.01f },
        { s_dbg_door_hit_z,    DBM_ATTR_VALUE | DBM_ATTR_FLOAT,
                               &MapHitDoorZ,        0.0f,  2.0f, 0.01f },
        { s_dbg_simi_end,      DBM_ATTR_VALUE,  &MapObjSimiEnd,   0.0f,  255.0f, 1.0f },
        { s_dbg_simi_time,     DBM_ATTR_VALUE,  &MapObjSimiTime,  0.0f, 1000.0f, 1.0f },
        { s_dbg_save,          DBM_ATTR_SWITCH, &mhdb.save,       0.0f,    1.0f, 1.0f },
        { s_dbg_obj_sp,        DBM_ATTR_MENU,   &dbg_kaza_main,   0.0f,    0.0f, 0.0f },
        /* Port additions -- the ROM's table ends at OBJ SP.  These four floats
         * are live every frame in plyr_mdlGetCharLightData() but the ROM points
         * no menu row at them, so they could only ever be changed by editing
         * DebugInit().  Appended at the tail rather than grouped with the
         * lighting rows above so the ROM's row order stays byte-identical --
         * the table is 20 slots and mnum is recounted from "_end_" every frame,
         * so this needs no other change.  Same reasoning as debug_menu.c's own
         * EVENT DEBUG / CAMERA rows.
         *
         * PL_AMBIENT is LD.vAmbient on the character light block -- the flat
         * floor under Mio and Mayu, one value for all three channels.
         * STEPLIGHT_* is aELDCD_MIO[PFT_STEP].vStaticDirLightColor, the static
         * directional fill that only applies while the flashlight is the
         * step-lamp type.  (The hand-lamp twin is debug_var.sis_para_r/g/b,
         * deliberately left off -- add it here if it is ever wanted.) */
        { s_dbg_pl_ambient,    DBM_ATTR_VALUE | DBM_ATTR_FLOAT,
                               &debug_var.pl_amb,                   0.0f, 1.0f, 0.01f },
        { s_dbg_steplight_r,   DBM_ATTR_VALUE | DBM_ATTR_FLOAT,
                               &debug_var.fStaticDirLightColStepR,  0.0f, 1.0f, 0.01f },
        { s_dbg_steplight_g,   DBM_ATTR_VALUE | DBM_ATTR_FLOAT,
                               &debug_var.fStaticDirLightColStepG,  0.0f, 1.0f, 0.01f },
        { s_dbg_steplight_b,   DBM_ATTR_VALUE | DBM_ATTR_FLOAT,
                               &debug_var.fStaticDirLightColStepB,  0.0f, 1.0f, 0.01f },
        { s_dbg_end,           0,               nullptr,          0.0f,    0.0f, 0.0f },
    },
    0,
    0,
    0,
    0,
};

static int MhCtlLoadFlg;
static int MhCtlRegBuffID;
static int s_bDrewShadow;
static int mh_ctl_disp_lock_cnt;

void MhCtlInit(void)
{
    mh_ctl_disp_lock_cnt = 0;
    MhCtlRegBuffID = -1;
}

int MhCtlGetMapHeight(float *tv, float *pos, int room_no, int flg)
{
    DrawMapHitRect(pos);

    int b_id = -1;
    if (MhCtlLoadFlg == 0)
    {
        b_id = MapLoadGetBuffID(room_no);
        if (b_id < 0)
        {
            PRINT_ERROR(" NO_ROOM_NO b_id[%d]:room_no[%d]\n", b_id, room_no);
            return -1;
        }

        if (b_id != 0 && mhdb.draw_hight != 0)
        {
            MhDrawHeight(b_id);
        }

        MLOAD_HEAD *hp = MapLoadGetHeadPtr(b_id);
        if (hp->high_addr != 0)
        {
            return MhGetMapHeight(tv, pos, b_id, flg);
        }

        g3dxVu0CopyVector(tv, pos);
        return 1;
    }

    return b_id;
}

int MhCtlHitLineCheck(float *pos1, float *pos2, int room_no)
{
    if (MhCtlLoadFlg == 0)
    {
        int b_id = MapLoadGetBuffID(room_no);
        if (b_id >= 0)
        {
            return MhHitLineCheck(pos1, pos2, b_id);
        }

        PRINT_ERROR(" NO_ROOM_NO b_id[%d]:room_no[%d]\n", b_id, room_no);
    }

    return -1;
}

int MhCtlGetRoomNo(int kai, float *vPos)
{
    int buff_id = MapLoadGetBuffID4Pos(kai, vPos);
    if (buff_id >= 0)
    {
        return MapLoadGetRoomNo4BuffID(buff_id);
    }

    return buff_id;
}

void MhCtlGetObjStatStart(int room_no, int kai)
{
    int buff_id = MapLoadGetRegBuffID(room_no, kai);
    if (buff_id >= 0)
    {
        RegDatGetStPtrStart(buff_id, 3);
        MhCtlRegBuffID = buff_id;
    }
}

MB_OUT_SECTION *MhCtlGetObjStatNext(void)
{
    return RegDatGetNextStPtr(MhCtlRegBuffID);
}

int MhCtlMain(int area_no)
{
    MapObjProc();
    MapAnimProc();
    acsChodoClothCtrl();

    if (MhCtlLoadFlg != 0 && MapLoadCheckLoadNow() == 0)
    {
        MhCtlLoadFlg = 0;
    }

    if (mhdb.save != 0)
    {
        DbmSave(&dbg_room_main, s_maplight_path, s_maplight_file, s_maplight_label);
        mhdb.save = 0;
    }

    MapLoadSetNowRoom(area_no);
    MapLoadMain();
    return 0;
}

void MhCtlDrawShadow(void)
{
    if (s_bDrewShadow != 0)
    {
        return;
    }

    s_bDrewShadow = 1;

    int bInFinder = PlayerModeIsFinder();
    if (bInFinder == 0)
    {
        int drew_room_shadow = 0;

        if (playerGetFlashlightType() == PFT_HAND)
        {
            MapLoadGetRoomNoNow();
            MLOAD_HEAD *hp = MapLoadGetHeader();

            if (hp != nullptr &&
                hp->model_addr != 0 &&
                hp->shadow_addr != 0 &&
                hp->shadow_s_addr != 0)
            {
                if (debug_var.shadow_model_disp != 0)
                {
                    gra3dSetGsRegisterDefault();
                    if (debug_var.shadow_model_disp != 0)
                    {
                        _gra3dDrawSGD((SGDFILEHEADER *)hp->shadow_addr,
                                      SRT_REALTIME, nullptr, -1);
                    }
                }

                if (g_gra3dShadowDebug.bDrawObjectShadow != 0 &&
                    GetSdwDrawFLG() != 0)
                {
                    gra3dDrawSGDShadowEveryObject(
                        (SGDFILEHEADER *)hp->shadow_addr, &plyr_wrk.fl);
                }

                drew_room_shadow = 1;
            }
        }

        if (drew_room_shadow == 0)
        {
            playerDrawShadow();
        }
    }

    sisterDrawShadow();
}

void MhCtlSetVuFlush(void)
{
    u_int *packet = (u_int *)g3dDmaOpenPacket();
    packet[0] = 0;
    packet[1] = 0x11000000;
    packet[2] = 0;
    packet[3] = 0;
    g3dDmaClosePacket(packet + 4);
}

int MhCtlDraw(void)
{
    if (mh_ctl_disp_lock_cnt == 0)
    {
        gra3dshadowClearProjectModel();
        _SetPREVIOUSTRI2PRIM(nullptr);
        gra3dSetGsRegisterDefault();
        s_bDrewShadow = 0;

        if (GetSkyDrawFLG() != 0)
        {
            MapSkyProc();
        }

        if (mhdb.predb_mode != 0)
        {
            DbFurnPreProc();
        }

        MapDrawRoom();
        MapPutDraw();
        MhCtlDrawShadow();
    }

    return 0;
}

void MhCtlDrawLock(void)
{
    mh_ctl_disp_lock_cnt++;
}

void MhCtlDrawUnlock(void)
{
    mh_ctl_disp_lock_cnt--;
}

void MhCtlDbOutObjPos(int room_no, int kai)
{
    printf("***********************************************\n");
    MhCtlGetObjStatStart(room_no, kai);

    MB_OUT_SECTION *mp;
    while ((mp = MhCtlGetObjStatNext()) != nullptr)
    {
        float *pos = (float *)((char *)mp + 0x30);
        printf("event obj pos[%f][%f][%f]\n", pos[0], pos[1], pos[2]);
    }
}

void MhCtlReload(void)
{
    if (MapLoadCheckLoadNow() == 0 && MapLoadGetHeader() != nullptr)
    {
        MapLoadReload(MapLoadGetRoomNoNow());
    }
}
