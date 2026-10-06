/* ==========================================================================
 *  system/eeiop/snd_buffer.c
 *
 *  The SPU voice pool.  48 slots, 24 per SPU2 core, indexed as
 *  core * 24 + voice; the voice number itself comes from spu_voice.c, so a
 *  slot is really a mirror of an SPU2 voice plus the EE-side fade state the
 *  IOP does not track.
 *
 *  Everything a caller holds is the packed id (wrk_no << 16) | play_id.
 *  play_id is bumped on every successful claim, so a stale id resolves to
 *  NULL once its voice has been recycled and every entry point silently does
 *  nothing -- that is the whole lifetime model, and it is why the callers can
 *  keep an id around indefinitely (see CSND_BUF_PLAY in snd_buffer.h).
 *
 *  SndBufPlayMain() is the per-frame pump: it retires finished voices, steps
 *  the volume and pitch ramps, runs the fade-out-then-stop handshake, and
 *  pushes any change through to the IOP as REQ_SB_SETVOL / REQ_SB_SETPITCH.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include <stdio.h>

#include "snd_buffer.h"

#include "ee_iop.h"                 /* iopCommandRegister, CheckEndPointThrough */
#include "snd.h"                    /* SndCalcValue, SndGetFrameAddVol         */
#include "snd3d.h"
#include "spu_voice.h"              /* GetSPUVoiceCore, FreeSPUVoiceCore       */

#include "../../common/utility2.h"  /* PRINT_WARNING */

static SND_BUF_PLAYER snd_buf_player[SND_BUF_PLAYER_MAX];                    /* bss 4bde58 */

static void SndBufRelease(SND_BUF_PLAYER *sbp);
static void SndBufStopSub(SND_BUF_PLAYER *sbp);
static void SndBufStopSub2(SND_BUF_PLAYER *sbp);

void SndBufInit(void)
{
    for (int i = 0; i < SND_BUF_PLAYER_MAX; i++)                             /* 58 */
        snd_buf_player[i].use = 0;                                           /* 59 */
}                                                                            /* 61 */

/* 65 */
void SndBufAllStop(void)
{
    for (int i = 0; i < SND_BUF_PLAYER_MAX; i++)                             /* 69 */
    {
        if (snd_buf_player[i].use)                                           /* 71 */
            SndBufStopSub(&snd_buf_player[i]);                               /* 72 */
    }
}                                                                            /* 75 */

/* Hand the voice back to spu_voice.c.  Only a slot that owns its 3D handle
 * frees it -- SndBufPlay()'s `s3d_free` says whether the handle came in from
 * the caller (snd_bank.c keeps its own) or was created for this voice. */
static void SndBufRelease(SND_BUF_PLAYER *sbp)
{
    if (sbp->s3d != nullptr && sbp->s3d_free)                                /* 79 */
        Snd3DFreeWrk(sbp->s3d);                                              /* 80 */

    sbp->use = 0;                                                            /* 81 */

    FreeSPUVoiceCore(sbp->p.attr.core, sbp->p.voice_no);                     /* 83 */
}                                                                            /* 84 */

/* Room change / event cut: a looping voice is only silenced (the sample keeps
 * its SPU memory, since the loop point is still valid), while a one-shot is
 * released outright. */
/* 89 */
void SndBufAllStopLoopSnd(void)
{
    for (int i = 0; i < SND_BUF_PLAYER_MAX; i++)                             /* 93 */
    {
        if (snd_buf_player[i].use)                                           /* 95 */
        {
            if (snd_buf_player[i].p.attr.loop)                               /* 96 */
                SndBufStopSub(&snd_buf_player[i]);                           /* 97 */
            else                                                             /* 98 */
                SndBufRelease(&snd_buf_player[i]);                           /* 100 */
        }
    }
}                                                                            /* 104 */

/* 107 */
void SndBufAllPause(void)
{
    SOUND_BUF_PAUSE pause;

    for (int i = 0; i < SND_BUF_PLAYER_MAX; i++)                                 /* 112 */
    {
        /* A voice already on its way out is left alone: pausing it would
         * strand it mid-fade with nothing to finish the handshake. */
        if (snd_buf_player[i].use && snd_buf_player[i].fadestop == FADE_STOP_NONE) /* 115 */
        {
            pause.voice_no = snd_buf_player[i].p.voice_no;                   /* 116 */
            pause.core     = snd_buf_player[i].p.attr.core;                  /* 117 */

            iopCommandRegister(REQ_SB_PAUSE, (char *)&pause,
                               sizeof(SOUND_BUF_PAUSE));                     /* 119 */
        }
    }
}                                                                            /* 122 */

