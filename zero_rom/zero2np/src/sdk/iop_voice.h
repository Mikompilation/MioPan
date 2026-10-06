/* ==========================================================================
 *  iop_voice.h  (SPU2 voice engine — PC-port implementation)
 *
 *  The synthesiser half of the SPU2 model.  iop_libsd.cpp owns the registers
 *  and the 2 MB of SPU RAM; this owns the 48 voices that actually read that
 *  RAM, decode its ADPCM, run the ADSR envelope, mix them into one stereo
 *  stream and put it on an audio device.  iop_reverb.cpp is the wet half of
 *  that mix.
 *
 *  The split follows the hardware: everything here is driven *from* register
 *  writes (iop_libsd.cpp calls in on key-on, key-off and parameter changes)
 *  and reports back *through* two callbacks -- MioPan_SdVoiceReachedAddress()
 *  for SD_A_IRQA and MioPan_SdSetVoiceEnd() for SD_S_ENDX -- so the ROM's own
 *  polling loops observe real playback rather than the placeholders that stood
 *  in before there was any audio.
 *
 *  Started from MikuPan's src/iop/se/voice.cpp, and has since diverged in four
 *  ways worth knowing:
 *
 *    - Addresses are BYTES here, not half-word indices.  MikuPan's spuRam is
 *      s16[], so its nax/lsa/irqa count half-words; MioPan's sd_ram is u_char[]
 *      and every address the game hands us (0x5050, 0x26ad0, ...) is a byte
 *      offset.  An ADPCM block is 16 bytes / 28 samples.
 *    - There is a real mixer.  MikuPan gives every voice its own SDL audio
 *      stream and lets SDL sum them, resampling inside SDL; that cannot carry
 *      an effect send, because a shared bus needs its contributors aligned in
 *      output time and SDL will not say where a voice's samples landed.  So the
 *      resampler is ours and every voice is summed here.  See iop_voice.cpp.
 *    - No fixed stereo voice pairing.  MikuPan hard-codes voices 0/1 and 46/47
 *      as its music pair; Fatal Frame's iop_stream.c allocates its channels
 *      dynamically.  With one mixer the pairing no longer has to align them at
 *      all -- it only ties their lifetimes together.
 *    - Voice ownership stays in the ROM (iop_snd.c's own tables).  There is no
 *      GetFreeVoice() equivalent here.
 * ======================================================================== */

#ifndef _SDK_IOP_VOICE_H
#define _SDK_IOP_VOICE_H

#include "scetypes.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IOP_VOICE_CORES     2
#define IOP_VOICE_PER_CORE  24
#define IOP_VOICE_NUM       (IOP_VOICE_CORES * IOP_VOICE_PER_CORE)

/* Brings up the audio device, the per-voice streams and the decode thread.
 * Idempotent, and safe to call when there is no audio device at all -- every
 * entry point below degrades to a no-op rather than failing, so a machine with
 * no sound card still runs the game. */
void MioPan_VoiceInit(void);
void MioPan_VoiceShutdown(void);

/* Key on / off, one bit per voice, exactly as SD_S_KON / SD_S_KOFF carry it. */
void MioPan_VoiceKeyOn(int core, u_int mask);
void MioPan_VoiceKeyOff(int core, u_int mask);

/* Register mirrors.  `reg` is the SD_VP_* index (SD_VP_VOLL >> 8 == 0). */
void MioPan_VoiceSetStartAddr(int core, int voice, u_int adrs);
void MioPan_VoiceSetLoopAddr(int core, int voice, u_int adrs);
void MioPan_VoiceSetParam(int core, int voice, int reg, u_short value);
void MioPan_VoiceSetMasterVolume(int core, u_short voll, u_short volr);

/* SD_S_VMIXL / VMIXR / VMIXEL / VMIXER for one core: which voices reach the
 * output, and which are additionally sent to that core's reverb. */
void MioPan_VoiceSetMixMasks(int core, u_int dry_l, u_int dry_r,
                             u_int wet_l, u_int wet_r);

/* Two voices that are channels of one multi-channel stream.  The mixer keeps
 * them sample-aligned on its own; what this still buys is a shared lifetime, so
 * one channel cannot outlive the other and pin SD_VA_NAX.  Retired at the next
 * key-on. */
void MioPan_VoiceSetStereoPair(int core0, int voice0, int core1, int voice1);

/* Real play position, in bytes.  0 when the voice is not sounding, which is
 * what a stopped SPU2 voice reports. */
u_int MioPan_VoiceGetNowAddr(int core, int voice);
int   MioPan_VoiceIsPlaying(int core, int voice);

/* ---- auto-DMA ("external input") --------------------------------------
 *
 * The other way sound reaches a core.  A voice reads ADPCM out of SPU RAM; the
 * auto-DMA streams 16-bit PCM straight from IOP memory into the core's external
 * input mixer, looping over a ring the writer keeps ahead of it.  The movie
 * player is the only user: audiodec.c fills the ring and sceSdBlockTrans()
 * arms it.
 *
 * `ring` is host memory (the IOP arena), `size` its length, `start_off` where
 * to begin.  Stereo is carried as alternating 512-byte blocks, left first --
 * that is the SPU2's own transfer granularity, and it is what the stream's
 * SShd header reports as interSize.
 *
 * GetOffset() answers where playback actually *is*, not where it has been
 * pushed to: audiodec.c subtracts it from its write cursor to decide how far
 * ahead it may fill, so reporting the write position would let it run away. */
void  MioPan_VoiceAutoDmaStart(int core, void *ring, u_int size, u_int start_off);
void  MioPan_VoiceAutoDmaStop(int core);
u_int MioPan_VoiceAutoDmaGetOffset(int core);
void  MioPan_VoiceAutoDmaSetVolume(int core, u_short voll, u_short volr);

#ifdef __cplusplus
}
#endif

#endif /* _SDK_IOP_VOICE_H */
