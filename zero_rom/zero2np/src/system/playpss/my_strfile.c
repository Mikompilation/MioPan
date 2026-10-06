// FILE: /home/akira_koide/zero2np/src/system/playpss/my_strfile.c
//
// The stream reader playpss.a is driven by.  Three functions over
// system/eeiop/file_stream.c: the movie player asks for a block, this posts one
// read to the IOP streamer and then answers "not yet" every frame until the
// RPC comes back.
//
// pss_block_read is what makes that work -- it is "a read is already in
// flight", so a caller that keeps asking for the same block does not queue a
// second request.
//
// PORT DEVIATION -- THE IOP STREAMING CHAIN IS BYPASSED BY DEFAULT.
//
// A movie is the only file the game streams rather than loads, and the ROM's
// way of doing it is the deepest stack in the port: an RPC to service 4, a
// ten-slot ring in "IOP" memory, a reader thread, two counting semaphores and
// a SIF DMA per block.  Every part of that exists to hide the latency of a
// real IOP reading a real DVD.  Neither is here -- the bytes are a local file,
// and the host RPC shim runs the IOP handler synchronously on the caller's own
// thread anyway (sceSifCheckStatRpc() always answers "nothing in flight"), so
// the ring and its threads buy nothing and each is a way for playback to
// wedge.
//
// So MyStrStart() opens the file directly (miopan/io/miopan_moviestream.h) and
// MyStrRead() reads it sequentially.  The ROM's bodies are still here and
// still correct: they run if the file cannot be resolved on the host.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
// playpss.a(my_strfile.o), .text 0x278e30..0x278eb0; all three exports.

#include "playpss.h"

#include "../eeiop/file_stream.h"
#include "../eeiop/cddat.h"                     /* GetFileNameBuffer / ...   */
#include "../../miopan/io/miopan_moviestream.h" /* the direct reader         */

#include <stdio.h>                              /* printf                    */

/* sbss 3f50b4 */
static int pss_block_read;

/* --------------------------------------------------------------------------
 *  MyStrStart  (0x278e30)
 * ------------------------------------------------------------------------ */
int MyStrStart(int file_no, int ring_buf_num, int one_buf_sector)       /* 16 */
{
    pss_block_read = 0;                                                 /* 17 */

    /* PORT: the direct reader first.  A compressed entry is left to the ROM's
     * path -- nothing streams one, and this reader hands over raw bytes. */
    if (cddatIsCmpFile(file_no) == 0)
    {
        MioPan_MovieStreamReq req;
        char                  name[256];

        GetFileNameBuffer(file_no, name);

        req.file_name    = name;
        req.start_sector = GetFileStartSector(file_no);
        req.size         = (int)GetFileSize(file_no);

        if (MioPan_MovieStreamOpen(&req) != 0)
        {
            return 1;
        }

        printf("movie: file %d not readable directly -- using the IOP stream path\n", file_no);
    }

    return FileStreamStart(file_no, ring_buf_num, one_buf_sector);      /* 18 */
}

/* --------------------------------------------------------------------------
 *  MyStrRead  (0x278e48)
 *
 *  Non-blocking: returns 0 while the read is still running and the byte count
 *  once it has landed.  playPss polls it.
 *
 *  PORT: the direct reader has no "still running" state -- the read completes
 *  in the call -- so it always answers `size`, which is what the ROM's reader
 *  ends up doing too once the RPC lands.
 * ------------------------------------------------------------------------ */
int MyStrRead(void *buff, int size)                                     /* 21 */
{
    int ret;

    if (MioPan_MovieStreamIsOpen() != 0)
    {
        return MioPan_MovieStreamRead(buff, size);
    }

    if (pss_block_read == 0)                                            /* 22 */
    {
        FileStreamRead(buff, size, 0);                                  /* 23 */
        pss_block_read = 1;                                             /* 24 */
    }

    ret = 0;

    if (FileStreamIsAct() == 0)                                         /* 27 */
    {
        pss_block_read = 0;                                             /* 30 */
        ret            = size;                                          /* 31 */
    }

    return ret;                                                         /* 33 */
}

/* --------------------------------------------------------------------------
 *  MyStrStop  (0x278e98)
 * ------------------------------------------------------------------------ */
int MyStrStop(void)
{
    if (MioPan_MovieStreamIsOpen() != 0)
    {
        MioPan_MovieStreamClose();
        return 1;
    }

    return FileStreamStop();                                            /* 37 */
}