/* 124 */
void SndBufAllRestart(void)
{
    SOUND_BUF_RESTART restart;

    for (int i = 0; i < SND_BUF_PLAYER_MAX; i++)                                 /* 129 */
    {
        if (snd_buf_player[i].use &&
            snd_buf_player[i].fadestop == FADE_STOP_NONE)                    /* 132 */
        {
            restart.voice_no = snd_buf_player[i].p.voice_no;                 /* 133 */
            restart.core     = snd_buf_player[i].p.attr.core;                /* 134 */

            iopCommandRegister(REQ_SB_RESTART, (char *)&restart, sizeof(SOUND_BUF_RESTART)); /* 136 */
        }
    }
}                                                                            /* 139 */

/* --------------------------------------------------------------------------
 *  Handle <-> slot
 * ------------------------------------------------------------------------ */

static SND_BUF_PLAYER *GetSndBufPlayerFromID(int id)
{
    int             wrk_id  = id >> 16;                                      /* 145 */
    int             play_id = id & 0xffff;                                   /* 146 */
    SND_BUF_PLAYER *sbp     = &snd_buf_player[wrk_id];                       /* 147 */

    if (wrk_id >= SND_BUF_PLAYER_MAX)                                        /* 150 */
        return nullptr;

    if (sbp->use == 0)
        return nullptr;

    if (sbp->play_id != play_id)
        return nullptr;                                                      /* 155 */

    return sbp;                                                              /* 157 */
}                                                                            /* 158 */

static int SndBufGetID(int wrk_no, int play_id)
{
    return (wrk_no << 16) | (play_id & 0xffff);                              /* 162 */
}

/* --------------------------------------------------------------------------
 *  Claim and start
 * ------------------------------------------------------------------------ */

/* 172 */
int SndBufPlay(int adrs, int core, int effect, int vol, int bvol, int pitch,
               int bpitch, int pan, int fade_time, int loop, int type,
               void *s3d, int s3d_free, int adsr1, int adsr2,
               int loopstart, int loopend)
{
    VOICE_LOOP_SET  slp;

    if (vol > 0x3fff)                                                        /* 177 */
        PRINT_WARNING("SndBufPlay vol is over max\n", vol);                  /* 178 */

    int voice_no = GetSPUVoiceCore(core);                                    /* 188 */
    if (voice_no == -1)                                                      /* 189 */
    {
        printf("SndBufPlay() Cannot Get Voice\n");                           /* 190 */
        return CSND_BUF_PLAY_NO_ID;                                          /* 191 */
    }

    int wrk_no = core * VOICES_PER_SPU2_CORE + voice_no;                     /* 194 */
    SND_BUF_PLAYER *sbp = &snd_buf_player[wrk_no];                           /* 197 */

    sbp->use        = 1;                                                     /* 198 */
    sbp->fadestop   = FADE_STOP_NONE;                                        /* 200 */
    sbp->pause      = 0;                                                     /* 201 */

    sbp->p.voice_no = (char)voice_no;                                        /* 205 */
    sbp->p.adrs     = adrs;                                                  /* 206 */
    sbp->p.adsr1    = adsr1;                                                 /* 207 */
    sbp->p.adsr2    = adsr2;                                                 /* 208 */

    sbp->s3d        = s3d;                                                   /* 209 */
    sbp->s3d_free   = s3d_free;                                              /* 210 */

    sbp->pan          = pan;                                                 /* 213 */
    sbp->pitch        = pitch;                                               /* 216 */
    sbp->bpitch       = bpitch;                                              /* 217 */
    sbp->target_pitch = pitch;                                               /* 218 */
    sbp->pspd         = 0;                                                   /* 219 */
    sbp->cnt          = 0;                                                   /* 220 */

    sbp->bvol       = bvol;                                                  /* 223 */
    sbp->target_vol = vol;                                                   /* 224 */
    if (fade_time == 0)                                                      /* 225 */
    {
        sbp->vol = vol;
    }
    else                                                                     /* 227 */
    {
        sbp->vol = 0;
        sbp->spd = SndGetFrameAddVol(vol, 0, fade_time);          /* 229 */
    }

    SndCalcValue(vol, pan, bvol, sbp->pitch, sbp->bpitch, type, s3d,
                 &sbp->p.vol, &sbp->p.pitch, 1.f);                  /* 234 */

    sbp->p.attr.core   = core;                                               /* 237 */
    sbp->p.attr.effect = effect;                                             /* 238 */
    sbp->p.attr.loop   = loop;                                               /* 239 */
    sbp->p.attr.s3d    = (s3d != nullptr);                                   /* 240 */
    sbp->p.attr.type   = type;                                               /* 241 */

    /* +0x10 skips the ADPCM block header, so the compare in SndBufPlayMain()
     * is against the first sample rather than the block boundary. */
    sbp->loopend_next = loopend + 0x10;                                      /* 242 */

    if (iopCommandRegister(REQ_SB_PLAY, (char *)&sbp->p, sizeof(SOUND_BUF_PLAY)) == 0)                     /* 247 */
        return CSND_BUF_PLAY_NO_ID;

    sbp->play_id++;                                                          /* 248 */

    /* A loop point that the sample header did not already carry has to be
     * pushed separately -- REQ_SB_PLAY has no field for it. */
    if (loop)                                                                /* 250 */
    {
        if (loopstart > 0)                                                   /* 251 */
        {
            slp.voice_no  = sbp->p.voice_no;                                 /* 253 */
            slp.core      = sbp->p.attr.core;                                /* 254 */
            slp.loop_adrs = loopstart;                                       /* 255 */

            iopCommandRegister(REQ_VOICE_LOOP_SET, (char *)&slp, sizeof(VOICE_LOOP_SET));                      /* 256 */
        }
    }

    return SndBufGetID(wrk_no, sbp->play_id);                                /* 265 */
}                                                                            /* 270 */

