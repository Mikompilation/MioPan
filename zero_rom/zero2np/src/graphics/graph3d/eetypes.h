/* ==========================================================================
 *  eetypes.h  (EE base scalar / 128-bit types -- minimal shim)
 *
 *  The small set of EE/SCE fundamental typedefs the graph3d sources rely on,
 *  taken verbatim from the prototype's debug type info.  Normally these come
 *  from <eekernel.h> / <eeregs.h>; collected here so the tree is self-contained.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _EETYPES_H
#define _EETYPES_H

/* The EE base scalar / 128-bit types are host-portable in the PC-port shim's
   scetypes.h (u_long = 64-bit per the EE ABI, u_long128 emulated on MSVC).
   Defer to it so there is a single definition across the whole tree. */
#include "scetypes.h"

#endif /* _EETYPES_H */
