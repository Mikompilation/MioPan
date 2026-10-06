/* ==========================================================================
 *  pc_prefix.h  (PC-port compatibility prefix)
 *
 *  Force-included into every translation unit (see CMakeLists.txt).  It makes
 *  the C runtime and the EE base scalar types ambient — matching the way the
 *  PS2 SDK's ubiquitous includes made printf/malloc/memcpy and u_int/u_long
 *  available on the EE without each source explicitly including them.
 * ======================================================================== */

#ifndef _PC_PREFIX_H
#define _PC_PREFIX_H

#include <stdio.h>              /* printf / sprintf / ...                     */
#include <stdlib.h>            /* malloc / free / rand / abort / ...         */
#include <string.h>            /* memcpy / memset / strcpy / ...             */

/* Pulled in HERE, ahead of the printf redirect below, and load-bearing for it.
   libstdc++'s <cstdio> opens with a run of #undefs -- printf and vprintf among
   them -- to get rid of any macro shadowing the C library names.  A translation
   unit that includes it after this header would therefore quietly lose the
   redirect and print to the console instead of the log, with nothing to show
   for it.  Including it first means the include guard is already set and those
   #undefs cannot run again, whoever asks for the header later. */
#ifdef __cplusplus
#include <cstdio>
#endif

#include "scetypes.h"          /* EE base types (u_long=64-bit) + NULL       */
#include "vu0_intrin.h"        /* VU0 macro-mode intrinsics + quadword types  */
#include "pcport_stubs.h"      /* EE scratchpad aliases + trap/uint stubs      */
#include "eekernel.h"          /* ambient kernel services / FlushCache         */
#include "sif.h"               /* ambient SIF system-memory allocation         */
#include "libexcep.h"          /* ambient exception debug-console services     */

/* MioPan_Rand()/MIOPAN_RAND_MAX -- the ROM's 31-bit rand() range, restored on
   every host.  Ambient here because rand() was ambient on the EE, and because
   a site that reaches for the host's rand()/RAND_MAX instead is a bug (the
   two runtimes disagree by 65536x).  The header pulls in no SDL of its own. */
#include "miopan/os/miopan_rand.h"

/* The ROM's own tracing, into the log file.
 *
 * The reconstruction reports through printf at roughly a thousand call sites,
 * transcribed verbatim from the prototype, and until this redirect existed all
 * of it went to a console that a shipped build should not have -- so the
 * evidence for any bug report was gone the moment the window closed.  One macro
 * in the port's own shim layer, which is force-included into every translation
 * unit anyway, beats editing a thousand call sites in files whose whole point
 * is to stay a matching decompilation.
 *
 * Function-like, so a bare `printf` used as a value still names the C library
 * function.  See miopan/io/miopan_log.h for what happens on the other side, and
 * NOTE the <cstdio> ordering above -- it is what keeps this from being silently
 * undone in a translation unit that reaches for the C++ header.
 */
#include "miopan/io/miopan_log.h"

#define printf(...)  MioPan_LogPrintf(__VA_ARGS__)
#define vprintf(...) MioPan_LogVPrintf(__VA_ARGS__)

/* A few PS2 SDK entry points the game code invokes without an explicit SDK
   include, as the EE build's ubiquitous headers made them ambient.  scePrintf
   expands to the token `printf`, which then goes through the macro above. */
#define scePrintf printf

/* EE data-cache prefetch — a no-op on the host. */
#ifndef prefetch
#define prefetch(...) ((void)0)
#endif

#ifdef _MSC_VER
#define ATTRIBUTE_ALIGNED(x, decl) __declspec(align(x)) decl
#else
#define ATTRIBUTE_ALIGNED(x, decl) decl __attribute__((aligned(x)))
#endif

#endif /* _PC_PREFIX_H */
