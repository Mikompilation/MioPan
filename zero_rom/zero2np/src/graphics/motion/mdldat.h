/* ==========================================================================
 *  graphics/motion/mdldat.h
 *
 *  Animation/model data storage.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRAPHICS_MOTION_MDLDAT_H
#define _GRAPHICS_MOTION_MDLDAT_H

#include "mdlwork.h"

extern MANMDL_DAT manmdl_dat[78];

/* Furniture-model cloth/collision table.  acsInitCloth reads this instead of
 * manmdl_dat[] when it is building a chodo (map drape) rather than a
 * character's cloth. */
extern FURNMDL_DAT furn_mdl_dat[18];        /* data 320ad8 */
extern ANI_TBL    anm_tbl[171];

#endif /* _GRAPHICS_MOTION_MDLDAT_H */
