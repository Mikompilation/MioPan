// FILE: /home/akira_koide/zero2np/src/system/playpss/audiodec.c
//
// The audio half of the PSS movie player.  playpss.o demuxes the ADPCM
// elementary stream out of the .pss and hands it here; this keeps it moving to
// the SPU2 and nothing else.
//
// Two rings, one pump.  The demuxer writes into the EE ring through
// audioDecBeginPut()/audioDecEndPut(); the SPU2's auto-DMA reads continuously
// out of an IOP-side ring it was pointed at once, at start; and
// audioDecSendToIOP() copies whole 1024-byte blocks from the first into the
// second, into the gap between where the SPU2 is reading and where the last
// copy ended.  There is no interrupt and no callback anywhere in the file --
// the whole thing is driven by polling the auto-DMA's play address once a
// frame, which is what sceSdBlockTransStatus() answers.
//
// Everything below 1024 bytes is deliberately not sent.  Every size the file
// computes is rounded down to a whole block, both ends of the copy are refused
// if either side has less than one block, and the play/write gap is measured a
// block short -- so the DMA is never overtaken, at the cost of always running
// up to a block behind.
//
// The header is read through the same producer API as the samples.  While
// `state` is 0 the free span audioDecBeginPut() reports is the 40-byte
// SShd/SSbd pair inside AudioDec itself rather than the ring, so the demuxer's
// copy loop does not know which it is filling; audioDecEndPut() notices when
// the 40th byte lands, prints the stream's parameters and moves to state 1.
//
// PORT: nothing calls any of this yet.  audioDecCreate() has exactly one caller
// in the ROM -- playpss.o -- and playpss.o is the port's one deliberate shim
// (see playpss.c), because it is a hardware pipeline: IPU, GS PATH3 and SPU2.
// This module is not, so it is reconstructed rather than shimmed, and the day a
// decoder goes in behind playpss.c the audio side is already here.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
// playpss.a(audiodec.o), .text 0x279c88..0x27a52c; all 10 exports plus the
// 4 statics.  Line annotations measured from the object's own $LM records.

#include "audiodec.h"

#include "../../sdk/libsd.h"
#include "../../sdk/libsdr.h"
#include "../../sdk/sifman.h"

#include <stdint.h>

/* The 40 bytes of "SShd" + "SSbd" that open the stream. */
#define AUDIODEC_HDR_SIZE   ((int)(sizeof(SpuStreamHeader) + sizeof(SpuStreamBody)))

/* Nothing smaller than this is ever shipped to the IOP -- see the banner. */
#define AUDIODEC_BLOCK      1024

/* Where the sector of silence is parked in SPU2 memory.  The auto-DMA is aimed
 * at it whenever the stream is not running, so a pause is silent rather than a
 * held buffer repeating. */
#define AUDIODEC_ZERO_SPU_ADRS  0x4000

/* sbss 3f50f4 -- the SPU2 core the stream plays on, from
 * ReqQuerySPUTransCoreGet() by way of audioDecCreate(). */
static int AUTODMA_CH;
/* sbss 3f50f8 -- the scene's volume as a percentage; changeInputVolume()
 * scales every write by it. */
static int audio_vol_percent;

static void iopGetArea(int *pd0, int *d0, int *pd1, int *d1,
                       AudioDec *ad, int pos);
static int  sendToIOP2area(int pd0, int d0, int pd1, int d1,
                           u_char *ps0, int s0, u_char *ps1, int s1);
static int  sendToIOP(int dst, u_char *src, int size);
static void changeInputVolume(u_int val);

/* --------------------------------------------------------------------------
 *  audioDecCreate  (0x279c88)
 *
 *  Both rings are already allocated by the caller -- InitMovie() claims the EE
 *  one out of the movie heap and the two IOP ones through iopalloc().  All this
 *  does is record them, blank the IOP-side sector of silence and upload it to
 *  the SPU2, so that everything before the first real block is quiet.
 * ------------------------------------------------------------------------ */
