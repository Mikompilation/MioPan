/* ==========================================================================
 *  miopan_moviestream.h  --  the movie's bytes, read straight off the host
 *
 *  PORT-ONLY.  This replaces the entire IOP streaming chain for the one thing
 *  in the game that streams a file.
 *
 *  WHAT IT REPLACES.  A movie is the only file the ROM streams rather than
 *  loads, and it does so through the deepest stack in the port:
 *
 *      my_strfile.c -> file_stream.c -> sceSifCallRpc(service 4)
 *                   -> iop_load_stream.c -> iop_ring_buf.c
 *                   -> a reader thread, two counting semaphores, and a SIF
 *                      DMA out of a ten-slot IOP-side ring
 *
 *  All of that exists to hide the latency of a real IOP reading a real DVD.
 *  There is no IOP and no DVD here: the bytes are in a local file, and the
 *  host RPC shim runs the "IOP" handler synchronously on the caller's own
 *  thread anyway -- so the ring, the threads and the semaphores buy nothing
 *  and each is a way for playback to wedge.  A movie that stops after its
 *  first packets has stalled in there.
 *
 *  WHAT IT DOES INSTEAD.  Opens the file once and reads it sequentially.  The
 *  caller (my_strfile.c) resolves every game-specific fact -- name, start
 *  sector, size -- and passes them in, so this module knows nothing about
 *  file numbers or the CD table, the same split MioPan_FileLoadServe() uses.
 *
 *  The read is resolved the way every other data file is: the named extracted
 *  file first, then the IMG_BD.BIN span at `start_sector`.
 *
 *  The ROM's RPC path is still compiled and still correct; my_strfile.c falls
 *  back to it only if the file cannot be resolved here.
 * ======================================================================== */

#ifndef MIOPAN_MOVIESTREAM_H
#define MIOPAN_MOVIESTREAM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Everything the reader needs, resolved by the caller. */
typedef struct
{
    const char *file_name;      /* PS2-style path, as GetFileNameBuffer gives */
    int         start_sector;   /* IMG_BD.BIN LBA, used if the name misses    */
    int         size;           /* byte size of the file                      */
} MioPan_MovieStreamReq;

/* Open a stream.  1 on success, 0 if the file could not be resolved -- in
 * which case the caller falls back to the ROM's path. */
int  MioPan_MovieStreamOpen(const MioPan_MovieStreamReq *req);

/* Read the next `size` bytes.  ALWAYS returns `size`, zero-filling past the
 * end of the file, because the ROM's reader does: MyStrRead() answering 0 for
 * "no more" would spin fillBuff()'s blocking read loop for ever, and the
 * 0x000001B9 terminator inside the data is what actually ends a movie. */
int  MioPan_MovieStreamRead(void *buf, int size);

void MioPan_MovieStreamClose(void);

/* Is a direct stream currently open? */
int  MioPan_MovieStreamIsOpen(void);

#ifdef __cplusplus
}
#endif

#endif /* MIOPAN_MOVIESTREAM_H */
