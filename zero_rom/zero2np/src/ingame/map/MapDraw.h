/* ==========================================================================
 *  ingame/map/MapDraw.h
 *
 *  Room drawing / lighting interface.  Every MapDraw.o symbol in ZERO2.MAP is
 *  implemented in MapDraw.c; the static MapDrawDeleteHit() is not exported.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MAP_MAPDRAW_H
#define _INGAME_MAP_MAPDRAW_H

#include "MapLoad.h"

/* ---- room coordinate setup --------------------------------------------- */
void            MapDrawCalcRoomCoord(void *sgd_top, float *pos);
void            MapDrawSetMatrixSGD(void *sgd_top, float mat[4][4]);
void            MapDrawSGD(void *sgd_top);
int             MapDrawGetCenPos(void *sgd_top, float (*rvec)[4]);

/* ---- light lookup ------------------------------------------------------ */
void            MapDrawSetSpRoomLight(GRA3DLIGHTDATA *lp);
GRA3DLIGHTDATA *MapDrawGetLightPtr4BuffID2(int buff_id);
GRA3DLIGHTDATA *MapDrawGetLightPtr4BuffID(int buff_id);
GRA3DLIGHTDATA *MapDrawGetLightPtr(int room_no);

/* ---- room resources ---------------------------------------------------- */
int             MapDrawInitRegDat(MLOAD_HEAD *hp);
int             MapDrawRegistModel(MLOAD_HEAD *hp);
int             MapDrawInitRoom(MLOAD_HEAD *hp);
void            MapDrawInitFurn(MLOAD_HEAD *hp);
void            MapDrawPreLight(MLOAD_HEAD *hp);

/* ---- teardown ---------------------------------------------------------- */
void            MapDrawDelRegDatAll(MLOAD_HEAD *hp);
void            MapDrawDeleteNoDraw(int buff_id);
void            MapDrawDeleteRoom(MLOAD_HEAD *hp);
void            MapDrawDeleteRoomAll(void);

/* ---- draw -------------------------------------------------------------- */
void            MapDrawObj(void *top, float mat[4][4]);
/* Same as MapDrawObj minus gra3dshadowAddProjectModel(); the light-come-in
 * effect uses it so its models do not cast. */
void            MapDrawObjNoShadow(void *top, float mat[4][4]);
void            MapDrawSetUpRoomCoordinate(MLOAD_HEAD *hp);
void            MapDrawShadowOne(MLOAD_HEAD *hp, GRA3DLIGHTDATA *lp);
void            MapDrawRoomOne(MLOAD_HEAD *hp, GRA3DLIGHTDATA *lp);
void            MapDrawRoom(void);

void            MapDrawEnableFlashlightOnly(int b);
int             MapDrawIsEnableFlashlightOnly(void);

#endif /* _INGAME_MAP_MAPDRAW_H */