/* 275 */
void SndBufferPrintStatus(void)
{
    for (int i = 0; i < SND_BUF_PLAYER_MAX; i++)                             /* 278 */
    {
        if (snd_buf_player[i].use)                                           /* 280 */
        {
            SND_BUF_PLAYER* sbp = &snd_buf_player[i];
            printf("<<%d>>\n", i);                                           /* 282 */
            printf("sbp->fadestop = %d\n", sbp->fadestop);                   /* 283 */
            printf("sbp->pause = %d\n", sbp->pause);                         /* 284 */
            printf("sbp->vol = %d\n", sbp->vol);                             /* 285 */
            printf("sbp->target_vol = %d\n", sbp->target_vol);               /* 286 */
            printf("sbp->play_id = %d\n", sbp->play_id);                     /* 287 */
        }
    }
}                                                                            /* 289 */

/* --------------------------------------------------------------------------
 *  Per-frame pump
 * ------------------------------------------------------------------------ */

/* 293 */
void SndBufPlayMain(void)
{
    short           pitch;
    VOLSET          volset;
    SOUND_BUF_SETVOL   set_vol;
    SOUND_BUF_SETPITCH set_pitch;

    for (int i = 0; i < SND_BUF_PLAYER_MAX; i++)                             /* 299 */
    {
        SND_BUF_PLAYER *sbp = &snd_buf_player[i];                            /* 300 */

        if (sbp->use && !sbp->pause)                                         /* 303 */
        {
            /* The IOP only reports the loop point being passed once the voice
             * has actually started, so the first ten frames are given away
             * rather than risk retiring a voice that has not begun. */
            if (sbp->cnt++ > 10 &&
                CheckEndPointThrough(sbp->p.attr.core, sbp->p.voice_no))     /* 307 */
            {
                SndBufRelease(sbp);                                          /* 310 */
                continue;                                                    /* 311 */
            }

            /* A looping voice cannot end by itself, so its stop has to be
             * forced through the raw voice command. */
            if (sbp->p.attr.loop)                                            /* 313 */
            {
                if (sbp->fadestop == FADE_STOP_WAIT)                         /* 314 */
                {
                    SndBufStopSub2(sbp);                                     /* 316 */
                    continue;                                                /* 317 */
                }
            }

            if (sbp->target_pitch != sbp->pitch)                             /* 323 */
            {
                int new_pitch = sbp->pitch + sbp->pspd;                      /* 325 */

                /* The product goes negative the moment the step crosses the
                 * target, which is the overshoot test in both directions. */
                if ((sbp->target_pitch - sbp->pitch) *
                    (sbp->target_pitch - new_pitch) <= 0)                    /* 327 */
                    sbp->pitch = sbp->target_pitch;
                else if (sbp->pspd != 0)
                    sbp->pitch = new_pitch;
                else
                    sbp->pitch = sbp->target_pitch;                          /* 332 */
            }

            if (sbp->target_vol != sbp->vol)                                 /* 338 */
            {
                int new_vol = sbp->vol + sbp->spd;                           /* 340 */

                if ((sbp->target_vol - sbp->vol) *
                    (sbp->target_vol - new_vol) <= 0)                        /* 342 */
                    sbp->vol = sbp->target_vol;
                else if (sbp->spd != 0)
                    sbp->vol = new_vol;
                else
                    sbp->vol = sbp->target_vol;                              /* 347 */
            }

            /* The ramp landing only promotes the request; the voice is not
             * stopped until the following frame, so it gets one last pass
             * through SndCalcValue() at its final volume. */
            if (sbp->fadestop == FADE_STOP_REQ && sbp->vol == sbp->target_vol) /* 353 */
            {
                sbp->fadestop = FADE_STOP_END;                               /* 355 */
            }                                                                /* 356 */
            else if (sbp->fadestop == FADE_STOP_END)                         /* 358 */
            {
                SndBufStopSub(sbp);                                          /* 359 */
                continue;                                                    /* 360 */
            }

            SndCalcValue(sbp->vol, sbp->pan, sbp->bvol, sbp->pitch,
                         sbp->bpitch, sbp->p.attr.type, sbp->s3d,
                         &volset, &pitch, 1.f);                     /* 364 */

            if (volset.l != sbp->p.vol.l || volset.r != sbp->p.vol.r)        /* 368 */
            {
                sbp->p.vol = volset;                                         /* 371 */

                set_vol.vol      = sbp->p.vol;                               /* 374 */
                set_vol.voice_no = sbp->p.voice_no;                          /* 375 */
                set_vol.core     = sbp->p.attr.core;                         /* 376 */

                iopCommandRegister(REQ_SB_SETVOL, (char *)&set_vol, sizeof(SOUND_BUF_SETVOL)); /* 378 */
            }

            if (pitch != sbp->p.pitch)                                       /* 383 */
            {
                sbp->p.pitch = pitch;

                set_pitch.pitch    = pitch;                                  /* 388 */
                set_pitch.voice_no = sbp->p.voice_no;                        /* 389 */
                set_pitch.core     = sbp->p.attr.core;                       /* 390 */

                iopCommandRegister(REQ_SB_SETPITCH, (char *)&set_pitch, sizeof(SOUND_BUF_SETPITCH)); /* 391 */
            }
        }
    }
}                                                                            /* 397 */

