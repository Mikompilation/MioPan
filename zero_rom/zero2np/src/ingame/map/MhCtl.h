/* ==========================================================================
 *  ingame/map/MhCtl.h
 *
 *  Room/model hierarchy controller interface.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MAP_MHCTL_H
#define _INGAME_MAP_MHCTL_H

#include "RegDat.h"
#include "../../debug/debug_menu.h"

typedef struct
{
    int draw_flg;
    int draw_hight;
    int predb_mode;
    int save;
} MH_CTL_DB_FLG;

extern MH_CTL_DB_FLG mhdb;
extern DEBUG_MENU dbg_room_main;

void MhCtlInit(void);
int MhCtlGetMapHeight(float *tv, float *pos, int room_no, int flg);
int MhCtlHitLineCheck(float *pos1, float *pos2, int room_no);
int MhCtlGetRoomNo(int kai, float *vPos);
void MhCtlGetObjStatStart(int room_no, int kai);
MB_OUT_SECTION *MhCtlGetObjStatNext(void);
int MhCtlMain(int area_no);
void MhCtlDrawShadow(void);
void MhCtlSetVuFlush(void);
int MhCtlDraw(void);
void MhCtlDrawLock(void);
void MhCtlDrawUnlock(void);
void MhCtlDbOutObjPos(int room_no, int kai);
void MhCtlReload(void);

#endif /* _INGAME_MAP_MHCTL_H */
