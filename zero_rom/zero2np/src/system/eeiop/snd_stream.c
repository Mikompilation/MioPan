/* ==========================================================================
 *  system/eeiop/snd_stream.c
 *
 *  ADPCM streaming.  Two slots, each holding one SPU voice per channel and
 *  one of the two IRQ cores; the IOP refills the SPU ring buffers from that
 *  IRQ and reports its own view of the state back through GetStreamWrkRet().
 *
 *  Three things are worth knowing before reading the rest:
 *
 *    - the EE's `status` and the IOP's are separate.  SndStreamMain() is the
 *      only place they meet, and every transition out of PRE_LOAD, PLAYING
 *      and WAIT_END is gated on what the IOP reports.
 *    - a looping stream that does not loop on a block boundary needs a third
 *      SPU buffer per channel (the "loop packet"), so SetStreamStartSub()
 *      allocates four interleave units instead of two for those.
 *    - stopping is asynchronous.  SndStreamStop() only asks; WAIT_END counts
 *      `abort_cnt` down and forces a REQ_STREAM_ABORT if the IOP has not
 *      come back within 600 frames.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "snd_stream.h"

#include "cddat.h"
#include "ee_iop.h"
#include "fileload.h"
#include "hxd.h"
#include "snd.h"
#include "snd3d.h"
#include "spu_mem.h"
#include "spu_voice.h"

#include "../../common/utility2.h"  /* PRINT_ASSERT, PRINT_WARNING */

static SND_STREAM_WRK snd_stream_wrk[2];                                     /* bss 4c0c80 */
static char           header_buf_64[2][2048];                                /* bss 4c1000 */
static int            irq_core_source[2];                                    /* sbss 3f50a8 */
static int            snd_stream_load_priority;                              /* sbss 3f50b0 */

static SND_STREAM_ERR SndStreamInitWrk(SND_STREAM_WRK *wrk, int file_no, int offset);
static void           SetStreamStartSub(SND_STREAM_WRK *wrk);
static void           StreamWrkRelease(SND_STREAM_WRK *wrk);
static void           SndStreamSet3DSub(SND_STREAM_WRK *wrk, SND_3D_SET *s3s);
static void           SndStreamFadeSub(int wi);
static void           SndStreamStop(int wrk_id);

/* 91 */
void SndStreamInit(int load_priority)
{
    snd_stream_load_priority = load_priority;                                /* 94 */

    iopCommandRegister(REQ_STREAM_CREATE, nullptr, 0);              /* 97 */

    for (int i = 0; i < 2; i++)                                              /* 102 */
    {
        snd_stream_wrk[i].status     = ST_STREAM_NO_USE;                     /* 103 */
        snd_stream_wrk[i].s.wrk_id   = i;                                    /* 104 */
        snd_stream_wrk[i].p.wrk_id   = i;
        snd_stream_wrk[i].header_buf = header_buf_64[i];                     /* 106 */
    }

    irq_core_source[0] = 0;                                                  /* 109 */
    irq_core_source[1] = 0;                                                  /* 110 */
}                                                                            /* 112 */

/* 115 */
SND_STREAM_ERR SndStreamStartHeaderOnMemory(int wrk_id, int file_no, void *header, int offset)
{
    SND_STREAM_WRK *wrk = &snd_stream_wrk[wrk_id];                           /* 116 */
    SND_STREAM_ERR  ret;

    ret = SndStreamInitWrk(wrk, file_no, offset);                            /* 117 */
    if (ret != SND_STREAM_OK)                                                /* 120 */
        return ret;

    CheckHXDData((HXD_HEADER *)header, 0);                                   /* 125 */
    SetStreamHeaderSub(wrk, (HXD_HEADER *)header);                           /* 129 */
    SetStreamStartSub(wrk);                                                  /* 131 */

    if (iopCommandRegister(REQ_STREAM_START, (char *)&wrk->s, sizeof(STREAM_START)) == 0)                /* 134 */
        return SND_STREAM_ERR_IOPSEND;                                       /* 136 */

    wrk->status       = ST_STREAM_PRE_LOAD;                                  /* 137 */
    wrk->header_ready = 1;                                                   /* 138 */

    return SND_STREAM_OK;                                                    /* 141 */
}                                                                            /* 143 */

