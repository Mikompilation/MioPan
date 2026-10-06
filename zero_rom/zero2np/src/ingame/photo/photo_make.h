/* ==========================================================================
 *  ingame/photo/photo_make.h
 *
 *  Photo image production: the compositor that draws the photo frame, the
 *  hint plates over it and the developed picture inside it, the VRAM->EE
 *  capture path, and the compressor the album pages are stored with.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), photo_make.o.
 * ======================================================================== */

#ifndef _INGAME_PHOTO_PHOTO_MAKE_H
#define _INGAME_PHOTO_PHOTO_MAKE_H

#include <stdint.h>                             /* uintptr_t */
#include <sys/types.h>

#include "../../graphics/graph2d/g2d_draw.h"     /* SPRT_DAT */

/* --------------------------------------------------------------------------
 *  Where a photo lives, and how big it is.
 *
 *  A picture is 384x128 in PSMCT24, i.e. one 0x30000-byte block; the encoder
 *  prefixes it with a 0x20-byte SLIDE_ENCODE_HEADER and the compressed result
 *  is filed in a PHOTO_ONE_SIZE slot.  The 45x15 thumbnail that the album
 *  index draws gets its own uncompressed SPHOTO_ONE_SIZE page.
 *
 *  SPHOTO_AREA is PHOTO_DATA_ADDR from system.h: sixteen thumbnail pages,
 *  then sixteen compressed slots at PHOTO_AREA.  photo.c carries the same two
 *  numbers under its own names; they are repeated here because the wrappers
 *  below are the only other place that indexes them.
 * ------------------------------------------------------------------------ */
#define PHOTO_W                 384
#define PHOTO_H                 128
#define PHOTO_RAW_SIZE          0x30000     /* 384 * 128 * 4                 */
#define PHOTO_ENCODE_SIZE       0x30020     /* header + raw                  */
#define PHOTO_ONE_SIZE          0xd360      /* one compressed album slot     */

#define SPHOTO_W                45
#define SPHOTO_H                15
#define SPHOTO_ONE_SIZE         0x1000

#define SPHOTO_AREA             0x019a9b00  /* = PHOTO_DATA_ADDR             */
#define PHOTO_AREA              0x019b9b00  /* = PHOTO_DATA_ADDR + 0x10000   */

/* --------------------------------------------------------------------------
 *  Drawing a stored picture.
 *
 *  DrawPhotoBuffer() is the one primitive: it uploads `mszw` x `mszh` texels
 *  from EE memory at `addr` into the VRAM scratch page and draws them as one
 *  sprite `szw` x `szh` big at (x, y).  `ftype` selects the surround --
 *  0 none, 1 inset by four texels, 2 the sepia border, 3 the full photo frame
 *  -- and `alp` is the sprite alpha.  `szfl` and `ztype` are dead in this
 *  build; every caller passes 0.
 *
 *  PORT: every `addr` below is `uintptr_t`, not the ROM's `int`.  Most callers
 *  pass an EE memory-map constant, but album.c's album B is a block claimed
 *  out of the outgame heap -- a full-width host pointer -- and it arrives here
 *  through GetAlbumDataAddr().  An `int` truncates it.  Resolution still
 *  happens at the bottom via MioPan_GetHostPointer(), which is idempotent, so
 *  both kinds of address work unchanged.
 * ------------------------------------------------------------------------ */
void DrawPhotoBuffer(u_int pri, uintptr_t addr, int szfl, int x, int y,
                     int szw, int szh, int mszw, int mszh,
                     int ftype, u_char alp, int ztype);

/* Album thumbnails, from the shared thumbnail area or from an explicit base.
 * The "2" pair take the on-screen size; the other two draw at 45x30, i.e. the
 * stored 45x15 stretched back to its 2:1 aspect. */
void DrawSPhotoFromSmallPhotoArea(int n, int pri, int ftype, int x, int y,
                                  u_char alp);
