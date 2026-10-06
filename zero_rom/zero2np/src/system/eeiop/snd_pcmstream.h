/* ==========================================================================
 *  system/eeiop/snd_pcmstream.h
 *
 *  Straight PCM streaming (snd_pcmstream.c).  Two slots, each a file streamed
 *  off disc by the IOP with no SPU voice management on the EE side at all --
 *  only the volume ramp and the state handshake live here.  This is the path
 *  the movie player's audio uses; snd_stream.c is the ADPCM one.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_EEIOP_SND_PCMSTREAM_H
#define _SYSTEM_EEIOP_SND_PCMSTREAM_H

#include "snd_def.h"

typedef enum _SND_PCM_STREAM_ERR
{
    SND_PCM_STREAM_OK              = 0,
    SND_PCM_STREAM_ERR_IOPSEND     = 1,
    SND_PCM_STREAM_ERR_HEADER      = 2,
    SND_PCM_STREAM_ERR_NOT_USE     = 3,
    SND_PCM_STREAM_ERR_IN_USE      = 4,
    SND_PCM_STREAM_ERR_WAIT_QUEUE  = 5
} SND_PCM_STREAM_ERR;

typedef enum _SND_PCM_STREAM_RET
{
    SND_PCM_STREAM_RET_NOT_USE    = 0,
    SND_PCM_STREAM_RET_PLAYING    = 1,
    SND_PCM_STREAM_RET_PRELOADING = 0,
    SND_PCM_STREAM_RET_PRELOAD_OK = 1
} SND_PCM_STREAM_RET;

/* `p` and `s` are the two IOP command payloads and are sent in place, which
 * is why they sit at the front. */
typedef struct _SND_PCM_STREAM_WRK  /* 0x128 */
{
    /* 0x000 */ PCM_STREAM_PLAY  p;
    /* 0x004 */ PCM_STREAM_START s;
    /* 0x114 */ int              status;    /* an EEIOP_STREAM_STATUS */
    /* 0x118:0 */ unsigned int pre_load_ok : 1;
    /* 0x118:1 */ unsigned int play_flg    : 1;
    /* 0x118:2 */ unsigned int pause       : 1;
    /* 0x11c */ int   file_no;
    /* 0x120 */ short vol;
    /* 0x122 */ short target_vol;
    /* 0x124 */ short spd;
} SND_PCM_STREAM_WRK;

/* Left non-static as the ROM does. */
extern SND_PCM_STREAM_WRK snd_pcm_stream_wrk[2];

void SndPCMStreamInit(void);
void SndPCMStreamMain(void);

SND_PCM_STREAM_ERR SndPCMStreamStart(int wrk_id, int file_no, int offset);
SND_PCM_STREAM_ERR SndPCMStreamPlay(int wrk_id, int loop2, int vol, int in_time);
SND_PCM_STREAM_ERR SndPCMStreamInitWrk(SND_PCM_STREAM_WRK *wrk, int file_no);

void SndPCMStreamStop(int wrk_id);
void SndPCMStreamAllStop(void);
void SndPCMStreamPause(int wrk_id);
void SndPCMStreamRestart(int wrk_id);
void SndPCMStreamFade(int wrk_id, int target_vol, int time);

int  SndPCMStreamIsUse(int wrk_id);
int  SndPCMStreamIsPreload(int wrk_id);

#endif /* _SYSTEM_EEIOP_SND_PCMSTREAM_H */