/* --------------------------------------------------------------------------
 *  Stop
 * ------------------------------------------------------------------------ */

/* 405 */
static void SndBufStopSub(SND_BUF_PLAYER *sbp)
{
    SOUND_BUF_STOP stop;

    if (sbp != nullptr)
    {
        stop.voice_no = sbp->p.voice_no;                                     /* 407 */
        stop.core     = sbp->p.attr.core;                                    /* 408 */

        if (iopCommandRegister(REQ_SB_STOP, (char *)&stop, sizeof(SOUND_BUF_STOP))) /* 411 */
            sbp->fadestop = FADE_STOP_WAIT;                                  /* 412 */
    }
}                                                                            /* 414 */

/* The blunt version: kills the voice outright rather than asking the sound
 * buffer to run its release, which is the only thing that gets a looping
 * sample to stop. */
/* 421 */
static void SndBufStopSub2(SND_BUF_PLAYER *sbp)
{
    VOICE_STOP stop2;

    if (sbp != nullptr)
    {
        stop2.voice_no = sbp->p.voice_no;                                    /* 423 */
        stop2.core     = sbp->p.attr.core;                                   /* 424 */

        iopCommandRegister(REQ_VOICE_STOP, (char *)&stop2, sizeof(VOICE_STOP)); /* 427 */
    }
}                                                                            /* 429 */

/* 435 */
void SndBufStop(int id)
{
    SndBufStopSub(GetSndBufPlayerFromID(id));
}                                                                            /* 437 */

/* --------------------------------------------------------------------------
 *  Per-handle control
 * ------------------------------------------------------------------------ */