int audioDecCreate(AudioDec *ad, u_char *buff, int buffSize,
                   int iopBuff, int iopBuffSize,
                   u_char *zeroBuff, int iopZeroBuff, int zeroBuffSize,
                   int audio_vol, int auto_dma_core)                    /* 50 */
{
    AUTODMA_CH         = auto_dma_core;                                 /* 52 */

    ad->state          = 0;                                             /* 54 */
    ad->hdrCount       = 0;                                             /* 55 */
    ad->data           = buff;                                          /* 56 */
    ad->put            = 0;                                             /* 57 */
    ad->count          = 0;                                             /* 58 */
    ad->size           = buffSize;                                      /* 59 */
    ad->totalBytes     = 0;                                             /* 60 */
    ad->totalBytesSent = 0;                                             /* 61 */

    ad->iopBuff        = iopBuff;                                       /* 63 */
    ad->iopLastPos     = 0;                                             /* 64 */
    ad->iopPausePos    = 0;                                             /* 65 */

    ad->iopBuffSize    = iopBuffSize;                                   /* 71 */

    ad->iopZero        = iopZeroBuff;                                   /* 77 */
    ad->iopZeroSize    = zeroBuffSize;                                  /* 78 */

    memset(zeroBuff, 0, zeroBuffSize);                                  /* 81 */
    sendToIOP(ad->iopZero, zeroBuff, zeroBuffSize);                     /* 82 */

    sceSdRemote(1, SDR_VOICE_TRANS, AUTODMA_CH, 0,                      /* 85 */
                ad->iopZero, AUDIODEC_ZERO_SPU_ADRS, ad->iopZeroSize);

    audio_vol_percent  = audio_vol;                                     /* 90 */

    return 1;                                                           /* 92 */
}

/* --------------------------------------------------------------------------
 *  audioDecDelete  (0x279d58)
 *
 *  Nothing to give back: every buffer belongs to the caller, and the auto-DMA
 *  has already been stopped by audioDecPause().  Eight bytes in the ROM.
 * ------------------------------------------------------------------------ */
int audioDecDelete(AudioDec *ad)                                        /* 99 */
{
    return 1;                                                           /* 105 */
}

/* --------------------------------------------------------------------------
 *  audioDecPause  (0x279d60)
 *
 *  Mute, stop the auto-DMA, and point it at the sector of silence.
 *
 *  Stopping it is also how the play position is read: SD_BLOCK_TRANS_STAT
 *  answers with the address the DMA had reached, which minus the ring base is
 *  what audioDecResume() restarts from.  The mask is 24 bits because the IOP's
 *  address space is 2 MB and the top byte of the answer is status.
 * ------------------------------------------------------------------------ */
void audioDecPause(AudioDec *ad)                                        /* 112 */
{
    int ret;

    ad->state = 3;                                                      /* 116 */

    changeInputVolume(0);                                               /* 122 */

    ret = sceSdRemote(1, SDR_BLOCK_TRANS, AUTODMA_CH,                   /* 128 */
                      SD_BLOCK_TRANS_STAT, 0, 0);

    ad->iopPausePos = (ret & 0xffffff) - ad->iopBuff;                   /* 130 */

    sceSdRemote(1, SDR_VOICE_TRANS, AUTODMA_CH, 0,                      /* 136 */
                ad->iopZero, AUDIODEC_ZERO_SPU_ADRS, ad->iopZeroSize);
}

/* --------------------------------------------------------------------------
 *  audioDecResume  (0x279de0)
 *
 *  Unmute and set the auto-DMA loop running again, from where the pause left
 *  it.  The ring length handed over is rounded down to a whole block; the tail
 *  of the last partial block is never played.
 * ------------------------------------------------------------------------ */
void audioDecResume(AudioDec *ad)                                       /* 145 */
{
    changeInputVolume(0x7fff);                                          /* 151 */

    sceSdRemote(1, SDR_BLOCK_TRANS, AUTODMA_CH, SD_BLOCK_TRANS_CONT,    /* 153 */
                ad->iopBuff,
                ad->iopBuffSize / AUDIODEC_BLOCK * AUDIODEC_BLOCK,
                ad->iopBuff + ad->iopPausePos);

    ad->state = 2;                                                      /* 158 */
}

/* --------------------------------------------------------------------------
 *  audioDecStart  (0x279e48)
 *
 *  A start is a resume from iopPausePos 0, which audioDecCreate() left there.
 * ------------------------------------------------------------------------ */
