/* ==========================================================================
 *  ingame/map/MapPut.h
 *
 *  Registration, ordering and drawing of placed map objects and callbacks.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MAP_MAPPUT_H
#define _INGAME_MAP_MAPPUT_H

#include <stdint.h>                 /* intptr_t -- see MapPutSetWork */

#include "../../graphics/graph3d/gra3dTypes.h"

typedef float MAPPUT_MATRIX[4][4];
typedef void (*MAPPUT_FUNC)(void);

void MapPutSetFlg(int buff_id, int flg);
void MapPutDeleteFlg(int buff_id, int flg);
void MapPutSetFirst(void *obj, short offset);
void MapPutSetZoffset(void *obj, float offset);
void MapPutSetMatrix(void *obj, MAPPUT_MATRIX mat);
void MapPutSetMatrixPtr(void *obj, MAPPUT_MATRIX *mp);
void MapPutSetBuffID(void *obj, int buff_id);
void MapPutSetLitPtr(void *obj, GRA3DLIGHTDATA *light_ptr);
void MapPutSetWork(void *obj, intptr_t work);
void MapPutSetModelPtr(void *obj, u_int *addr);
MAPPUT_MATRIX *MapPutGetMatrixPtr(void *obj);
int *MapPutGetFlgPtr(void *obj);
GRA3DLIGHTDATA *MapPutGetLitPtr(void *obj);
u_int *MapPutGetModelPtr(void *obj);
intptr_t MapPutGetWork(void *obj);
void MapPutSetFuncAddr(void *obj, MAPPUT_FUNC func);

void *MapPutSetObj(int buff_id, u_int *addr, float *pos, float *rot,
                   float *scale, GRA3DLIGHTDATA *lit, int flg);
void *MapPutSetFunc(int buff_id, u_int *addr, int flg);
void MapPutChangeFunc(void *pHdl, MAPPUT_FUNC func);
void MapPutChangeObj(void *pHdl);
void MpaPutDeleteOneObj(void *hdl);
void MapPutDelete(int buff_id);

void *MapPutGetNowHdl(void);
int MapPutGetNowBuffID(void);
void MapPutDraw(void);
void MapPutResetAll(void);

#endif /* _INGAME_MAP_MAPPUT_H */
