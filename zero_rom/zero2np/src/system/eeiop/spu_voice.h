/* ==========================================================================
 *  system/eeiop/spu_voice.h
 *
 *  SPU2 voice allocator (spu_voice.c).  One 24-bit occupancy word per core;
 *  a claim is the lowest clear bit.  snd_buffer.c and snd_stream.c are the
 *  only callers -- the stream takes two voices (one per channel) and holds
 *  them for the life of the stream.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_EEIOP_SPU_VOICE_H
#define _SYSTEM_EEIOP_SPU_VOICE_H

#define SPU_VOICE_MAX   24          /* voices per SPU2 core */

/* One occupancy mask per core.  The ROM leaves this non-static (nothing
 * outside spu_voice.c reads it -- the missing `static` looks like an
 * oversight), so it is declared here to match. */
extern int core_voices[2];

void InitSPUVoice(void);
void InitSPUVoiceCore(int core);

/* Lowest free voice on `core`, or -1 if all 24 are taken. */
int  GetSPUVoiceCore(int core);
void FreeSPUVoiceCore(int core, int voice_no);

#endif /* _SYSTEM_EEIOP_SPU_VOICE_H */