/* 149 */
SND_STREAM_ERR SndStreamStart(int wrk_id, int file_no, int header_file_no, int offset)
{
    SND_STREAM_WRK *wrk = &snd_stream_wrk[wrk_id];
    SND_STREAM_ERR  ret;

    ret = SndStreamInitWrk(wrk, file_no, offset);                            /* 150 */
    if (ret != SND_STREAM_OK)                                                /* 158 */
        return ret;

    /* -0x21 means the caller has already filled in the header fields, so
     * there is nothing to load and the start can go straight out. */
    if (header_file_no == -0x21)                                             /* 159 */
    {
        SetStreamStartSub(wrk);                                              /* 163 */

        if (iopCommandRegister(REQ_STREAM_START, (char *)&wrk->s,
                               sizeof(STREAM_START)) == 0)                   /* 164 */
            return SND_STREAM_ERR_IOPSEND;                                   /* 166 */

        wrk->status       = ST_STREAM_PRE_LOAD;                              /* 168 */
        wrk->header_ready = 1;                                               /* 169 */
    }                                                                        /* 170 */
    else
    {
        wrk->header_id = FileLoadReqEE(header_file_no, wrk->header_buf,
                                       snd_stream_load_priority,
                                       (FILE_LOAD_CALLBACK)0, (void *)0);    /* 172 */
    }

    return SND_STREAM_OK;                                                    /* 176 */
}                                                                            /* 182 */

/* 187 */
void SndStreamFade(int wrk_id, int target_vol, int time)
{
    SND_STREAM_WRK *wrk = &snd_stream_wrk[wrk_id];

    if (target_vol > 0x3fff)                                                 /* 191 */
        PRINT_WARNING("SndStreamPlay vol is over max\n", target_vol);        /* 192 */

    /* A fade-out already in flight owns the ramp; letting a caller re-target
     * it would strand the stop. */
    if (wrk->fade_stop != 0)                                                 /* 196 */
    {
        printf("in fade stop, so cannot fade\n");                            /* 197 */
        return;
    }

    wrk->target_vol = target_vol;                                            /* 200 */
    wrk->spd        = SndGetFrameAddVol(target_vol, wrk->vol, time);         /* 201 */
}                                                                            /* 202 */

/* 205 */
int SndStreamIsHeaderReady(int wrk_id)
{
    if (snd_stream_wrk[wrk_id].header_ready == 0)                            /* 206 */
        return 0;

    return GetStreamWrkRet(wrk_id)->status == ST_STREAM_PLAYING;             /* 208, 213 */
}                                                                            /* 214 */

