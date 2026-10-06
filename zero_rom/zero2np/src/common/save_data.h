/* ==========================================================================
 *  common/save_data.h
 *
 *  Descriptor used by the save system to collect a live block of game data.
 *  The original EE structure held a 32-bit address.  This is runtime metadata,
 *  not serialized payload, so the PC port deliberately keeps a native pointer.
 * ======================================================================== */

#ifndef _COMMON_SAVE_DATA_H
#define _COMMON_SAVE_DATA_H

#include "eetypes.h"

typedef struct _MC_SAVE_DATA
{
    /* 0x0 */ u_char *addr;
    /* 0x4 */ int     size;
} MC_SAVE_DATA;

#endif /* _COMMON_SAVE_DATA_H */
