/* ==========================================================================
 *  system/eeiop/stream_auto.c
 *
 *  Priority-scheduled streaming audio.  Two play slots (one per snd_stream.c
 *  work area) and a caller-sized wait queue.  A request is always appended to
 *  the wait queue; StreamPlayQueueWrk() sorts that queue by priority and pops
 *  the head into any slot that has gone idle.  Requesting a stream whose
 *  priority beats something already playing evicts the loser through
 *  PlayQueueReturn(), which puts it *back* on the wait queue with its current
 *  offset, so it resumes where it left off once a slot frees up.
 *
 *  Two flags decide the shape of a request:
 *    - `resume`   (the ROM's play_flg argument) -- start playing on its own as
 *      soon as the preload lands, rather than waiting for
 *      StreamAutoPreloadPlay();
 *    - `reset_flg` -- on eviction, throw the offset away and restart from the
 *      top instead of resuming.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 *
 *  PORT NOTE: this file carries fewer trailing ROM-line annotations than its
 *  neighbours.  The two largest bodies (StreamAutoSetPlayNum, StreamPlayQueueWrk)
 *  have line spans starting back at 109, i.e. they inline a helper that left no
 *  symbol, so per-statement attribution inside them could not be measured
 *  reliably; the assert line numbers, which are exact, are annotated.
 * ======================================================================== */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stream_auto.h"

#include "cddat.h"                  /* GetFileName, GetFileSectorSize */
#include "snd_stream.h"

#include "../../common/utility2.h"  /* PRINT_ASSERT, PRINT_WARNING, GetAlignUp */
#include "../../sdk/libvu0.h"

#define STREAM_PLAY_MAX  2          /* snd_stream.c's slot count */

static int           dbg_stream_enable_flg;                                  /* sdata 3f4978 */
static int           stream_queue_max;                                       /* sbss 3f5028 */
static STREAM_QUEUE *stream_wait_queue;                                      /* sbss 3f502c */
static STREAM_QUEUE  stream_play_queue[STREAM_PLAY_MAX];                     /* bss 4c0030 */
static char          stream_play_disable[STREAM_PLAY_MAX];                   /* sbss 3f5030 */
static char          stream_play_disable_num;                                /* sbss 3f5032 */
static int           stream_queue_id;                                        /* sbss 3f5034 */
static int           stream_wait_num;                                        /* sbss 3f5038 */

static void StreamAutoSet3DSub(STREAM_QUEUE *queue, SND_3D_SET *s3s);
static void PlayQueueReturn(int id, int time);

/* Debug kill switch: every public entry point checks it first, so setting it
 * makes the whole streaming layer inert without unpicking any callers. */
void StreamAutoEnable(void)
{
    printf("StreamAutoEnable\n");                                            /* 77 */
    dbg_stream_enable_flg = 0;                                               /* 78 */
}                                                                            /* 79 */

void StreamAutoDisable(void)
{
    printf("StreamAutoDisable\n");                                           /* 82 */
    dbg_stream_enable_flg = 1;                                               /* 83 */
}                                                                            /* 84 */

/* 91 */
static void StreamAutoStartSub(int wrk_id, int header_file_no)
{
    STREAM_QUEUE *play_q = &stream_play_queue[wrk_id];

    SndStreamStart(wrk_id, play_q->file_no, header_file_no, play_q->offset);

    play_q->playing = 0;
    play_q->end     = 0;
}                                                                            /* 97 */

/* Descending priority: qsort puts the *highest* number first, so the head of
 * the queue (which PopStreamWaitQueue takes from the tail) is the lowest
 * number, i.e. the most important stream. */
static int cmpStreamQueue(const void *a, const void *b)
{
    return ((STREAM_QUEUE *)b)->priority - ((STREAM_QUEUE *)a)->priority;    /* 104 */
}

