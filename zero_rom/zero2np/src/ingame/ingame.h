/* ==========================================================================
 *  ingame/ingame.h
 *
 *  Story/ingame phase driver interface.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_INGAME_H
#define _INGAME_INGAME_H

#include "../main/gphase.h"
#include "../main/phasefunc.h"
#include "../common/save_data.h"
#include "../graphics/scene/IngameScene.h"

/* InitBeforeGame, ClearBeforeGameInit and IngameWrkInitNotPlayData are file
 * local to ingame.c in the ROM and are deliberately not declared here. */

GPHASE_ID_ENUM IngameDecideNextPhase(void);
void SetIngameListnerInfo(void);
void IngameLoopSEPause(void);
void IngameLoopSERestart(void);
void IngamePlyrNoActJob(void);

void IngameSceneReq(int scene_no);
void IngameEventMsgDispReq(int flg);
void IngameEventFileDispReq(int flg);
void SendIngameGameOver(int flg);
void SendIngameGameOverPre(int flg);
void SendIngameEndingNormal(int flg);
void SendIngameEndingHard(int flg);
void SetIngameDamageMode(int flg);
void SetIngameDoorMode(int flg);
void SendIngameEventLoadEndFlg(int flg);
void SetIngameEventModeFlg(int flg);
void SetIngameEffectModeTime(int time);
void SetIngameMenuMode(int flg);
void SetIngameMapMode(int flg);
void SetIngamePauseMode(int flg);
void SetIngameDbgMenu(int flg);
void SetIngameEneDead(int flg);
void SetIngamePhoto(int flg);
void SetIngameMovieRoomMenu(int flg);
void SetIngameMission(int flg);
int  CheckIngameMission(void);
void ResetOutReqFlg(void);
void IngameWrkInit(int chapter_no, int difficulty_label);
void InitCostume(void);

/* Memory-card save descriptors. */
void SetSave_IngameWrk(MC_SAVE_DATA *data);
void fene_entrySetSave(MC_SAVE_DATA *data);

/* Per-frame camera step and the non-world draw pass.  Defined in ingame.c;
 * the movie preload phase keeps both running behind the fade. */
void IngameCameraMain(void);
void IngameDrawSub(void);

#endif /* _INGAME_INGAME_H */
