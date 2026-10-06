/* ==========================================================================
 *  ingame/map/map_height.h
 *
 *  Height-mesh loading and queries.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MAP_MAP_HEIGHT_H
#define _INGAME_MAP_MAP_HEIGHT_H

#include <stdint.h>

void MhFirstInit(void);
int MhInitMapHeight(uintptr_t addr, float *offset, int id);
int MhGetMapHeight(float *pos_h, float *pos, int id, int limit_flg);
int MhDrawHeight(int id);
int MhHitLineCheck(float *pos1, float *pos2, int id);
int MhSetOffset2(int id, float *in_offset);

#endif /* _INGAME_MAP_MAP_HEIGHT_H */