/* 117 */
static int PopStreamWaitQueue(STREAM_QUEUE *play_q)
{
    if (stream_wait_num == 0)
        return 0;

    /* A paused entry at the head blocks the whole queue rather than being
     * skipped -- it is still the most important stream. */
    if (stream_wait_queue[stream_wait_num - 1].pause != 0)
        return 0;

    *play_q = stream_wait_queue[stream_wait_num - 1];
    stream_wait_num--;

    return 1;
}                                                                            /* 135 */

/* 168 */
static int DeleteStreamWaitQueue(int id)
{
    int i;

    for (i = 0; i < stream_wait_num; i++)
    {
        if (stream_wait_queue[i].id == id)
        {
            for (; i < stream_wait_num - 1; i++)
                stream_wait_queue[i] = stream_wait_queue[i + 1];

            stream_wait_num--;
            printf("DeleteStreamWaitQueue stream_wait_num = %d\n", stream_wait_num);

            return 1;
        }
    }

    return 0;
}                                                                            /* 189 */

/* 197 */
static int GetWrkNoFromID(int id)
{
    int i;

    for (i = 0; i < STREAM_PLAY_MAX; i++)
    {
        if (stream_play_queue[i].use && stream_play_queue[i].id == id)
            return i;
    }

    return -1;
}                                                                            /* 206 */

/* Both of these were fully inlined -- ZERO2.MAP has no symbol for either --
 * but their assert banners name them and give their line numbers, so they are
 * written out here rather than folded into their callers. */
/* 140 */
static STREAM_QUEUE *PushStreamWaitQueue(void)
{
    STREAM_QUEUE *q = &stream_wait_queue[stream_wait_num];

    stream_wait_num++;
    if (stream_wait_num >= stream_queue_max)                                 /* 144 */
        PRINT_ASSERT("StreamQueueMax Over!");

    return q;
}

/* 155 */
static STREAM_QUEUE *GetNewStreamQueue(void)
{
    STREAM_QUEUE *q = &stream_wait_queue[stream_wait_num];

    stream_wait_num++;
    if (stream_wait_num >= stream_queue_max)                                 /* 159 */
        PRINT_ASSERT("StreamQueueMax Over!");

    return q;
}

/* Evict a playing slot back onto the wait queue.  A one-shot that has already
 * ended, or a stream nobody is waiting to resume, is simply dropped. */
/* 140 */
static void PlayQueueReturn(int id, int time)
{
    STREAM_QUEUE *play_q = &stream_play_queue[id];
    STREAM_QUEUE *queue;

    if ((play_q->loop == 1 && play_q->end == 0) || play_q->resume == 0)
    {
        /* reset_flg throws the position away, so the stream restarts from
         * the top when it comes back. */
        if (play_q->reset_flg)
            play_q->offset = 0;
        else
            play_q->offset = SndStreamGetNowOffset(id);

        queue  = PushStreamWaitQueue();
        *queue = *play_q;

        SndStreamFadeStop(id, time);

        play_q->first_flg = 0;
    }

    play_q->use = 0;
}                                                                            /* 230 */

/* 236 */
static void StreamAutoSet3DSub(STREAM_QUEUE *queue, SND_3D_SET *s3s)
{
    if (s3s->pos == nullptr)                                      /* 237 */
        PRINT_ASSERT("s3s->pos is NULL");

    sceVu0CopyVector(queue->pos, *s3s->pos);

    if (s3s->vel != nullptr)
    {
        queue->vel_flg = 1;
        sceVu0CopyVector(queue->vel, *s3s->vel);
    }
    else
    {
        queue->vel_flg = 0;
    }

    if (s3s->dir != nullptr)
    {
        queue->dir_flg = 1;
        sceVu0CopyVector(queue->dir, *s3s->dir);
    }
    else
    {
        queue->dir_flg = 0;
    }
}                                                                            /* 257 */

