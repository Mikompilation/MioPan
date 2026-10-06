/* ==========================================================================
 *  system/playpss/ldimage.h
 *
 *  playpss.a(ldimage.o) -- the GS side of the movie player.  Two exported
 *  functions over seven static packet builders: setLoadImageTags() writes a
 *  PATH3 DMA chain that walks the IPU's RGB32 macroblocks into a rectangle of
 *  GS local memory, and loadImage() kicks it.
 *
 *  Declarations from ZERO2.MAP and functions.txt.  Reconstructed from the
 *  Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_PLAYPSS_LDIMAGE_H
#define _SYSTEM_PLAYPSS_LDIMAGE_H

#include "eetypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build the transfer chain for a `width` x `height` image sitting at `image`
 * (a run of sceIpuRGB32 macroblocks), landing at (x, y) inside the GS page at
 * `vram_adrs`.  Returns one past the last tag word written, which is what
 * playPssSetPacket() hands back through its pTagEndAdrs out-parameter so the
 * caller can splice the chain into its own display list. */
u_int *setLoadImageTags(u_int *tags, void *image, int x, int y,
                        int width, int height, int vram_adrs);

/* Send a chain built by setLoadImageTags() down PATH3. */
void   loadImage(u_int *tags);

#ifdef __cplusplus
}
#endif

#endif /* _SYSTEM_PLAYPSS_LDIMAGE_H */
