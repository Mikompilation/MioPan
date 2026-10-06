/* ==========================================================================
 *  miopan_video.h  --  the direct movie picture path
 *
 *  PORT-ONLY.  There is no ROM counterpart: this is the seam that takes the
 *  GS out of movie playback altogether.
 *
 *  WHAT THE ROM DOES, and why it cannot stand on the host.  The IPU decodes a
 *  picture as 16x16 RGBA32 macroblocks; ldimage.c walks them into GS local
 *  memory over PATH3; and the game then draws an ordinary textured primitive
 *  whose TEX0 names that block -- movie.c's full-screen sprite at GS 0x2bc0,
 *  CMovieRoom::Draw()'s screen quad at 0x3aa0.  Every step of that is free on
 *  the console.  Here each one is a copy of the whole frame:
 *
 *    - the decoder repacks its linear output into macroblock order,
 *    - loadImage() hands 1120 of them to MioPan_GsUpload(), each swizzled
 *      into the emulated GS memory vector,
 *    - the renderer then hashes that region, un-swizzles it back out, and
 *      creates a host texture from the result.
 *
 *  That last step is the one that actually breaks.  A texture is cached by the
 *  hash of its GS content and g_texture_cache has no eviction, so a movie --
 *  whose content is different every frame, by construction -- creates one
 *  1024x512 GPU texture per frame and never releases any of them.  A
 *  fifty-second cutscene at 30 fps asks for about three gigabytes of texture
 *  memory before it ends.
 *
 *  WHAT HAPPENS INSTEAD.  The decoder publishes its linear RGBA frame here,
 *  and the renderer serves any TEX0 naming the movie's GS block from one
 *  persistent texture that this frame is uploaded into.  The picture never
 *  enters emulated GS memory at all, so there is nothing to swizzle, nothing
 *  to hash and nothing to leak; the draw code above it is untouched, and the
 *  UVs it wrote against a 1024x512 page still land where they did, because the
 *  picture is uploaded into that page's top-left corner exactly as the
 *  transfer chain placed it.
 *
 *  The publisher (sdk/libmpeg_video.cpp) and the consumer (the renderer) are
 *  the same thread in NTSC and two threads in PAL, where playpss.c runs the
 *  decode on videoDecMain() -- hence the lock.
 *
 *  This is the only picture path; the ROM's GS transfer chain in ldimage.c is
 *  still built and still correct, but nothing walks it while a movie is up.
 * ======================================================================== */

#ifndef MIOPAN_VIDEO_H
#define MIOPAN_VIDEO_H

#ifdef __cplusplus
extern "C" {
#endif

/* Arm the override: from here until MioPan_VideoEnd(), a TEX0 whose TBP0 is
 * `gs_addr` is answered with the published picture rather than with whatever
 * emulated GS memory holds.  Called by setLoadImageTags(), the one function
 * that is told where the picture was going. */
void MioPan_VideoBegin(int gs_addr);

/* Disarm it.  Called when the decoder is created or reset -- i.e. at both ends
 * of a movie -- so nothing is overridden between films. */
void MioPan_VideoEnd(void);

/* Armed?  ldimage.c's loadImage() skips the whole transfer chain while this is
 * true, and the renderer uses it to decide whether a TEX0 is the movie's. */
int  MioPan_VideoIsLive(void);
int  MioPan_VideoGsAddr(void);


/* The publisher's two calls.  BeginFrame hands back a `width` x `height` RGBA
 * staging buffer to decode into -- tightly packed, `width` * 4 bytes per row --
 * or NULL if the direct path is off or the buffer could not be sized, in which
 * case the caller falls back to the ROM's macroblock path.  EndFrame publishes
 * what was written.  Nothing is copied by either. */
unsigned char *MioPan_VideoBeginFrame(int width, int height);
void           MioPan_VideoEndFrame(void);

#ifdef __cplusplus
}

#include <vector>

namespace MioPan
{
namespace Video
{
/* The renderer's side: take the picture published since the last call, if any.
 * `out` is swapped with the published buffer rather than copied, so the caller
 * may move it straight into a pending GPU upload. */
bool AcquireFrame(std::vector<unsigned char> &out, int *width, int *height);
}
}
#endif

#endif /* MIOPAN_VIDEO_H */