/* 220 */
SND_STREAM_ERR SndStreamPlay(int wrk_id, int effect, int loop2, int vol, int in_time,
                             SND_3D_SET *s3s, float play_spd, int pitch)
{
    int             i;
    int             j;
    int             loop;
    SND_STREAM_WRK *wrk = &snd_stream_wrk[wrk_id];                           /* 223 */

    if (vol > 0x3fff)                                                        /* 226 */
        PRINT_WARNING("SndStreamPlay vol is over max\n", vol);               /* 227 */

    if (wrk->status == ST_STREAM_NO_USE)                                     /* 235 */
        return SND_STREAM_ERR_NOT_USE;

    if (wrk->header_ready == 0)                                              /* 240 */
        return SND_STREAM_ERR_HEADER;

    if (GetStreamWrkRet(wrk_id)->status != ST_STREAM_PLAYING)                /* 245 */
    {
        printf("SndSteamPlay() Not Ready\n");                                /* 246 */
        return SND_STREAM_ERR_HEADER;                                        /* 247 */
    }

    if (in_time == 0)                                                        /* 253 */
    {
        wrk->vol = vol;
    }
    else
    {
        wrk->vol = 0;                                                        /* 255 */
        wrk->spd = SndGetFrameAddVol(vol, 0, in_time);                       /* 257 */
    }

    wrk->target_vol   = vol;                                                 /* 259 */
    wrk->play_spd     = play_spd;                                            /* 260 */
    wrk->pitch        = pitch;                                               /* 262 */
    wrk->target_pitch = pitch;
    wrk->pspd         = 0;

    SndStreamSet3DSub(wrk, s3s);                                             /* 267 */

    loop = wrk->info[0].attr.loop;                                           /* 270 */

    /* Every channel has to agree about looping -- they share one ring
     * refill, so a disagreement would desynchronise them. */
    for (i = 1; i < wrk->s.nchannel; i++)                                    /* 272 */
    {
        if (loop != (int)wrk->info[i].attr.loop)                             /* 273 */
        {
            printf("******************************\n");                      /* 274 */
            printf("File[%s] Loop Script Illegal!\n", GetFileName(wrk->file_no));  /* 275 */
            printf("******************************\n");                      /* 276 */
        }
    }

    wrk->p.irq_core = SetIRQCore(wrk);                                       /* 283 */

    for (i = 0; i < wrk->s.nchannel; i++)                                    /* 287 */
    {
        wrk->p.voice[i] = (char)GetSPUVoiceCore(wrk->p.attr[i].core);        /* 289 */

        /* Partial failure has to unwind: the IRQ core and every voice taken
         * so far go back before the error is reported. */
        if (wrk->p.voice[i] == -1)                                           /* 290 */
        {
            printf("SndStreamPlay() Cannot Get Voice\n");                    /* 293 */

            irq_core_source[wrk->p.irq_core] = 0;                            /* 296 */

            for (j = 0; j < i; j++)                                          /* 298 */
                FreeSPUVoiceCore(wrk->p.attr[j].core, wrk->p.voice[j]);      /* 299 */

            return SND_STREAM_ERR_VOICE;                                     /* 301 */
        }

        SndCalcValue(wrk->vol, wrk->info[i].pan, wrk->info[i].vol,
                     wrk->pitch, wrk->info[i].pitch, wrk->info[i].attr.type,
                     wrk->s3dhndl, &wrk->p.vol[i], &wrk->p.pitch,
                     wrk->play_spd);                                         /* 305 */

        wrk->p.attr[i].loop   = loop & loop2;                                /* 320 */
        wrk->p.attr[i].effect = wrk->info[i].attr.effect & effect;           /* 323 */
        wrk->p.adsr1[i]       = wrk->info[i].adsr1;                          /* 326 */
        wrk->p.adsr2[i]       = wrk->info[i].adsr2;                          /* 327 */
    }

    if (iopCommandRegister(REQ_STREAM_PLAY, (char *)&wrk->p, sizeof(STREAM_PLAY)) == 0)                        /* 333 */
        return SND_STREAM_ERR_IOPSEND;                                       /* 335 */

    wrk->play_flg = 1;                                                       /* 336 */

    return SND_STREAM_OK;                                                    /* 339 */
}                                                                            /* 340 */

/* 346 */
void SndStreamMain(void)
{
    int             wi;
    SND_STREAM_WRK *wrk;
    STREAM_ABORT    abort;

    for (wi = 0; wi < 2; wi++)                                               /* 351 */
    {
        wrk = &snd_stream_wrk[wi];                                           /* 352 */

        switch (wrk->status)                                                 /* 353 */
        {
        case ST_STREAM_HEADER_LOAD:
            if (FileLoadIsEnd(wrk->header_id))                               /* 357 */
            {
                CheckHXDData((HXD_HEADER *)wrk->header_buf, 0);              /* 359 */
                SetStreamHeaderSub(wrk, (HXD_HEADER *)wrk->header_buf);      /* 361 */
                SetStreamStartSub(wrk);                                      /* 363 */

                if (iopCommandRegister(REQ_STREAM_START, (char *)&wrk->s,
                                       sizeof(STREAM_START)))                /* 366 */
                {
                    wrk->status       = ST_STREAM_PRE_LOAD;                  /* 368 */
                    wrk->header_ready = 1;                                   /* 369 */
                }
            }
            break;                                                           /* 372 */

        case ST_STREAM_PRE_LOAD:
            /* The IOP has filled the ring, so the stream is startable; fall
             * straight into the START case so a Play() that already arrived
             * takes effect this frame. */
            if (GetStreamWrkRet(wi)->status == ST_STREAM_PLAYING)            /* 378 */
            {
                wrk->status      = ST_STREAM_START;                          /* 379 */
                wrk->pre_load_ok = 1;                                        /* 380 */
                goto case_start;
            }
            break;

        case ST_STREAM_START:
        case_start:
            if (wrk->play_flg)                                               /* 435 */
                wrk->status = ST_STREAM_PLAYING;                             /* 437 */
            break;                                                           /* 440 */

        case ST_STREAM_PLAYING:
            if (GetStreamWrkRet(wi)->status == ST_STREAM_NO_USE)             /* 391 */
            {
                /* Ran off the end by itself. */
                printf("<<<<<<<<<<<<<AUTO END STOP[%d]>>>>>>>>>>>>.\n", wi); /* 393 */
                StreamWrkRelease(wrk);
            }
            else
            {
                if (wrk->fade_stop != 0 && wrk->target_vol == wrk->vol)      /* 403 */
                    SndStreamStop(wi);                                       /* 404 */

                SndStreamFadeSub(wi);                                        /* 406 */
            }
            break;

        case ST_STREAM_WAIT_END:
            /* The IOP has been asked to stop and has not answered; after 600
             * frames the stream is taken away from it outright.
             *
             * ROM BUG on the receiving side, confirmed against iopsys.irx:
             * iopCommand() has no case for REQ_STREAM_ABORT (14) at all -- its
             * jump-table slot is the switch's break target -- so the command
             * does nothing AND its 4-byte payload is never skipped.  `wrk_id`
             * is then read as the next command id: 0 reads as IOP_COM_END and
             * drops the rest of the frame's queue, 1 reads as REQ_IOP_REBOOT.
             * The handler it should reach, StreamAbort() in iop_stream.c, is
             * written but has zero references anywhere in the module, so the
             * case was simply never wired up.  It survived because this is a
             * 10-second timeout on an error path.  Left as found; the fix
             * belongs on the IOP side. */
            if (--wrk->abort_cnt == -1)                                      /* 410 */
            {
                abort.wrk_id = wi;                                           /* 413 */
                PRINT_WARNING("SndStream[%d] Is Abort!!", wi);               /* 416 */
                iopCommandRegister(REQ_STREAM_ABORT, (char *)&abort, sizeof(STREAM_ABORT));  /* 418 */
            }

            if (GetStreamWrkRet(wi)->status == ST_STREAM_NO_USE)             /* 424 */
                StreamWrkRelease(wrk);                                       /* 427 */
            break;                                                           /* 428 */

        case ST_STREAM_END:
            StreamWrkRelease(wrk);                                           /* 433 */
            break;
        }
    }
}                                                                            /* 454 */

