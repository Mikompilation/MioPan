/* ==========================================================================
 *  system/playpss/audiodec.h
 *
 *  The audio half of the PSS movie player -- playpss.a(audiodec.o).
 *
 *  Declarations from ZERO2.MAP, functions.txt and the prototype's own debug
 *  type info.  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_PLAYPSS_AUDIODEC_H
#define _SYSTEM_PLAYPSS_AUDIODEC_H

#include "eetypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The 32-byte "SShd" chunk at the head of a PSS audio elementary stream.
 * `type` 0 is big-endian PCM, 1 little-endian PCM, 2 ADPCM; loopStart and
 * loopEnd are block addresses, not byte offsets.  audioDecEndPut() prints the
 * lot the moment the header has arrived. */
typedef struct                      /* 0x20 */
{
    /* 0x00 */ char id[4];          /* "SShd"                                */
    /* 0x04 */ int  size;
    /* 0x08 */ int  type;
    /* 0x0c */ int  rate;
    /* 0x10 */ int  ch;
    /* 0x14 */ int  interSize;
    /* 0x18 */ int  loopStart;
    /* 0x1c */ int  loopEnd;
} SpuStreamHeader;

/* The "SSbd" chunk that follows it, and then the samples. */
typedef struct                      /* 0x8 */
{
    /* 0x00 */ char id[4];          /* "SSbd"                                */
    /* 0x04 */ int  size;
} SpuStreamBody;

/* The decoder's audio ring and its IOP-side twin.
 *
 * Two buffers are in play and it is worth keeping them apart.  `data` is the
 * EE-side ring the demuxer writes into (audioDecBeginPut / audioDecEndPut);
 * `iopBuff` is the IOP-side ring the SPU2's auto-DMA plays straight out of, and
 * audioDecSendToIOP() is the pump between them.  Everything named iop* is an
 * IOP address or an offset into that ring, which is why they are ints rather
 * than pointers -- they were never EE-addressable.
 *
 * `state` is the machine: 0 still collecting the 40-byte header, 1 header read
 * and the IOP ring pre-filling, 2 playing, 3 paused.
 *
 * PORT: `data` is 4 bytes on the EE and 8 here, so every offset past 0x30 sits
 * 4 bytes higher than the ROM's.  Nothing reads this struct as bytes, and the
 * offsets below are the ROM's, kept as documentation. */
typedef struct                      /* 0x60 on the EE */
{
    /* 0x00 */ int             state;
    /* 0x04 */ SpuStreamHeader sshd;
    /* 0x24 */ SpuStreamBody   ssbd;
    /* 0x2c */ int             hdrCount;      /* header bytes taken so far   */
    /* 0x30 */ u_char         *data;          /* EE ring                     */
    /* 0x34 */ int             put;           /* write cursor into it        */
    /* 0x38 */ int             count;         /* bytes buffered, not yet sent*/
    /* 0x3c */ int             size;
    /* 0x40 */ int             totalBytes;
    /* 0x44 */ int             iopBuff;       /* IOP ring                    */
    /* 0x48 */ int             iopBuffSize;
    /* 0x4c */ int             iopLastPos;    /* write cursor into it        */
    /* 0x50 */ int             iopPausePos;   /* where a pause stopped it    */
    /* 0x54 */ int             totalBytesSent;
    /* 0x58 */ int             iopZero;       /* one sector of silence, IOP  */
    /* 0x5c */ int             iopZeroSize;
} AudioDec;

/* Claim the two rings and prime the SPU2 with silence.  `audio_vol` is a
 * percentage every later volume write is scaled by; `auto_dma_core` is the SPU2
 * core the stream will play on, from ReqQuerySPUTransCoreGet().  Always 1. */
int  audioDecCreate(AudioDec *ad, u_char *buff, int buffSize,
                    int iopBuff, int iopBuffSize,
                    u_char *zeroBuff, int iopZeroBuff, int zeroBuffSize,
                    int audio_vol, int auto_dma_core);
int  audioDecDelete(AudioDec *ad);

/* Start, stop and rewind the auto-DMA.  Pause parks the play position so
 * Resume can pick it up; Reset additionally empties both rings. */
void audioDecStart(AudioDec *ad);
void audioDecPause(AudioDec *ad);
void audioDecResume(AudioDec *ad);
void audioDecReset(AudioDec *ad);

/* The demuxer's producer side.  BeginPut hands back the one or two spans of
 * the EE ring that are free -- two when the write cursor has to wrap -- and
 * EndPut says how many bytes were actually written.  While the header is still
 * being collected the first span is the header itself, so the same loop fills
 * both without knowing which it is on. */
void audioDecBeginPut(AudioDec *ad, u_char **ptr0, int *len0,
                      u_char **ptr1, int *len1);
void audioDecEndPut(AudioDec *ad, int size);

/* True once enough has been shipped to fill the IOP ring, i.e. playback can
 * safely start. */
int  audioDecIsPreset(AudioDec *ad);

/* Ship whole 1024-byte blocks from the EE ring to the IOP one, as far behind
 * the SPU2's read position as fits.  Returns the byte count sent. */
int  audioDecSendToIOP(AudioDec *ad);

#ifdef __cplusplus
}
#endif

#endif /* _SYSTEM_PLAYPSS_AUDIODEC_H */