/* 155 */
static int StreamAutoNew(int file_no, int header_file_no, int priority, int effect,
                         int loop, int vol, int in_time, int reset_in_time,
                         SND_3D_SET *s3s, int play_flg, int reset_flg, int offset)
{
    STREAM_QUEUE *queue;

    queue = GetNewStreamQueue();

    if (file_no < 0)                                                         /* 267 */
        PRINT_ASSERT("file_no is under 0");                                  /* 268 */
    else if (header_file_no < 0 && header_file_no != -0x21)                  /* 270 */
        PRINT_ASSERT("header_file_no is under 0");                           /* 271 */

    printf("priority = %d\n", priority);

    queue->effect         = effect;
    queue->resume         = play_flg;
    queue->use            = 1;
    queue->loop           = loop;
    queue->reset_in_time  = reset_in_time;
    queue->priority       = priority;
    queue->file_no        = file_no;
    queue->header_file_no = header_file_no;
    queue->vol            = vol;
    queue->in_time        = in_time;
    queue->offset         = offset;
    queue->play_spd       = 1.f;
    queue->end            = 0;
    queue->first_flg      = 1;
    queue->reset_flg      = reset_flg;
    queue->pause          = 0;

    if (s3s != nullptr)
    {
        queue->s3d = 1;
        StreamAutoSet3DSub(queue, s3s);
    }
    else
    {
        queue->s3d = 0;
    }

    /* The id only ever has to be unique among the live entries, so it wraps
     * at INT_MAX rather than being recycled. */
    stream_queue_id++;
    if (stream_queue_id < 0)
        stream_queue_id = 0;

    queue->id = stream_queue_id;

    return stream_queue_id;
}                                                                            /* 307 */

/* If the new request outranks the worst thing currently playing, evict that
 * one now so the new one can be picked up on the next Main(). */
/* 312 */
static void StreamAutoSub(int priority)
{
    int i;
    int least_priority    = -1;
    int least_priority_no = 0;

    for (i = 0; i < STREAM_PLAY_MAX; i++)
    {
        if (stream_play_disable[i] != 0)
            continue;

        /* A free slot means nothing has to be evicted at all. */
        if (stream_play_queue[i].use == 0)
            return;

        if (stream_play_queue[i].pause == 0 &&
            stream_play_queue[i].priority > least_priority)
        {
            least_priority    = stream_play_queue[i].priority;
            least_priority_no = i;
        }
    }

    if (least_priority != -1 && priority < least_priority)
        PlayQueueReturn(least_priority_no, 5);
}                                                                            /* 348 */

/* 354 */
int StreamAutoPreload(int file_no, int header_file_no, int priority, int effect,
                      int loop, int vol, int in_time, SND_3D_SET *s3s)
{
    int id;

    if (dbg_stream_enable_flg != 0)
        return -1;

    id = StreamAutoNew(file_no, header_file_no, priority, effect, loop, vol,
                       in_time, in_time, s3s, 0, 1, 0);
    StreamAutoSub(priority);

    return id;
}                                                                            /* 370 */

/* 374 */
int StreamAutoPlayNonReset(int file_no, int header_file_no, int priority, int effect,
                           int loop, int vol, int in_time, int reset_in_time,
                           SND_3D_SET *s3s, int start_sector)
{
    int id;

    if (dbg_stream_enable_flg != 0)
        return -1;

    id = StreamAutoNew(file_no, header_file_no, priority, effect, loop, vol,
                       in_time, reset_in_time, s3s, 1, 0, start_sector);
    StreamAutoSub(priority);

    return id;
}                                                                            /* 389 */

/* 394 */
int StreamAutoPreloadNonReset(int file_no, int header_file_no, int priority, int effect,
                              int loop, int vol, int in_time, int reset_in_time,
                              SND_3D_SET *s3s)
{
    int id;

    if (dbg_stream_enable_flg != 0)
        return -1;

    id = StreamAutoNew(file_no, header_file_no, priority, effect, loop, vol,
                       in_time, reset_in_time, s3s, 0, 0, 0);
    StreamAutoSub(priority);

    return id;
}                                                                            /* 410 */

