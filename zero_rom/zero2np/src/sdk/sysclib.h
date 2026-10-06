/* sysclib.h  (IOP C library — PC-port shim)
 *
 * The IOP kernel exported its own copies of the C string/format routines.  On
 * the host they are the host's, which pc_prefix.h already makes ambient; this
 * header exists so the reconstructed sources can keep their <sysclib.h>
 * include exactly as iopsys.irx had it. */
#ifndef _IOP_SYSCLIB_H
#define _IOP_SYSCLIB_H

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#endif /* _IOP_SYSCLIB_H */