/* 458 */
char *SndStreamPrintStatus(EEIOP_STREAM_STATUS status)
{
    switch (status)
    {
    case ST_STREAM_HEADER_LOAD: return "ST_STREAM_HEADER_LOAD";              /* 460 */
    case ST_STREAM_PRE_LOAD:    return "ST_STREAM_PRE_LOAD";                 /* 462 */
    case ST_STREAM_START:       return "ST_STREAM_START";                    /* 464 */
    case ST_STREAM_PLAYING:     return "ST_STREAM_PLAYING";                  /* 466 */
    case ST_STREAM_WAIT_END:    return "ST_STREAM_WAIT_END";                 /* 468 */
    case ST_STREAM_END:         return "ST_STREAM_END";                      /* 470 */
    default:                    return "ST_STREAM_NO_USE";                   /* 475 */
    }
}

/* TEMP PROBE: status of a play slot, for stream_auto.c's stall dump. */
const char *MioPan_SndStreamStatusName(int wrk_id)
{
    if (wrk_id < 0 || wrk_id >= 2)
        return "?";
    return SndStreamPrintStatus((EEIOP_STREAM_STATUS)snd_stream_wrk[wrk_id].status);
}

/* 482 */
static void SndStreamFadeSub(int wi)
{
    int             ch;
    short           pitch;
    VOLSET          volset;
    int             vol_change;
    int             pitch_change;
    SND_STREAM_WRK *wrk = &snd_stream_wrk[wi];
    STREAM_SETVOL   set_vol;
    STREAM_SETPITCH set_pitch;

    vol_change   = 0;                                                        /* 486 */
    pitch_change = 0;                                                        /* 487 */

    if (wrk->target_vol != wrk->vol)                                         /* 488 */
        wrk->vol = SndAddFadeVol(wrk->vol, wrk->target_vol, wrk->spd);       /* 491 */

    if (wrk->target_pitch != wrk->pitch)                                     /* 492 */
        wrk->pitch = SndAddFadeVol(wrk->pitch, wrk->target_pitch,
                                   wrk->pspd);                               /* 496 */

    for (ch = 0; ch < wrk->s.nchannel; ch++)                                 /* 497 */
    {
        SndCalcValue(wrk->vol, wrk->info[ch].pan, wrk->info[ch].vol,
                     wrk->pitch, wrk->info[ch].pitch, wrk->info[ch].attr.type,
                     wrk->s3dhndl, &volset, &pitch, wrk->play_spd);          /* 501 */

        if (volset.l != wrk->p.vol[ch].l || volset.r != wrk->p.vol[ch].r)    /* 503 */
        {
            wrk->p.vol[ch] = volset;
            vol_change     = 1;                                              /* 509 */
        }

        if (pitch != wrk->p.pitch)                                           /* 510 */
        {
            wrk->p.pitch = pitch;                                            /* 511 */
            pitch_change = 1;                                                /* 514 */
        }
    }                                                                        /* 515, 516 */

    /* One command for the whole stream rather than one per channel: the
     * payload already carries both volumes. */
    if (vol_change)                                                          /* 521 */
    {
        for (ch = 0; ch < wrk->s.nchannel; ch++)                             /* 523 */
            set_vol.vol[ch] = wrk->p.vol[ch];                                /* 524 */

        set_vol.wrk_id = wi;                                                 /* 534 */
        iopCommandRegister(REQ_STREAM_SETVOL, (char *)&set_vol,
                           sizeof(STREAM_SETVOL));                           /* 536 */
    }

    if (pitch_change)                                                        /* 538 */
    {
        set_pitch.pitch  = pitch;
        set_pitch.wrk_id = wi;
        iopCommandRegister(REQ_STREAM_SETPITCH, (char *)&set_pitch,
                           sizeof(STREAM_SETPITCH));                         /* 540 */
    }
}                                                                            /* 542 */