void audioDecStart(AudioDec *ad)                                        /* 165 */
{
    audioDecResume(ad);                                                 /* 168 */
}

/* --------------------------------------------------------------------------
 *  audioDecReset  (0x279e60)
 *
 *  Stop and empty both rings, back to the state audioDecCreate() left -- so
 *  the next stream is read from its header again.  The buffers themselves and
 *  their sizes survive.
 * ------------------------------------------------------------------------ */
void audioDecReset(AudioDec *ad)                                        /* 175 */
{
    audioDecPause(ad);                                                  /* 181 */

    ad->state          = 0;                                             /* 183 */
    ad->hdrCount       = 0;                                             /* 184 */
    ad->put            = 0;                                             /* 185 */
    ad->count          = 0;                                             /* 186 */
    ad->totalBytes     = 0;                                             /* 187 */
    ad->totalBytesSent = 0;                                             /* 188 */
    ad->iopLastPos     = 0;                                             /* 189 */
    ad->iopPausePos    = 0;                                             /* 190 */
}

/* --------------------------------------------------------------------------
 *  audioDecBeginPut  (0x279ea8)
 *
 *  Report where the demuxer may write and how much, as up to two spans.
 *
 *  While the header is still short (state 0) the first span is the SShd/SSbd
 *  pair inside AudioDec and the second is the whole ring -- so a caller that
 *  fills span 0 then span 1 lands the header and the samples that follow it in
 *  one pass, without a special case.
 *
 *  Once past that, span 0 runs from the write cursor to whichever comes first,
 *  the end of the ring or the read cursor, and span 1 is the wrapped remainder.
 *  Note the free length is `size - count`, so a full ring reports a zero-length
 *  span rather than a full one.
 * ------------------------------------------------------------------------ */
void audioDecBeginPut(AudioDec *ad, u_char **ptr0, int *len0,
                      u_char **ptr1, int *len1)                         /* 198 */
{
    int len;

    if (ad->state == 0)                                                 /* 206 */
    {
        *ptr0 = (u_char *)&ad->sshd + ad->hdrCount;                     /* 207 */
        *len0 = AUDIODEC_HDR_SIZE - ad->hdrCount;                       /* 208 */
        *ptr1 = ad->data;                                               /* 209 */
        *len1 = ad->size;                                               /* 210 */
    }
    else                                                                /* 211 */
    {
        len = ad->size - ad->count;                                     /* 219 */

        if (len <= ad->size - ad->put)                                  /* 221 */
        {
            *ptr0 = ad->data + ad->put;                                 /* 222 */
            *len0 = len;                                                /* 223 */
            *ptr1 = NULL;                                               /* 224 */
            *len1 = 0;                                                  /* 225 */
        }
        else                                                            /* 226 */
        {
            *ptr0 = ad->data + ad->put;                                 /* 227 */
            *len0 = ad->size - ad->put;                                 /* 228 */
            *ptr1 = ad->data;                                           /* 229 */
            *len1 = len - (ad->size - ad->put);                         /* 230 */
        }
    }
}                                                                       /* 232 */

/* --------------------------------------------------------------------------
 *  audioDecEndPut  (0x279f50)
 *
 *  Commit `size` bytes the demuxer just wrote.
 *
 *  Whatever part of them belongs to the header is taken off the front first --
 *  that is `hdr_add` -- and only the remainder advances the ring, which is why
 *  the very first call after a create can carry both.  The 40th header byte is
 *  what moves the state on and prints the stream's parameters.
 * ------------------------------------------------------------------------ */
