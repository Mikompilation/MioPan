/* ==========================================================================
 *  libscf.cpp  (SCE system-configuration library -- PC-port shim)
 * ======================================================================== */

#include "libscf.h"

extern "C" {

int sceScfGetLanguage(void)
{
    return SCE_ENGLISH_LANGUAGE;
}

}