/* 548 */
void SetStreamHeaderSub(SND_STREAM_WRK *wrk, HXD_HEADER *header)
{
    wrk->s.nchannel        = header->num;                                    /* 550 */
    wrk->s.interleave_byte = header->interleave_byte;                        /* 553 */

    memcpy(wrk->info, header + 1, wrk->s.nchannel * sizeof(SOUND_INFO));     /* 557 */

    /* The loop has to span more than one refill unit, or the IOP would be
     * asked to seek backwards inside the buffer it is filling. */
    if (wrk->info[0].attr.loop &&
        wrk->info[0].loopend - wrk->info[0].loopstart <=
        (wrk->s.interleave_byte >> 4) * 2)                                   /* 562 */
        PRINT_ASSERT("Loop Info Is Illegal");                                /* 563 */
}

/* 573 */
static void SetStreamStartSub(SND_STREAM_WRK *wrk)
{
    wrk->s.start_sector = GetFileStartSector(wrk->file_no);                  /* 573 */
    wrk->s.size         = GetFileSize(wrk->file_no);                         /* 575 */
    GetFileNameBuffer(wrk->file_no, wrk->s.file_name);                       /* 578 */

    /* A resume offset has to leave at least 64 sectors of file behind it,
     * and a file under 128 KB can never be resumed at all. */
    if (wrk->s.size < 0x20000)                                               /* 587 */
        wrk->s.offset = 0;                                                   /* 589 */
    else if (wrk->s.offset >= (wrk->s.size >> 11) - 0x40)
        wrk->s.offset = 0;

    wrk->s.loop_start_block    = wrk->info[0].loopstart;                     /* 593 */
    wrk->s.loop_end_block      = wrk->info[0].loopend;                       /* 594 */
    wrk->s.loop_start_fraction = wrk->info[0].loopstart %
                                 (wrk->s.interleave_byte >> 4);              /* 597 */
    wrk->s.loop_end_fraction   = wrk->info[0].loopend %
                                 (wrk->s.interleave_byte >> 4);              /* 598 */

    int need_loop_packet = (wrk->s.loop_start_fraction != 0 || wrk->s.loop_end_fraction != 0);  /* 599 */

    for (int i = 0; i < wrk->s.nchannel; i++)                                    /* 602 */
    {
        /* Looping off a block boundary needs a fourth interleave unit to
         * splice the wrap through, and the play buffer then starts halfway
         * into the allocation. */
        if (need_loop_packet && wrk->info[0].attr.loop)                      /* 600, 603 */
        {
            wrk->s.spu_loop_packet[i] = (unsigned int)(uintptr_t)GetSPUMemory(wrk->s.interleave_byte * 4); /* 605 */
            wrk->s.spu_packet[i][0] = wrk->s.spu_loop_packet[i] +
                                      wrk->s.interleave_byte * 2;            /* 609 */
        }                                                                    /* 611 */
        else
        {
            wrk->s.spu_loop_packet[i] = 0;
            wrk->s.spu_packet[i][0] = (unsigned int)(uintptr_t)GetSPUMemory(wrk->s.interleave_byte * 2); /* 616 */
        }

        wrk->s.spu_packet[i][1] = wrk->s.spu_packet[i][0] +
                                  wrk->s.interleave_byte;                    /* 620 */
    }
}                                                                            /* 624 */