/* 415 */
int StreamAutoPlay(int file_no, int header_file_no, int priority, int effect,
                   int loop, int vol, int in_time, SND_3D_SET *s3s)
{
    int id;

    if (dbg_stream_enable_flg != 0)
        return -1;

    id = StreamAutoNew(file_no, header_file_no, priority, effect, loop, vol,
                       in_time, in_time, s3s, 1, 1, 0);
    StreamAutoSub(priority);

    return id;
}                                                                            /* 431 */

/* Raise or lower the number of slots allowed to play.  Disabling a slot that
 * is playing costs an eviction, so the lowest-priority candidate is chosen. */
void StreamAutoSetPlayNum(int iNum, int fade_time)
{
    int iNewlyDisableNum;
    int iSubDisableNum;
    int i;
    int j;
    int iPriority;

    if ((unsigned int)iNum > STREAM_PLAY_MAX)
    {
        PRINT_ASSERT("StreamAutoSetPlayNum ArgNum[%d] Is Illegal", iNum);    /* 521 */
        return;
    }

    iNewlyDisableNum = (STREAM_PLAY_MAX - iNum) - stream_play_disable_num;

    if (iNewlyDisableNum > 0)
    {
        /* Idle slots first -- disabling one of those costs nothing. */
        for (i = 0; i < STREAM_PLAY_MAX && iNewlyDisableNum > 0; i++)
        {
            if (stream_play_disable[i] == 0 &&
                !(stream_play_queue[i].use && stream_play_queue[i].end == 0))
            {
                stream_play_disable[i] = 1;
                iNewlyDisableNum--;
            }
        }

        for (i = 0; i < STREAM_PLAY_MAX; i++)
        {
            if (iNewlyDisableNum < 1)
                break;

            if (stream_play_disable[i] != 0)
                continue;

            iPriority = stream_play_queue[i].priority;
            printf("i = %d\n", i);
            printf("iPriority = %d\n", iPriority);

            /* Only take the slot if no other live slot ranks below it. */
            for (j = i + 1; j < STREAM_PLAY_MAX; j++)
            {
                if (stream_play_disable[j] == 0 &&
                    iPriority < stream_play_queue[j].priority)
                {
                    printf("wrk[%d] priority is lower than [%d]\n", j, i);
                    break;
                }
            }

            if (j == STREAM_PLAY_MAX)
            {
                printf("===========================disable %d\n", i);
                stream_play_disable[i] = 1;
                PlayQueueReturn(i, fade_time);
                iNewlyDisableNum--;
            }
        }

        qsort(stream_wait_queue, stream_wait_num, sizeof(STREAM_QUEUE),
              cmpStreamQueue);
    }
    else if (iNewlyDisableNum < 0)
    {
        for (iSubDisableNum = 0; iNewlyDisableNum < iSubDisableNum; iSubDisableNum--)
        {
            if (stream_play_disable[0] != 0)
            {
                stream_play_disable[0] = 0;
            }
            else
            {
                for (j = 1; j < STREAM_PLAY_MAX; j++)
                {
                    if (stream_play_disable[j] != 0)
                    {
                        stream_play_disable[j] = 0;
                        break;
                    }
                }
            }
        }
    }

    stream_play_disable_num = (char)(STREAM_PLAY_MAX - iNum);
}                                                                            /* 582 */

/* 588 */
void StreamAutoSetExclusiveMode(int flg, int fade_time)
{
    if (dbg_stream_enable_flg != 0)
        return;

    if (flg)
        StreamAutoSetPlayNum(1, fade_time);
    else
        StreamAutoSetPlayNum(STREAM_PLAY_MAX, fade_time);
}                                                                            /* 600 */

/* 0x70 rather than sizeof(STREAM_QUEUE) (0x60): the extra 0x10 per entry pays
 * for the 16-byte alignment StreamAutoPlayInit() applies to the base. */
int StreamAutoGetOneWrkSize(void)
{
    return 0x70;                                                             /* 604 */
}

