/* ==========================================================================
 *  system/iop/iop_pcmstream.h
 *
 *  Straight PCM streaming (iop_pcmstream.c): the IOP end of the EE's
 *  snd_pcmstream.c.  Two slots, each with its own reader thread and its own
 *  128 KB ring in IOP RAM.  Unlike iop_stream.c this uses no SPU voices --
 *  the data goes out through sceSdBlockTrans(), the SPU2's streaming DMA --
 *  so there is no pitch, no ADSR and no per-voice state, only a volume.
 *
 *  Reconstructed from iopsys.irx (Feb 6 2004 prototype, SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_IOP_IOP_PCMSTREAM_H
#define _SYSTEM_IOP_IOP_PCMSTREAM_H

#include <stdint.h>

#include "iop_snd_def.h"
#include "iop_types.h"              /* IOP_STREAM_RET, EEIOP_STREAM_STATUS */

/* PORT NOTE: `pause_phase` is a four-state handshake between the RPC handlers,
 * PCMStreamMain()'s volume ramp and the reader thread:
 *   0 running, 1 pause asked for, 2 paused, 3 restart asked for.
 * PCMStreamPause()/Restart() only ever move between 1/2 and 2/3, so a request
 * that lands mid-transition is folded into the current one rather than lost.
 *
 * That is the design; it does not run.  Nothing in the module calls
 * PCMStreamMain(), so nothing ever advances `pause_phase` past the state the
 * RPC handler sets.  See the note on its definition. */
typedef struct                      /* 0x138 */
{
    /* 0x000:0 */ unsigned int play_ok : 1;
    /* 0x000:1 */ unsigned int stop    : 1;
    /* 0x000:2 */ unsigned int use     : 1;
    /* 0x000:3 */ unsigned int loop    : 1;
    /* 0x000:4 */ unsigned int pause   : 1;
    /* 0x004 */ int   offset_sector;
    /* 0x008 */ char  file_name[256];
    /* 0x108 */ int   trans_core;       /* SPU block-transfer core, -1 = none */
    /* 0x10c */ int   read_th_idx;
    /* 0x110 */ int   start_sector;
    /* 0x114 */ int   size;
    /* 0x118 */ int   rb_size;          /* bytes per ring slot */
    /* 0x11c */ uintptr_t rb_top;          /* PORT: was int -- an address */
    /* 0x120 */ short rb_num;
    /* 0x122 */ char  rb_read;          /* slot the reader will fill next */
    /* 0x123 */ char  nchannel;
    /* 0x124 */ VOLSET vol;
    /* 0x128 */ char  id;
    /* 0x12c */ int   block_trans_sema; /* signalled by the SPU DMA interrupt */
    /* 0x130 */ int   pause_offset;
    /* 0x134 */ unsigned char pause_phase;
} PCM_STREAM_WRK;

/* Left non-static as the ROM does. */
extern PCM_STREAM_WRK pcm_stream_wrk[2];

void PCMStreamCreate(void);
void PCMStreamMain(void);

void PCMStreamInit(PCM_STREAM_INIT *p);         /* REQ_PCM_STREAMINIT    */
void PCMStreamStart(PCM_STREAM_START *p);       /* REQ_PCM_STREAMSTART   */
void PCMStreamPlay(PCM_STREAM_PLAY *p);         /* REQ_PCM_STREAMPLAY    */
void PCMStreamStop(PCM_STREAM_STOP *p);         /* REQ_PCM_STREAMSTOP    */
void PCMStreamPause(PCM_STREAM_PAUSE *p);       /* REQ_PCM_STREAMPAUSE   */
void PCMStreamRestart(PCM_STREAM_RESTART *p);   /* REQ_PCM_STREAMRESTART */
void PCMStreamVolSet(PCM_STREAM_SETVOL *p);     /* REQ_PCM_STREAMSETVOL  */

void SetSPU_PCMZeroBlock(PCM_STREAM_WRK *stp, int zero_buf);

#endif /* _SYSTEM_IOP_IOP_PCMSTREAM_H */