/* 632 */
static SND_STREAM_ERR SndStreamInitWrk(SND_STREAM_WRK *wrk, int file_no, int offset)
{
    if (wrk->status != ST_STREAM_NO_USE)                                     /* 632 */
    {
        printf("This Stream File Is Used!\n");                         /* 633 */
        return SND_STREAM_ERR_IN_USE;                                        /* 634 */
    }

    wrk->status       = ST_STREAM_HEADER_LOAD;                               /* 637 */
    wrk->header_ready = 0;                                                   /* 640 */
    wrk->pre_load_ok  = 0;                                                   /* 641 */
    wrk->play_flg     = 0;                                                   /* 642 */
    wrk->fade_stop    = 0;                                                   /* 643 */
    wrk->stop         = 0;                                                   /* 644 */
    wrk->file_no      = file_no;                                             /* 645 */
    wrk->s3dhndl      = nullptr;                                             /* 646 */
    wrk->header_id    = -1;                                                  /* 647 */
    wrk->offset       = 0;                                                   /* 648 */

    for (int i = 0; i < 2; i++)                                              /* 650 */
    {
        wrk->p.voice[i]           = -1;                                      /* 652 */
        wrk->s.spu_packet[i][0]   = 0;                                       /* 654 */
        wrk->s.spu_loop_packet[i] = 0;                                       /* 655 */
    }

    wrk->s.offset = offset;                                                  /* 657 */

    return SND_STREAM_OK;                                                    /* 659 */
}                                                                            /* 660 */

/* 665 */
int SetIRQCore(SND_STREAM_WRK *wrk)
{
    for (int i = 0; i < 2; i++)                                                  /* 667 */
    {
        if (irq_core_source[i] == 0)                                         /* 668 */
        {
            irq_core_source[i] = 1;                                          /* 669 */
            return i;                                                        /* 670 */
        }
    }

    printf("SetIRQCore() theres is no irq_core_source!\n");                  /* 674 */
    for (;;)                                                                 /* 675 */
        ;
}

/* 681 */
static void StreamWrkRelease(SND_STREAM_WRK *wrk)
{
    int i;

    /* Every voice has to have passed its end point before the SPU memory
     * under it can be handed back, so a slot that is not there yet simply
     * goes back to END and is retried next frame. */
    for (i = 0; i < wrk->s.nchannel; i++)                                    /* 685 */
    {
        if (wrk->p.voice[i] >= 0)                                            /* 686 */
        {
            if (CheckEndPointThrough(wrk->p.attr[i].core,
                                     wrk->p.voice[i]) == 0)                  /* 687 */
            {
                wrk->status = ST_STREAM_END;                                 /* 690 */
                return;
            }
        }
    }

    if (wrk->header_ready)                                                   /* 696 */
    {
        for (i = 0; i < wrk->s.nchannel; i++)                                /* 698 */
        {
            /* The loop packet is the base of the allocation when there is
             * one, so freeing it frees the play buffer with it. */
            if (wrk->s.spu_loop_packet[i] != 0)                              /* 700 */
                ReleaseSPUMemory((void *)(uintptr_t)wrk->s.spu_loop_packet[i]); /* 701 */
            else if (wrk->s.spu_packet[i][0] != 0)                           /* 702 */
                ReleaseSPUMemory((void *)(uintptr_t)wrk->s.spu_packet[i][0]);   /* 703 */
        }

        /* The voices and the IRQ core only exist once Play() ran. */
        if (wrk->play_flg)                                                   /* 707 */
        {
            irq_core_source[wrk->p.irq_core] = 0;                            /* 709 */

            if (wrk->s3dhndl != (void *)0)                                   /* 713 */
                Snd3DFreeWrk(wrk->s3dhndl);                                  /* 714 */

            for (i = 0; i < wrk->s.nchannel; i++)                            /* 717 */
            {
                if (wrk->p.voice[i] >= 0)                                    /* 719 */
                    FreeSPUVoiceCore(wrk->p.attr[i].core, wrk->p.voice[i]);  /* 720 */
            }
        }
    }

    wrk->status = ST_STREAM_NO_USE;                                          /* 726 */
    wrk->offset = GetStreamWrkRet(wrk->s.wrk_id)->offset;                    /* 727 */
}                                                                            /* 729 */

