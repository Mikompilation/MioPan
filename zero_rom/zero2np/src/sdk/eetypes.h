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

/* Host-portable definitions of the EE base scalar / 128-bit types live in the
 * PC-port SDK shim (u_long is 64-bit there, and the 128-bit type is emulated
 * where the host compiler lacks __int128). */
#include "scetypes.h"

#endif /* _EETYPES_H */
