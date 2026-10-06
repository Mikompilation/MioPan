/* ==========================================================================
 *  system/eeiop/snd_pcmstream.c
 *
 *  Straight PCM streaming.  The EE only owns the state machine and the volume
 *  ramp; the IOP does the reading and the mixing, and reports back through
 *  GetPCMStreamWrkRet().  The two status words are deliberately separate: the
 *  EE's `status` is what the game asked for, the IOP's is what has actually
 *  happened, and SndPCMStreamMain() is the place they are reconciled.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include <stdio.h>

#include "snd_pcmstream.h"

#include "cddat.h"                  /* GetFileStartSector / Size / NameBuffer */
#include "ee_iop.h"
#include "snd.h"                    /* SndGetFrameAddVol, SndAddFadeVol       */

#include "../../common/utility2.h"  /* PRINT_ASSERT */

SND_PCM_STREAM_WRK snd_pcm_stream_wrk[2];                                    /* data 35e9c0 */

/* 38 */
void SndPCMStreamInit(void)
{
    int i;

    iopCommandRegister(REQ_PCM_STREAMCREATE, (char *)0, 0);                  /* 42 */

    for (i = 0; i < 2; i++)                                                  /* 46 */
    {
        snd_pcm_stream_wrk[i].status   = ST_STREAM_NO_USE;                   /* 47 */
        snd_pcm_stream_wrk[i].s.wrk_id = i;                                  /* 48 */
        snd_pcm_stream_wrk[i].p.wrk_id = i;
    }
}                                                                            /* 51 */

/* 55 */
SND_PCM_STREAM_ERR SndPCMStreamStart(int wrk_id, int file_no, int offset)
{
    SND_PCM_STREAM_WRK *wrk = &snd_pcm_stream_wrk[wrk_id];                   /* 56 */

    SndPCMStreamInitWrk(wrk, file_no);                                       /* 58 */

    wrk->s.start_sector = GetFileStartSector(wrk->file_no);                  /* 60 */
    GetFileNameBuffer(wrk->file_no, wrk->s.file_name);                       /* 63 */
    wrk->s.size         = GetFileSize(wrk->file_no);                         /* 65 */

    wrk->pause = 0;                                                          /* 66 */

    /* Streaming wants the drive in its low-latency read mode. */
    SetCDReadMode(0);                                                        /* 70 */

    if (iopCommandRegister(REQ_PCM_STREAMSTART, (char *)&wrk->s,
                           sizeof(PCM_STREAM_START)) == 0)                   /* 73 */
        return SND_PCM_STREAM_ERR_IOPSEND;                                   /* 80 */

    printf("PCMStream[%d] IOP_WRK START\n", wrk->s.wrk_id);                  /* 74 */

    return SND_PCM_STREAM_OK;                                                /* 76 */
}                                                                            /* 82 */

/* 86 */
void SndPCMStreamFade(int wrk_id, int target_vol, int time)
{
    SND_PCM_STREAM_WRK *wrk = &snd_pcm_stream_wrk[wrk_id];                   /* 87 */

    wrk->target_vol = target_vol;
    wrk->spd        = SndGetFrameAddVol(target_vol, wrk->vol, time);         /* 90 */
}                                                                            /* 91 */

/* 95 */
SND_PCM_STREAM_ERR SndPCMStreamPlay(int wrk_id, int loop2, int vol, int in_time)
{
    SND_PCM_STREAM_WRK *wrk = &snd_pcm_stream_wrk[wrk_id];                   /* 96 */

    if (wrk->status == ST_STREAM_NO_USE)                                     /* 98 */
    {
        printf("SndPCMStreamPlay() This Wrk Is Not Used\n");                 /* 99 */
        return SND_PCM_STREAM_ERR_NOT_USE;                                   /* 100 */
    }

    if (in_time != 0)                                                        /* 105 */
    {
        wrk->vol = 0;                                                        /* 106 */
        /* Dead store: SndPCMStreamFade() immediately recomputes the same
         * value from the same inputs (vol is 0 either way). */
        wrk->spd = SndGetFrameAddVol(vol, 0, in_time);                       /* 107 */

        SndPCMStreamFade(wrk_id, vol, in_time);                              /* 108 */
    }                                                                        /* 109 */
    else
    {
        wrk->vol = wrk->target_vol = vol;                                    /* 110 */
    }

    wrk->p.vol = wrk->vol;                                                   /* 113 */

    if (iopCommandRegister(REQ_PCM_STREAMPLAY, (char *)&wrk->p,
                           sizeof(PCM_STREAM_PLAY)) == 0)                    /* 116 */
        return SND_PCM_STREAM_ERR_IOPSEND;                                   /* 118 */

    wrk->play_flg = 1;                                                       /* 117 */

    return SND_PCM_STREAM_OK;                                                /* 121 */
}                                                                            /* 122 */