void audioDecEndPut(AudioDec *ad, int size)                             /* 238 */
{
    if (ad->state == 0)                                                 /* 240 */
    {
        int hdr_add;

        hdr_add = (size < AUDIODEC_HDR_SIZE - ad->hdrCount)             /* 241 */
                      ? size : AUDIODEC_HDR_SIZE - ad->hdrCount;
        ad->hdrCount += hdr_add;                                        /* 242 */

        if (ad->hdrCount >= AUDIODEC_HDR_SIZE)                          /* 244 */
        {
            ad->state = 1;                                              /* 245 */

            printf("-------- audio information --------------------\n");/* 247 */
            printf("[%c%c%c%c]\nheader size:\t\t\t\t\t\t\t%d\ntype(0:PCM big, 1:PCM little, 2:ADPCM): %d\nsampling rate:\t\t\t\t\t\t  %dHz\nchannels:\t\t\t\t\t\t\t   %d\ninterleave size:\t\t\t\t\t\t%d\ninterleave start block address:\t\t %d\ninterleave end block address:\t\t   %d\n",
                   ad->sshd.id[0], ad->sshd.id[1],                      /* 248 */
                   ad->sshd.id[2], ad->sshd.id[3],
                   ad->sshd.size, ad->sshd.type, ad->sshd.rate,
                   ad->sshd.ch, ad->sshd.interSize,
                   ad->sshd.loopStart, ad->sshd.loopEnd);
            printf("[%c%c%c%c]\ndata size:\t\t\t\t\t\t\t  %d\n",        /* 269 */
                   ad->ssbd.id[0], ad->ssbd.id[1],
                   ad->ssbd.id[2], ad->ssbd.id[3], ad->ssbd.size);
        }

        size -= hdr_add;                                                /* 279 */
    }

    ad->put         = (ad->put + size) % ad->size;                      /* 281 */
    ad->count      += size;                                             /* 282 */
    ad->totalBytes += size;                                             /* 283 */
}                                                                       /* 284 */

/* --------------------------------------------------------------------------
 *  audioDecIsPreset  (0x27a060)
 *
 *  "The IOP ring has been filled once."  playpss.o waits on this before it
 *  calls audioDecStart(), so the auto-DMA never begins on a short buffer.
 * ------------------------------------------------------------------------ */
int audioDecIsPreset(AudioDec *ad)                                      /* 290 */
{
    return ad->totalBytesSent >= ad->iopBuffSize;                       /* 292 */
}

/* --------------------------------------------------------------------------
 *  audioDecSendToIOP  (0x27a078)
 *
 *  One frame's worth of the pump.  Work out the writable span of the IOP ring,
 *  work out the readable span of the EE ring, copy the smaller of the two in
 *  whole blocks, and advance both cursors by what actually went.
 *
 *  The two states that copy differ only in how the destination is found.  In
 *  state 1 nothing is playing yet, so the destination is simply the far end of
 *  what has been filled so far and the span runs to the end of the ring.  In
 *  state 2 the SPU2 is reading, so the span is whatever iopGetArea() says is
 *  safely behind it.  States 0 and 3 -- header not read, and paused -- send
 *  nothing at all.
 *
 *  Note that pd0/d0/pd1/d1 are left uninitialised on any state outside 0..3;
 *  the machine cannot reach one, and the ROM does not defend against it.
 * ------------------------------------------------------------------------ */
int audioDecSendToIOP(AudioDec *ad)                                     /* 299 */
{
    int     pd0, d0, pd1, d1;
    u_char *ps0;
    int     s0, s1;
    int     count_sent = 0;
    /* PORT: two more locals the ROM has and the stabs do not.  `pos` is the
     * play position within the IOP ring, and `len` the whole-block byte count
     * available in the EE one -- both are written at a line of their own (321
     * and 335) but their pseudos were coalesced away, so no RSYM survives.
     * The names are the port's; `pos` matches iopGetArea's own parameter. */
    int     pos;
    int     len;
    /* The EE ring's read cursor.  This one really is a repeated subexpression
     * -- the ROM writes it out at both 331 and 337 and GCC CSE'd the pair --
     * named here because spelling it twice buys nothing. */
    int     get;

    switch (ad->state)                                                  /* 308 */
    {
    case 0:                                                             /* 309 */
        return 0;                                                       /* 310 */

    case 1:                                                             /* 313 */
        pd0 = ad->iopBuff + ad->totalBytesSent % ad->iopBuffSize;       /* 314 */
        d0  = ad->iopBuffSize - ad->totalBytesSent;                     /* 315 */
        pd1 = 0;                                                        /* 316 */
        d1  = 0;                                                        /* 317 */
        break;                                                          /* 318 */

    case 2:                                                             /* 320 */
        pos = (sceSdRemote(1, SDR_BLOCK_TRANS_STATUS, AUTODMA_CH)       /* 321 */
               & 0xffffff) - ad->iopBuff;

        iopGetArea(&pd0, &d0, &pd1, &d1, ad, pos);                      /* 323 */
        break;                                                          /* 324 */

    case 3:                                                             /* 326 */
        return 0;                                                       /* 327 */
    }

    get = (ad->put - ad->count + ad->size) % ad->size;
    ps0 = ad->data + get;                                               /* 331 */

    len = ad->count / AUDIODEC_BLOCK * AUDIODEC_BLOCK;                  /* 335 */

    s0  = (ad->size - get < len) ? ad->size - get : len;                /* 337 */
    s1  = len - s0;                                                     /* 338 */

    if (AUDIODEC_BLOCK <= d0 + d1 && AUDIODEC_BLOCK <= s0 + s1)         /* 340 */
    {
        count_sent = sendToIOP2area(pd0, d0, pd1, d1,                   /* 341 */
                                    ps0, s0, ad->data, s1);
    }                                                                   /* 342 */

    ad->count          -= count_sent;                                   /* 344 */

    ad->totalBytesSent += count_sent;                                   /* 346 */
    ad->iopLastPos      = (ad->iopLastPos + count_sent) % ad->iopBuffSize;
                                                                        /* 347 */
    return count_sent;                                                  /* 349 */
}