void DrawSPhotoFromSmallPhotoAreaAddr(uintptr_t addr, int n, int pri, int ftype,
                                      int x, int y, u_char alp);
void DrawSPhotoFromSmallPhotoArea2(int n, int pri, int ftype, int x, int y,
                                   int szw, int szh, u_char alp);
void DrawSPhotoFromSmallPhotoAreaAddr2(uintptr_t addr, int n, int pri, int ftype,
                                       int x, int y, int szw, int szh, int alp);

/* Full-size picture out of the decompression work area.  Both drive the
 * decompressor themselves: while it is still running they hand it another
 * slice and draw nothing, and only once CheckPhotoExpandEnd() is true does
 * the picture appear. */
void DrawPhotoFromWorkArea(int pri, int ftype, int x, int y,
                           int szw, int szh, u_char alp);
void DrawPhotoFromWorkAreaAddr(uintptr_t addr, int pri, int ftype, int x, int y,
                               int szw, int szh, u_char alp);

/* --------------------------------------------------------------------------
 *  Capture and storage.
 * ------------------------------------------------------------------------ */

/* Renders the live world into the photo scratch page: a 384x256 window of the
 * previous frame buffer squashed 2:1 into the 384x128 picture area. */
void DrawSpecialFurnPhoto(float photo_x, float photo_y);

/* Builds the album thumbnail in VRAM by drawing the captured 384x256 window
 * down into a 45x15 render target. */
void MakeSmallPhotoV(float x, float y);

/* Pulls a `mw` x `mh` rectangle of GS local memory at `addr_i` down into EE
 * memory at `addr_o`, starting at (mx, my) in the source.  `szfl` is dead. */
void CopyScreenToBuffer2(int addr_i, int addr_o, int szfl,
                         int mx, int my, int mw, int mh);

/* Compresses PHOTO_ENCODE_SIZE bytes at `addri` and files the result in slot
 * `n` of the album area at `addro`; CompPhotoFromWorkArea() is the same thing
 * with both addresses fixed. */
void CompressData(int addri, int addro, int n);
void CompPhotoFromWorkArea(int n);

/* Expands album slot `n` (255 = whatever ReqPhotoExpand() last asked for) from
 * `addri` into `addro`.  UncompressPhoto() only files the request. */
void UncompressData(uintptr_t addri, int n, int addro);
void UncompressPhoto(int n);

/* SLIDE_ENCODE_HEADER wrappers round the LZSS codec.  The encoder returns the
 * compression ratio, which is what CompressData() tests to decide whether the
 * lossless result was small enough to keep. */
float SlideEncodeHeader(u_char *base, u_char *addrs, int max_size);
void  SlideDecodeHeader(u_char *base, u_char *addrs);

/* --------------------------------------------------------------------------
 *  The photo phase's own drawing.
 * ------------------------------------------------------------------------ */

/* One frame of the photo presentation.  `fl` selects the stage -- 0 arms it,
 * 1 is the shutter flash, 2 the developed hold, 3 the fade out -- and
 * iPercent drives the vignette brightness in hundredths. */
void DispPhotoFrame1(int fl, float x, float y, int iPercent);

/* The photo frame itself: the fourteen border sprites round the picture. */
void DrawPhotoFrame(float x, float y);

/* Cross-fades a hint plate over the picture.  `sw` is the direction (0 in,
 * 1 hold, 2 out), photo_cnt the phase's own countdown, and bGradual selects
 * the four-way blur rather than a straight fade. */
void DrawPhotoFilterPK2(u_int sw, float x, float y, const SPRT_DAT *dat,
                        int photo_cnt, void *tex_adrs, int bGradual);

/* The hint-plate cross-fade against hint_dat[].  Exported but dead: no `jal`
 * site anywhere in the loadable segments. */
void DrawPhotoHinttex(u_int sw, float x, float y, int photo_cnt,
                      int photo_spno, u_int *tex_adrs);

#endif /* _INGAME_PHOTO_PHOTO_MAKE_H */
