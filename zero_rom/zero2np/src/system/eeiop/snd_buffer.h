/* ==========================================================================
 *  system/eeiop/snd_buffer.h
 *
 *  Sound-buffer management (snd_buffer.c): the pool of 48 SPU voices that
 *  one-shot and looping samples play through.  A voice is claimed by
 *  SndBufPlay(), which returns a packed id -- (wrk_no << 16) | play_id -- and
 *  every other entry point resolves that id back to its slot, refusing the
 *  call if the slot has since been recycled.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_EEIOP_SND_BUFFER_H
#define _SYSTEM_EEIOP_SND_BUFFER_H

#include "snd_def.h"                /* SOUND_BUF_PLAY */
#include "snd3d.h"                  /* SND_3D_SET */

/* No play id.  The ROM seeds CSND_BUF_PLAY::play_id with this rather than 0,
 * because 0 is a valid buffer handle.  SndBufPlay() returns it on failure. */
#define CSND_BUF_PLAY_NO_ID 0x300000

#define VOICES_PER_SPU2_CORE    24
#define NUM_SPU2_CORES          2

/* 24 voices per SPU2 core, two cores. */
#define SND_BUF_PLAYER_MAX      VOICES_PER_SPU2_CORE * NUM_SPU2_CORES

/* Where a slot is in the fade-out-then-stop handshake.  REQ is raised by
 * SndBufFadeStop(); SndBufPlayMain() promotes it to END once the ramp lands,
 * stops the voice on the next pass and parks it in WAIT until the IOP reports
 * the loop point has been passed. */
enum
{
    FADE_STOP_NONE = 0,
    FADE_STOP_REQ  = 1,
    FADE_STOP_END  = 2,
    FADE_STOP_WAIT = 3
};

/* One voice.  `p` is the REQ_SB_PLAY payload and is handed to the IOP as-is,
 * which is why it sits first; everything after it is EE-side bookkeeping.
 *
 * PORT NOTE: `s3d` holds a pointer, 4 bytes on target and 8 here, so every
 * offset from 0x18 on drifts on the host.  The comments are the ROM's. */
typedef struct _SND_BUF_PLAYER      /* 0x34 */
{
    /* 0x00 */ SOUND_BUF_PLAY p;
    /* 0x14 */ void         *s3d;           /* Snd3DCreateWrk() handle       */
    /* 0x18 */ short         play_id;       /* bumped on every claim         */
    /* 0x1a */ short         vol;
    /* 0x1c */ short         target_vol;
    /* 0x1e */ short         bvol;          /* per-sample base volume        */
    /* 0x20 */ short         spd;           /* volume step per frame         */
    /* 0x22 */ short         pitch;
    /* 0x24 */ short         bpitch;        /* per-sample base pitch         */
    /* 0x26 */ short         target_pitch;
    /* 0x28 */ short         pspd;          /* pitch step per frame          */
    /* 0x2a:0 */ unsigned char use      : 1;
    /* 0x2a:1 */ unsigned char s3d_free : 1; /* free the 3D handle on release */
    /* 0x2a:2 */ unsigned char pause    : 1;
    /* 0x2b */ char          fadestop;
    /* 0x2c */ short         pan;
    /* 0x2e */ short         cnt;           /* frames since the voice started */
    /* 0x30 */ int           loopend_next;
} SND_BUF_PLAYER;

void SndBufInit(void);
void SndBufPlayMain(void);
void SndBufferPrintStatus(void);

/* Claims a free voice on `core` and starts it.  Returns the packed play id,
 * or CSND_BUF_PLAY_NO_ID if no voice was free or the IOP refused the command. */
int  SndBufPlay(int adrs, int core, int effect, int vol, int bvol, int pitch,
                int bpitch, int pan, int fade_time, int loop, int type,
                void *s3d, int s3d_free, int adsr1, int adsr2,
                int loopstart, int loopend);

void SndBufStop(int id);
/* Fades the buffer out over `time` frames, then stops it. */
void SndBufFadeStop(int id, int time);
void SndBufPause(int id);
void SndBufRestart(int id);
int  SndBufIsPlaying(int id);

void SndBufAllStop(void);
void SndBufAllStopLoopSnd(void);
void SndBufAllPause(void);
void SndBufAllRestart(void);

/* Ramp the buffer's volume / pitch to `vol` / `pitch` over `time` frames. */
void SndBufVolFade(int id, int vol, int time);
void SndBufFadePitch(int id, int pitch, int time);
void SndBufPitchSet(int id, int pitch);

void SndBufSetPosition(int id, float *pos);
void SndBufSet3D(int id, SND_3D_SET *s3s);

/* A held sound-buffer handle.  Subclasses only differ by which pool they
 * allocate from; the shared state is the one id.
 *
 * NOTE ON PLACEMENT: the ROM declares this in **common/zero2_util.h**, not
 * here.  Every expansion of it in the build -- n_plyr_camera.o, photo_dat.o,
 * player.o and sp_chance.o -- carries `SOL common/zero2_util.h`, and
 * `snd_buffer.h` never appears as a SOL anywhere.  The line numbers annotated
 * below are that file's and are correct as they stand (ctor 43/44, Fade 45/47,
 * PitchFade 49/50, IsPlaying 52, Stop 56/57/58); only the home is wrong.
 *
 * Left here for now: zero2_util.h would have to see the SndBuf* prototypes,
 * which are declared in this header, so the move is a two-file untangle rather
 * than a cut-and-paste and nothing depends on it. */
struct CSND_BUF_PLAY                /* 0x4 */
{
    /* 0x0 */ int play_id;

    CSND_BUF_PLAY() { play_id = CSND_BUF_PLAY_NO_ID; }                           /* 44 */

    void Fade(int vol, int time)
    {
        SndBufVolFade(play_id, vol, time);                                       /* 47 */
    }

    void PitchFade(int pitch, int time)
    {
        SndBufFadePitch(play_id, pitch, time);                                   /* 50 */
    }

    int IsPlaying(void) { return play_id != CSND_BUF_PLAY_NO_ID; }               /* 52 */

    /* Fades out over `time` frames and drops the handle.  Note it releases the
     * id unconditionally, so a second Stop() on the same object is a no-op
     * against SndBufFadeStop(CSND_BUF_PLAY_NO_ID). */
    void Stop(int time)
    {
        SndBufFadeStop(play_id, time);                                           /* 57 */
        play_id = CSND_BUF_PLAY_NO_ID;                                           /* 58 */
    }
};

struct CPLYR_SND_BUF_PLAY : CSND_BUF_PLAY   /* 0x4 */
{
};

#endif /* _SYSTEM_EEIOP_SND_BUFFER_H */