/* --------------------------------------------------------------------------
 *  iopGetArea  (0x27a228)  static
 *
 *  Which part of the IOP ring may be written, given that the auto-DMA is
 *  currently reading at `pos`.
 *
 *  The answer is the span from the last write to one block short of the play
 *  position, rounded down to whole blocks, split in two if it wraps.  The block
 *  of margin is what stops a write landing under the DMA's nose.
 *
 *  `diff` is tested unsigned so that a play position that has wrapped past the
 *  write cursor -- which reads as negative -- counts as "plenty of room" rather
 *  than "less than a block".
 * ------------------------------------------------------------------------ */
static void iopGetArea(int *pd0, int *d0, int *pd1, int *d1,
                       AudioDec *ad, int pos)                           /* 357 */
{
    int len;
    int diff;

    len = (pos + ad->iopBuffSize - ad->iopLastPos - AUDIODEC_BLOCK)     /* 359 */
              % ad->iopBuffSize;

    diff = pos - ad->iopLastPos;                                        /* 362 */

    if ((u_int)diff < AUDIODEC_BLOCK)                                   /* 365 */
    {
        *pd0 = ad->iopBuff;                                             /* 366 */
        *d0  = 0;                                                       /* 367 */
        *pd1 = ad->iopBuff;                                             /* 368 */
        *d1  = 0;                                                       /* 369 */
    }
    else                                                                /* 370 */
    {
        len = len / AUDIODEC_BLOCK * AUDIODEC_BLOCK;                    /* 374 */

        if (len <= ad->iopBuffSize - ad->iopLastPos)                    /* 376 */
        {
            *pd0 = ad->iopBuff + ad->iopLastPos;                        /* 377 */
            *d0  = len;                                                 /* 378 */
            *pd1 = 0;                                                   /* 379 */
            *d1  = 0;                                                   /* 380 */
        }
        else                                                            /* 381 */
        {
            *pd0 = ad->iopBuff + ad->iopLastPos;                        /* 382 */
            *d0  = ad->iopBuffSize - ad->iopLastPos;                    /* 383 */
            *pd1 = ad->iopBuff;                                         /* 384 */
            *d1  = len - (ad->iopBuffSize - ad->iopLastPos);            /* 385 */
        }
    }
}                                                                       /* 387 */

/* --------------------------------------------------------------------------
 *  sendToIOP2area  (0x27a2f0)  static
 *
 *  Copy up to two source spans into up to two destination spans, and report
 *  how many bytes went.
 *
 *  The two rings wrap at different places, so a copy is one, two or three DMAs
 *  depending on how the seams line up; all three cases are written out.  If the
 *  source is longer than the destination it is trimmed from the tail first --
 *  taking it out of the second span, and only then out of the first.
 *
 *  Nothing here rounds to blocks: the caller has already done that.
 * ------------------------------------------------------------------------ */
