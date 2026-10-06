/* ==========================================================================
 *  eeProfile.c
 *
 *  GCC -finstrument-functions hooks.  When the engine is built with function
 *  instrumentation the compiler emits a call to __cyg_profile_func_enter on
 *  entry to (and __cyg_profile_func_exit on exit from) every instrumented
 *  function, passing the address of the function and of its call site.  These
 *  hooks maintain a simple shadow call stack (g_aProfileFunction) so the
 *  current call depth can be inspected from a debugger.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "eetypes.h"

/* --------------------------------------------------------------------------
 *  Shadow call stack (see globals.txt).
 * ------------------------------------------------------------------------ */
typedef struct                          /* 0x8 */
{
    void *pThisFunction;                /* 0x0 */
    void *pCallSite;                    /* 0x4 */
} PROFILEFUNCTION;

PROFILEFUNCTION g_aProfileFunction[256];
int            g_iCallStackCount;

/* --------------------------------------------------------------------------
 *  __cyg_profile_func_enter
 *
 *  Push the entered function onto the shadow stack.
 * ------------------------------------------------------------------------ */
extern "C" void __cyg_profile_func_enter(void *this_fn, void *call_site)
{
    g_aProfileFunction[g_iCallStackCount].pThisFunction = this_fn;
    g_iCallStackCount = g_iCallStackCount + 1;
}

/* --------------------------------------------------------------------------
 *  __cyg_profile_func_exit
 *
 *  Pop the returning function off the shadow stack.
 * ------------------------------------------------------------------------ */
extern "C" void __cyg_profile_func_exit(void *this_fn, void *call_site)
{
    g_iCallStackCount = g_iCallStackCount - 1;
}
