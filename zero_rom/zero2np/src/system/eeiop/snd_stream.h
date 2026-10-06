/* ==========================================================================
 *  system/eeiop/snd_stream.h
 *
 *  ADPCM streaming (snd_stream.c).  Two slots; each streams one interleaved
 *  multi-channel file off disc through a pair of SPU ring buffers that the
 *  IOP refills on an SPU IRQ.  Unlike snd_pcmstream.c the EE owns real SPU
 *  voices here, so a stream costs one voice per channel plus one of the two
 *  IRQ "cores" that trigger the refill.
 *
 *  The lifecycle runs HEADER_LOAD -> PRE_LOAD -> START -> PLAYING ->
 *  WAIT_END -> END -> NO_USE, driven by SndStreamMain() against the status
 *  the IOP reports back through GetStreamWrkRet().
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_EEIOP_SND_STREAM_H
#define _SYSTEM_EEIOP_SND_STREAM_H

#include "ee_iop.h"                 /* EEIOP_STREAM_STATUS via snd_def.h */
#include "snd3d.h"                  /* SND_3D_SET */
#include "snd_def.h"

typedef enum _SND_STREAM_ERR
{
    SND_STREAM_OK               = 0,
    SND_STREAM_ERR_IOPSEND      = 1,
    SND_STREAM_ERR_HEADER       = 2,
    SND_STREAM_ERR_NOT_USE      = 3,
    SND_STREAM_ERR_IN_USE       = 4,
    SND_STREAM_ERR_VOICE        = 5,
    SND_STREAM_ERR_SPU_MEM      = 6,
    SND_STREAM_ERR_WAIT_QUEUE   = 7
} SND_STREAM_ERR;

typedef enum _SND_STREAM_RET
{
    SND_STREAM_RET_NOT_USE    = 0,
    SND_STREAM_RET_PLAYING    = 1,
    SND_STREAM_RET_PRELOADING = 0,
    SND_STREAM_RET_PRELOAD_OK = 1
} SND_STREAM_RET;

/* PORT NOTE: `s3dhndl` and `header_buf` hold pointers, so offsets from 0x19c
 * on drift on the host.  `p` and `s` are the two IOP payloads and are sent in
 * place, which is why they lead. */
typedef struct _SND_STREAM_WRK      /* 0x1c0 */
{
    /* 0x000 */ STREAM_PLAY  p;
    /* 0x01c */ STREAM_START s;
    /* 0x158 */ SOUND_INFO   info[2];
    /* 0x190 */ int          status;        /* an EEIOP_STREAM_STATUS */
    /* 0x194:0 */ unsigned int header_ready : 1;
    /* 0x194:1 */ unsigned int pre_load_ok  : 1;
    /* 0x194:2 */ unsigned int play_flg     : 1;
    /* 0x194:3 */ unsigned int fade_stop    : 1;
    /* 0x194:4 */ unsigned int stop         : 1;
    /* 0x198 */ int    header_id;           /* fileload id of the HXD load  */
    /* 0x19c */ void  *s3dhndl;
    /* 0x1a0 */ void  *header_buf;
    /* 0x1a4 */ short  vol;
    /* 0x1a6 */ short  target_vol;
    /* 0x1a8 */ short  spd;
    /* 0x1aa */ short  pitch;
    /* 0x1ac */ short  target_pitch;
    /* 0x1ae */ short  pspd;
    /* 0x1b0 */ short  abort_cnt;           /* frames before a forced abort */
    /* 0x1b2 */ char   s3d[2];              /* per channel */
    /* 0x1b4 */ int    offset;              /* where playback ended         */
    /* 0x1b8 */ float  play_spd;
    /* 0x1bc */ int    file_no;
} SND_STREAM_WRK;

void SndStreamInit(int load_priority);
void SndStreamMain(void);

/* header_file_no == -0x21 means "the header is already in wrk->s", used by
 * the resume path; anything else is loaded through fileload.c. */
SND_STREAM_ERR SndStreamStart(int wrk_id, int file_no, int header_file_no, int offset);
SND_STREAM_ERR SndStreamStartHeaderOnMemory(int wrk_id, int file_no, void *header, int offset);
SND_STREAM_ERR SndStreamPlay(int wrk_id, int effect, int loop2, int vol, int in_time,
                             SND_3D_SET *s3s, float play_spd, int pitch);

void SndStreamFade(int wrk_id, int target_vol, int time);
void SndStreamFadePitch(int wrk_id, int pitch, int time);
void SndStreamFadeStop(int wrk_id, int time);
void SndStreamAllStop(void);
void SndStreamPause(int wrk_id);
void SndStreamRestart(int wrk_id);

int  SndStreamIsHeaderReady(int wrk_id);
int  SndStreamIsUse(int wrk_id);
int  SndStreamIsPreload(int wrk_id);

SND_STREAM_ERR SndStreamGetInfo(int wrk_id, int *nchannel, int *interleave_byte,
                                SOUND_INFO **info);
int  SndStreamGetEndOffset(int wrk_id);
int  SndStreamGetNowOffset(int wrk_id);

SND_STREAM_ERR SndStreamChangePlaySpeed(int wrk_id, float rate);
void SndStreamSetPosition(int wrk_id, float *pos);
void SndStreamSet3D(int wrk_id, SND_3D_SET *s3s);

char *SndStreamPrintStatus(EEIOP_STREAM_STATUS status);
void  SetStreamHeaderSub(SND_STREAM_WRK *wrk, HXD_HEADER *header);
int   SetIRQCore(SND_STREAM_WRK *wrk);

#endif /* _SYSTEM_EEIOP_SND_STREAM_H */
