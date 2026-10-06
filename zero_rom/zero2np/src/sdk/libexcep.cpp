/* ==========================================================================
 *  libexcep.cpp  (SCE exception debug-console library -- PC-port shim)
 * ======================================================================== */

#include "libexcep.h"

#include <stdarg.h>
#include <stdio.h>

extern "C" {

void sceExcepConsOpen(int x, int y, int w, int h)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}

void sceExcepConsLocate(int x, int y)
{
    (void)x;
    (void)y;
}

int sceExcepConsPrintf(const char *fmt, ...)
{
    int ret;
    va_list ap;

    va_start(ap, fmt);
    ret = vprintf(fmt, ap);
    va_end(ap);
    return ret;
}

int sceExcepSetDebugIOPHandler(char *moduleName, void (*handler)(void *, void *), void *work)
{
    (void)moduleName;
    (void)handler;
    (void)work;
    return 0;
}

}