/* 609 */
void *StreamAutoPlayInit(void *wrk_buffer, int num)
{
    int i;

    /* The queue holds sceVu0FVECTORs, so it has to be quadword aligned.
     * PORT NOTE: the ROM rounds the pointer as a 32-bit int; done through
     * uintptr_t here so a host address above 4 GB survives. */
    stream_wait_queue = (STREAM_QUEUE *)(((uintptr_t)wrk_buffer + 15) & ~(uintptr_t)15);

    stream_wait_num  = 0;
    stream_queue_max = num;

    for (i = 0; i < num; i++)
    {
        stream_wait_queue[i].id  = i;
        stream_wait_queue[i].use = 0;
    }

    for (i = 0; i < STREAM_PLAY_MAX; i++)
    {
        stream_play_disable[i]   = 0;
        stream_play_queue[i].use = 0;
    }

    stream_play_disable_num = 0;

    return (char *)stream_wait_queue + num * StreamAutoGetOneWrkSize();
}                                                                            /* 636 */

/* The scheduler proper: one pass per slot. */
static void StreamPlayQueueWrk(int wrk_id)
{
    STREAM_QUEUE *play_q = &stream_play_queue[wrk_id];
    SND_3D_SET   *ptr;
    SND_3D_SET    s3s;

    if (SndStreamIsUse(wrk_id) == 0)
    {
        /* The slot has gone idle with an entry still in it.  A loop that has
         * not been told to end is restarted in place (its header is already
         * loaded, hence the -0x21); anything else releases the entry. */
        if (play_q->use)
        {
            if (play_q->loop == 1 && play_q->end == 0)
                StreamAutoStartSub(wrk_id, -0x21);
            else
                play_q->use = 0;

            if (play_q->use)
                return;
        }

        qsort(stream_wait_queue, stream_wait_num, sizeof(STREAM_QUEUE),
              cmpStreamQueue);

        if (PopStreamWaitQueue(play_q))
        {
            play_q->pause = 0;
            play_q->use   = 1;

            StreamAutoStartSub(wrk_id, play_q->header_file_no);
        }
    }
    else if (play_q->playing == 0 && play_q->resume == 1 && play_q->pause == 0 &&
             SndStreamIsHeaderReady(wrk_id))
    {
        memset(&s3s, 0, sizeof(SND_3D_SET));

        if (play_q->s3d == 0)
        {
            ptr = (SND_3D_SET *)0;
        }
        else
        {
            ptr     = &s3s;
            s3s.pos = &play_q->pos;
            s3s.vel = play_q->vel_flg ? &play_q->vel : (sceVu0FVECTOR *)0;
            s3s.dir = play_q->dir_flg ? &play_q->dir : (sceVu0FVECTOR *)0;
        }

        /* A first play uses reset_in_time; a resume uses the ordinary fade,
         * because the stream is picking up mid-track. */
        if (play_q->first_flg)
        {
            SndStreamPlay(wrk_id, play_q->effect, play_q->loop, play_q->vol,
                          play_q->reset_in_time, ptr, play_q->play_spd, 0x1000);
            play_q->first_flg = 0;
        }
        else
        {
            SndStreamPlay(wrk_id, play_q->effect, play_q->loop, play_q->vol,
                          play_q->in_time, ptr, play_q->play_spd, 0x1000);
        }

        play_q->playing = 1;
    }
}                                                                            /* 725 */

/* 729 */
void StreamAutoPlayMain(void)
{
    int wrk_id;

    if (dbg_stream_enable_flg != 0)
        return;

    for (wrk_id = 0; wrk_id < STREAM_PLAY_MAX; wrk_id++)
    {
        if (stream_play_disable[wrk_id] == 0)
            StreamPlayQueueWrk(wrk_id);
    }
}                                                                            /* 739 */

