/* ==========================================================================
 *  graphics/motion/acs_dat.h
 *
 *  Accessory, rope, cloth, and collision data storage.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRAPHICS_MOTION_ACS_DAT_H
#define _GRAPHICS_MOTION_ACS_DAT_H

#include "mdlwork.h"

extern WMIM_DAT ch000_wmim_tbl[];
extern WMIM_DAT ch001_wmim_tbl[];
extern WMIM_DAT ch003_wmim_tbl[];
extern WMIM_DAT ch017_wmim_tbl[];
extern WMIM_DAT ch030_wmim_tbl[];
extern WMIM_DAT ch039_wmim_tbl[];

extern CLOTH_DAT ch003_cloth[];
extern CLOTH_DAT ch004_cloth[];
extern CLOTH_DAT ch006_cloth[];
extern CLOTH_DAT ch011_cloth[];
extern CLOTH_DAT ch013_cloth[];
extern CLOTH_DAT ch014_cloth[];
extern CLOTH_DAT ch018_cloth[];
extern CLOTH_DAT ch019_cloth[];
extern CLOTH_DAT ch020_cloth[];
extern CLOTH_DAT ch021_cloth[];
extern CLOTH_DAT ch022_cloth[];
extern CLOTH_DAT ch024_cloth[];
extern CLOTH_DAT ch025_cloth[];
extern CLOTH_DAT ch026_cloth[];
extern CLOTH_DAT ch027_cloth[];
extern CLOTH_DAT ch028_cloth[];
extern CLOTH_DAT ch029_cloth[];
extern CLOTH_DAT ch031_cloth[];
extern CLOTH_DAT ch032_cloth[];
extern CLOTH_DAT ch033_cloth[];
extern CLOTH_DAT ch038_cloth[];
extern CLOTH_DAT ch039_cloth[];
extern CLOTH_DAT ch041_cloth[];
extern CLOTH_DAT ch043_cloth[];
extern CLOTH_DAT ch044_cloth[];
extern CLOTH_DAT ch047_cloth[];
extern CLOTH_DAT ch048_cloth[];
extern CLOTH_DAT ch049_cloth[];
extern CLOTH_DAT ch053_cloth[];
extern CLOTH_DAT ch058_cloth[];
extern CLOTH_DAT ch064_cloth[];
extern CLOTH_DAT ch065_cloth[];
extern CLOTH_DAT ch066_cloth[];
extern CLOTH_DAT ch067_cloth[];
extern CLOTH_DAT ch068_cloth[];
extern CLOTH_DAT ch069_cloth[];
extern CLOTH_DAT ch070_cloth[];
extern CLOTH_DAT ch071_cloth[];

extern COLLISION_DAT ch006_collision[];
extern COLLISION_DAT ch013_collision[];
extern COLLISION_DAT ch019_collision[];
extern COLLISION_DAT ch020_collision[];
extern COLLISION_DAT ch021_collision[];
extern COLLISION_DAT ch022_collision[];
extern COLLISION_DAT ch024_collision[];
extern COLLISION_DAT ch025_collision[];
extern COLLISION_DAT ch027_collision[];
extern COLLISION_DAT ch028_collision[];
extern COLLISION_DAT ch029_collision[];
extern COLLISION_DAT ch031_collision[];
extern COLLISION_DAT ch033_collision[];

extern CLOTH_DAT f000_cloth[];
extern CLOTH_DAT fc_irori000[];
extern CLOTH_DAT fc_nuno000[];
extern CLOTH_DAT fc_nuno001[];
extern CLOTH_DAT fc_nuno002[];
extern CLOTH_DAT fc_irori001[];
extern CLOTH_DAT fc_meian000[];
extern CLOTH_DAT fc_meian001[];
extern CLOTH_DAT fc_genkan001[];
extern CLOTH_DAT fc_genkan002[];
extern CLOTH_DAT fc_genkan003[];
extern CLOTH_DAT fc_genkan004[];
extern CLOTH_DAT fc_meian002[];
extern CLOTH_DAT fc_meian003[];
extern CLOTH_DAT fc_genkan000[];
extern CLOTH_DAT fc_meian005[];
extern CLOTH_DAT fc_meian004[];
extern CLOTH_DAT fc_okunoma000[];

/* Rope shapes by type index.  acsInitRopeSub picks rope_tbl[type] and seeds the
 * particle chain from its vtx[] list. */
extern ROPE_DAT *rope_tbl[32];      /* data 2d24d0 */

#endif /* _GRAPHICS_MOTION_ACS_DAT_H */
