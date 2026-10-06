/* ==========================================================================
 *  graphics/effect/effect_pak.h
 *
 *  COMPLETE - the effect packet sorter.  All three ZERO2.MAP .text symbols
 *  and the module's three statics.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), 0x0015bf58.
 * ======================================================================== */

#ifndef _GRAPHICS_EFFECT_EFFECT_PAK_H
#define _GRAPHICS_EFFECT_EFFECT_PAK_H

#include "eetypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Claim a slot at priority `pri` in the effect packet sort order.  Both of
 * these are already empty in the prototype (0x0015c030 / 0x0015c038 are a bare
 * `jr ra`), so the calls are kept purely to preserve the ROM's statement
 * order for the day the sorter comes back. */
void Reserve2DPacket(u_int pri);
void Reserve2DPacket_Load(void);

/* Selection-sort the reserved packets into descending priority order, or drop
 * the whole list if either buffer overflowed.  With the two reservation calls
 * empty in this build the list is always empty, so this is a no-op in
 * practice -- reconstructed because the ROM exports it. */
void SortEffectPacket(void);

#ifdef __cplusplus
}
#endif

#endif /* _GRAPHICS_EFFECT_EFFECT_PAK_H */
