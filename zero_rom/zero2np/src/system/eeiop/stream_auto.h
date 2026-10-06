/* ==========================================================================
 *  system/eeiop/stream_auto.h
 *
 *  Priority-scheduled streaming audio (stream_auto.c).  snd_stream.c has two
 *  slots; this is what decides which of the queued streams gets one.  A
 *  caller asks for a stream and gets back an id, not a slot: the request goes
 *  onto a wait queue sorted by priority, and StreamAutoPlayMain() moves the
 *  head of that queue into a free slot as slots free up.
 *
 *  Every entry point takes the id, and every one of them looks in both the
 *  play queue and the wait queue, so a caller never has to know whether its
 *  stream has actually started.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_EEIOP_STREAM_AUTO_H
#define _SYSTEM_EEIOP_STREAM_AUTO_H

#include "../../sdk/libvu0.h"       /* sceVu0FVECTOR */
#include "snd3d.h"                  /* SND_3D_SET */
#include "snd_def.h"                /* SOUND_INFO */

/* One queued or playing stream.  The 3D vectors are copied in, so the caller
 * need not keep its SND_3D_SET alive. */
typedef struct _STREAM_QUEUE        /* 0x60 */
{
    /* 0x00 */ sceVu0FVECTOR pos;
    /* 0x10 */ sceVu0FVECTOR vel;
    /* 0x20 */ sceVu0FVECTOR dir;
    /* 0x30 */ int   status;
    /* 0x34 */ int   priority;      /* lower number wins */
    /* 0x38:0 */ unsigned int s3d       : 1;
    /* 0x38:1 */ unsigned int effect    : 1;
    /* 0x38:2 */ unsigned int loop      : 1;
    /* 0x38:3 */ unsigned int use       : 1;
    /* 0x38:4 */ unsigned int playing   : 1;
    /* 0x38:5 */ unsigned int end       : 1;
    /* 0x38:6 */ unsigned int resume    : 1;  /* start as soon as preloaded */
    /* 0x38:7 */ unsigned int pause     : 1;
    /* 0x39:0 */ unsigned int reset_flg : 1;  /* restart from the top on requeue */
    /* 0x39:1 */ unsigned int first_flg : 1;  /* has not played yet */
    /* 0x39:2 */ unsigned int vel_flg   : 1;
    /* 0x39:3 */ unsigned int dir_flg   : 1;
    /* 0x3c */ int   reset_in_time;
    /* 0x40 */ int   in_time;
    /* 0x44 */ int   vol;
    /* 0x48 */ int   file_no;
    /* 0x4c */ int   header_file_no;
    /* 0x50 */ int   id;
    /* 0x54 */ int   offset;
    /* 0x58 */ float play_spd;
    /* 0x5c */ char  padding[4];     /* quadword tail, from the leading pos */
} STREAM_QUEUE;

int   StreamAutoGetOneWrkSize(void);
void *StreamAutoPlayInit(void *wrk_buffer, int num);
void  StreamAutoPlayMain(void);

void  StreamAutoEnable(void);
void  StreamAutoDisable(void);

/* The four request forms differ only in `play_flg` (start on its own once
 * preloaded) and `reset_flg` (restart from the top rather than resuming). */
int  StreamAutoPlay(int file_no, int header_file_no, int priority, int effect, int loop, int vol, int in_time, SND_3D_SET *s3s);
int  StreamAutoPlayNonReset(int file_no, int header_file_no, int priority, int effect, int loop, int vol, int in_time, int reset_in_time, SND_3D_SET *s3s, int start_sector);
int  StreamAutoPreload(int file_no, int header_file_no, int priority, int effect, int loop, int vol, int in_time, SND_3D_SET *s3s);
int  StreamAutoPreloadNonReset(int file_no, int header_file_no, int priority, int effect, int loop, int vol, int in_time, int reset_in_time, SND_3D_SET *s3s);

int  StreamAutoIsPreload(int id);
int  StreamAutoPreloadPlay(int id);
int  StreamAutoIsPlaying(int id);   /* 0x272ed0 */
int  StreamAutoIsAllStop(void);

void StreamAutoFade(int id, int target_vol, int time);
void StreamAutoFadeOut(int id, int fade_time);
void StreamAutoAllStop(void);
void StreamAutoAllPause(void);
void StreamAutoAllRestart(void);
int  StreamAutoPause(int id);
int  StreamAutoRestart(int id);

/* Caps how many of the two slots may play at once; 1 is the exclusive mode
 * the event system uses. */
void StreamAutoSetPlayNum(int iNum, int fade_time);
void StreamAutoSetExclusiveMode(int flg, int fade_time);

int   StreamAutoGetInfo(int id, int *num, int *interleave_byte, SOUND_INFO **info);
int   StreamAutoGetNowSector(int id);
float StreamAutoNowPlayPercentage(int id);
int   StreamAutoChangePlaySpeed(int id, float rate);
int   StreamAutoSetPosition(int id, float *pos);
int   StreamAutoSet3D(int id, SND_3D_SET *s3s);

void  PrintStreamAutoStatus(void);

#endif /* _SYSTEM_EEIOP_STREAM_AUTO_H */