/* 127 */
void SndPCMStreamMain(void)
{
    int                 wi;
    SND_PCM_STREAM_WRK *wrk;

    for (wi = 0; wi < 2; wi++)                                               /* 132 */
    {
        wrk = &snd_pcm_stream_wrk[wi];                                       /* 133 */

        if (wrk->status != ST_STREAM_NO_USE)                                 /* 136 */
        {
            if (wrk->status == ST_STREAM_START)                              /* 139 */
            {
                /* The IOP reaching PLAYING means the ring buffer is primed.
                 * A Play() that arrived before that only set play_flg, so it
                 * is honoured here instead. */
                if (GetPCMStreamWrkRet(wi)->status == ST_STREAM_PLAYING)     /* 141 */
                {
                    wrk->pre_load_ok = 1;                                    /* 142 */

                    if (wrk->play_flg)                                       /* 145 */
                        wrk->status = ST_STREAM_PLAYING;
                }
            }                                                                /* 150 */
            else if (wrk->status == ST_STREAM_PLAYING)
            {
                if (wrk->target_vol != wrk->vol)                             /* 152 */
                {
                    PCM_STREAM_SETVOL sts;

                    wrk->vol = SndAddFadeVol(wrk->vol, wrk->target_vol,
                                             wrk->spd);                      /* 154 */

                    sts.wrk_id = wi;
                    sts.vol    = wrk->vol;                                   /* 158 */

                    iopCommandRegister(REQ_PCM_STREAMSETVOL, (char *)&sts,
                                       sizeof(PCM_STREAM_SETVOL));           /* 159 */
                }

                if (GetPCMStreamWrkRet(wi)->status == ST_STREAM_END)         /* 163 */
                    wrk->status = ST_STREAM_END;
            }                                                                /* 166 */
            else if (wrk->status == ST_STREAM_END)
            {
                PCM_STREAM_INIT sts;

                sts.wrk_id = wi;
                iopCommandRegister(REQ_PCM_STREAMINIT, (char *)&sts,
                                   sizeof(PCM_STREAM_INIT));                 /* 169 */

                /* Back to the ordinary read mode now that nothing streams. */
                SetCDReadMode(1);                                            /* 170 */

                wrk->status = ST_STREAM_NO_USE;                              /* 171 */
            }
        }
    }
}                                                                            /* 174 */

/* 180 */
SND_PCM_STREAM_ERR SndPCMStreamInitWrk(SND_PCM_STREAM_WRK *wrk, int file_no)
{
    if (wrk->status != ST_STREAM_NO_USE)                                     /* 181 */
    {
        printf("This PCMStream File Is Used!\n");                            /* 182 */
        return SND_PCM_STREAM_ERR_IN_USE;                                    /* 185 */
    }

    wrk->file_no     = file_no;                                              /* 186 */
    wrk->status      = ST_STREAM_START;                                      /* 187 */
    wrk->pre_load_ok = 0;                                                    /* 188 */
    wrk->play_flg    = 0;                                                    /* 190 */

    return SND_PCM_STREAM_OK;                                                /* 191 */
}

/* 197 */
void SndPCMStreamStop(int wrk_id)
{
    PCM_STREAM_STOP stop;

    if (snd_pcm_stream_wrk[wrk_id].status != ST_STREAM_NO_USE)               /* 198 */
    {
        stop.wrk_id = wrk_id;                                                /* 201 */
        iopCommandRegister(REQ_PCM_STREAMSTOP, (char *)&stop,
                           sizeof(PCM_STREAM_STOP));                         /* 206 */
    }
}                                                                            /* 209 */

/* 213 */
void SndPCMStreamAllStop(void)
{
    SndPCMStreamStop(0);                                                     /* 217 */
    SndPCMStreamStop(1);
}                                                                            /* 219 */

/* 224 */
void SndPCMStreamPause(int wrk_id)
{
    SND_PCM_STREAM_WRK *wrk = &snd_pcm_stream_wrk[wrk_id];
    PCM_STREAM_PAUSE    pause;

    if (wrk->status != ST_STREAM_NO_USE && wrk->pause == 0)                  /* 225 */
    {
        wrk->pause = 1;                                                      /* 228 */

        pause.wrk_id = wrk_id;                                               /* 229 */
        iopCommandRegister(REQ_PCM_STREAMPAUSE, (char *)&pause,
                           sizeof(PCM_STREAM_PAUSE));                        /* 232 */
    }
}                                                                            /* 238 */

/* 242 */
void SndPCMStreamRestart(int wrk_id)
{
    SND_PCM_STREAM_WRK *wrk = &snd_pcm_stream_wrk[wrk_id];
    PCM_STREAM_RESTART  restart;

    if (wrk->status != ST_STREAM_NO_USE && wrk->pause != 0)                  /* 243 */
    {
        wrk->pause = 0;                                                      /* 246 */

        restart.wrk_id = wrk_id;                                             /* 247 */
        iopCommandRegister(REQ_PCM_STREAMRESTART, (char *)&restart,
                           sizeof(PCM_STREAM_RESTART));                      /* 250 */
    }
}                                                                            /* 257 */

int SndPCMStreamIsUse(int wrk_id)
{
    return snd_pcm_stream_wrk[wrk_id].status != ST_STREAM_NO_USE;            /* 265 */
}

/* 271 */
int SndPCMStreamIsPreload(int wrk_id)
{
    /* ROM BUG, reproduced: the test is inverted -- it fires the assert for
     * every slot that IS in use, which is every slot this is ever called on.
     * The message says "This Wrk Is Not Used", so the intended test was
     * `== ST_STREAM_NO_USE`. */
    if (snd_pcm_stream_wrk[wrk_id].status != ST_STREAM_NO_USE)               /* 275 */
        PRINT_ASSERT("SndPCMStreamPreLoad() This Wrk Is Not Used");          /* 276 */

    return snd_pcm_stream_wrk[wrk_id].pre_load_ok != 0;                      /* 281 */
}