/* 749 */
static int StreamAutoSearchWrkID(int id)
{
    int i;

    for (i = 0; i < STREAM_PLAY_MAX; i++)
    {
        if (stream_play_queue[i].use && stream_play_queue[i].id == id)
            return i;
    }

    return -1;
}                                                                            /* 757 */

/* 765 */
static int StreamAutoSearchWaitWrkID(int id)
{
    int i;

    for (i = 0; i < stream_wait_num; i++)
    {
        if (stream_wait_queue[i].id == id)
            return i;
    }

    return -1;
}                                                                            /* 773 */

/* 782 */
int StreamAutoGetInfo(int id, int *num, int *interleave_byte, SOUND_INFO **info)
{
    int wrk_id;

    if (dbg_stream_enable_flg != 0)
        return 0;

    wrk_id = StreamAutoSearchWrkID(id);
    if (wrk_id < 0)
        return 0;

    return SndStreamGetInfo(wrk_id, num, interleave_byte, info) == SND_STREAM_OK;
}                                                                            /* 803 */

/* 807 */
void StreamAutoAllStop(void)
{
    int i;

    if (dbg_stream_enable_flg != 0)
        return;

    for (i = 0; i < STREAM_PLAY_MAX; i++)
    {
        if (stream_play_queue[i].use && stream_play_queue[i].end == 0)
        {
            printf("StreamAutoallStop stop\n");
            SndStreamFadeStop(i, 1);

            stream_play_queue[i].use       = 0;
            stream_play_queue[i].first_flg = 0;
        }
    }

    /* The wait queue is thrown away wholesale rather than drained. */
    stream_wait_num = 0;
    for (i = 0; i < stream_queue_max; i++)
    {
        stream_wait_queue[i].id  = i;
        stream_wait_queue[i].use = 0;
    }
}                                                                            /* 837 */

/* 841 */
void StreamAutoAllPause(void)
{
    int i;

    if (dbg_stream_enable_flg != 0)
        return;

    for (i = 0; i < STREAM_PLAY_MAX; i++)
    {
        if (stream_play_queue[i].use)
        {
            SndStreamPause(i);
            stream_play_queue[i].pause = 1;
        }
    }

    for (i = 0; i < stream_wait_num; i++)
        stream_wait_queue[i].pause = 1;
}                                                                            /* 865 */

/* 869 */
void StreamAutoAllRestart(void)
{
    int i;

    if (dbg_stream_enable_flg != 0)
        return;

    for (i = 0; i < STREAM_PLAY_MAX; i++)
    {
        if (stream_play_queue[i].use)
        {
            SndStreamRestart(i);
            stream_play_queue[i].pause = 0;
        }
    }

    for (i = 0; i < stream_wait_num; i++)
        stream_wait_queue[i].pause = 0;
}                                                                            /* 893 */

/* 897 */
void StreamAutoFade(int id, int target_vol, int time)
{
    int wait_wrk_no;
    int play_wrk_no;

    if (dbg_stream_enable_flg != 0)
        return;

    /* Still queued: the new volume just replaces what it will start at. */
    wait_wrk_no = StreamAutoSearchWaitWrkID(id);
    if (wait_wrk_no >= 0)
    {
        stream_wait_queue[wait_wrk_no].vol = target_vol;
        return;
    }

    play_wrk_no = GetWrkNoFromID(id);
    if (play_wrk_no >= 0)
        SndStreamFade(play_wrk_no, target_vol, time);
}                                                                            /* 919 */

/* 923 */
void StreamAutoFadeOut(int id, int fade_time)
{
    int play_wrk_no;

    if (dbg_stream_enable_flg != 0)
        return;

    /* Dropping a queued entry is enough; only a playing one has to fade. */
    if (DeleteStreamWaitQueue(id))
        return;

    play_wrk_no = GetWrkNoFromID(id);
    if (play_wrk_no >= 0)
    {
        SndStreamFadeStop(play_wrk_no, fade_time);

        stream_play_queue[play_wrk_no].use       = 0;
        stream_play_queue[play_wrk_no].end       = 1;
        stream_play_queue[play_wrk_no].first_flg = 0;
    }
}                                                                            /* 948 */

