/* ==========================================================================
 *  system/iop/iop_snd.h
 *
 *  SPU2 voice control (iop_snd.c): key on/off, volume ramps, pitch, ADSR,
 *  reverb routing and the pause/restart handshake.  Everything above it --
 *  iop_sb.c, iop_stream.c, iop_pcmstream.c -- goes through these.
 *
 *  Two calling conventions live side by side and are easy to confuse.  Some
 *  entry points take `core` and `voice_no` separately; PitchSet() and
 *  AdsrSet() instead take a single packed key, `(voice_no << 1) | core`,
 *  named `set` in the ROM's own locals.
 *
 *  Reconstructed from iopsys.irx (Feb 6 2004 prototype, SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_IOP_IOP_SND_H
#define _SYSTEM_IOP_IOP_SND_H

#include "iop_types.h"
#include "iop_snd_def.h"

/* Per-voice pause state, stepped by iopSndMain().  A pause is not a stop: the
 * voice fades to silence (IN), is keyed off and held there (KEEP), and is keyed
 * back on at its saved position when it is let go (OUT).  MyOnVoice() resets it
 * to NONE, which is why OUT only ever runs for one pass. */
typedef enum _PAUSE_PHASE
{
    PAUSE_PHASE_NONE = 0,
    PAUSE_PHASE_IN   = 1,
    PAUSE_PHASE_KEEP = 2,
    PAUSE_PHASE_OUT  = 3
} PAUSE_PHASE;

/* The one non-static global in iop_snd.c, though nothing outside it reads the
 * value -- the reverb type currently loaded in each core's work area. */
extern int now_effect_mode[SPU_CORE_NUM];

/* ---- per-frame ---------------------------------------------------------- */
void FrameInitVoices(void);
void FrameWrkVoices(void);
void iopSndMain(void);

/* ---- bring-up ----------------------------------------------------------- */
void iopSndInit(IOP_SND_INIT *si);
void iopSndSetEffect(SET_SND_EFFECT *eff);

/* ---- voice state -------------------------------------------------------- */
/* `adrs` is an SPU address; MyOnVoiceAutoRelease() additionally arms the
 * auto-release so the voice retires itself at its end point. */
void MyOnVoice(int core, int voice_no, int adrs);
void MyOnVoiceAutoRelease(int core, int voice_no, int adrs);
void MyOnVoiceSub(int core, int voice_no, int adrs);
void MyOffVoice(int core, int voice_no);
void MyPauseVoice(int core, int voice_no);
void MyRestartVoice(int core, int voice_no);
void MyRestartVoiceOffset(int core, int voice_no, int target_core,
                          int target_voice_no, int offset);

/* ---- parameters --------------------------------------------------------- */
/* VolSet() ramps; VolSetDirect() writes the SPU registers immediately. */
void VolSet(int core, int voice_no, VOLSET volset);
void VolSetDirect(int core, int voice_no, VOLSET volset);
/* `set` is the packed (voice_no << 1) | core key. */
void PitchSet(int set, short pitch);
void AdsrSet(int set, unsigned short adsr1, unsigned short adsr2);
/* `voice_shift` here is a 1 << voice_no bit mask, not the packed key. */
void EffectMix(int core, int voice_shift, int on);

/* ---- the silent block --------------------------------------------------- */
/* A short run of silence every retired voice is parked on, so a voice that
 * outlives its sample never reads freed SPU memory. */
void SetLoopAdrsStopBlock(int core, int voice_no);
void SetAdrsStopBlock(int core, int voice_no);
int  IsInStopBlock(int core, int voice_no);
int  IsValidVoice(int iTmpAdrs);

/* ---- EE commands -------------------------------------------------------- */
void SndVoiceStop(VOICE_STOP *sbp);
void iopSndVoiceStop(VOICE_STOP *vs);
void SndVoiceLoopSet(VOICE_LOOP_SET *slp);

#endif /* _SYSTEM_IOP_IOP_SND_H */
