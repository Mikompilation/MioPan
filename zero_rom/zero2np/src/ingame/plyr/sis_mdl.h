/* ==========================================================================
 *  ingame/plyr/sis_mdl.h
 *
 *  Sister ("Mayu") model cross-module interface -- the counterpart of
 *  plyr_mdl.h.  sis_mdl.o owns SIS_DATA, the MAN_DATA subclass that holds her
 *  model / animation / shadow / accessory residency, plus her animation
 *  request path, her lighting and shadow draw, and the neck (look-at) target
 *  arbitration.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_PLYR_SIS_MDL_H
#define _INGAME_PLYR_SIS_MDL_H

#include "../../common/save_data.h"          /* MC_SAVE_DATA */
#include "../../system/eeiop/snd3d.h"        /* SND_3D_SET */

#include "../../graphics/motion/mdlwork.h"   /* ANI_CTRL, LOOK_AT_PARAM */

/* Recovered from the stabs entry for _LOOK_TARGET_PRIORITY_MAYU. */
enum LOOK_TARGET_PRIORITY_MAYU
{
    LTP_MAYU_ATTACK_ENEMY = 0,
    LTP_MAYU_ENEMY        = 1,
    LTP_MAYU_EVENT_OBJ    = 2,
    LTP_MAYU_MIO_FAR      = 3,
    LTP_DUMMY_TIRED       = 4,
    LTP_MAYU_MIO_NEAR     = 5,
    LTP_MAYU_BACKGROUND   = 6,
    LTP_MAYU_ITEM         = 7,
    LTP_MAYU_OBJ          = 8,
    LTP_MAYU_MIO_MIDDLE   = 9,
    LTP_MAYU_KAIDAN       = 10,
    LTP_MAYU_LEAST        = 0xffffffff
};

/* ---- residency -------------------------------------------------------- */
void sis_mdlInit(void);
void SetupSisMdl(void);
int  IsReadySisMdl(void);
void ReleaseSisMdl(void);
void sis_mdlSetSave(MC_SAVE_DATA *data);

/* ---- draw lock -------------------------------------------------------- */
void SisterDrawLock(void);
void SisterDrawUnlock(void);
int  SisterIsLocked(void);

/* ---- sister algorithm script buffer ----------------------------------- */
void *sis_mdlGetAlgAdrs(void);

/* ---- per-frame -------------------------------------------------------- */
void sis_mdlMotionWork(void);
void sisterAnimationProc(void);

/* ---- sound bank ------------------------------------------------------- */
int sis_mdlBankPlay(int no, int effect, int loop, int fade_time,
                    SND_3D_SET *s3d, int vol, int pitch);
int sis_mdlBankIsLoopSnd(int no);

/* ---- animation / MIME ------------------------------------------------- */
u_short GetSisterFtype(void);
void    ReqSisterMim(int no, int rev);
void    ReqSisterMimContinue(int no, int rev);
void    StopSisterMim(int no);
int     IsSisterMimParts(int no);
void    ReqSisterAnime(u_char flame);

/* ---- draw ------------------------------------------------------------- */
void DrawSister(void);
void sisterDrawShadow(void);

/* ---- accessors -------------------------------------------------------- */
ANI_CTRL *sis_mdlGetANI_CTRL(void);
ANI_CTRL *sis_mdlGetShadowANI_CTRL(void);
void      sis_mdlGetMATRIX(float (*mtx)[4], int bone_no);

/* ---- neck (look-at) --------------------------------------------------- */
/* fLimitDist is compared against a *squared* distance -- the caller passes
 * the square of the range it wants. */
int  SisNeckRegisterTarget(LOOK_AT_PARAM *param, LOOK_TARGET_PRIORITY_MAYU priority,
                           float fLimitDist);
void SetSisNeckFlg(int flg);
void DbgSisLookPointCtrl(void);

#endif /* _INGAME_PLYR_SIS_MDL_H */
