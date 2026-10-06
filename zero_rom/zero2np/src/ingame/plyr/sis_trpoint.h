/* ==========================================================================
 *  ingame/plyr/sis_trpoint.h
 *
 *  Companion path-finding graph (sis_trpoint.o).  Data only -- the object has
 *  no .text at all; sister.c owns every algorithm that reads these tables.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_PLYR_SIS_TRPOINT_H
#define _INGAME_PLYR_SIS_TRPOINT_H

#include "../../sdk/libvu0.h"           /* sceVu0FVECTOR */

/* Number of trace points in each room, indexed by PROOM_INFO::area_no.
 * Zero means the room has no companion routing graph. */
extern int room_point_num[63];          /* data 3450d0 */

/* Point list per room, or nullptr where room_point_num[] is 0.  The w
 * component of each point is its floor number. */
extern sceVu0FVECTOR *room_point_pos[63];   /* data 346cf0 */

/* Connection cost matrix per room, flattened.  The matrix is
 * (room_point_num[r] + 1) square, with row/column 0 unused, so the cost
 * between points i and j is room_connect[r][(i + 1) * (n + 1) + (j + 1)].
 * 9999 means "not connected". */
extern int *room_connect[63];           /* data 34f2c0 */

#endif /* _INGAME_PLYR_SIS_TRPOINT_H */