/* 954 */
int StreamAutoPause(int id)
{
    int wrk_id;

    wrk_id = StreamAutoSearchWrkID(id);
    if (wrk_id >= 0)
    {
        stream_play_queue[wrk_id].pause = 1;
        SndStreamPause(wrk_id);
        return 1;
    }

    wrk_id = StreamAutoSearchWaitWrkID(id);
    if (wrk_id >= 0)
    {
        stream_wait_queue[wrk_id].pause = 1;
        return 1;
    }

    return 0;
}                                                                            /* 973 */

/* 979 */
int StreamAutoRestart(int id)
{
    int wrk_id;

    wrk_id = StreamAutoSearchWrkID(id);
    if (wrk_id >= 0)
    {
        stream_play_queue[wrk_id].pause = 0;
        SndStreamRestart(wrk_id);
        return 1;
    }

    wrk_id = StreamAutoSearchWaitWrkID(id);
    if (wrk_id >= 0)
    {
        stream_wait_queue[wrk_id].pause = 0;
        return 1;
    }

    return 0;
}                                                                            /* 998 */

/* 1002 */
int StreamAutoSetPosition(int id, float *pos)
{
    SND_3D_SET set;
    int        wrk_id;

    memset(&set, 0, sizeof(SND_3D_SET));

    if (dbg_stream_enable_flg != 0)
        return 1;

    wrk_id  = StreamAutoSearchWrkID(id);
    set.pos = &stream_play_queue[wrk_id].pos;

    if (wrk_id >= 0)
    {
        sceVu0CopyVector(*set.pos, pos);

        if (stream_play_queue[wrk_id].vel_flg)
            set.vel = &stream_play_queue[wrk_id].vel;
        if (stream_play_queue[wrk_id].dir_flg)
            set.dir = &stream_play_queue[wrk_id].dir;

        SndStreamSet3D(wrk_id, &set);
        return 1;
    }

    wrk_id = StreamAutoSearchWaitWrkID(id);
    if (wrk_id >= 0)
    {
        sceVu0CopyVector(stream_wait_queue[wrk_id].pos, pos);
        return 1;
    }

    return 0;
}                                                                            /* 1036 */

/* 1039 */
void PrintStreamAutoStatus(void)
{
    int i;

    printf("\n");
    printf("================================\n");
    printf("<<Wait Streams>>\n");

    for (i = 0; i < stream_wait_num; i++)
        printf("%s priority %d file_no %d\n",
               GetFileName(stream_wait_queue[i].file_no),
               stream_wait_queue[i].priority,
               stream_wait_queue[i].file_no);

    printf("\n");
    printf("<<Play Streams>>\n");

    for (i = 0; i < STREAM_PLAY_MAX; i++)
    {
        if (stream_play_queue[i].use == 0)
            printf("[%d] NONE Is Played\n", i);
        else
            printf("[%d] %s priority %d file_no %d\n", i,
                   GetFileName(stream_play_queue[i].file_no),
                   stream_play_queue[i].priority,
                   stream_play_queue[i].file_no);
    }

    printf("================================\n");
    printf("\n");
}                                                                            /* 1059 */

/* 1064 */
int StreamAutoIsPreload(int id)
{
    int wrk_id;

    if (dbg_stream_enable_flg != 0)
        return 1;

    wrk_id = StreamAutoSearchWrkID(id);
    if (wrk_id >= 0)
        return SndStreamIsPreload(wrk_id);

    /* Queued but not started yet, so nothing is preloaded. */
    wrk_id = StreamAutoSearchWaitWrkID(id);
    if (wrk_id >= 0)
    {
        printf("StreamAutoIsPreload() Wait Other Stream End\n");
        return 0;
    }

    /* -1 is the "no stream" id every caller may legitimately hold. */
    if (id != -1)
    {
        PRINT_WARNING("StreamAutoIsPreload() ID[%d] is not Found", id);      /* 1079 */
        return 0;
    }

    return 0;
}                                                                            /* 1091 */

