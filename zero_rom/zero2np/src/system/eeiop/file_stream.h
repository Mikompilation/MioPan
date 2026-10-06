/* ==========================================================================
 *  system/eeiop/file_stream.h
 *
 *  The EE half of the file streamer -- five entry points over RPC service 4,
 *  whose IOP half (system/iop/iop_load_stream.c) is fully ported.  Only
 *  playpss.a's my_strfile.c uses it, so a movie is the only thing in the game
 *  that streams a file rather than loading one.
 *
 *  Reconstructed; see file_stream.c.  Signatures are from ZERO2.MAP /
 *  functions.txt.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_EEIOP_FILE_STREAM_H
#define _SYSTEM_EEIOP_FILE_STREAM_H

#ifdef __cplusplus
extern "C" {
#endif

void FileStreamInit(void);                                          /* 0x276670 */

/* Open `file_no` as a stream of `ring_buf_num` slots of `one_buf_sector`
 * sectors each.  1 on success -- the ROM returns 1 unconditionally. */
int  FileStreamStart(int file_no, int ring_buf_num, int one_buf_sector);
                                                                    /* 0x276718 */

/* Ask for `size` bytes into `buff`; `size` must be a multiple of 2048.  With
 * `block_read` non-zero the call waits, otherwise it returns immediately and
 * FileStreamIsAct() reports when it has landed.  Returns the byte count the
 * IOP moved. */
int  FileStreamRead(void *buff, int size, int block_read);          /* 0x2767c8 */

/* Non-zero while a read is still in flight. */
int  FileStreamIsAct(void);                                         /* 0x276900 */

int  FileStreamStop(void);                                          /* 0x276928 */

#ifdef __cplusplus
}
#endif

#endif /* _SYSTEM_EEIOP_FILE_STREAM_H */
