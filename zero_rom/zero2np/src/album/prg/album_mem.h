/* ==========================================================================
 *  album/prg/album_mem.h
 *
 *  The album's memory-card staging buffer (album_mem.o).  Copying an album to
 *  or from a card needs a megabyte-scale block, and by the time the album is
 *  open every heap in the map is already spoken for -- so the load and save
 *  screens borrow one from the top of the 3D DMA packet ring, which is idle
 *  for as long as a card transfer is running.
 *
 *  The ring is PACKET3D_ADDR..PACKET2D_ADDR, 0x3d8600 bytes, handed to
 *  dmaVif1Init() by main.c as 0x1ec30 (126000) tags of 32 bytes.  Asking for
 *  `get_size` bytes shrinks it to (0x3d8600 - get_size) >> 5 tags and hands
 *  back the freed tail, which starts at PACKET2D_ADDR - get_size.
 *
 *  The lifecycle is the same in both screens -- album_load.o and album_save.o
 *  are source-level twins and call these four in the same order:
 *
 *      AlbumMemInit(size, __FILE__, __LINE__);   once, as the screen opens
 *      while (AlbumMemMain() == 0) { }           a frame per step, 1 = ready
 *      GetAlbumMemAddr()                         the buffer, for the transfer
 *      AlbumMemFree(__FILE__, __LINE__);         from the screen's OutReq
 *
 *  `file` / `line` are the caller's own __FILE__ / __LINE__ -- both entry
 *  points trace every call with them, which is how a leaked buffer was meant
 *  to be traced back to the screen that took it.  Whether the ROM wrapped the
 *  pair in a macro is not recoverable: the line numbers the callers pass are
 *  their own call sites either way, so only the functions are declared here.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _ALBUM_PRG_ALBUM_MEM_H
#define _ALBUM_PRG_ALBUM_MEM_H

#include "../../sdk/scetypes.h"                  /* u_int */

/* Claim `get_size` bytes off the top of the packet ring.  Nothing is reserved
 * yet -- this only records the request; AlbumMemMain() does the work.  A
 * request of ALBUM_MEM_AREA_SIZE or more asserts and is then honoured anyway,
 * which would leave the ring with no tags at all. */
void AlbumMemInit(u_int get_size, const char *file, int line);   /* 0x125f10 */

/* Drive the claim.  Returns 0 while the resize is still in flight and 1 once
 * the block is reserved and cleared, so a caller polls it a frame at a time. */
int AlbumMemMain(void);                                          /* 0x125fb0 */

/* The block, or NULL before AlbumMemMain() has reported 1. */
void *GetAlbumMemAddr(void);                                     /* 0x126060 */

/* Give the block up.  This clears the control block only -- see the note in
 * album_mem.c: it issues no resize, so the ring stays short until some later
 * AlbumMemMain() runs with the size back at 0. */
void AlbumMemFree(const char *file, int line);                   /* 0x126070 */

#endif /* _ALBUM_PRG_ALBUM_MEM_H */