static int sendToIOP2area(int pd0, int d0, int pd1, int d1,
                          u_char *ps0, int s0, u_char *ps1, int s1)     /* 395 */
{
    if (d0 + d1 < s0 + s1)                                              /* 397 */
    {
        int diff;

        diff = s0 + s1 - (d0 + d1);                                     /* 398 */

        if (s1 <= diff)                                                 /* 399 */
        {
            s0 -= diff - s1;                                            /* 400 */
            s1  = 0;                                                    /* 401 */
        }
        else                                                            /* 402 */
        {
            s1 -= diff;                                                 /* 403 */
        }
    }                                                                   /* 405 */

    if (d0 <= s0)                                                       /* 410 */
    {
        /* The destination seam falls inside the first source span. */
        sendToIOP(pd0, ps0, d0);                                        /* 411 */
        sendToIOP(pd1, ps0 + d0, s0 - d0);                              /* 412 */
        sendToIOP(pd1 + s0 - d0, ps1, s1);                              /* 413 */
    }
    else                                                                /* 414 */
    {
        if (d0 - s0 <= s1)                                              /* 415 */
        {
            /* ... inside the second one. */
            sendToIOP(pd0, ps0, s0);                                    /* 416 */
            sendToIOP(pd0 + s0, ps1, d0 - s0);                          /* 417 */
            sendToIOP(pd1, ps1 + d0 - s0, s1 - (d0 - s0));              /* 418 */
        }
        else                                                            /* 419 */
        {
            /* ... past the end of the source: one contiguous destination. */
            sendToIOP(pd0, ps0, s0);                                    /* 420 */
            sendToIOP(pd0 + s0, ps1, s1);                               /* 421 */
        }
    }

    return s0 + s1;                                                     /* 424 */
}

/* --------------------------------------------------------------------------
 *  sendToIOP  (0x27a458)  static
 *
 *  One EE -> IOP transfer, run to completion.  The spin on sceSifDmaStat() is
 *  the ROM's -- there is no completion callback in this file -- and the
 *  FlushCache() before it is what makes the just-written EE buffer visible to
 *  the bus.
 * ------------------------------------------------------------------------ */
static int sendToIOP(int dst, u_char *src, int size)                    /* 431 */
{
    sceSifDmaData transData;
    int           did;

    if (size <= 0)                                                      /* 436 */
    {
        return 0;
    }

    /* PORT: the ROM's sceSifDmaData is four u_ints and this stores
     * (u_int)src into `data`.  `src` is a real EE pointer and 8 bytes wide
     * here, so it is assigned as a pointer; `addr` keeps the IOP address the
     * ROM put there.  See sceSifSetDma() in sdk/sif.cpp for what happens to it. */
    transData.data = src;                                               /* 440 */
    transData.addr = (void *)(uintptr_t)dst;                            /* 441 */
    transData.size = size;                                              /* 442 */
    transData.mode = 0;                                                 /* 443 */

    FlushCache(0);                                                      /* 444 */

    did = (int)sceSifSetDma(&transData, 1);                             /* 446 */

    while (sceSifDmaStat((u_int)did) >= 0)                              /* 448 */
    {
    }

    return size;                                                        /* 452 */
}

/* --------------------------------------------------------------------------
 *  changeInputVolume  (0x27a4c8)  static
 *
 *  Set the auto-DMA input volume on both channels of the core, scaled by the
 *  scene's percentage.  This is the only volume control in the file: pause and
 *  resume drive it to 0 and 0x7fff respectively.
 *
 *  BVOLL/BVOLR are the *external input* volumes, not the master ones -- the
 *  auto-DMA feeds the core through the same path an external digital input
 *  would, so this attenuates the movie without touching game audio on the
 *  other core.
 * ------------------------------------------------------------------------ */
static void changeInputVolume(u_int val)                                /* 458 */
{
    /* The stabs put `val` in a0, which says it is never written; the scaled
     * value lives in a register with no symbol of its own.  Either the ROM
     * assigned back to the parameter as here, or it had a local the debug info
     * dropped -- the two are indistinguishable and identical in effect. */
    val = val * audio_vol_percent / 100;                                /* 460 */

    sceSdRemote(1, SDR_SET_PARAM, AUTODMA_CH | SD_P_BVOLL, val);        /* 462 */
    sceSdRemote(1, SDR_SET_PARAM, AUTODMA_CH | SD_P_BVOLR, val);        /* 463 */
}
