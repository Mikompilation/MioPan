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
#include "../../miopan/io/miopan_xbox_assets.h"   /* PORT: MIOPAN_MAN_MDL_MAX */

/* PORT: 78 in the ROM; the tail is the Xbox costumes' (mdldat.c). */
extern MANMDL_DAT manmdl_dat[MIOPAN_MAN_MDL_MAX];

/* Furniture-model cloth/collision table.  acsInitCloth reads this instead of
 * manmdl_dat[] when it is building a chodo (map drape) rather than a
 * character's cloth. */
extern FURNMDL_DAT furn_mdl_dat[18];        /* data 320ad8 */
extern ANI_TBL    anm_tbl[171];

#endif /* _GRAPHICS_MOTION_MDLDAT_H */
