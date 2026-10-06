// FILE: /home/akira_koide/zero2np/src/system/eeiop/file_stream.c
//
// The EE half of the file streamer -- five functions over RPC service 4, whose
// IOP half is system/iop/iop_load_stream.c.
//
// It is the *other* way the game gets bytes off the disc.  An ordinary load
// (fileload.c, RPC 3) asks for a whole file and waits for it; this asks the IOP
// to keep a ring of sector buffers topped up from one file and then hands over
// slices of it on demand, so a movie can be read at the rate it is consumed
// rather than all at once.  playpss.a's my_strfile.c is the only caller, which
// makes a movie the only thing in the game that streams.
//
// There is no state here beyond a lock flag: the ring, the reader thread and
// the file position all live on the IOP.  This file is three RPC payloads and
// the marshalling for them.
//
// PORT: `strSTM_READ::ee_buf` is a pointer, 4 bytes on the PS2 and 8 here, and
// it travels through the RPC payload into the IOP's transfer path.  That path
// carried it as an `int` -- iopCommandLoadStm() read word 1 of the payload,
// CdvdStmRead() and RingBufTransEE() passed it on -- which truncated a real
// host pointer and DMA'd onto a wild address.  All three are widened to
// uintptr_t; see [[int-pointer-out-params-must-widen]].
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
// iopsys.a(file_stream.o), .text 0x276670..0x27699c; all five exports plus the
// four statics.

#include "file_stream.h"

#include "cddat.h"                  /* GetFileNameBuffer / StartSector / Size */
#include "../iop/iop_load_stream.h" /* strSTM_START / strSTM_READ / REQ_STM_* */
#include "../../common/utility2.h"  /* PRINT_ASSERT                           */
#include "../../sdk/sifrpc.h"

/* A sector is 2048 bytes and every request is a whole number of them. */
#define FILE_STREAM_SECTOR_SIZE     2048

/* bss 4c0ac0 -- the IOP's reply window.  Only the first word is ever read (the
 * byte count a read moved), but the RPC is set up for the whole buffer. */
static char             iop_file_stm_ret[64];
/* bss 4c0b00 -- the request window, big enough for the largest payload
 * (strSTM_START, 0x110) with room to spare. */
static char             iop_file_stm_arg[320];
/* bss 4c0c40 */
static sceSifClientData sif_cli_data_s;
/* sbss 3f50a0 -- set while a stream is open.  Written and never read; the ROM
 * has no accessor for it. */
static int              file_stream_lock;

/* --------------------------------------------------------------------------
 *  FileStreamInit  (0x276670)
 *
 *  Bind RPC service 4, spinning until the IOP has registered it.
 *
 *  The retry is the whole point: ee_iopInitSub() calls this immediately after
 *  the REQ_IOP_REBOOT round trip that creates the service, and the IOP may not
 *  have got there yet.  A bind that *fails* (rather than one that succeeds with
 *  nothing bound) is a different matter and hangs deliberately -- there is no
 *  disc without the IOP.
 * ------------------------------------------------------------------------ */
void FileStreamInit(void)                                               /* 20 */
{
    int i;

    do                                                                  /* 24 */
    {
        if (sceSifBindRpc(&sif_cli_data_s, 4, 0) < 0)                   /* 25 */
        {
            printf("error: sceSifBindRpc QUERY\n");                     /* 26 */
            for (;;)                                                    /* 27 */
            {
            }
        }

        /* Give the IOP a moment before asking again. */
        for (i = 0; i < 10000; i++)                                     /* 29 */
        {
        }
    } while (sif_cli_data_s.serve == NULL);                             /* 30 */

    file_stream_lock = 0;                                               /* 35 */
}                                                                       /* 36 */

/* --------------------------------------------------------------------------
 *  FileStreamStart  (0x276718)
 *
 *  Open `file_no` as a stream.  The IOP gets the file's name, start sector and
 *  size out of the CD table on this side -- it has the table too, but the ROM
 *  resolves here and sends the answer -- plus the shape of the ring it should
 *  keep topped up: `ring_buf_num` slots of `one_buf_sector` sectors each.
 *
 *  Returns 1 unconditionally; the caller's retry loop
 *  (`while (MyStrStart(...) == 0)`) therefore always takes one pass.
 * ------------------------------------------------------------------------ */
