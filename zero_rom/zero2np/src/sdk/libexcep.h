/* ==========================================================================
 *  libexcep.h  (SCE exception debug-console library -- PC-port shim)
 * ======================================================================== */

#ifndef _LIBEXCEP_H
#define _LIBEXCEP_H

#ifdef __cplusplus
extern "C" {
#endif

void sceExcepConsOpen(int x, int y, int w, int h);
void sceExcepConsLocate(int x, int y);
int  sceExcepConsPrintf(const char *fmt, ...);
int  sceExcepSetDebugIOPHandler(char *moduleName, void (*handler)(void *, void *), void *work);

#ifdef __cplusplus
}
#endif

#endif /* _LIBEXCEP_H */
