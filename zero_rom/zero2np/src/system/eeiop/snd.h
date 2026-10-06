/* ==========================================================================
 *  system/eeiop/snd.h
 *
 *  Sound front end (snd.c).  SndInit() brings up every other sound module in
 *  turn and carves their work areas out of the one buffer ee_iop.c hands it;
 *  SndMain() pumps them once a frame.  The rest is the shared plumbing the
 *  voice owners call into: the mixer group volumes, the volume/pan/pitch
 *  calculation, and the two fade-step helpers.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_EEIOP_SND_H
#define _SYSTEM_EEIOP_SND_H

#include "ee_iop.h"                 /* EEIOP_DEF */
#include "snd_def.h"                /* VOLSET, SND_GROUP */

/* Global mixer state.  `type_vol` has five slots although SND_GROUP only names
 * two -- SndInit() seeds all five and SndSetGroupVolume() bounds-checks
 * against 4. */
#define VOICE_TYPE_MAX 5
typedef struct _SOUND_SYS           /* 0x18 */
{
    /* 0x00 */ char mono;
    /* 0x04 */ int  type_vol[VOICE_TYPE_MAX];    /* 0x00..0x100, 0x100 = unity */
} SOUND_SYS;

/* SPU2 reverb work-area size per sceSdEffectAttr mode.  Left non-static as the
 * ROM does. */
extern int eff_use_size_tbl[];

int   sndGetNeedSize(EEIOP_DEF *def);
void *SndInit(EEIOP_DEF *def, void *buffer);
void  SndInitAfter_ee_iopInit(void);
void  SndMain(void);
void  SndFremaAfterMain(void);
void  SndAllStop(void);

void  SndSetEffect(int core, int eff_vol, int mode);

void  SndSetStereo(void);
void  SndSetMono(void);
int   SndIsMono(void);

void  SndSetGroupVolume(int type, int vol);
int   SndGetGroupVolume(int type);

/* Folds `vol`/`bvol`/the group volume into a stereo pair, `pan` (0..0x80, 0 =
 * hard left) into its balance, and `pitch`/`bpitch`/`play_speed` into a pitch;
 * a non-NULL `s3d` overrides the pan and adds the doppler shift. */
void  SndCalcValue(int vol, int pan, int bvol, int pitch, int bpitch, int type,
                   void *s3d, VOLSET *volset, short *pPitch, float play_speed);

/* Per-frame step that walks `now` to `target` in `time` frames. */
short SndGetFrameAddVol(int target, int now, int time);
/* Applies one such step, clamping at the target. */
int   SndAddFadeVol(int vol, int target_vol, int spd);

/* Counts the free voices by claiming every one of them and handing them all
 * back -- so it must not be called while anything is mid-claim. */
int   GetFreeVoiceNum(void);
int   IsExistFreeVoice(void);

#endif /* _SYSTEM_EEIOP_SND_H */
