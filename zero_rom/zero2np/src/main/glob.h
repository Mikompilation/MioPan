/* ==========================================================================
 *  main/glob.h
 *
 *  glob.c defines the engine-wide shared globals (pad[], key_now/key_bak,
 *  sys_wrk, opt_wrk, cam_custom_wrk, debug_wrk, plus larger work blocks like
 *  debug_var / plyr_wrk).  Their declarations and the struct types that back
 *  them live in common/variable.h, which is the header every consumer includes;
 *  this header simply re-exports it so a TU can pull the globals in via the
 *  owning module's name if it prefers.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _MAIN_GLOB_H
#define _MAIN_GLOB_H

#include "../common/variable.h"     /* the shared globals + their types */

#endif /* _MAIN_GLOB_H */