/* 438 */
void SndBufPause(int id)
{
    SOUND_BUF_PAUSE pause;

    SND_BUF_PLAYER *sbp = GetSndBufPlayerFromID(id);                                         /* 443 */

    if (sbp != nullptr && sbp->pause == 0)                       /* 447 */
    {
        pause.voice_no = sbp->p.voice_no;                                    /* 452 */
        pause.core     = sbp->p.attr.core;                                   /* 453 */

        if (iopCommandRegister(REQ_SB_PAUSE, (char *)&pause,
                               sizeof(SOUND_BUF_PAUSE)))                     /* 457 */
            sbp->pause = 1;                                                  /* 458 */
    }
}                                                                            /* 461 */

/* 466 */
void SndBufRestart(int id)
{
    SOUND_BUF_RESTART restart;

    SND_BUF_PLAYER *sbp = GetSndBufPlayerFromID(id);                                         /* 471 */

    if (sbp != nullptr && sbp->pause != 0)                       /* 476 */
    {
        restart.voice_no = sbp->p.voice_no;                                  /* 477 */
        restart.core     = sbp->p.attr.core;

        if (iopCommandRegister(REQ_SB_RESTART, (char *)&restart,
                               sizeof(SOUND_BUF_RESTART)))                   /* 480 */
            sbp->pause = 0;                                                  /* 481 */
    }
}                                                                            /* 484 */

/* 490 */
void SndBufVolFade(int id, int vol, int frame)
{
    SND_BUF_PLAYER *sbp;

    sbp = GetSndBufPlayerFromID(id);                                         /* 493 */

    if (vol > 0x3fff)                                                        /* 494 */
        PRINT_WARNING("SndBufVolFade vol is over max\n", vol);

    if (sbp != nullptr)                                          /* 498 */
    {
        sbp->target_vol = vol;                                               /* 501 */
        sbp->spd        = SndGetFrameAddVol(vol, sbp->vol, frame);           /* 502 */
    }
}                                                                            /* 503 */

/* 508 */
void SndBufPitchSet(int id, int pitch)
{
    SND_BUF_PLAYER *sbp = GetSndBufPlayerFromID(id);                                         /* 511 */

    if (pitch > 0x3fff)                                                      /* 512 */
        PRINT_WARNING("SndBufPitchSet pitch is over max\n", pitch);

    if (sbp != nullptr)                                          /* 516 */
        sbp->pitch = pitch;
}                                                                            /* 518 */

/* 525 */
void SndBufFadeStop(int id, int time)
{
    SND_BUF_PLAYER *sbp = GetSndBufPlayerFromID(id);

    if (sbp != nullptr)                                          /* 527 */
    {
        sbp->target_vol = 0;                                                 /* 530 */
        sbp->spd        = SndGetFrameAddVol(0, sbp->vol, time);              /* 531 */
        sbp->fadestop   = FADE_STOP_REQ;                                     /* 532 */
    }
}                                                                            /* 533 */

/* 538 */
int SndBufIsPlaying(int id)
{
    return GetSndBufPlayerFromID(id) != nullptr;                 /* 541 */
}

/* 551 */
void SndBufSetPosition(int id, float *pos)
{
    SND_BUF_PLAYER *sbp = GetSndBufPlayerFromID(id);

    if (sbp != nullptr)                                          /* 554 */
    {
        if (sbp->s3d != nullptr)                                           /* 557 */
            Snd3DSetPosition(sbp->s3d, pos);                                 /* 558 */
        else                                                                 /* 559 */
            printf("SndBufSetPosition() This Voice Is Not 3D Sound!!!\n");   /* 560 */
    }
}                                                                            /* 562 */

/* 567 */
void SndBufFadePitch(int id, int pitch, int time)
{
    SND_BUF_PLAYER *sbp = GetSndBufPlayerFromID(id);

    if (sbp != nullptr)                                          /* 570 */
    {
        if (time != 0)                                                       /* 573 */
            sbp->pspd = SndGetFrameAddVol(pitch, sbp->pitch, time);          /* 574 */
        else                                                                 /* 575 */
            sbp->pitch = pitch;                                              /* 576 */

        sbp->target_pitch = pitch;                                           /* 578 */
    }
}                                                                            /* 579 */

/* 584 */
void SndBufSet3D(int id, SND_3D_SET *s3s)
{
    SND_BUF_PLAYER *sbp = GetSndBufPlayerFromID(id);

    if (sbp != nullptr)                                          /* 587 */
    {
        if (sbp->s3d != nullptr)                                           /* 590 */
            snd3DSetSET(sbp->s3d, s3s);                                      /* 591 */
        else                                                                 /* 592 */
            printf("SndBufSet3D() This Voice Is Not 3D Sound!!!\n");         /* 593 */
    }
}                                                                            /* 595 */
