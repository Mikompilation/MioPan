/* ==========================================================================
 *  ingame/plyr/plyr_mdl.h
 *
 *  Player model, animation, lighting, shadow, neck, and costume interface.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_PLYR_PLYR_MDL_H
#define _INGAME_PLYR_PLYR_MDL_H

#include "../../common/save_data.h"
#include "../../graphics/graph3d/gra3dTypes.h"
#include "../../graphics/motion/mdlwork.h"
#include "../../system/eeiop/snd3d.h"

enum LOOK_TARGET_PRIORITY_MIO
{
    LTP_MIO_ATTACK_ENEMY = 0,
    LTP_MIO_ENEMY = 1,
    LTP_MIO_EVENT_OBJ = 2,
    LTP_DUMMY_MIO_TIRED = 3,
    LTP_MIO_SPOT_LIGHT = 4,
    LTP_MIO_KAIDAN = 5,
    LTP_MIO_DOOR = 6,
    LTP_MIO_MAYU = 7,
    LTP_MIO_LEAST = -1
};

extern int g_iMaxPlayerAlpha;
extern int g_iMinPlayerAlpha;

void plyr_mdlInit(void);
void plyr_mdlResetReq(void);
void SetupPlyrMdl(int mdl_no, int anm_no, int smdl_no, int acs_no);
int IsReadyPlyrMdl(void);
void ReleasePlyrMdl(void);

void ReqPlayerMim(int no, int rev);
void ReqPlayerMimContinue(int no, int rev);
void StopPlayerMim(int no);
int IsPlayerMimParts(int no);
void plyr_mdlSetSave(MC_SAVE_DATA *data);
u_short GetPlyrFtype(void);
void ReqPlayerAnime(u_char flame);
void plyr_mdlMotionWork(void);
void plyr_mdlGetMATRIX(float (*mtx)[4], int bone_no);
void CalcGirlCoord(int pause_flg);

void playerUseDoorLight(int b);
void playerSetLight(const float *vPosition,
                    const GRA3DEMULATIONLIGHTDATACREATIONDATA *pData);
/* _GetEmulationLightdataCreationDataRef() is deliberately NOT declared here.
 * sis_mdl.c defines its own file-static function of the same name (the ROM
 * emits that one as a STATICPROC), and a visible non-static declaration would
 * clash with it.  Nothing outside plyr_mdl.c calls the player's copy. */
int playerCalcAlpha(const ANI_CTRL *pAC);
void playerDrawShadow(void);
void PlayerDrawLock(void);
void PlayerDrawUnlock(void);
void DrawGirl(void);

ANI_CTRL *plyr_mdlGetANI_CTRL(void);
ANI_CTRL *plyr_mdlGetShadowANI_CTRL(void);
int PlyrNeckRegisterTarget(LOOK_AT_PARAM *param, LOOK_TARGET_PRIORITY_MIO priority);
void SetPlyrNeckFlg(int flg);

int plyr_mdlBankPlay(int no, int effect, int loop, int fade_time,
                     SND_3D_SET *s3d, int vol, int pitch);
int plyr_mdlBankIsLoopSnd(int no);

int GetPlyrMdlNo(void);
void SetPlyrMdlNo(int iMdlNo);
int GetPlyrAcsNo(void);
void SetPlyrAcsNo(int iMdlNo);
int GetSisterMdlNo(void);
void SetSisterMdlNo(int iMdlNo);
int GetSisterAcsNo(void);
void SetSisterAcsNo(int iAcsNo);
void CostumeSetSave(MC_SAVE_DATA *data);

#endif /* _INGAME_PLYR_PLYR_MDL_H */