/* 733 */
static void SndStreamSet3DSub(SND_STREAM_WRK *wrk, SND_3D_SET *s3s)
{
    for (int i = 0; i < wrk->s.nchannel; i++)                                    /* 736 */
    {
        if (s3s == nullptr)                                          /* 741 */
        {
            wrk->s3d[i] = 0;
        }
        else
        {
            /* One handle for the whole stream, not one per channel. */
            if (wrk->s3dhndl == nullptr)                                   /* 744 */
                wrk->s3dhndl = Snd3DCreateWrk(s3s);                          /* 745 */

            wrk->s3d[i] = 1;                                                 /* 750 */
        }
    }
}                                                                            /* 772 */

/* 798 */
static void SndStreamStop(int wrk_id)
{
    SND_STREAM_WRK *wrk = &snd_stream_wrk[wrk_id];
    STREAM_STOP     stop;

    if (wrk->stop == 0 && wrk->status != ST_STREAM_NO_USE)                   /* 799 */
    {
        wrk->stop      = 1;                                                  /* 803 */
        wrk->abort_cnt = 600;                                                /* 808 */

        stop.wrk_id = wrk_id;                                                /* 810 */

        if (iopCommandRegister(REQ_STREAM_STOP, (char *)&stop,
                               sizeof(STREAM_STOP)))                         /* 811 */
            wrk->status = ST_STREAM_WAIT_END;                                /* 813 */
    }
}                                                                            /* 816 */

/* 820 */
void SndStreamFadeStop(int wrk_id, int time)
{
    SND_STREAM_WRK *wrk = &snd_stream_wrk[wrk_id];

    switch (wrk->status)                                                     /* 821 */
    {
    case ST_STREAM_HEADER_LOAD:
        /* Nothing is playing yet, so the header load is simply withdrawn. */
        if (FileLoadIsEnd(wrk->header_id) == 0)                              /* 825 */
        {
            printf("wrk->header_cancel header_id = %d, %s\n", wrk->header_id, GetFileName(wrk->file_no));/* 829 */
            FileLoadCancel(wrk->header_id, nullptr, nullptr); /* 830 */
        }

        StreamWrkRelease(wrk);                                               /* 831 */
        printf("ST_STREAM_HEADER_LOAD = %d\n", ST_STREAM_HEADER_LOAD); /* 833 */
        break;                                                               /* 834 */

    case ST_STREAM_PRE_LOAD:
    case ST_STREAM_START:
        SndStreamStop(wrk_id);                                               /* 839 */
        break;

    case ST_STREAM_PLAYING:
        wrk->target_vol = 0;                                                 /* 845 */
        wrk->spd        = SndGetFrameAddVol(0, wrk->vol, time);              /* 847 */
        wrk->fade_stop  = 1;                                                 /* 848 */
        break;
    }
}                                                                            /* 855 */

/* 860 */
void SndStreamAllStop(void)
{
    SndStreamStop(0);                                                        /* 864 */
    SndStreamStop(1);
}                                                                            /* 866 */

/* 871 */
void SndStreamPause(int wrk_id)
{
    STREAM_PAUSE pause;

    if (snd_stream_wrk[wrk_id].status != ST_STREAM_NO_USE &&
        snd_stream_wrk[wrk_id].status != ST_STREAM_WAIT_END)                 /* 872 */
    {
        pause.wrk_id = wrk_id;                                               /* 876 */
        iopCommandRegister(REQ_STREAM_PAUSE, (char *)&pause,
                           sizeof(STREAM_PAUSE));                            /* 901 */
    }
}                                                                            /* 906 */

/* 910 */
void SndStreamRestart(int wrk_id)
{
    STREAM_RESTART restart;

    if (snd_stream_wrk[wrk_id].status != ST_STREAM_NO_USE)                   /* 911 */
    {
        restart.wrk_id = wrk_id;                                             /* 915 */
        iopCommandRegister(REQ_STREAM_RESTART, (char *)&restart,
                           sizeof(STREAM_RESTART));                          /* 924 */
    }
}                                                                            /* 928 */

/* 933 */
void SndStreamSetPosition(int wrk_id, float *pos)
{
    SND_STREAM_WRK *wrk = &snd_stream_wrk[wrk_id];
    SND_3D_SET      s3s;

    if (wrk->status == ST_STREAM_NO_USE)                                     /* 934 */
        return;

    if (wrk->s3dhndl == (void *)0)                                           /* 936 */
    {
        memset(&s3s, 0, sizeof(SND_3D_SET));                        /* 939 */
        s3s.pos = (sceVu0FVECTOR *)pos;                                      /* 940 */
        s3s.vel = nullptr;                                                   /* 941 */
        s3s.dir = nullptr;

        SndStreamSet3DSub(wrk, &s3s);                                        /* 944 */
    }                                                                        /* 946 */
    else
    {
        Snd3DSetPosition(wrk->s3dhndl, pos);                                 /* 947 */
    }
}                                                                            /* 952 */

