/* ==========================================================================
 *  mathf.h  (EE single-precision math — PC-port shim)
 *
 *  The EE newlib <mathf.h> exposed the single-precision float math routines
 *  (sinf/cosf/tanf/atan2f/acosf/powf/logf/fmodf/...) plus the 'f'-suffixed
 *  classifiers.  On the host these all come from the standard <math.h>; this
 *  shim just forwards and re-adds the couple of non-standard spellings.
 * ======================================================================== */

#ifndef _MATHF_H
#define _MATHF_H

#include <math.h>

/* EE spelled the single-precision classifiers with an 'f'. */
#ifndef isnanf
#define isnanf(x)  (isnan(x))
#endif
#ifndef isinff
#define isinff(x)  (isinf(x))
#endif
#ifndef finitef
#define finitef(x) (isfinite(x))
#endif
#ifndef finite
#define finite(x)  (isfinite(x))
#endif

#endif /* _MATHF_H */
