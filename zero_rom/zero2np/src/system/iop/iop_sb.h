/* ==========================================================================
 *  system/iop/iop_sb.h
 *
 *  Sound-buffer commands (iop_sb.c): the IOP end of the EE's snd_buffer.c.
 *  Each entry point is the handler for one REQ_SB_* command and does nothing
 *  but unpack the payload onto iop_snd.c's voice primitives.
 *
 *  Reconstructed from iopsys.irx (Feb 6 2004 prototype, SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_IOP_IOP_SB_H
#define _SYSTEM_IOP_IOP_SB_H

#include "iop_snd_def.h"

void SoundBufferPlay(SOUND_BUF_PLAY *sbp);          /* REQ_SB_PLAY     */
void SoundBufferStop(SOUND_BUF_STOP *sbp);          /* REQ_SB_STOP     */
void SoundBufferPause(SOUND_BUF_PAUSE *sbp);        /* REQ_SB_PAUSE    */
void SoundBufferRestart(SOUND_BUF_RESTART *sbp);    /* REQ_SB_RESTART  */
void SoundBufferSetVol(SOUND_BUF_SETVOL *sbp);      /* REQ_SB_SETVOL   */
void SoundBufferSetPitch(SOUND_BUF_SETPITCH *sbp);  /* REQ_SB_SETPITCH */

#endif /* _SYSTEM_IOP_IOP_SB_H */
