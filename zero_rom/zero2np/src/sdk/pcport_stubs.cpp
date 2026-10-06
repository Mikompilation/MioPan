/* ==========================================================================
 *  pcport_stubs.cpp  (definitions for genuinely cross-cutting PC-port state)
 * ======================================================================== */

#include "pcport_stubs.h"

/* The EE VU1/EE scratchpad, zero-initialised.  See pcport_stubs.h. */
extern "C" float g_sceScratchpad[0x4000 / 4] = { 0.0f };
