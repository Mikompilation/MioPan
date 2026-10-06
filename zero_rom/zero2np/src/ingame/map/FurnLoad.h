/* ==========================================================================
 *  ingame/map/FurnLoad.h
 *
 *  Furniture loader -- decides which furniture/door models a room needs and
 *  loads the distinct set into one contiguous run of the map heap.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MAP_FURNLOAD_H
#define _INGAME_MAP_FURNLOAD_H

#include "eetypes.h"
#include "RegDat.h"                     /* MB_OUT_SECTION */

enum
{
    FURN_TBL_NUM = 16
};

/* One model-name prefix.  `key` is matched against the front of a model name
 * ("f0" matches "f012.sgd"); `label` is the base file number of that key's
 * block, `attr` the attribute every model in it inherits. */
typedef struct FURN_LOAD_TBL            /* 0x10 */
{
    /* 0x00 */ char key[8];
    /* 0x08 */ int  label;
    /* 0x0c */ int  attr;
} FURN_LOAD_TBL;

/* Callback shape for the registration walk -- one MB_OUT_SECTION per call. */
typedef void (*FURN_LOAD_REG_FUNC)(void *op);

/* Loads every distinct model the room's registrations name, starting at
 * `addr`, and returns the new top of the used region.  Each loaded model is
 * handed to FurnCtlRegist() under `buff_id`. */
char *FurnLoadRegID(int buff_id, int reg_id, char *addr);

/* Loads one model by name.  Returns the new end of the run, or `addr`
 * unchanged when the name resolves to no file.  file_id may be NULL. */
char *FurnLoadOne(const char *furn_name, char *addr, int *file_id);

/* Attribute of the model-name key, or -1 when the name matches no key. */
int FurnLoadGetAttr(char *name);

#endif /* _INGAME_MAP_FURNLOAD_H */
