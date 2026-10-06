/* ==========================================================================
 *  g3dUtil.h
 *
 *  SGD offset<->pointer relocation helpers and the indexof<> template used by
 *  the SGD data layer.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _G3DUTIL_H
#define _G3DUTIL_H

#include <stddef.h>
#include <stdint.h>
#include "eetypes.h"            /* u_int */
#include "g3ddbg.h"             /* G3DASSERT */

#define INVALID_SGD_OBJECTID -1
#define SGD_VALID_VERSIONID  0x1050u
#define SGD_REMAP_BORDER     0x30000000u    /* below => still a file offset */

#define SGD_ADDR(base, off)  ( (uintptr_t)(base) + (uintptr_t)(u_int)(off) )        /* offset -> ptr  */
#define SGD_REMAP(base, off) ( (u_int)(off) < SGD_REMAP_BORDER                      /* guarded remap  */ \
                               ? SGD_ADDR(base, off) : (u_int)(off) )
#define SGD_UNMAP(base, ptr) ( (u_int)((uintptr_t)(ptr) - (uintptr_t)(base)) )      /* ptr -> offset  */

/* indexof<T>: (obj-arraytop)/sizeof(T), asserting exact alignment (g3dUtil.h:0x8f) */
template <class T> int indexof(const T *arraytop, const T *obj)
{
    ptrdiff_t diff = (const char *)obj - (const char *)arraytop;

    G3DASSERT((diff % sizeof(T)) == 0, "");
    return (int)(diff / sizeof(T));
}

#endif /* _G3DUTIL_H */
