/* ==========================================================================
 *  system/iop/iop_stream.h
 *
 *  ADPCM streaming (iop_stream.c): two slots, each with a reader thread
 *  pulling interleaved packets off the disc into a ring buffer and a voice
 *  thread handing them to the SPU, paced by the SPU's own address interrupt.
 *
 *  Reconstructed from iopsys.irx (Feb 6 2004 prototype, SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_IOP_IOP_STREAM_H
#define _SYSTEM_IOP_IOP_STREAM_H

#include <stdint.h>

#include "iop_types.h"
#include "iop_snd_def.h"

/* One stream slot.  The bit flags divide into three groups: what the EE has
 * asked for (use / loop / pause / stop), what the file looks like (just_loop /
 * file_end), and where the two threads have got to (read_th / voice_th /
 * read_end / voice_end).  `ready` and `pre_load` are declared but never
 * written, and `play_ok` is what StreamPlay() raises to release the voice
 * thread from its wait.
 *
 * The ring cursors are `rb_read` (slot the reader fills next) and `rb_voice`
 * (slot the voice thread copies out next); `rb_read_diff` / `rb_voice_diff`
 * are the half-ring thresholds each thread uses to decide it is far enough
 * ahead or behind. */
typedef struct                      /* 0x15c */
{
    /* 0x000:0  */ unsigned int play_ok   : 1;
    /* 0x000:1  */ unsigned int ready     : 1;
    /* 0x000:2  */ unsigned int pre_load  : 1;
    /* 0x000:3  */ unsigned int file_end  : 1;
    /* 0x000:4  */ unsigned int use       : 1;
    /* 0x000:5  */ unsigned int loop      : 1;
    /* 0x000:6  */ unsigned int just_loop : 1;   /* loops on a packet boundary */
    /* 0x000:7  */ unsigned int read_end  : 1;
    /* 0x000:8  */ unsigned int voice_end : 1;
    /* 0x000:9  */ unsigned int read_th   : 1;
    /* 0x000:10 */ unsigned int voice_th  : 1;
    /* 0x000:11 */ unsigned int pause     : 1;
    /* 0x000:12 */ unsigned int stop      : 1;
    /* PORT: not in the ROM.  Set by StreamAbort() when the stream's threads
     * could not be got out, and read by StreamReleaseSub() to leak the ring
     * buffer instead of freeing it -- see the note on StreamAbort() below.
     * Takes a spare bit of the ROM's own bitfield word, so 0x15c stands. */
    /* 0x000:13 */ unsigned int abandoned : 1;
    /* 0x004 */ int          offset_sector;
    /* 0x008 */ int          read_th_idx;
    /* 0x00c */ int          voice_th_idx;
    /* 0x010 */ int          start_sector;
    /* 0x014 */ int          size;
    /* 0x018 */ int          block_offset;        /* position, in ADPCM blocks */
    /* 0x01c */ int          loop_start_block;
    /* 0x020 */ int          loop_end_block;
    /* 0x024 */ int          loop_start_fraction; /* block within the packet   */
    /* 0x028 */ int          loop_end_fraction;
    /* 0x02c */ unsigned int spu_loop_packet[2];  /* third buffer, per channel */
    /* 0x034 */ unsigned int spu_packet[2][2];    /* [channel][ping/pong]      */
    /* 0x044 */ int          interleave_byte;     /* one packet, one channel   */
    /* 0x048 */ int          rb_size;             /* bytes per ring slot       */
    /* 0x04c */ uintptr_t    rb_top;              /* PORT: was int -- an address */
    /* 0x050 */ char         rb_num;
    /* 0x051 */ char         rb_read;
    /* 0x052 */ char         rb_voice;
    /* 0x053 */ char         rb_read_diff;
    /* 0x054 */ char         rb_voice_diff;
    /* 0x055 */ char         core[2];
    /* 0x057 */ char         voice[2];
    /* 0x059 */ char         nchannel;
    /* 0x05a */ char         irq_core;            /* core whose IRQ paces this */
    /* 0x05b */ char         id;
    /* 0x05c */ char         file_name[256];
} STREAM_WRK;

/* Left non-static as the ROM does. */
extern STREAM_WRK stream_wrk[2];

/* ---- EE commands -------------------------------------------------------- */
void StreamCreate(void);
void StreamStart(STREAM_START *p);
void StreamPlay(STREAM_PLAY *p);
void StreamStop(STREAM_STOP *p);
void StreamPause(STREAM_PAUSE *p);
void StreamRestart(STREAM_RESTART *p);
void StreamVolSet(STREAM_SETVOL *p);
void StreamPitchSet(STREAM_SETPITCH *p);

/* Empty in this build -- the slot is released by the threads on their way out,
 * not by this. */
void StreamRelease(STREAM_RELEASE *p);

/* PORT: reachable here.  The ROM never dispatched to this (iopCommand() has no
 * case for REQ_STREAM_ABORT), and the port wired the case up because it is the
 * only recovery for a slot stuck in ST_STREAM_WAIT_END.  The body had to change
 * with it: the ROM's version kills both threads outright and releases, and the
 * host cannot kill a thread.  See the comment on the definition. */
void StreamAbort(STREAM_STOP *p);

/* ---- internals, non-static in the ROM but used only by iop_stream.c ------ */
void SetStreamLoopFlgSub(STREAM_WRK *stp, SPU_BLOCK_DATA *data, int id, int loop_flg);
void VoiceThreadExit(STREAM_WRK *stp);
void ReadThreadExit(STREAM_WRK *stp);

#endif /* _SYSTEM_IOP_IOP_STREAM_H */