/* 1095 */
int StreamAutoPreloadPlay(int id)
{
    int wrk_id;

    if (dbg_stream_enable_flg != 0)
        return 1;

    wrk_id = StreamAutoSearchWrkID(id);
    if (wrk_id < 0 || SndStreamIsPreload(wrk_id) == 0)
        return 0;

    stream_play_queue[wrk_id].resume = 1;

    return 1;
}                                                                            /* 1113 */

/* 1117 */
int StreamAutoIsAllStop(void)
{
    int i;

    if (dbg_stream_enable_flg != 0)
        return 1;

    for (i = 0; i < STREAM_PLAY_MAX; i++)
    {
        if (SndStreamIsUse(i))
            return 0;
    }

    return 1;
}                                                                            /* 1130 */

/* 1134 */
int StreamAutoIsPlaying(int id)
{
    if (dbg_stream_enable_flg != 0)
        return 1;

    if (StreamAutoSearchWrkID(id) >= 0)
        return 1;

    if (StreamAutoSearchWaitWrkID(id) < 0)
        return 0;

    /* Queued counts as playing: the caller only wants to know the stream is
     * still alive. */
    printf("StreamAutoIsPlaying() Wait Other Stream End\n");

    return 1;
}                                                                            /* 1162 */

/* 1165 */
int StreamAutoChangePlaySpeed(int id, float rate)
{
    int wrk_id;

    if (dbg_stream_enable_flg != 0)
        return 1;

    wrk_id = StreamAutoSearchWrkID(id);
    if (wrk_id >= 0)
    {
        stream_play_queue[wrk_id].play_spd = rate;
        SndStreamChangePlaySpeed(wrk_id, rate);
        return 1;
    }

    wrk_id = StreamAutoSearchWaitWrkID(id);
    if (wrk_id >= 0)
    {
        stream_wait_queue[wrk_id].play_spd = rate;
        return 1;
    }

    return 0;
}                                                                            /* 1188 */

/* 1191 */
float StreamAutoNowPlayPercentage(int id)
{
    int wrk_id;

    if (dbg_stream_enable_flg != 0)
        return 0.f;

    wrk_id = StreamAutoSearchWrkID(id);
    if (wrk_id >= 0)
        return (float)SndStreamGetNowOffset(wrk_id) /
               (float)GetFileSectorSize(stream_play_queue[wrk_id].file_no);

    /* A queued entry still has the offset it was evicted at. */
    wrk_id = StreamAutoSearchWaitWrkID(id);
    if (wrk_id >= 0)
        return (float)stream_wait_queue[wrk_id].offset /
               (float)GetFileSectorSize(stream_wait_queue[wrk_id].file_no);

    return 0.f;
}                                                                            /* 1219 */

/* 1225 */
int StreamAutoGetNowSector(int id)
{
    int wrk_id;

    wrk_id = StreamAutoSearchWrkID(id);
    if (wrk_id >= 0)
        return SndStreamGetNowOffset(wrk_id);

    return 0;
}                                                                            /* 1230 */

/* 1234 */
int StreamAutoSet3D(int id, SND_3D_SET *s3s)
{
    int wrk_id;

    if (dbg_stream_enable_flg != 0)
        return 1;

    wrk_id = StreamAutoSearchWrkID(id);
    if (wrk_id >= 0)
    {
        StreamAutoSet3DSub(&stream_play_queue[wrk_id], s3s);
        SndStreamSet3D(wrk_id, s3s);
        return 1;
    }

    wrk_id = StreamAutoSearchWaitWrkID(id);
    if (wrk_id >= 0)
    {
        StreamAutoSet3DSub(&stream_wait_queue[wrk_id], s3s);
        return 1;
    }

    return 0;
}                                                                            /* 1257 */
