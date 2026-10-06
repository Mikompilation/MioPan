/* ==========================================================================
 *  main/main_decls.h
 *
 *  Narrow declarations for game-owned boot/system entry points that are
 *  referenced by main.c but whose owning modules have not been reconstructed
 *  into public headers yet.  Signatures are taken from functions.txt / ZERO2.MAP.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _MAIN_MAIN_DECLS_H
#define _MAIN_MAIN_DECLS_H

#include "../common/variable.h"     /* OPTION_WRK */

void InitCostume(void);
void ClearFlgCtrlInit(void);
void MemoryCardInit(void);
void MemoryCardDebugReqSizeDisp(void);
void Zero2PrintWarningFunc(char *str);
void IngameWrkInit(int chapter_no, int difficulty_label);
void IngameSceneReq(int scene_no);
void EventDataLoadReq(void);
void EventRootStart(void);
void EventMain(void);
void SendIngameEventLoadEndFlg(int flg);
void SetIngameDamageMode(int flg);
void SetIngameDoorMode(int flg);
void SetIngameEffectModeTime(int time);
void SetIngameMenuMode(int flg);
void SetIngameMapMode(int flg);
void SetIngamePauseMode(int flg);
void SetIngameDbgMenu(int flg);
void SetIngameEneDead(int flg);
void SetIngamePhoto(int flg);
void SetIngameMovieRoomMenu(int flg);
void SetIngameMission(int flg);
void SendIngameGameOver(int flg);
void SendIngameGameOverPre(int flg);
void SendIngameEndingNormal(int flg);
void SendIngameEndingHard(int flg);
void FinderBankSetup(void);

int  PadSyncCallback(void);
void PadAnalogMain(void);
void EachDebugMain(void);
void ResetOutReqFlg(void);
void MissionReleaseSaveData(void);

#endif /* _MAIN_MAIN_DECLS_H */