int FileStreamStart(int file_no, int ring_buf_num, int one_buf_sector)  /* 39 */
{
    strSTM_START *stm = (strSTM_START *)iop_file_stm_arg;               /* 41 */

    GetFileNameBuffer(file_no, stm->ld.file_name);                      /* 44 */

    stm->ld.start_sector = GetFileStartSector(file_no);                 /* 46 */
    stm->ld.ring_buf_num = ring_buf_num;                                /* 47 */
    stm->ld.one_buf_size = one_buf_sector * FILE_STREAM_SECTOR_SIZE;    /* 48 */
    stm->ld.size         = GetFileSize(file_no);                        /* 49 */

    sceSifCallRpc(&sif_cli_data_s, REQ_STM_START, 0,                    /* 52 */
                  stm, sizeof(iop_file_stm_arg),
                  iop_file_stm_ret, sizeof(iop_file_stm_ret), NULL, NULL);

    file_stream_lock = 1;                                               /* 57 */

    return 1;                                                           /* 59 */
}

/* --------------------------------------------------------------------------
 *  FileStreamRead  (0x2767c8)
 *
 *  Ask for `size` bytes into `buff`.  `size` must be a whole number of sectors
 *  and no read may already be in flight -- both are asserts rather than
 *  recoveries, because either one means the caller has lost track of its own
 *  request.
 *
 *  `block_read` picks the mode: non-zero waits for the bytes, zero posts the
 *  request and returns, leaving FileStreamIsAct() to report when it lands.
 *  my_strfile.c uses the second and polls.
 *
 *  The FlushCache() is what makes the destination visible to the SIF DMA; the
 *  return is the byte count the IOP actually moved, out of the reply window.
 * ------------------------------------------------------------------------ */
int FileStreamRead(void *buff, int size, int block_read)                /* 63 */
{
    strSTM_READ *stm = (strSTM_READ *)iop_file_stm_arg;                 /* 65 */

    if ((size & (FILE_STREAM_SECTOR_SIZE - 1)) != 0)                    /* 68 */
    {
        PRINT_ASSERT("FileStreamRead ReadSize Is Not Sector Align");    /* 69 */
    }

    if (FileStreamIsAct() != 0)                                         /* 71 */
    {
        PRINT_ASSERT("FileStreamRead Overlap Call");                    /* 72 */
    }

    stm->read_size = size;                                              /* 76 */
    stm->ee_buf    = buff;                                              /* 77 */

    FlushCache(0);                                                      /* 79 */

    if (block_read != 0)                                                /* 80 */
    {
        sceSifCallRpc(&sif_cli_data_s, REQ_STM_READ, 0,                 /* 81 */
                      stm, sizeof(iop_file_stm_arg),
                      iop_file_stm_ret, sizeof(iop_file_stm_ret), NULL, NULL);
    }
    else                                                                /* 85 */
    {
        sceSifCallRpc(&sif_cli_data_s, REQ_STM_READ, SIF_RPC_M_NOWAIT,  /* 86 */
                      stm, sizeof(iop_file_stm_arg),
                      iop_file_stm_ret, sizeof(iop_file_stm_ret), NULL, NULL);
    }

    return *(int *)iop_file_stm_ret;                                    /* 92 */
}

/* --------------------------------------------------------------------------
 *  FileStreamIsAct  (0x276900)
 *
 *  Non-zero while a read is still in flight.  Normalised to 0/1 rather than
 *  passed through, which is why the caller can test it against 1.
 * ------------------------------------------------------------------------ */
int FileStreamIsAct(void)                                               /* 95 */
{
    return (sceSifCheckStatRpc(&sif_cli_data_s.rpcd) != 0);             /* 97 */
}

/* --------------------------------------------------------------------------
 *  FileStreamStop  (0x276928)
 *
 *  Close the stream.  The wait first is not politeness: the IOP is about to
 *  tear down the ring the outstanding read is transferring out of.
 * ------------------------------------------------------------------------ */
int FileStreamStop(void)                                                /* 104 */
{
    while (sceSifCheckStatRpc(&sif_cli_data_s.rpcd) != 0)               /* 106 */
    {
    }

    sceSifCallRpc(&sif_cli_data_s, REQ_STM_STOP, 0,                     /* 108 */
                  iop_file_stm_arg, sizeof(iop_file_stm_arg),
                  iop_file_stm_ret, sizeof(iop_file_stm_ret), NULL, NULL);

    file_stream_lock = 0;                                               /* 113 */

    return 1;                                                           /* 115 */
}
