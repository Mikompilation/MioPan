/* ==========================================================================
 *  libsd.h  (SPU2 sound library — IOP half — PC-port shim)
 *
 *  Every voice register write in the game funnels through iop_snd.c into these
 *  calls, so this is the whole surface the SPU2 needs to present.  Register
 *  "entries" are an address in the top bits and a voice key in the low ones:
 *  bit 0 selects the core, bits 1..5 the voice.
 *
 *  STAGE: the implementation in iop_libsd.cpp is a register model only -- it
 *  records what the game writes and answers reads consistently, but does not
 *  synthesise audio.  That keeps the IOP reconstruction running end to end
 *  (voices key on and off, streams advance, the EE gets its status block)
 *  without an ADPCM mixer.  Producing sound means filling in the same
 *  functions; the register model is the part that would not change.
 * ======================================================================== */

#ifndef _LIBSD_H
#define _LIBSD_H

#include "scetypes.h"

/* ---- core selector, the low bit of every entry ------------------------- */
#define SD_CORE_0           0x00
#define SD_CORE_1           0x01

/* ---- per-voice parameters (sceSdSetParam / sceSdGetParam) -------------- */
#define SD_VP_VOLL          0x0000
#define SD_VP_VOLR          0x0100
#define SD_VP_PITCH         0x0200
#define SD_VP_ADSR1         0x0300
#define SD_VP_ADSR2         0x0400
#define SD_VP_ENVX          0x0500

/* ---- per-core parameters ----------------------------------------------- */
#define SD_P_MMIX           0x0800
#define SD_P_MVOLL          0x0980
#define SD_P_MVOLR          0x0a80
#define SD_P_EVOLL          0x0b80
#define SD_P_EVOLR          0x0c80
#define SD_P_AVOLL          0x0d80
#define SD_P_AVOLR          0x0e80
#define SD_P_BVOLL          0x0f80
#define SD_P_BVOLR          0x1080

/* ---- switches (sceSdSetSwitch / sceSdGetSwitch), one bit per voice ----- */
#define SD_S_KON            0x1500
#define SD_S_KOFF           0x1600
#define SD_S_ENDX           0x1700
#define SD_S_VMIXL          0x1800
#define SD_S_VMIXEL         0x1900
#define SD_S_VMIXR          0x1a00
#define SD_S_VMIXER         0x1b00

/* ---- addresses (sceSdSetAddr / sceSdGetAddr) --------------------------- */
#define SD_A_IRQA           0x1f00  /* interrupt when playback reaches here */
#define SD_A_EEA            0x1d00  /* effect work area end                 */
#define SD_VA_SSA           0x2040  /* sample start                         */
#define SD_VA_LSAX          0x2140  /* loop start                           */
#define SD_VA_NAX           0x2240  /* current play position                */

/* ---- core attributes (sceSdSetCoreAttr) -------------------------------- */
#define SD_C_EFFECT_ENABLE  0x02
#define SD_C_IRQ_ENABLE     0x04
#define SD_C_SPDIF_MODE     0x0a

/* ---- sceSdBlockTrans modes --------------------------------------------- */
#define SD_BLOCK_TRANS_STAT 0x02
#define SD_BLOCK_TRANS_LOOP 0x10
#define SD_BLOCK_TRANS_CONT 0x13

/* SPU2 reverb attributes.  Named as the SDK names them so the field uses in
 * iopSndSetEffect() read the same way. */
typedef struct                      /* 0x14 */
{
    u_int  core;
    u_int  mode;
    short  depth_L;
    short  depth_R;
    int    delay;
    int    feedback;
} sceSdEffectAttrSdk;

#ifdef __cplusplus
extern "C" {
#endif

int    sceSdInit(int flag);

void   sceSdSetParam(u_short entry, u_short value);
u_short sceSdGetParam(u_short entry);
void   sceSdSetSwitch(u_short entry, u_int value);
u_int  sceSdGetSwitch(u_short entry);
void   sceSdSetAddr(u_short entry, u_int value);
u_int  sceSdGetAddr(u_short entry);
void   sceSdSetCoreAttr(u_short entry, u_short value);

/* Uploads `size` bytes of ADPCM from IOP RAM to SPU RAM.  Returns < 0 if the
 * core is busy; every caller retries in a loop. */
int    sceSdVoiceTrans(short chan, u_int mode, u_char *iopaddr, u_int spuaddr, u_int size);
/* The streaming form.  Called with four arguments at some sites and five at
 * others -- `start_addr` is only read for SD_BLOCK_TRANS_CONT. */
int    sceSdBlockTrans(short chan, u_short mode, u_char *iopaddr, u_int size, u_int start_addr);
int    sceSdVoiceTransStatus(short chan, short flag);
/* `flag` 0 answers with the address the auto-DMA is reading from, which is how
 * audiodec.c finds the SPU2's play position inside its own IOP ring. */
int    sceSdBlockTransStatus(short chan, short flag);

int    sceSdSetEffectAttr(int core, void *attr);
int    sceSdClearEffectWorkArea(int core, int channel, int effect_mode);

/* Completion callbacks.  The transfer handler fires when a voice/block upload
 * finishes; the SPU2 handler when playback crosses a core's SD_A_IRQA. */
void  *sceSdSetTransIntrHandler(int core, void *func, void *arg);
void  *sceSdSetSpu2IntrHandler(void *func, void *arg);

/* Host pointer to `size` bytes of SPU RAM at SPU address `spu_adrs`, or NULL if
 * that span leaves the 2 MB.  SPU addresses are offsets into the SPU's own
 * memory, not host pointers -- anything given an SPU destination (a sound bank
 * body, a stream packet) has to come through here rather than dereference it. */
void  *MioPan_SpuRamPointer(u_int spu_adrs, u_int size);

/* Called by the voice engine (iop_voice.cpp) as playback actually advances.
 *
 * ReachedAddress fires the SPU2 handler when a voice crosses its core's
 * SD_A_IRQA, which is the clock the ADPCM streamer runs on -- StreamVoiceThread()
 * arms IRQA at the buffer half it is about to refill and parks until this
 * fires, so raising it early makes the stream free-run through the song.
 * SetVoiceEnd latches SD_S_ENDX, which EndVoiceFindWork() edge-detects to
 * retire an auto-release voice. */
void   MioPan_SdVoiceReachedAddress(int core, u_int nax);
void   MioPan_SdSetVoiceEnd(int core, int voice);

/* Counts SPU2 interrupts fired.  The mixer samples it to end a pass as soon as
 * one lands: it holds the voice lock for the whole pass, so until it lets go
 * the IOP cannot service the interrupt and re-arm SD_A_IRQA -- and a voice that
 * crossed a second armed address before then would match nothing and lose that
 * refill.  See MIX_MAX_BLOCKS_PER_PASS in iop_voice.cpp. */
u_int  MioPan_SdIrqSerial(void);

#ifdef __cplusplus
}
#endif

#endif /* _LIBSD_H */