int SndStreamIsUse(int wrk_id)
{
    return snd_stream_wrk[wrk_id].status != ST_STREAM_NO_USE;                /* 964 */
}

/* 970 */
int SndStreamIsPreload(int wrk_id)
{
    if (snd_stream_wrk[wrk_id].status == ST_STREAM_NO_USE)                   /* 971 */
    {
        PRINT_ASSERT("SndStreamPreLoad() This Wrk Is Not Used");             /* 973 */
        return 1;                                                            /* 974 */
    }

    return snd_stream_wrk[wrk_id].pre_load_ok != 0;                          /* 978 */
}                                                                            /* 982 */

/* 986 */
SND_STREAM_ERR SndStreamGetInfo(int wrk_id, int *nchannel, int *interleave_byte,
                                SOUND_INFO **info)
{
    SND_STREAM_WRK *wrk = &snd_stream_wrk[wrk_id];

    if (wrk->status == ST_STREAM_NO_USE)                                     /* 987 */
        PRINT_ASSERT("SndStreamGetInfo() This Wrk Is Not Used");             /* 990 */

    if (wrk->header_ready == 0)                                              /* 991 */
        return SND_STREAM_ERR_HEADER;                                        /* 996 */

    *info            = wrk->info;                                            /* 997 */
    *nchannel        = wrk->s.nchannel;                                      /* 998 */
    *interleave_byte = wrk->s.interleave_byte;                               /* 999 */

    return SND_STREAM_OK;                                                    /* 1002 */
}                                                                            /* 1004 */

/* Where playback stopped, latched by StreamWrkRelease(). */
int SndStreamGetEndOffset(int wrk_id)
{
    return snd_stream_wrk[wrk_id].offset;                                    /* 1012 */
}

/* 1017 */
int SndStreamGetNowOffset(int wrk_id)
{
    if (snd_stream_wrk[wrk_id].status != ST_STREAM_PLAYING)                  /* 1018 */
        return 0;

    return GetStreamWrkRet(snd_stream_wrk[wrk_id].s.wrk_id)->offset;         /* 1021, 1023 */
}                                                                            /* 1027 */

/* 1030 */
SND_STREAM_ERR SndStreamChangePlaySpeed(int wrk_id, float rate)
{
    if (snd_stream_wrk[wrk_id].status == ST_STREAM_NO_USE)                   /* 1031 */
        PRINT_ASSERT("SndStreamChangePlaySpeed() This Wrk Is Not Used");     /* 1034 */

    snd_stream_wrk[wrk_id].play_spd = rate;                                  /* 1035 */

    return SND_STREAM_OK;                                                    /* 1039 */
}                                                                            /* 1041 */

/* 1045 */
void SndStreamSet3D(int wrk_id, SND_3D_SET *s3s)
{
    SND_STREAM_WRK *wrk = &snd_stream_wrk[wrk_id];

    if (wrk->status != ST_STREAM_NO_USE)                                     /* 1046 */
    {
        if (wrk->s3dhndl == nullptr)                                       /* 1048 */
            SndStreamSet3DSub(wrk, s3s);                                     /* 1051 */
        else                                                                 /* 1052 */
            snd3DSetSET(wrk->s3dhndl, s3s);                                  /* 1053 */
    }
}                                                                            /* 1058 */

/* 1061 */
void SndStreamFadePitch(int wrk_id, int pitch, int time)
{
    SND_STREAM_WRK *wrk = &snd_stream_wrk[wrk_id];                           /* 1062 */

    if (wrk->status != ST_STREAM_NO_USE)                                     /* 1064 */
    {
        if (time == 0)                                                       /* 1067 */
        {
            wrk->pitch = pitch;
        }
        else
        {
            /* ROM BUG, reproduced: the target and current arguments are the
             * wrong way round -- SndBufFadePitch() passes
             * (pitch, sbp->pitch, time).  The step comes out negated, so a
             * timed pitch fade on a stream walks away from its target
             * instead of towards it. */
            wrk->pspd = SndGetFrameAddVol(wrk->pitch, pitch, time);          /* 1068, 1069 */
        }

        wrk->target_pitch = pitch;                                           /* 1072 */
    }
}                                                                            /* 1073 */
