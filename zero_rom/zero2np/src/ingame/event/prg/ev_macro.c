// FILE: /home/zero_rom/zero2np/src/ingame/event/prg/ev_macro.c
//
// Event macro interpreter: the opcode set every event program is written in.
//
// An event program is a flat byte stream.  EventExeFuncCall() reads the
// leading byte, looks it up in ev_exe_wrk[], calls the handler, and advances
// the cursor by that opcode's ev_dat_size -- so the operand width lives in the
// table, not in the stream.  Three things make that loop less trivial than it
// sounds:
//
//   * A handler returns 1 when it has finished, 0 when it wants to be called
//     again next frame (a motion still playing, a model still loading, a
//     message still on screen).  Returning 0 stops the loop for this frame
//     *without* advancing the cursor, which is how a single opcode blocks an
//     event for as long as it likes.  ctrl->process is the handler's own
//     scratch state across those re-entries, and the interpreter clears it the
//     moment the opcode completes.
//
//   * EV_IF / EV_ELSE / EV_ELSEIF / EV_ENDIF are dispatched ahead of the
//     if_state gate, because the gate is exactly what they maintain.  While
//     ctrl->if_state says a branch is being skipped (2 or 3), every other
//     opcode is stepped over by operand width alone and never runs.
//
//   * EV_END (0xff) is not in the table at all.  Event_EvEnd() ends the
//     program, and which of the three programs just ended is inferred from the
//     event's state -- an init program ending is what promotes the event to
//     its main program.
//
// The other half of this file is the bookkeeping the opcodes need in order to
// be undoable and saveable: which sounds and streams this event started
// (ev_sound_ctrl / ev_stream_ctrl), which ghosts it spawned (ev_ghost_ctrl),
// and the five ev_save_* blocks that let a stream or a screen effect survive a
// save/load round trip -- EventMacroLoadInit() restarts them after a load.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "ev_macro.h"

#include <stdio.h>                                  // printf (error banners)
#include <string.h>                                 // memset / strcpy / strcat / strlen / strncpy / strrchr
#include <stdlib.h>                                 // rand

#include "ev_change.h"                              // Req_CompulsionSetEventState / SetEventWaitFlg
#include "ev_disp.h"                                // Ev*DispReq / CheckEvDisp2DDataLoad
#include "ev_ene.h"                                 // ev_ene* file registration
#include "ev_exe.h"                                 // EV_EXE_CTRL / EventExeRelease
#include "ev_gaze.h"                                // CEventGazeWrk helpers
#include "ev_get.h"                                 // Get1Byte / Get2Byte / Get4Byte / GetEvState / EvGetRot360
#include "ev_main.h"                                // EV_STATE_* / ev_wrk / ev_sister_gaze
#include "ev_open.h"                                // SetOpenCondSwitch / SetEndCondSwitch
#include "ev_se.h"                                  // ev_se* bank registration
#include "ev_sis.h"                                 // ev_sis* registration
#include "ev_talk.h"                                // TalkTblInit / TalkDataAdd / TalkExeMain / ...
#include "ev_timer.h"                               // EvTimerRegist

#include "../../ingame.h"                           // INGAME_WRK / IngameEventMsgDispReq / IngameDecideNextPhase
#include "../../camera/event_camera.h"              // EventCamera* / EventCameraVCIReq
#include "../../mission.h"                          // MisSetClearType / MisDispDeleteFlg
#include "../../enemy/enemy.h"                      // EneActReq / EneReleaseReq / ChangeEneAlgorithm
#include "../../enemy/fene_entry.h"                 // CFEneEntry
#include "../../menu/ghost_seal_door.h"             // GhostSealDoorAppear / GhostSealDoorRelease
#include "../../item/prg/crystal.h"                 // GetCrystal / LostCrystal
#include "../../item/prg/file.h"                    // FileGet / FileLost / GetFileTypeMaxNum
#include "../../item/prg/item.h"                    // ItemGet / ItemUse / ItemLost / ItemGetPossible
#include "../../item/prg/level_gem.h"               // GetLevelGem / LostLevelGem
#include "../../item/prg/memo.h"                    // UpdateMemo
#include "../../map/MapAnim.h"                      // MapAnimProc
#include "../../map/MapLBuff.h"                     // MapLBuffSetLoadFile / MapLBuffDeleteFile
#include "../../map/MapLoad.h"                      // MapLoad*
#include "../../map/MapObj.h"                       // MapObjItemOn / MapObjItemOff
#include "../../map/MapObjReg.h"                    // MapObjSetHit
#include "../../map/MapSave.h"                      // MapSaveSetStat
#include "../../map/MhCtl.h"                        // MhCtlGetMapHeight
#include "../../../graphics/graph2d/draw_cmn.h"     // DrawCmnFileWindow / DrawCmnButton / DrawCmnCaption
#include "../../../graphics/graph2d/fade.h"          // FadeInReq / FadeOutReq
#include "../../../graphics/graph2d/message.h"       // PrintMsgDef_W / PrintChoice / Mes*
#include "../../../main/gphase.h"                    // GPHASE_ID_ENUM / SetNextGPhase
#include "../../../system/os/system.h"               // SystemBankPlay / SystemBankIsLoopSnd
#include "../../../system/pad/pad.h"                 // pad / paddat / GetPadAnalogRpt
#include "../../plyr/plyr_mdl.h"                     // SetupPlyrMdl / IsReadyPlyrMdl / plyr_mdlBankPlay
#include "../../map/RegDat.h"                       // RegDatGetStPtr4Label / RegDatGetStPtr4Label3
#include "../../door/prg/door.h"                    // Door* / GetDoorLockState
#include "../../map/map_bgm.h"                      // map_bgmFadeIn / map_bgmFadeOut / map_bgmChangeTbl
#include "../../photo/filament.h"                   // CFilament
#include "../../photo/finder.h"                     // RTFillamentMode* / FilamentDraw*
#include "../../photo/m_plyr_camera.h"               // m_plyr_camera
#include "../../photo/n_equip_tray.h"                // CNEquipTrayWrk
#include "../../photo/photo_dat.h"                  // photo_datObjStart / photo_datObjEnd / GetPhotoDatNum
#include "../../plyr/player.h"                      // plyr_wrk / Player* / SetPlayer* / SetPlyr*
#include "../../plyr/sis_mdl.h"                     // SisterDrawLock / sis_mdlBankPlay
#include "../../plyr/sister.h"                      // Sister* / SetSis* / GetSisJoinFlg
#include "../../puzzle/puzzle.h"                    // PuzzleStartReq / GetPuzzleClearInfo
#include "../../savepoint/savepoint.h"              // SavePointStartReq
#include "../../subtitle/subtitle.h"                // SubTitleReq / SubTitleStop / SubTitleIsEnd

#include "../../../common/utility2.h"               // PRINT_ASSERT / PRINT_WARNING
#include "../../../common/zero2_util.h"             // GetObjectPos
#include "../../../graphics/effect/effect.h"        // SetEffects / ResetEffects / CutEffects
#include "../../../graphics/effect/effect_butterfly.h"
#include "../../../graphics/effect/effect_oth.h"    // EffectThunderLightReq
#include "../../ingame_effect.h"                    // IgEffectRenzFlareDispFlgSet
#include "../../../graphics/graph3d/ctl/fixed_array.h"
#include "../../../graphics/graph3d/gra3d.h"        // gra3dMonotoneDrawEnable
#include "../../../graphics/graph3d/gra3dMisc.h"    // gra3dPrelight
#include "../../../system/eeiop/cddat.h"           // GetFileName
#include "../../../system/eeiop/snd_buffer.h"       // SndBufPause / SndBufRestart / SndBufStop
#include "../../../system/eeiop/sndbank.h"          // SndBankPlay / SndBankIsLoopSnd
#include "../../../system/eeiop/snd_util.h"         // snd_utilAutoBDPlay
#include "../../../system/eeiop/stream_auto.h"      // StreamAutoPlay / StreamAutoFadeOut / StreamAutoAllStop

/* ------------------------------------------------------------------------ *
 *  Per-event bookkeeping tables
 * ------------------------------------------------------------------------ */

#define EV_SOUND_CTRL_MAX  30
#define EV_STREAM_CTRL_MAX 2
#define EV_GHOST_CTRL_MAX  10
#define EV_SAVE_STREAM_MAX 2

/* Where the message-choice opcode leaves its result, for IF_CHOICE_SELECT. */
typedef struct                      /* 0x2 */
{
    /* 0x0 */ char csr;
    /* 0x1 */ char sel_num;
} EV_CHOICE_CTRL;

/* One sound started by EV_SOUND_PLAY and friends.  sound_id == -1 is free.
 * `pos` is the sound's slot within its bank, which is what EV_SOUND_STOP
 * matches on -- the same bank entry may be playing more than once. */
typedef struct                      /* 0xc */
{
    /* 0x0 */ int sound_id;
    /* 0x4 */ int file_label;
    /* 0x8 */ int pos;
} EV_SOUND_CTRL;

/* One stream started by EV_STREAM_PLAY and friends.  stream_id == -1 is free. */
typedef struct                      /* 0x8 */
{
    /* 0x0 */ int stream_id;
    /* 0x4 */ int file_label;
} EV_STREAM_CTRL;

/* One ghost an event brought into the world.  ghost_label == -1 is free. */
typedef struct                      /* 0xc */
{
    /* 0x0 */ int    ghost_label;
    /* 0x4 */ u_char ghost_type;
    /* 0x8 */ int    wrk_id;
} EV_GHOST_CTRL;

typedef struct                      /* 0x4 */
{
    /* 0x0 */ void *dither_id;
} EV_EFF_CTRL;

/* The five ev_save_* blocks below are the part of the interpreter's state that
 * has to survive a save/load.  A stream is not a fire-and-forget effect: it is
 * still playing when the player saves, so the opcode that started it records
 * enough to start it again, and EventMacroLoadInit() replays them all. */

typedef struct                      /* 0x10 */
{
    /* 0x0 */ u_char set_flg;
    /* 0x4 */ int    stream_id;
    /* 0x8 */ int    file_label;
    /* 0xc */ int    volume;
} EV_SAVE_STREAM;

typedef struct                      /* 0x18 */
{
    /* 0x00 */ u_char set_flg;
    /* 0x04 */ int    obj_type;
    /* 0x08 */ int    obj_id;
    /* 0x0c */ int    stream_id;
    /* 0x10 */ int    file_label;
    /* 0x14 */ int    volume;
} EV_SAVE_OBJ_STREAM;

typedef struct                      /* 0x20 */
{
    /* 0x00 */ u_char set_flg;
    /* 0x04 */ int    stream_id;
    /* 0x08 */ int    file_label;
    /* 0x0c */ int    volume;
    /* 0x10 */ float  pos[4];
} EV_SAVE_POS_STREAM;

typedef struct                      /* 0x6 */
{
    /* 0x0 */ u_char set_flg;
    /* 0x1 */ u_char type;
    /* 0x2 */ u_char speed;
    /* 0x3 */ u_char alpha;
    /* 0x4 */ u_char alpha_max;
    /* 0x5 */ u_char col_max;
} EV_SAVE_EFF_DITHER;

typedef struct                      /* 0x4 */
{
    /* 0x0 */ int eff_number;
} EV_SAVE_SCREEN_EFFECT;

/* One opcode.  ev_dat_size is the total width of the command in the byte
 * stream (opcode byte included), which is what the interpreter advances by. */
typedef struct                      /* 0x2c */
{
    /* 0x00 */ u_char event_label;
    /* 0x04 */ int  (*event_func)(EV_EXE_CTRL *ctrl_addr);
    /* 0x08 */ int    ev_dat_size;
    /* 0x0c */ char   label_name[32];
} EV_EXE_WRK;

/* One EV_IF predicate.  Reads its own operands straight out of the stream. */
typedef struct                      /* 0x28 */
{
    /* 0x00 */ u_char  if_label;
    /* 0x04 */ u_char (*ev_if_func)(u_char *dat_addr);
    /* 0x08 */ char    if_name[32];
} IF_COND_WRK;

/* ------------------------------------------------------------------------ *
 *  Opcode handlers
 *
 *  Every one is file-local and reached only through ev_exe_wrk[].  A handler
 *  returns 1 when the opcode is complete and the interpreter should move on,
 *  0 to be called again on the next frame with ctrl->process preserved.
 * ------------------------------------------------------------------------ */

static int EvPlyrPosSet(EV_EXE_CTRL *ctrl_addr);
static int EvPlyrHeightSet(EV_EXE_CTRL *ctrl_addr);
static int EvPlyrRotSet(EV_EXE_CTRL *ctrl_addr);
static int EvPlyrPosMove(EV_EXE_CTRL *ctrl_addr);
static int EvPlyrDisp(EV_EXE_CTRL *ctrl_addr);
static int EvPlyrPad(EV_EXE_CTRL *ctrl_addr);
static int EvPlyrFloorChange(EV_EXE_CTRL *ctrl_addr);
static int EvPlyrFinderMode(EV_EXE_CTRL *ctrl_addr);
static int EvPlyrMotionCall(EV_EXE_CTRL *ctrl_addr);
static int EvPlyrFacialCall(EV_EXE_CTRL *ctrl_addr);
static int EvPlyrDamageRequest(EV_EXE_CTRL *ctrl_addr);
static int EvPlyrMotionChange(EV_EXE_CTRL *ctrl_addr);
static int EvPlyrModelChange(EV_EXE_CTRL *ctrl_addr);
static int EvPlyrFlashLightSet(EV_EXE_CTRL *ctrl_addr);
static int EvPlyrGazePointObjSet(EV_EXE_CTRL *ctrl_addr);
static int EvPlyrGazePointPosSet(EV_EXE_CTRL *ctrl_addr);
static int EvPlyrGazePointDefSet(EV_EXE_CTRL *ctrl_addr);
static int EvSisPosSet(EV_EXE_CTRL *ctrl_addr);
static int EvSisHeightSet(EV_EXE_CTRL *ctrl_addr);
static int EvSisRotSet(EV_EXE_CTRL *ctrl_addr);
static int EvSisPosMove(EV_EXE_CTRL *ctrl_addr);
static int EvSisDisp(EV_EXE_CTRL *ctrl_addr);
static int EvSisJoin(EV_EXE_CTRL *ctrl_addr);
static int EvSisLeave(EV_EXE_CTRL *ctrl_addr);
static int EvSisFloorChange(EV_EXE_CTRL *ctrl_addr);
static int EvSisRegist(EV_EXE_CTRL *ctrl_addr);
static int EvSisDelete(EV_EXE_CTRL *ctrl_addr);
static int EvSisMotionCall(EV_EXE_CTRL *ctrl_addr);
static int EvSisFacialCall(EV_EXE_CTRL *ctrl_addr);
static int EvSisDamageRequest(EV_EXE_CTRL *ctrl_addr);
static int EvSisGazePointObjSet(EV_EXE_CTRL *ctrl_addr);
static int EvSisGazePointPosSet(EV_EXE_CTRL *ctrl_addr);
static int EvSisGazePointDefSet(EV_EXE_CTRL *ctrl_addr);
static int EvGhostAppear(EV_EXE_CTRL *ctrl_addr);
static int EvGhostDisappear(EV_EXE_CTRL *ctrl_addr);
static int EvGhostRegist(EV_EXE_CTRL *ctrl_addr);
static int EvGhostDelete(EV_EXE_CTRL *ctrl_addr);
static int EvFloatageGhost(EV_EXE_CTRL *ctrl_addr);
static int EvLockAreaFloatageGhost(EV_EXE_CTRL *ctrl_addr);
static int EvUnLockAreaFloatageGhost(EV_EXE_CTRL *ctrl_addr);
static int EvNpcPosSet(EV_EXE_CTRL *ctrl_addr);
static int EvNpcHeightSet(EV_EXE_CTRL *ctrl_addr);
static int EvNpcRotSet(EV_EXE_CTRL *ctrl_addr);
static int EvNpcPosMove(EV_EXE_CTRL *ctrl_addr);
static int EvNpcDisp(EV_EXE_CTRL *ctrl_addr);
static int EvNpcFloorChange(EV_EXE_CTRL *ctrl_addr);
static int EvSetObjHitCheck(EV_EXE_CTRL *ctrl_addr);
static int EvSetObjPhotoAble(EV_EXE_CTRL *ctrl_addr);
static int EvSetObjEffect(EV_EXE_CTRL *ctrl_addr);
static int EvSetObjVisible(EV_EXE_CTRL *ctrl_addr);
static int EvSetObjReqAction(EV_EXE_CTRL *ctrl_addr);
static int EvSetObjActionType(EV_EXE_CTRL *ctrl_addr);
static int EvPhotoLock(EV_EXE_CTRL *ctrl_addr);
static int EvPhotoUnlock(EV_EXE_CTRL *ctrl_addr);
static int EvItemGet(EV_EXE_CTRL *ctrl_addr);
static int EvItemUse(EV_EXE_CTRL *ctrl_addr);
static int EvItemLost(EV_EXE_CTRL *ctrl_addr);
static int EvFileGet(EV_EXE_CTRL *ctrl_addr);
static int EvFileLost(EV_EXE_CTRL *ctrl_addr);
static int EvFileRead(EV_EXE_CTRL *ctrl_addr);
static int EvCrystalGet(EV_EXE_CTRL *ctrl_addr);
static int EvCrystalLost(EV_EXE_CTRL *ctrl_addr);
static int EvLevelGemGet(EV_EXE_CTRL *ctrl_addr);
static int EvLevelGemLost(EV_EXE_CTRL *ctrl_addr);
static int EvCamSpecialShotGet(EV_EXE_CTRL *ctrl_addr);
static int EvCamAddFunctionGet(EV_EXE_CTRL *ctrl_addr);
static int EvCamEquipFunctionGet(EV_EXE_CTRL *ctrl_addr);
static int EvMemoUpdate(EV_EXE_CTRL *ctrl_addr);
static int EvPuzzleStart(EV_EXE_CTRL *ctrl_addr);
static int EvEvCam(EV_EXE_CTRL *ctrl_addr);
static int EvSetEvCamVCI(EV_EXE_CTRL *ctrl_addr);
static int EvSetEvCamVP(EV_EXE_CTRL *ctrl_addr);
static int EvSetEvCamVR(EV_EXE_CTRL *ctrl_addr);
static int EvSetEvCamRot(EV_EXE_CTRL *ctrl_addr);
static int EvSetEvCamProj(EV_EXE_CTRL *ctrl_addr);
static int EvCamVPSetObj(EV_EXE_CTRL *ctrl_addr);
static int EvCamVRSetObj(EV_EXE_CTRL *ctrl_addr);
static int EvCamSetWorldSwitch(EV_EXE_CTRL *ctrl_addr);
static int EvMonoDisp(EV_EXE_CTRL *ctrl_addr);
static int EvFadeIn(EV_EXE_CTRL *ctrl_addr);
static int EvFadeOut(EV_EXE_CTRL *ctrl_addr);
static int EvSoundLoad(EV_EXE_CTRL *ctrl_addr);
static int EvSoundRelease(EV_EXE_CTRL *ctrl_addr);
static int EvSoundPlay(EV_EXE_CTRL *ctrl_addr);
static int EvSoundStop(EV_EXE_CTRL *ctrl_addr);
static int EvSound3DObjPlay(EV_EXE_CTRL *ctrl_addr);
static int EvSound3DPosPlay(EV_EXE_CTRL *ctrl_addr);
static int EvStreamPlay(EV_EXE_CTRL *ctrl_addr);
static int EvStreamStop(EV_EXE_CTRL *ctrl_addr);
static int EvMapStreamPlay(EV_EXE_CTRL *ctrl_addr);
static int EvMapStreamStop(EV_EXE_CTRL *ctrl_addr);
static int EvMapStreamChange(EV_EXE_CTRL *ctrl_addr);
static int EvStream3DObjPlay(EV_EXE_CTRL *ctrl_addr);
static int EvStream3DPosPlay(EV_EXE_CTRL *ctrl_addr);
static int EvStreamAllStop(EV_EXE_CTRL *ctrl_addr);
static int EvDoorOpen(EV_EXE_CTRL *ctrl_addr);
static int EvDoorClose(EV_EXE_CTRL *ctrl_addr);
static int EvDoorLock(EV_EXE_CTRL *ctrl_addr);
static int EvDoorUnlock(EV_EXE_CTRL *ctrl_addr);
static int EvLoadRequest(EV_EXE_CTRL *ctrl_addr);
static int EvReleaseRequest(EV_EXE_CTRL *ctrl_addr);
static int EvAreaChange(EV_EXE_CTRL *ctrl_addr);
static int EvBackGroundLoad(EV_EXE_CTRL *ctrl_addr);
static int EvAreaSwitch(EV_EXE_CTRL *ctrl_addr);
static int EvDispMsg(EV_EXE_CTRL *ctrl_addr);
static int EvMsgChoice(EV_EXE_CTRL *ctrl_addr);
static int EvTalkTblInit(EV_EXE_CTRL *ctrl_addr);
static int EvTalkDataAdd(EV_EXE_CTRL *ctrl_addr);
static int EvTalkSubtitleAdd(EV_EXE_CTRL *ctrl_addr);
static int EvTalkTypeChange(EV_EXE_CTRL *ctrl_addr);
static int EvTalkExe(EV_EXE_CTRL *ctrl_addr);
static int EvTalkCam(EV_EXE_CTRL *ctrl_addr);
static int EvMoviePlay(EV_EXE_CTRL *ctrl_addr);
static int EvDisp2DStart(EV_EXE_CTRL *ctrl_addr);
static int EvDisp2DEnd(EV_EXE_CTRL *ctrl_addr);
static int EvChapterDispStart(EV_EXE_CTRL *ctrl_addr);
static int EvEvFog(EV_EXE_CTRL *ctrl_addr);
static int EvSetEvFogColor(EV_EXE_CTRL *ctrl_addr);
static int EvSetEvFogDistNear(EV_EXE_CTRL *ctrl_addr);
static int EvSetEvFogDistFar(EV_EXE_CTRL *ctrl_addr);
static int EvSetEvFogDist(EV_EXE_CTRL *ctrl_addr);
static int EvOverLapStart(EV_EXE_CTRL *ctrl_addr);
static int EvOverLapEnd(EV_EXE_CTRL *ctrl_addr);
static int EvFilamentTimerCall(EV_EXE_CTRL *ctrl_addr);
static int EvFilamentCall(EV_EXE_CTRL *ctrl_addr);
static int EvFilamentRelease(EV_EXE_CTRL *ctrl_addr);
static int EvSetObjAlgorithm(EV_EXE_CTRL *ctrl_addr);
static int EvSetPlyrSisDistance(EV_EXE_CTRL *ctrl_addr);
static int EvEvSetState(EV_EXE_CTRL *ctrl_addr);
static int EvEvSetTimer(EV_EXE_CTRL *ctrl_addr);
static int EvEvStop(EV_EXE_CTRL *ctrl_addr);
static int EvChapterLoadRequest(EV_EXE_CTRL *ctrl_addr);
static int EvChangeChapter(EV_EXE_CTRL *ctrl_addr);
static int EvGameDataSave(EV_EXE_CTRL *ctrl_addr);
static int EvGameClear(EV_EXE_CTRL *ctrl_addr);
static int EvSetConditionCheck(EV_EXE_CTRL *ctrl_addr);
static int EvSetButterfly(EV_EXE_CTRL *ctrl_addr);
static int EvMoveButterfly(EV_EXE_CTRL *ctrl_addr);
static int EvReleaseButterfly(EV_EXE_CTRL *ctrl_addr);
static int EvEffDitherStart(EV_EXE_CTRL *ctrl_addr);
static int EvEffDitherEnd(EV_EXE_CTRL *ctrl_addr);
static int EvEffThunderReq(EV_EXE_CTRL *ctrl_addr);
static int EvSetScreenEffect(EV_EXE_CTRL *ctrl_addr);
static int EvSynchroModeStart(EV_EXE_CTRL *ctrl_addr);
static int EvSynchroModeEnd(EV_EXE_CTRL *ctrl_addr);
static int EvItemNameDispStart(EV_EXE_CTRL *ctrl_addr);
static int EvItemNameDispEnd(EV_EXE_CTRL *ctrl_addr);
static int EvMenuLock(EV_EXE_CTRL *ctrl_addr);
static int EvMenuUnlock(EV_EXE_CTRL *ctrl_addr);
static int EvPauseLock(EV_EXE_CTRL *ctrl_addr);
static int EvPauseUnlock(EV_EXE_CTRL *ctrl_addr);
static int EvSetPhotoCurse(EV_EXE_CTRL *ctrl_addr);
static int EvPadWait(EV_EXE_CTRL *ctrl_addr);
static int EvMovieRoomRequest(EV_EXE_CTRL *ctrl_addr);
static int EvGameOverRequest(EV_EXE_CTRL *ctrl_addr);
static int EvSubTitleDispReq(EV_EXE_CTRL *ctrl_addr);
static int EvSubTitle3DObjDispReq(EV_EXE_CTRL *ctrl_addr);
static int EvSubTitle3DPosDispReq(EV_EXE_CTRL *ctrl_addr);
static int EvSubTitleStop(EV_EXE_CTRL *ctrl_addr);
static int EvSetGhostSealDoor(EV_EXE_CTRL *ctrl_addr);
static int EvReleaseGhostSealDoor(EV_EXE_CTRL *ctrl_addr);
static int EvMissionStart(EV_EXE_CTRL *ctrl_addr);
static int EvMissionClear(EV_EXE_CTRL *ctrl_addr);
static int EvMissionFailed(EV_EXE_CTRL *ctrl_addr);
static int EvChapterSelInit(EV_EXE_CTRL *ctrl_addr);
static int EvEvIf(EV_EXE_CTRL *ctrl_addr);
static int EvEvElse(EV_EXE_CTRL *ctrl_addr);
static int EvEvElseIf(EV_EXE_CTRL *ctrl_addr);
static int EvEvEndIf(EV_EXE_CTRL *ctrl_addr);

/* EV_IF predicates, reached through if_cond_wrk[]. */
static u_char EvIfItemMax(u_char *dat_addr);
static u_char EvIfChoiceSelect(u_char *dat_addr);
static u_char EvIfRandom(u_char *dat_addr);
static u_char EvIfWithSister(u_char *dat_addr);
static u_char EvIfPlyrFlashLightHave(u_char *dat_addr);
static u_char EvIfPuzzleClear(u_char *dat_addr);
static u_char EvIfGameDifficulty(u_char *dat_addr);
static u_char EvIfGameClearNum(u_char *dat_addr);

/* ------------------------------------------------------------------------ *
 *  The opcode table.  Indexed directly by the leading byte of a command, so
 *  the order here is the opcode numbering -- event_label repeats the index
 *  and label_name is the authoring name, both carried for the debug prints.
 * ------------------------------------------------------------------------ */

static EV_EXE_WRK ev_exe_wrk[EV_MACRO_LABEL_MAX] =    /* data 30dc78 */
{
    { PLYR_POS_SET,             EvPlyrPosSet,               8, "PLYR_POS_SET" },
    { PLYR_HEIGHT_SET,          EvPlyrHeightSet,            4, "PLYR_HEIGHT_SET" },
    { PLYR_ROT_SET,             EvPlyrRotSet,               4, "PLYR_ROT_SET" },
    { PLYR_POS_MOVE,            EvPlyrPosMove,             12, "PLYR_POS_MOVE" },
    { PLYR_DISP,                EvPlyrDisp,                 4, "PLYR_DISP" },
    { PLYR_PAD,                 EvPlyrPad,                  4, "PLYR_PAD" },
    { PLYR_FLOOR_CHANGE,        EvPlyrFloorChange,          4, "PLYR_FLOOR_CHANGE" },
    { PLYR_FINDER_MODE,         EvPlyrFinderMode,           4, "PLYR_FINDER_MODE" },
    { PLYR_MOTION_CALL,         EvPlyrMotionCall,           4, "PLYR_MOTION_CALL" },
    { PLYR_FACIAL_CALL,         EvPlyrFacialCall,           4, "PLYR_FACIAL_CALL" },
    { PLYR_DAMAGE_REQUEST,      EvPlyrDamageRequest,        4, "PLYR_DAMAGE_REQUEST" },
    { PLYR_MOTION_CHANGE,       EvPlyrMotionChange,         4, "PLYR_MOTION_CHANGE" },
    { PLYR_MODEL_CHANGE,        EvPlyrModelChange,          4, "PLYR_MODEL_CHANGE" },
    { PLYR_FLASHLIGHT_SET,      EvPlyrFlashLightSet,        4, "PLYR_FLASHLIGHT_SET" },
    { PLYR_GAZE_POINT_OBJ_SET,  EvPlyrGazePointObjSet,      8, "PLYR_GAZE_POINT_OBJ_SET" },
    { PLYR_GAZE_POINT_POS_SET,  EvPlyrGazePointPosSet,      8, "PLYR_GAZE_POINT_POS_SET" },
    { PLYR_GAZE_POINT_DEF_SET,  EvPlyrGazePointDefSet,      4, "PLYR_GAZE_POINT_DEF_SET" },
    { SIS_POS_SET,              EvSisPosSet,                8, "SIS_POS_SET" },
    { SIS_HEIGHT_SET,           EvSisHeightSet,             4, "SIS_HEIGHT_SET" },
    { SIS_ROT_SET,              EvSisRotSet,                4, "SIS_ROT_SET" },
    { SIS_POS_MOVE,             EvSisPosMove,              12, "SIS_POS_MOVE" },
    { SIS_DISP,                 EvSisDisp,                  4, "SIS_DISP" },
    { SIS_JOIN,                 EvSisJoin,                  4, "SIS_JOIN" },
    { SIS_LEAVE,                EvSisLeave,                 4, "SIS_LEAVE" },
    { SIS_FLOOR_CHANGE,         EvSisFloorChange,           4, "SIS_FLOOR_CHANGE" },
    { SIS_REGIST,               EvSisRegist,                8, "SIS_REGIST" },
    { SIS_DELETE,               EvSisDelete,                4, "SIS_DELETE" },
    { SIS_MOTION_CALL,          EvSisMotionCall,            4, "SIS_MOTION_CALL" },
    { SIS_FACIAL_CALL,          EvSisFacialCall,            4, "SIS_FACIAL_CALL" },
    { SIS_DAMAGE_REQUEST,       EvSisDamageRequest,         4, "SIS_DAMAGE_REQUEST" },
    { SIS_GAZE_POINT_OBJ_SET,   EvSisGazePointObjSet,       8, "SIS_GAZE_POINT_OBJ_SET" },
    { SIS_GAZE_POINT_POS_SET,   EvSisGazePointPosSet,       8, "SIS_GAZE_POINT_POS_SET" },
    { SIS_GAZE_POINT_DEF_SET,   EvSisGazePointDefSet,       4, "SIS_GAZE_POINT_DEF_SET" },
    { GHOST_APPEAR,             EvGhostAppear,              4, "GHOST_APPEAR" },
    { GHOST_DISAPPEAR,          EvGhostDisappear,           4, "GHOST_DISAPPEAR" },
    { GHOST_REGIST,             EvGhostRegist,              8, "GHOST_REGIST" },
    { GHOST_DELETE,             EvGhostDelete,              8, "GHOST_DELETE" },
    { FLOATAGE_GHOST,           EvFloatageGhost,            4, "FLOATAGE_GHOST" },
    { LOCK_AREA_F_GHOST,        EvLockAreaFloatageGhost,    4, "LOCK_AREA_F_GHOST" },
    { UNLOCK_AREA_F_GHOST,      EvUnLockAreaFloatageGhost,  4, "UNLOCK_AREA_F_GHOST" },
    { NPC_POS_SET,              EvNpcPosSet,                8, "NPC_POS_SET" },
    { NPC_HEIGHT_SET,           EvNpcHeightSet,             8, "NPC_HEIGHT_SET" },
    { NPC_ROT_SET,              EvNpcRotSet,                8, "NPC_ROT_SET" },
    { NPC_POS_MOVE,             EvNpcPosMove,              12, "NPC_POS_MOVE" },
    { NPC_DISP,                 EvNpcDisp,                  4, "NPC_DISP" },
    { NPC_FLOOR_CHANGE,         EvNpcFloorChange,           8, "NPC_FLOOR_CHANGE" },
    { SET_OBJ_HITCHECK,         EvSetObjHitCheck,           8, "SET_OBJ_HITCHECK" },
    { SET_OBJ_PHOTOABLE,        EvSetObjPhotoAble,          8, "SET_OBJ_PHOTOABLE" },
    { SET_OBJ_EFFECT,           EvSetObjEffect,             8, "SET_OBJ_EFFECT" },
    { SET_OBJ_VISIBLE,          EvSetObjVisible,            8, "SET_OBJ_VISIBLE" },
    { SET_OBJ_REQ_ACTION,       EvSetObjReqAction,          8, "SET_OBJ_REQ_ACTION" },
    { SET_OBJ_ACTIONTYPE,       EvSetObjActionType,         8, "SET_OBJ_ACTIONTYPE" },
    { PHOTO_LOCK,               EvPhotoLock,                4, "PHOTO_LOCK" },
    { PHOTO_UNLOCK,             EvPhotoUnlock,              4, "PHOTO_UNLOCK" },
    { ITEM_GET,                 EvItemGet,                  4, "ITEM_GET" },
    { ITEM_USE,                 EvItemUse,                  4, "ITEM_USE" },
    { ITEM_LOST,                EvItemLost,                 4, "ITEM_LOST" },
    { FILE_GET,                 EvFileGet,                  4, "FILE_GET" },
    { FILE_LOST,                EvFileLost,                 4, "FILE_LOST" },
    { FILE_READ,                EvFileRead,                 4, "FILE_READ" },
    { CRYSTAL_GET,              EvCrystalGet,               4, "CRYSTAL_GET" },
    { CRYSTAL_LOST,             EvCrystalLost,              4, "CRYSTAL_LOST" },
    { LEVELGEM_GET,             EvLevelGemGet,              4, "LEVELGEM_GET" },
    { LEVELGEM_LOST,            EvLevelGemLost,             4, "LEVELGEM_LOST" },
    { CAM_SPECIALSHOT_GET,      EvCamSpecialShotGet,        4, "CAM_SPECIALSHOT_GET" },
    { CAM_ADD_FUNCTION_GET,     EvCamAddFunctionGet,        4, "CAM_ADD_FUNCTION_GET" },
    { CAM_EQUIP_FUNCTION_GET,   EvCamEquipFunctionGet,      4, "CAM_EQUIP_FUNCTION_GET" },
    { MEMO_UPDATE,              EvMemoUpdate,               4, "MEMO_UPDATE" },
    { PUZZLE_START,             EvPuzzleStart,              4, "PUZZLE_START" },
    { EV_CAM,                   EvEvCam,                    4, "EV_CAM" },
    { SET_EV_CAM_VCI,           EvSetEvCamVCI,              4, "SET_EV_CAM_VCI" },
    { SET_EV_CAM_VP,            EvSetEvCamVP,               8, "SET_EV_CAM_VP" },
    { SET_EV_CAM_VR,            EvSetEvCamVR,               8, "SET_EV_CAM_VR" },
    { SET_EV_CAM_ROT,           EvSetEvCamRot,              4, "SET_EV_CAM_ROT" },
    { SET_EV_CAM_PROJ,          EvSetEvCamProj,             4, "SET_EV_CAM_PROJ" },
    { EV_CAM_VP_SET_OBJ,        EvCamVPSetObj,             16, "EV_CAM_VP_SET_OBJ" },
    { EV_CAM_VR_SET_OBJ,        EvCamVRSetObj,             16, "EV_CAM_VR_SET_OBJ" },
    { EV_CAM_SET_WORLD_SWITCH,  EvCamSetWorldSwitch,        4, "EV_CAM_SET_WORLD_SWITCH" },
    { MONO_DISP,                EvMonoDisp,                 4, "MONO_DISP" },
    { FADE_IN,                  EvFadeIn,                   8, "FADE_IN" },
    { FADE_OUT,                 EvFadeOut,                  8, "FADE_OUT" },
    { EV_SOUND_LOAD,            EvSoundLoad,                4, "EV_SOUND_LOAD" },
    { EV_SOUND_RELEASE,         EvSoundRelease,             4, "EV_SOUND_RELEASE" },
    { EV_SOUND_PLAY,            EvSoundPlay,                8, "EV_SOUND_PLAY" },
    { EV_SOUND_STOP,            EvSoundStop,                8, "EV_SOUND_STOP" },
    { EV_SOUND3D_OBJ_PLAY,      EvSound3DObjPlay,          12, "EV_SOUND3D_OBJ_PLAY" },
    { EV_SOUND3D_OBJ_STOP,      EvSoundStop,                8, "EV_SOUND3D_OBJ_STOP" },
    { EV_SOUND3D_POS_PLAY,      EvSound3DPosPlay,          12, "EV_SOUND3D_POS_PLAY" },
    { EV_SOUND3D_POS_STOP,      EvSoundStop,                8, "EV_SOUND3D_POS_STOP" },
    { EV_STREAM_PLAY,           EvStreamPlay,              12, "EV_STREAM_PLAY" },
    { EV_STREAM_STOP,           EvStreamStop,               8, "EV_STREAM_STOP" },
    { EV_MAP_STREAM_PLAY,       EvMapStreamPlay,            4, "EV_MAP_STREAM_PLAY" },
    { EV_MAP_STREAM_STOP,       EvMapStreamStop,            4, "EV_MAP_STREAM_STOP" },
    { EV_MAP_STREAM_CHAHGE,     EvMapStreamChange,          8, "EV_MAP_STREAM_CHAHGE" },
    { EV_STREAM3D_OBJ_PLAY,     EvStream3DObjPlay,         16, "EV_STREAM3D_OBJ_PLAY" },
    { EV_STREAM3D_OBJ_STOP,     EvStreamStop,               8, "EV_STREAM3D_OBJ_STOP" },
    { EV_STREAM3D_POS_PLAY,     EvStream3DPosPlay,         16, "EV_STREAM3D_POS_PLAY" },
    { EV_STREAM3D_POS_STOP,     EvStreamStop,               8, "EV_STREAM3D_POS_STOP" },
    { EV_STREAM_ALL_STOP,       EvStreamAllStop,            4, "EV_STREAM_ALL_STOP" },
    { EV_DOOR_OPEN,             EvDoorOpen,                 4, "EV_DOOR_OPEN" },
    { EV_DOOR_CLOSE,            EvDoorClose,                4, "EV_DOOR_CLOSE" },
    { EV_DOOR_LOCK,             EvDoorLock,                 4, "EV_DOOR_LOCK" },
    { EV_DOOR_UNLOCK,           EvDoorUnlock,               4, "EV_DOOR_UNLOCK" },
    { LOAD_REQUEST,             EvLoadRequest,              8, "LOAD_REQUEST" },
    { RELEASE_REQUEST,          EvReleaseRequest,           8, "RELEASE_REQUEST" },
    { AREA_CHANGE,              EvAreaChange,               4, "AREA_CHANGE" },
    { BACK_GROUND_LOAD,         EvBackGroundLoad,           4, "BACK_GROUND_LOAD" },
    { AREA_SWITCH,              EvAreaSwitch,               4, "AREA_SWITCH" },
    { MSG_DISP,                 EvDispMsg,                  4, "MSG_DISP" },
    { MSG_CHOICE,               EvMsgChoice,               12, "MSG_CHOICE" },
    { TALK_TBL_INIT,            EvTalkTblInit,              4, "TALK_TBL_INIT" },
    { TALK_DATA_ADD,            EvTalkDataAdd,              4, "TALK_DATA_ADD" },
    { TALK_SUBTITLE_ADD,        EvTalkSubtitleAdd,          4, "TALK_SUBTITLE_ADD" },
    { TALK_TYPE_CHANGE,         EvTalkTypeChange,           4, "TALK_TYPE_CHANGE" },
    { TALK_EXE,                 EvTalkExe,                  4, "TALK_EXE" },
    { TALK_CAM,                 EvTalkCam,                  4, "TALK_CAM" },
    { MOVIE_PLAY,               EvMoviePlay,                4, "MOVIE_PLAY" },
    { DISP2D_START,             EvDisp2DStart,             12, "DISP2D_START" },
    { DISP2D_END,               EvDisp2DEnd,                4, "DISP2D_END" },
    { CHAPTER_DISP_START,       EvChapterDispStart,         4, "CHAPTER_DISP_START" },
    { EV_FOG,                   EvEvFog,                    4, "EV_FOG" },
    { SET_EV_FOG_COLOR,         EvSetEvFogColor,            8, "SET_EV_FOG_COLOR" },
    { SET_EV_FOG_DIST_NEAR,     EvSetEvFogDistNear,         8, "SET_EV_FOG_DIST_NEAR" },
    { SET_EV_FOG_DIST_FAR,      EvSetEvFogDistFar,          8, "SET_EV_FOG_DIST_FAR" },
    { SET_EV_FOG_DIST,          EvSetEvFogDist,             8, "SET_EV_FOG_DIST" },
    { EV_OVER_LAP_START,        EvOverLapStart,             4, "EV_OVER_LAP_START" },
    { EV_OVER_LAP_END,          EvOverLapEnd,               4, "EV_OVER_LAP_END" },
    { FILAMENT_TIMER_CALL,      EvFilamentTimerCall,        4, "FILAMENT_TIMER_CALL" },
    { FILAMENT_CALL,            EvFilamentCall,             4, "FILAMENT_CALL" },
    { FILAMENT_RELEASE,         EvFilamentRelease,          4, "FILAMENT_RELEASE" },
    { SET_OBJ_ALGORITHM,        EvSetObjAlgorithm,          8, "SET_OBJ_ALGORITHM" },
    { SET_PLYR_SIS_DISTANCE,    EvSetPlyrSisDistance,       4, "SET_PLYR_SIS_DISTANCE" },
    { EV_SET_STATE,             EvEvSetState,               4, "EV_SET_STATE" },
    { EV_SET_TIMER,             EvEvSetTimer,               8, "EV_SET_TIMER" },
    { EV_STOP,                  EvEvStop,                   8, "EV_STOP" },
    { CHAPTER_LOAD_REQUEST,     EvChapterLoadRequest,       4, "CHAPTER_LOAD_REQUEST" },
    { CHANGE_CHAPTER,           EvChangeChapter,            4, "CHANGE_CHAPTER" },
    { GAME_DATA_SAVE,           EvGameDataSave,             4, "GAME_DATA_SAVE" },
    { GAME_CLEAR,               EvGameClear,                4, "GAME_CLEAR" },
    { SET_CONDITION_CHECK,      EvSetConditionCheck,        4, "SET_CONDITION_CHECK" },
    { SET_BUTTERFLY,            EvSetButterfly,            16, "SET_BUTTERFLY" },
    { MOVE_BUTTERFLY,           EvMoveButterfly,           16, "MOVE_BUTTERFLY" },
    { RELEASE_BUTTERFLY,        EvReleaseButterfly,         4, "RELEASE_BUTTERFLY" },
    { EV_EFF_DITHER_START,      EvEffDitherStart,           8, "EV_EFF_DITHER_START" },
    { EV_EFF_DITHER_END,        EvEffDitherEnd,             4, "EV_EFF_DITHER_END" },
    { EV_EFF_THUNDER_REQ,       EvEffThunderReq,           28, "EV_EFF_THUNDER_REQ" },
    { EV_SET_SCREEN_EFFECT,     EvSetScreenEffect,          4, "EV_SET_SCREEN_EFFECT" },
    { SYNCHRO_MODE_START,       EvSynchroModeStart,         4, "SYNCHRO_MODE_START" },
    { SYNCHRO_MODE_END,         EvSynchroModeEnd,           4, "SYNCHRO_MODE_END" },
    { ITEM_NAME_DISP_START,     EvItemNameDispStart,        8, "ITEM_NAME_DISP_START" },
    { ITEM_NAME_DISP_END,       EvItemNameDispEnd,          4, "ITEM_NAME_DISP_END" },
    { MENU_LOCK,                EvMenuLock,                 4, "MENU_LOCK" },
    { MENU_UNLOCK,              EvMenuUnlock,               4, "MENU_UNLOCK" },
    { PAUSE_LOCK,               EvPauseLock,                4, "PAUSE_LOCK" },
    { PAUSE_UNLOCK,             EvPauseUnlock,              4, "PAUSE_UNLOCK" },
    { SET_PHOTO_CURSE,          EvSetPhotoCurse,            4, "SET_PHOTO_CURSE" },
    { EV_PAD_WAIT,              EvPadWait,                  4, "EV_PAD_WAIT" },
    { EV_MOVIE_ROOM_REQUEST,    EvMovieRoomRequest,         4, "EV_MOVIE_ROOM_REQUEST" },
    { GAMEOVER_REQUEST,         EvGameOverRequest,          4, "GAMEOVER_REQUEST" },
    { SUBTITLE_DISP_REQ,        EvSubTitleDispReq,          4, "SUBTITLE_DISP_REQ" },
    { SUBTITLE3D_OBJ_DISP_REQ,  EvSubTitle3DObjDispReq,     8, "SUBTITLE3D_OBJ_DISP_REQ" },
    { SUBTITLE3D_POS_DISP_REQ,  EvSubTitle3DPosDispReq,    16, "SUBTITLE3D_POS_DISP_REQ" },
    { SUBTITLE_STOP,            EvSubTitleStop,             4, "SUBTITLE_STOP" },
    { SET_GHOST_SEAL_DOOR,      EvSetGhostSealDoor,         4, "SET_GHOST_SEAL_DOOR" },
    { RELEASE_GHOST_SEAL_DOOR,  EvReleaseGhostSealDoor,     4, "RELEASE_GHOST_SEAL_DOOR" },
    { EV_MISSION_START,         EvMissionStart,             4, "EV_MISSION_START" },
    { EV_MISSION_CLEAR,         EvMissionClear,             4, "EV_MISSION_CLEAR" },
    { EV_MISSION_FAILED,        EvMissionFailed,            4, "EV_MISSION_FAILED" },
    { CHAPTER_SEL_INIT,         EvChapterSelInit,           8, "CHAPTER_SEL_INIT" },
    { EV_IF,                    EvEvIf,                     8, "EV_IF" },
    { EV_ELSE,                  EvEvElse,                   4, "EV_ELSE" },
    { EV_ELSEIF,                EvEvElseIf,                 8, "EV_ELSEIF" },
    { EV_ENDIF,                 EvEvEndIf,                  4, "EV_ENDIF" },
};

static IF_COND_WRK if_cond_wrk[IF_COND_MAX] =         /* data 30fa38 */
{
    { IF_ITEM_MAX_CHECK,         EvIfItemMax,            "ITEM_MAX_CHECK" },
    { IF_SELECT_CHOICE,          EvIfChoiceSelect,       "SELECT_CHOICE" },
    { IF_RANDOM_CHECK,           EvIfRandom,             "RANDOM_CHECK" },
    { IF_WITH_SISTER,            EvIfWithSister,         "WITH_SISTER" },
    { IF_PLYR_FLASH_LIGHT_HAVE,  EvIfPlyrFlashLightHave, "PLYR_FLASH_LIGHT_HAVE" },
    { IF_PUZZLE_CLEAR,           EvIfPuzzleClear,        "PUZZLE_CLEAR" },
    { IF_GAME_DIFFICULTY,        EvIfGameDifficulty,     "GAME_DIFFICULTY" },
    { IF_GAME_CLEAR_NUM,         EvIfGameClearNum,       "GAME_CLEAR_NUM" },
};

/* ------------------------------------------------------------------------ *
 *  File-local helpers
 * ------------------------------------------------------------------------ */

static void Event_EvEnd(EV_EXE_CTRL *exe_ctrl);
static int  EvWaitFinderOff(EV_EXE_CTRL *ctrl_addr);
static void EvBankSoundSub(int bd_file_no, int no, SND_3D_SET *set);
static void EvStreamStopSub(int file_label, int fade_out_time);
static int  GetNextEventCom(EV_EXE_CTRL *ctrl);

static void EventMacroLoad_StreamInit(void);
static void EventMacroLoad_ObjStreamInit(void);
static void EventMacroLoad_PosStreamInit(void);
static void EventMacroLoad_EffDitherInit(void);
static void EventMacroLoad_ScreenEffectInit(void);

static void EvChoiceCtrlInit(void);
static void EvSoundCtrlInit(void);
static void EvStreamCtrlInit(void);
static void EvGhostCtrlInit(void);
static void EvEffCtrlInit(void);
static void EvSaveStreamInit(void);
static void EvSaveObjStreamInit(void);
static void EvSavePosStreamInit(void);
static void EvSaveEffDitherInit(void);
static void EvSaveScreenEffectInit(void);

static void Regist_SoundID(int file_label, int pos, int sound_id);
static void Del_SoundID(int sound_id);
static void Regist_StreamID(int stream_id, int file_label);
static void Del_StreamID(int stream_id);
static int  IsRegist_StreamID(int file_label);

static void  SetEvEffDitherID(void *dither_id);
static void  DelEvEffDitherID(void);
static void *GetEvEffDitherID(void);

static void SetEvSaveStream(int stream_id, int file_label, int volume);
static void DelEvSaveStream(int stream_id);
static void SetEvSaveObjStream(int stream_id, int obj_type, int obj_id, int file_label, int volume);
static void DelEvSaveObjStream(int stream_id);
static void SetEvSavePosStream(int stream_id, float *pos, int file_label, int volume);
static void DelEvSavePosStream(int stream_id);
static void SetEvSaveEffDither(u_char type, u_char speed, u_char alpha, u_char alpha_max, u_char col_max);
static void SetEvSaveScreenEffect(int eff_number);

/* ------------------------------------------------------------------------ *
 *  Interpreter state
 * ------------------------------------------------------------------------ */

/* Nesting depth of PLYR_PAD off requests.  The pad only comes back when the
 * last event that took it away gives it back, so events that overlap cannot
 * unlock each other's input. */
static int                                            evPlyrLockCnt;        /* sbss 3f4c4c */

static char                                           synchro_mode_flg;     /* sdata 3f042e */

static EV_CHOICE_CTRL                                 ev_choice_ctrl;       /* sbss 3f4c30 */
static EV_EFF_CTRL                                    ev_eff_ctrl;          /* sbss 3f4c38 */

static fixed_array<EV_SOUND_CTRL,  EV_SOUND_CTRL_MAX>  ev_sound_ctrl;       /* bss 47a1b0 */
static fixed_array<EV_STREAM_CTRL, EV_STREAM_CTRL_MAX> ev_stream_ctrl;      /* bss 47a318 */
static fixed_array<EV_GHOST_CTRL,  EV_GHOST_CTRL_MAX>  ev_ghost_ctrl;       /* bss 47a328 */

static fixed_array<EV_SAVE_STREAM,     EV_SAVE_STREAM_MAX> ev_save_stream;      /* bss 47a3a0 */
static fixed_array<EV_SAVE_OBJ_STREAM, EV_SAVE_STREAM_MAX> ev_save_obj_stream;  /* bss 47a3c0 */
static fixed_array<EV_SAVE_POS_STREAM, EV_SAVE_STREAM_MAX> ev_save_pos_stream;  /* bss 47a3f0 */
static EV_SAVE_EFF_DITHER                                  ev_save_eff_dither;  /* sbss 3f4c40 */
static EV_SAVE_SCREEN_EFFECT                               ev_save_screen_effect; /* sbss 3f4c48 */

/* ------------------------------------------------------------------------ *
 *  Init / load
 * ------------------------------------------------------------------------ */

void EventMacroInit(void)
{                                                                       /* 784 */
    evPlyrLockCnt = 0;

    EvChoiceCtrlInit();                                                 /* 790 */
    EvSoundCtrlInit();
    EvStreamCtrlInit();                                                 /* 796 */
    EvGhostCtrlInit();
    EvEffCtrlInit();

    synchro_mode_flg = 0;

    EvSaveStreamInit();                                                 /* 808 */
    EvSaveObjStreamInit();
    EvSavePosStreamInit();                                              /* 813 */
    EvSaveEffDitherInit();
    EvSaveScreenEffectInit();                                           /* 816 */
}

/* Called once the save block has been read back in.  Every ev_save_* entry
 * that was live when the game was saved is started again here -- the opcodes
 * that created them are long past and will not run a second time. */
void EventMacroLoadInit(void)
{                                                                       /* 829 */
    EventMacroLoad_StreamInit();
    EventMacroLoad_ObjStreamInit();
    EventMacroLoad_PosStreamInit();
    EventMacroLoad_EffDitherInit();
    EventMacroLoad_ScreenEffectInit();                                  /* 832 */
}

static void EventMacroLoad_StreamInit(void)
{                                                                       /* 855 */
    int i;

    for (i = 0; i < EV_SAVE_STREAM_MAX; i++) {
        if (ev_save_stream[i].set_flg == 1) {
            ev_save_stream[i].stream_id =
                StreamAutoPlay(ev_save_stream[i].file_label,
                               ev_save_stream[i].file_label - 1,
                               0x11, 0, 1, ev_save_stream[i].volume, 5,
                               (SND_3D_SET *)0);
            Regist_StreamID(ev_save_stream[i].stream_id,
                            ev_save_stream[i].file_label);
        }
    }                                                                   /* 867 */
}

static void EventMacroLoad_ObjStreamInit(void)
{                                                                       /* 875 */
    SND_3D_SET snd_3d_data;
    float      obj_pos[4];
    int        i;

    memset(&snd_3d_data, 0, sizeof(snd_3d_data));
    memset(&obj_pos, 0, sizeof(obj_pos));

    for (i = 0; i < EV_SAVE_STREAM_MAX; i++) {
        if (ev_save_obj_stream[i].set_flg == 1) {
            /* The object may have gone away while the game was saved; if it
             * has, the stream comes back as a plain 2D one. */
            if (GetObjectPos(obj_pos, (u_char)ev_save_obj_stream[i].obj_type,
                             ev_save_obj_stream[i].obj_id) == 0) {       /* 878 */
                ev_save_obj_stream[i].stream_id =
                    StreamAutoPlay(ev_save_obj_stream[i].file_label,
                                   ev_save_obj_stream[i].file_label - 1,
                                   0x11, 0, 1, ev_save_obj_stream[i].volume, 5,
                                   (SND_3D_SET *)0);                     /* 879 */
            } else {
                snd_3d_data.pos = (sceVu0FVECTOR *)obj_pos;
                ev_save_obj_stream[i].stream_id =
                    StreamAutoPlay(ev_save_obj_stream[i].file_label,
                                   ev_save_obj_stream[i].file_label - 1,
                                   0x11, 0, 1, ev_save_obj_stream[i].volume, 5,
                                   &snd_3d_data);                        /* 883 */
            }

            Regist_StreamID(ev_save_obj_stream[i].stream_id,
                            ev_save_obj_stream[i].file_label);
        }
    }                                                                   /* 904 */
}

static void EventMacroLoad_PosStreamInit(void)
{                                                                       /* 912 */
    SND_3D_SET snd_3d_data;
    int        i;

    memset(&snd_3d_data, 0, sizeof(snd_3d_data));

    for (i = 0; i < EV_SAVE_STREAM_MAX; i++) {
        if (ev_save_pos_stream[i].set_flg == 1) {                       /* 915 */
            snd_3d_data.pos = (sceVu0FVECTOR *)ev_save_pos_stream[i].pos;
            ev_save_pos_stream[i].stream_id =
                StreamAutoPlay(ev_save_pos_stream[i].file_label,
                               ev_save_pos_stream[i].file_label - 1,
                               0x11, 0, 1, ev_save_pos_stream[i].volume, 5,
                               &snd_3d_data);                           /* 919 */
            Regist_StreamID(ev_save_pos_stream[i].stream_id,
                            ev_save_pos_stream[i].file_label);
        }
    }                                                                   /* 933 */
}

static void EventMacroLoad_EffDitherInit(void)
{                                                                       /* 941 */
    void *dither_id;

    if (ev_save_eff_dither.set_flg == 1) {                              /* 948 */
        /* The two ramp bytes went out in variadic slots the DITHER handler
         * read as doubles, so a saved alpha of 0x40 reached it as the double
         * whose bit pattern is 0x40 -- a denormal that stores as exactly 0.0f.
         * The typed entry point takes real floats, so the ROM's result is
         * written out rather than reproduced by accident; the saved bytes are
         * named in the comment so the intent is not lost.  ROM behaviour, kept.
         *
         *     alpha = ev_save_eff_dither.alpha, spd = ev_save_eff_dither.speed
         */
        dither_id = SetEffects_DITHER(2, ev_save_eff_dither.type,       /* 951 */
                                      0.0f, 0.0f,
                                      ev_save_eff_dither.alpha_max,
                                      ev_save_eff_dither.col_max,
                                      0, 0, 0);
        SetEvEffDitherID(dither_id);                                    /* 953 */
    }
}

static void EventMacroLoad_ScreenEffectInit(void)
{                                                                       /* 962 */
    if (ev_save_screen_effect.eff_number != 0) {                        /* 966 */
        EffectSetScreenEffectNo(ev_save_screen_effect.eff_number);      /* 967 */
    }
}

static void EvChoiceCtrlInit(void)
{                                                                       /* 976 */
    memset(&ev_choice_ctrl, 0, sizeof(ev_choice_ctrl));                 /* 979 */
}

static void EvSoundCtrlInit(void)
{                                                                       /* 987 */
    int i;

    for (i = 0; i < EV_SOUND_CTRL_MAX; i++) {                           /* 991 */
        ev_sound_ctrl[i].sound_id   = -1;
        ev_sound_ctrl[i].file_label = -1;
        ev_sound_ctrl[i].pos        = -1;
    }                                                                   /* 995 */
}

static void EvStreamCtrlInit(void)
{                                                                       /* 1003 */
    int i;

    for (i = 0; i < EV_STREAM_CTRL_MAX; i++) {                          /* 1007 */
        ev_stream_ctrl[i].stream_id  = -1;
        ev_stream_ctrl[i].file_label = -1;
    }                                                                   /* 1010 */
}

static void EvGhostCtrlInit(void)
{                                                                       /* 1018 */
    int i;

    for (i = 0; i < EV_GHOST_CTRL_MAX; i++) {                           /* 1022 */
        ev_ghost_ctrl[i].ghost_label = -1;
        ev_ghost_ctrl[i].ghost_type  = 0xff;
        ev_ghost_ctrl[i].wrk_id      = -1;
    }                                                                   /* 1026 */
}

static void EvEffCtrlInit(void)
{
    ev_eff_ctrl.dither_id = (void *)0;                                  /* 1038 */
}

static void EvSaveStreamInit(void)
{                                                                       /* 1045 */
    memset(&ev_save_stream[0], 0, sizeof(ev_save_stream));              /* 1048 */
}

static void EvSaveObjStreamInit(void)
{                                                                       /* 1055 */
    memset(&ev_save_obj_stream[0], 0, sizeof(ev_save_obj_stream));      /* 1058 */
}

static void EvSavePosStreamInit(void)
{                                                                       /* 1065 */
    memset(&ev_save_pos_stream[0], 0, sizeof(ev_save_pos_stream));      /* 1068 */
}

static void EvSaveEffDitherInit(void)
{                                                                       /* 1075 */
    memset(&ev_save_eff_dither, 0, sizeof(ev_save_eff_dither));         /* 1078 */
}

static void EvSaveScreenEffectInit(void)
{
    ev_save_screen_effect.eff_number = 0;                               /* 1088 */
}

/* ------------------------------------------------------------------------ *
 *  The interpreter
 * ------------------------------------------------------------------------ */

void EventExeFuncCall(EV_EXE_CTRL *exe_ctrl)
{                                                                       /* 1100 */
    u_char  loop_flg;
    u_char  com;
    u_char  ret;
    u_char *dat_addr;

    loop_flg = 1;                                                       /* 1107 */
    dat_addr = exe_ctrl->event_addr;                                    /* 1110 */

    do {
        com = Get1Byte(dat_addr);                                       /* 1117 */

        if (com == EV_END) {                                            /* 1120 */
            /* Not a table entry: the end of a program is handled here so that
             * it can end the event rather than just the opcode. */
            loop_flg = 0;
            Event_EvEnd(exe_ctrl);                                      /* 1124 */
        }                                                               /* 1127 */
        else if ((com >= EV_IF) && (com <= EV_ENDIF)) {
            /* Dispatched ahead of the if_state gate below, because these four
             * are what set it -- gating them would strand a skipped branch. */
            if (ev_exe_wrk[com].event_func != 0) {                      /* 1134 */
                (*ev_exe_wrk[com].event_func)(exe_ctrl);                /* 1136 */
                dat_addr += ev_exe_wrk[com].ev_dat_size;                /* 1138 */
                exe_ctrl->event_addr = dat_addr;                        /* 1141 */
            }
        }
        else {
            if ((exe_ctrl->if_state == 2) || (exe_ctrl->if_state == 3)) { /* 1147 */
                /* Inside a branch that was not taken: step over the command
                 * on operand width alone, without running it. */
                dat_addr += ev_exe_wrk[com].ev_dat_size;                /* 1150 */
                exe_ctrl->event_addr = dat_addr;                        /* 1151 */
            }
            else if (ev_exe_wrk[com].event_func != 0) {                 /* 1155 */
                ret = (*ev_exe_wrk[com].event_func)(exe_ctrl);          /* 1158 */

                if (ret == 1) {                                         /* 1165 */
                    /* Opcode complete: advance, and hand the next one a clean
                     * process field. */
                    dat_addr += ev_exe_wrk[com].ev_dat_size;            /* 1168 */
                    exe_ctrl->process    = 0;                           /* 1170 */
                    exe_ctrl->event_addr = dat_addr;                    /* 1172 */
                }
                else if (ret == 0) {                                    /* 1174 */
                    /* Wants another frame.  An init program cannot span
                     * frames -- it runs off a stack-local control block in
                     * SetEventInitStatus() -- so blocking in one is a bug in
                     * the event data. */
                    if (GetEvState(exe_ctrl->event_id) == EV_STATE_INIT) { /* 1178 */
                        PRINT_ASSERT("Error! EventExeFuncCall EventID %d",
                                     exe_ctrl->event_id);               /* 1179 */
                    }

                    loop_flg = 0;                                       /* 1182 */
                }
            }
            else {
                loop_flg = 0;
                printf("***********************************************\n");   /* 1187 */
                printf("*  Error!! The function is not registered!!!  *\n");   /* 1188 */
                printf("*     EventExeFuncCall() Command Num %3d      *\n", com); /* 1189 */
                printf("***********************************************\n");   /* 1190 */
                PRINT_ASSERT("Error!! EventExeFuncCall()  "
                             "The function is not registered");         /* 1191 */
            }
        }
    } while (loop_flg);                                                 /* 1193 */
}

/* EV_END.  Which program just ended is read back off the event's state: an
 * init program ending promotes the event to its main program, a main program
 * ending arms its close conditions, and an end program ending is simply the
 * event finishing for good. */
static void Event_EvEnd(EV_EXE_CTRL *exe_ctrl)
{
    int event_id;

    event_id = exe_ctrl->event_id;

    if (exe_ctrl->if_state != 0) {                                      /* 1214 */
        printf("**************************************************\n"); /* 1215 */
        printf("*  Error!! It has been forgotten to set ENDIF!!  *\n"); /* 1216 */
        printf("*           Event_EvEnd() Event ID %3d           *\n", event_id); /* 1217 */
        printf("**************************************************\n"); /* 1218 */
        PRINT_ASSERT("Error! Event_EvEnd() Event ID %3d ", event_id);   /* 1219 */
    }

    EventExeRelease(event_id);                                          /* 1223 */

    switch (GetEvState(event_id)) {                                     /* 1225 */
    case EV_STATE_INIT:
        SetEventExeStatus(event_id);                                    /* 1229 */
        break;

    case EV_STATE_EXE:
        EventSetCloseCondition(event_id);                               /* 1232 */
        break;

    case EV_STATE_END:                                                  /* 1236 */
    case EV_STATE_LOCK:
        break;

    default:
        printf("**************************************************\n"); /* 1244 */
        printf("*  Error!! The state of the event is illegal!!   *\n"); /* 1245 */
        printf("*           Event_EvEnd() Event ID %3d           *\n", event_id); /* 1246 */
        printf("**************************************************\n"); /* 1247 */
        PRINT_ASSERT("Error! Event_EvEnd() Event ID %3d ", event_id);   /* 1248 */
        break;
    }
}                                                                       /* 1250 */

/* ------------------------------------------------------------------------ *
 *  Player
 * ------------------------------------------------------------------------ */

/* Coordinates in an event program are authored per room, as signed 16-bit
 * integers relative to that room's origin -- so every position opcode adds the
 * room offset MapLoadGetOffset() hands back before using them. */
static int EvPlyrPosSet(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 1284 */
    float   set_plyr_pos[4];
    float   mst_plyr_mst[4];

    u_char *dat_addr = ctrl_addr->event_addr;
    float *offset = MapLoadGetOffset(GetPlyrAreaNo());                         /* 1288 */
    printf("addr [%p], AreaNo [%d]\n", offset, GetPlyrAreaNo());        /* 1290 */

    mst_plyr_mst[0] = (float)(short int)Get2Byte(dat_addr + 2) + offset[0]; /* 1292 */
    /* Y is not authored: the height comes from the map below. */
    mst_plyr_mst[1] = plyr_wrk.cmn_wrk.mbox.pos[1];                     /* 1295 */
    mst_plyr_mst[2] = (float)(short int)Get2Byte(dat_addr + 4) + offset[2]; /* 1296 */
    mst_plyr_mst[3] = 0.0f;                                             /* 1298 */

    MhCtlGetMapHeight(set_plyr_pos, mst_plyr_mst, GetPlyrAreaNo(), 0);  /* 1301 */
    SetPlayerPos(set_plyr_pos);                                         /* 1306 */

    return 1;                                                           /* 1311 */
}

static int EvPlyrHeightSet(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 1333 */
    u_char *dat_addr;
    float  *offset;
    float   set_plyr_height[4];

    dat_addr = ctrl_addr->event_addr;

    offset = MapLoadGetOffset(GetPlyrAreaNo());                         /* 1336 */

    set_plyr_height[0] = plyr_wrk.cmn_wrk.mbox.pos[0];                  /* 1339 */
    set_plyr_height[1] = (float)(short int)Get2Byte(dat_addr + 2) + offset[1]; /* 1340 */
    set_plyr_height[2] = plyr_wrk.cmn_wrk.mbox.pos[2];                  /* 1342 */
    set_plyr_height[3] = 0.0f;

    SetPlayerPos(set_plyr_height);                                      /* 1346 */

    return 1;                                                           /* 1350 */
}

static int EvPlyrRotSet(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 1367 */
    short int rot360;
    float     set_plyr_rot[4];

    rot360 = Get2Byte(ctrl_addr->event_addr + 2);                       /* 1373 */

    set_plyr_rot[0] = plyr_wrk.cmn_wrk.mbox.rot[0];                     /* 1376 */
    set_plyr_rot[1] = EvGetRot360(rot360);                              /* 1378 */
    set_plyr_rot[2] = plyr_wrk.cmn_wrk.mbox.rot[2];                     /* 1380 */
    set_plyr_rot[3] = 1.0f;                                             /* 1381 */

    SetPlayerRot(set_plyr_rot);                                         /* 1385 */

    return 1;                                                           /* 1388 */
}

/* PLYR_POS_MOVE is reserved: the opcode exists and carries 12 bytes of
 * operands, but no implementation was ever written.  Returning 0 parks the
 * event on it forever, which is presumably why no event data uses it. */
static int EvPlyrPosMove(EV_EXE_CTRL *ctrl_addr)
{
    return 0;                                                           /* 1403 */
}

static int EvPlyrDisp(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 1418 */
    u_char sw = Get1Byte(ctrl_addr->event_addr + 2);                           /* 1424 */

    if (sw == 1) {                                                      /* 1427 */
        printf("EvSwitch()\n");                                         /* 1429 */
        PlayerDrawUnlock();                                             /* 1430 */
    } else if (sw == 0) {                                               /* 1432 */
        PlayerDrawLock();                                               /* 1434 */
    } else {
        printf("*************************************************************\n"); /* 1438 */
        printf("*     Error!! The value of the event data is illegal!!!     *\n"); /* 1439 */
        printf("*               PLYR_DISPM   Event ID %3d                   *\n", ctrl_addr->event_id); /* 1440 */
        printf("*************************************************************\n"); /* 1441 */
        PRINT_ASSERT("Error!! PLYR_DISPM EventID %3d", ctrl_addr->event_id); /* 1442 */
    }

    return 1;                                                           /* 1447 */
}

/* Take the pad away from the player, or give it back.  Nested: two events that
 * both lock input each have to unlock before the player moves again, and the
 * phase is only recomputed on the transition back to zero. */
static int EvPlyrPad(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 1462 */
    u_char sw;

    sw = Get1Byte(ctrl_addr->event_addr + 2);                           /* 1468 */

    if (sw == 1) {                                                      /* 1471 */
        IngameEventMsgDispReq(0);                                       /* 1473 */
        evPlyrLockCnt--;                                                /* 1475 */
        PRINT_WARNING("PAD_ON LockCnt = %d event %d",
                      evPlyrLockCnt, ctrl_addr->event_id);              /* 1477 */

        if (evPlyrLockCnt < 0) {                                        /* 1479 */
            evPlyrLockCnt = 0;
        }

        SetNextGPhase(IngameDecideNextPhase());                         /* 1486 */
    } else if (sw == 0) {                                               /* 1488 */
        IngameEventMsgDispReq(1);                                       /* 1493 */
        evPlyrLockCnt++;                                                /* 1494 */
        PRINT_WARNING("PAD_OFF LockCnt = %d event %d",
                      evPlyrLockCnt, ctrl_addr->event_id);              /* 1496 */
    } else {
        printf("*************************************************************\n"); /* 1501 */
        printf("*     Error!! The value of the event data is illegal!!!     *\n"); /* 1502 */
        printf("*               PLYR_PADM    Event ID %3d                   *\n", ctrl_addr->event_id); /* 1503 */
        printf("*************************************************************\n"); /* 1504 */
        /* The ROM really does assert with an empty message here -- the four
         * banner lines above are the whole diagnostic. */
        PRINT_ASSERT("");                                               /* 1505 */
    }

    return 1;                                                           /* 1510 */
}

static int EvPlyrFloorChange(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 1525 */
    u_short floor = Get2Byte(ctrl_addr->event_addr + 2);        /* 1531 */
    SetPlayerFloor((int)floor);                                         /* 1535 */

    return 1;                                                           /* 1538 */
}

/* The "leave finder mode" half of PLYR_FINDER_MODE, split out because
 * EvDispMsg / EvTalkExe / EvMsgChoice need the same wait before they can put
 * text on screen.
 *
 * While the pad is locked the player cannot be driven out of the finder by
 * input, so the wait would never finish -- that case quits the finder outright
 * instead and reports success immediately. */
static int EvWaitFinderOff(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 1544 */
    int ret;

    if (evPlyrLockCnt == 0) {                                           /* 1569 */
        SetPlyrFinderEnd();                                             /* 1572 */

        if (ctrl_addr->process == 0) {                                  /* 1573 */
            PlayerFinderLock();                                         /* 1575 */
            ctrl_addr->process = 1;
        }

        if (plyr_wrk.cmn_wrk.mode == 0) {                               /* 1578 */
            PlayerFinderUnlock();                                       /* 1582 */
            ret = 1;                                                    /* 1583 */
        } else {
            ret = 0;                                                    /* 1585 */
        }
    } else {
        if (ctrl_addr->process != 0) {                                  /* 1589 */
            PlayerFinderUnlock();                                       /* 1590 */
        }

        SetPlyrFinderQEnd();                                            /* 1591 */
        ret = 1;
    }

    return ret;                                                         /* 1598 */
}

static int EvPlyrFinderMode(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 1606 */
    u_char *dat_addr;
    u_char  sw;
    int     ret;

    dat_addr = ctrl_addr->event_addr;

    sw = Get1Byte(dat_addr + 2);                                        /* 1610 */
    Get1Byte(dat_addr + 3);                                             /* 1614 */

    if (sw == 1) {                                                      /* 1617 */
        /* Entering the finder needs the pad, so a locked pad means this will
         * never complete.  The ROM warns rather than refusing. */
        if (evPlyrLockCnt != 0) {                                       /* 1628 */
            PRINT_WARNING("Finder On Req With Player Pad Lock");        /* 1632 */
        }

        SetPlyrFinderIn();                                              /* 1635 */

        if (plyr_wrk.cmn_wrk.mode == 6) {                               /* 1637 */
            ret = 1;                                                    /* 1638 */
        } else {
            ret = 0;
        }
    } else {
        ret = EvWaitFinderOff(ctrl_addr);                               /* 1642 */
    }

    return ret;                                                         /* 1647 */
}

/* Byte 3 selects whether the opcode waits: 0 fires the animation and moves on,
 * anything else blocks until CheckPlyrAnimeEnd() says the clip finished. */
static int EvPlyrMotionCall(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 1656 */
    u_char *dat_addr;
    u_char  anime_no;
    u_char  frame;
    u_char  wait_flg;
    int     ret;

    ret = 0;

    dat_addr = ctrl_addr->event_addr;
    anime_no = Get1Byte(dat_addr + 1);                                  /* 1665 */
    frame    = Get1Byte(dat_addr + 2);                                  /* 1668 */
    wait_flg = Get1Byte(dat_addr + 3);                                  /* 1671 */

    if (ctrl_addr->process == 0) {                                      /* 1674 */
        SetPlyrAnime(anime_no, frame);                                  /* 1677 */

        if (wait_flg == 0) {                                            /* 1679 */
            ret = 1;                                                    /* 1682 */
        } else {
            ctrl_addr->process = 1;                                     /* 1686 */
        }
    } else if (ctrl_addr->process == 1) {                               /* 1687 */
        ret = (CheckPlyrAnimeEnd((int)(char)anime_no) != 0);            /* 1692 */
    } else {
        ret = 0;                                                        /* 1697 */
    }

    return ret;                                                         /* 1703 */
}

/* PLYR_FACIAL_CALL parses its operand and does nothing with it -- facial
 * animation never shipped in this prototype. */
static int EvPlyrFacialCall(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 1718 */
    Get2Byte(ctrl_addr->event_addr + 2);                                /* 1724 */

    return 1;                                                           /* 1729 */
}

static int EvPlyrDamageRequest(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 1744 */
    u_char damage;

    damage = Get1Byte(ctrl_addr->event_addr + 2);                       /* 1750 */
    ReqPlyrHPdownP(&plyr_wrk.cmn_wrk, (short int)(char)damage);         /* 1754 */

    return 1;                                                           /* 1757 */
}

/* Swapping a motion set or a model is a reload, so both of these run as a
 * three-step state machine: hide the player, kick the load, then wait for
 * IsReadyPlyrMdl() before showing them again.  process 4 is the "load has been
 * requested" step -- it is not a count, just a distinct tag. */
static int EvPlyrMotionChange(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 1772 */
    u_short mot_no;

    mot_no = Get2Byte(ctrl_addr->event_addr + 2);                       /* 1777 */

    if (ctrl_addr->process == 0) {                                      /* 1779 */
        PlayerDrawLock();                                               /* 1780 */
        ctrl_addr->process = 4;                                         /* 1782 */
    } else if (ctrl_addr->process == 4) {                               /* 1783 */
        SetupPlyrMdl(-1, (int)mot_no, -1, GetPlyrAcsNo());              /* 1784 */
        ctrl_addr->process = 1;                                         /* 1786 */
    } else {
        if (IsReadyPlyrMdl() == 0) {                                    /* 1788 */
            return 0;
        }

        PlayerDrawUnlock();                                             /* 1789 */
        return 1;                                                       /* 1790 */
    }

    return 0;                                                           /* 1797 */
}

static int EvPlyrModelChange(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 1811 */
    u_short mdl_no;

    mdl_no = Get2Byte(ctrl_addr->event_addr + 2);                       /* 1816 */

    if (ctrl_addr->process == 0) {                                      /* 1818 */
        PlayerDrawLock();                                               /* 1819 */
        ctrl_addr->process = 4;                                         /* 1821 */
    } else if (ctrl_addr->process == 4) {                               /* 1822 */
        SetupPlyrMdl((int)mdl_no, -1, -1, GetPlyrAcsNo());              /* 1823 */
        ctrl_addr->process = 1;                                         /* 1825 */
    } else {
        if (IsReadyPlyrMdl() == 0) {                                    /* 1827 */
            return 0;
        }

        PlayerDrawUnlock();                                             /* 1828 */
        return 1;                                                       /* 1829 */
    }

    return 0;                                                           /* 1838 */
}

/* Putting the flashlight in or out of Mio's hand is a model swap (accessory
 * slot 0x10), so it runs the same lock / reload / wait machine as the two
 * above.  Bit 0x8000 of the player's status word is the "flashlight out" flag
 * the rest of the game reads. */
static int EvPlyrFlashLightSet(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 1846 */
    u_char sw;
    int    ret;

    ret = 0;                                                            /* 1852 */
    sw  = Get1Byte(ctrl_addr->event_addr + 1);                          /* 1855 */

    /* Already in the requested state -- a redundant request is free. */
    if ((plyr_wrk.cmn_wrk.st.sta & 0x8000) != 0) {                      /* 1861 */
        if (sw != 0) {
            return 1;
        }
    } else {
        if (sw == 0) {
            return 1;
        }
    }

    switch (ctrl_addr->process) {                                       /* 1867 */
    case 0:
        PlayerDrawLock();                                               /* 1869 */
        ctrl_addr->process = 4;                                         /* 1870 */
        return ret;                                                     /* 1871 */

    case 4:
        if (sw != 0) {                                                  /* 1873 */
            SetupPlyrMdl(GetPlyrMdlNo(), 1, 0x10, GetPlyrAcsNo());      /* 1874 */
        } else {
            SetupPlyrMdl(GetPlyrMdlNo(), 0, 0x10, GetPlyrAcsNo());      /* 1877 */
        }

        ctrl_addr->process = 1;                                         /* 1879 */
        /* fall through -- the readiness check runs in this same frame */

    case 1:
        if (IsReadyPlyrMdl() == 0) {                                    /* 1883 */
            return ret;
        }

        PlayerDrawUnlock();                                             /* 1884 */

        if (sw != 0) {                                                  /* 1885 */
            plyr_wrk.cmn_wrk.st.sta |= 0x8000;                          /* 1886 */
            ReqPlayerMim(0x19, 0);                                      /* 1888 */
            IgEffectRenzFlareDispFlgSet(1);                             /* 1890 */
        } else {
            IgEffectRenzFlareDispFlgSet(0);                             /* 1894 */
            plyr_wrk.cmn_wrk.st.sta &= ~0x8000;                         /* 1895 */
        }

        break;                                                          /* 1900 */

    default:
        PRINT_ASSERT("Error! EvPlyrFlashLightSet");                     /* 1902 */
        return ret;
    }

    ret = 1;                                                            /* 1907 */

    return ret;                                                         /* 1910 */
}

/* The three PLYR_GAZE_POINT_* opcodes read their operands and drop them: only
 * the sister's gaze work (ev_sister_gaze) was wired up in this prototype, and
 * the player has no equivalent.  Kept as the ROM has them so operand widths
 * stay correct and the event stream still advances. */
static int EvPlyrGazePointObjSet(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 1926 */
    u_char *dat_addr;

    dat_addr = ctrl_addr->event_addr;

    Get1Byte(dat_addr + 1);                                             /* 1932 */
    Get4Byte(dat_addr + 4);                                             /* 1937 */

    return 1;                                                           /* 1942 */
}

static int EvPlyrGazePointPosSet(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 1957 */
    u_char *dat_addr;
    float   gaze_point[4];

    dat_addr = ctrl_addr->event_addr;

    Get2Byte(dat_addr + 2);                                             /* 1964 */
    Get2Byte(dat_addr + 4);                                             /* 1967 */
    Get2Byte(dat_addr + 6);                                             /* 1970 */

    return 1;                                                           /* 1977 */
}

static int EvPlyrGazePointDefSet(EV_EXE_CTRL *ctrl_addr)
{
    return 1;                                                           /* 1990 */
}

/* ------------------------------------------------------------------------ *
 *  Sister
 *
 *  Mayu's opcodes mirror the player's, with two differences: her position is
 *  authored against the *player's* room offset (she is always in the same room
 *  as Mio when an event moves her), and she has a lock of her own that has to
 *  be taken before the event can pose her, because her follow AI would
 *  otherwise walk straight back out of the pose.
 * ------------------------------------------------------------------------ */

static int EvSisPosSet(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 2014 */
    u_char *dat_addr;
    float  *offset;
    float   set_sis_pos[4];
    float   mst_sis_mst[4];

    dat_addr = ctrl_addr->event_addr;

    offset = MapLoadGetOffset(GetPlyrAreaNo());                         /* 2018 */

    if (offset == (float *)0) {                                         /* 2019 */
        PRINT_ASSERT("EvSisPosSet Cannot Get Room%d Info", GetPlyrAreaNo());
    }

    mst_sis_mst[0] = (float)(short int)Get2Byte(dat_addr + 2) + offset[0]; /* 2023 */
    mst_sis_mst[1] = sis_wrk.cmn_wrk.mbox.pos[1];                       /* 2026 */
    mst_sis_mst[2] = (float)(short int)Get2Byte(dat_addr + 4) + offset[2]; /* 2027 */
    mst_sis_mst[3] = 0.0f;                                              /* 2029 */

    MhCtlGetMapHeight(set_sis_pos, mst_sis_mst, GetPlyrAreaNo(), 0);    /* 2037 */
    SetSisterPos(set_sis_pos);                                          /* 2041 */

    return 1;                                                           /* 2044 */
}

static int EvSisHeightSet(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 2066 */
    u_char *dat_addr;
    float  *offset;
    float   set_sis_height[4];

    dat_addr = ctrl_addr->event_addr;

    offset = MapLoadGetOffset(GetPlyrAreaNo());                         /* 2070 */

    set_sis_height[0] = sis_wrk.cmn_wrk.mbox.pos[0];                    /* 2072 */
    set_sis_height[1] = (float)(short int)Get2Byte(dat_addr + 2) + offset[1]; /* 2073 */
    set_sis_height[2] = sis_wrk.cmn_wrk.mbox.pos[2];                    /* 2075 */
    set_sis_height[3] = 0.0f;

    SetSisterPos(set_sis_height);                                       /* 2080 */

    return 1;                                                           /* 2084 */
}

static int EvSisRotSet(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 2101 */
    short int rot360;
    float     set_sis_rot[4];

    rot360 = Get2Byte(ctrl_addr->event_addr + 2);                       /* 2107 */

    set_sis_rot[0] = sis_wrk.cmn_wrk.mbox.rot[0];                       /* 2110 */
    set_sis_rot[1] = EvGetRot360(rot360);                               /* 2113 */
    set_sis_rot[2] = sis_wrk.cmn_wrk.mbox.rot[2];                       /* 2114 */
    set_sis_rot[3] = 0.0f;                                              /* 2115 */

    SetSisterRot(set_sis_rot);                                          /* 2121 */

    return 1;                                                           /* 2124 */
}

/* Unlike the player's reserved PLYR_POS_MOVE, the sister really can be walked
 * to a point: SetFindMode() hands the destination to her follow AI, and the
 * opcode completes immediately rather than waiting for her to arrive. */
static int EvSisPosMove(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 2147 */
    u_char *dat_addr;
    float  *offset;
    float   set_sis_pos[4];

    dat_addr = ctrl_addr->event_addr;

    offset = MapLoadGetOffset(GetPlyrAreaNo());                         /* 2151 */

    set_sis_pos[0] = (float)(short int)Get2Byte(dat_addr + 2) + offset[0]; /* 2153 */
    set_sis_pos[1] = (float)(short int)Get2Byte(dat_addr + 4) + offset[1]; /* 2155 */
    set_sis_pos[2] = (float)(short int)Get2Byte(dat_addr + 6) + offset[2]; /* 2158 */
    set_sis_pos[3] = 0.0f;

    SetFindMode(set_sis_pos, (int)Get2Byte(dat_addr + 8));              /* 2160 */

    return 1;                                                           /* 2166 */
}

/* Showing or hiding Mayu also flips whether the floating ghosts may appear in
 * groups: the crowd behaviour reads badly when she is not on screen to react
 * to it. */
static int EvSisDisp(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 2181 */
    u_char sw;

    sw = Get1Byte(ctrl_addr->event_addr + 2);                           /* 2187 */

    if (sw == 1) {                                                      /* 2190 */
        SisterUnlock();                                                 /* 2192 */
        SisterDrawUnlock();                                             /* 2193 */
        SetSisWrk(1);                                                   /* 2194 */
        fene_entry.MultiAppearDisable();                                /* 2196 */
    } else if (sw == 0) {                                               /* 2198 */
        SisterLock();                                                   /* 2200 */
        SisterDrawLock();                                               /* 2201 */
        SetSisWrk(0);                                                   /* 2202 */
        fene_entry.MultiAppearEnable();                                  /* 2204 */
    } else {
        printf("*************************************************************\n"); /* 2208 */
        printf("*     Error!! The value of the event data is illegal!!!     *\n"); /* 2209 */
        printf("*               SIS_DISPM    Event ID %3d                   *\n", ctrl_addr->event_id); /* 2210 */
        printf("*************************************************************\n"); /* 2211 */
        PRINT_ASSERT("");                                               /* 2212 */
    }

    return 1;                                                           /* 2216 */
}

static int EvSisJoin(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 2225 */
    SetSisJoinFlg(1);                                                   /* 2228 */

    return 1;                                                           /* 2230 */
}

static int EvSisLeave(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 2239 */
    SetSisJoinFlg(0);                                                   /* 2242 */

    return 1;                                                           /* 2244 */
}

/* Written straight into her work block -- there is no SetSisterFloor(). */
static int EvSisFloorChange(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 2259 */
    sis_wrk.cmn_wrk.floor = Get2Byte(ctrl_addr->event_addr + 2);        /* 2265 */

    return 1;                                                           /* 2272 */
}

static int EvSisRegist(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 2289 */
    u_char *dat_addr;
    u_short label;
    u_short mdl_no;
    u_short mot_no;

    dat_addr = ctrl_addr->event_addr;

    label  = Get2Byte(dat_addr + 2);                                    /* 2295 */
    mdl_no = Get2Byte(dat_addr + 4);                                    /* 2298 */
    mot_no = Get2Byte(dat_addr + 6);                                    /* 2301 */

    ev_sisRegister((int)label, (int)mdl_no, (int)mot_no);               /* 2304 */

    return 1;                                                           /* 2307 */
}

static int EvSisDelete(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 2320 */
    u_short label;

    printf("ctrl_addr event id %d\n", ctrl_addr->event_id);             /* 2323 */

    label = Get2Byte(ctrl_addr->event_addr + 2);                        /* 2329 */
    ev_sisDelete((int)label);                                           /* 2332 */

    return 1;                                                           /* 2335 */
}

/* Always blocking, and always for exactly one frame: the lock is taken, the
 * clip is started, and the next entry releases the lock and reports done.  The
 * clip is not waited on -- SIS_MOTION_CALL is a pose, not a cutscene. */
static int EvSisMotionCall(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 2344 */
    u_char *dat_addr;
    u_char  anime_no;
    u_char  frame;
    int     ret;

    ret = 0;

    dat_addr = ctrl_addr->event_addr;
    anime_no = Get1Byte(dat_addr + 1);                                  /* 2355 */
    frame    = Get1Byte(dat_addr + 2);                                  /* 2358 */

    if (ctrl_addr->process == 0) {                                      /* 2361 */
        SisterLock();                                                   /* 2363 */
        SetSisterAnime(anime_no, frame);                                /* 2366 */
        ctrl_addr->process = 1;                                         /* 2367 */
    } else if (ctrl_addr->process == 1) {                               /* 2369 */
        ret = 1;                                                        /* 2370 */
        SisterUnlock();                                                 /* 2372 */
    } else {
        ret = 0;                                                        /* 2375 */
    }

    return ret;                                                         /* 2380 */
}

static int EvSisFacialCall(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 2395 */
    Get2Byte(ctrl_addr->event_addr + 2);                                /* 2401 */

    return 1;                                                           /* 2405 */
}

static int EvSisDamageRequest(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 2420 */
    u_char damage;

    damage = Get1Byte(ctrl_addr->event_addr + 2);                       /* 2426 */
    ReqPlyrHPdownP(&sis_wrk.cmn_wrk, damage);          /* 2430 */

    return 1;                                                           /* 2433 */
}

/* These three do reach ev_sister_gaze -- the sister's head-turn work block --
 * unlike their player counterparts. */
static int EvSisGazePointObjSet(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 2449 */
    u_char *dat_addr;
    u_char  obj_type;
    u_int   obj_id;

    dat_addr = ctrl_addr->event_addr;

    obj_type = Get1Byte(dat_addr + 1);                                  /* 2455 */
    obj_id   = Get4Byte(dat_addr + 4);                                  /* 2460 */

    ev_sister_gaze.SetObjType(obj_type, obj_id); /* 2464 */

    return 1;                                                           /* 2467 */
}

static int EvSisGazePointPosSet(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 2484 */
    u_char *dat_addr;
    float   gaze_point[4];

    dat_addr = ctrl_addr->event_addr;

    /* No room offset here: the gaze point is stored as authored. */
    gaze_point[0] = (float)(short int)Get2Byte(dat_addr + 2);           /* 2491 */
    gaze_point[1] = (float)(short int)Get2Byte(dat_addr + 4);           /* 2494 */
    gaze_point[2] = (float)(short int)Get2Byte(dat_addr + 6);           /* 2497 */
    gaze_point[3] = 0.0f;                                               /* 2500 */

    ev_sister_gaze.SetPoint(gaze_point);               /* 2503 */

    return 1;                                                           /* 2506 */
}

static int EvSisGazePointDefSet(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 2515 */
    ev_sister_gaze.Init();                               /* 2519 */

    return 1;                                                           /* 2521 */
}

/* ------------------------------------------------------------------------ *
 *  Ghosts
 * ------------------------------------------------------------------------ */

/* Blocks until the ghost has actually entered the world.  Bringing one in also
 * evicts the floating-ghost entry, because the two cannot be resident at once
 * -- the memory they want is the same. */
static int EvGhostAppear(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 2538 */
    u_char *dat_addr;
    u_char  ene_type;
    u_short ene_no;
    int     ret;

    dat_addr = ctrl_addr->event_addr;                                   /* 2541 */

    ene_type = Get1Byte(dat_addr + 1);                                  /* 2544 */
    ene_no   = Get2Byte(dat_addr + 2);                                  /* 2547 */

    if (GetEneDatStatus((int)ene_type, (int)ene_no) == ENE_STATUS_NO_USE) { /* 2552 */
        PRINT_WARNING("Enemy Is Not Preloaded type %d no %d", ene_type, ene_no); /* 2553 */
    }

    ret = EneActReq((int)(char)ene_type, (int)ene_no);                  /* 2558 */

    if (ret != 0) {                                                     /* 2559 */
        fene_entry.Release();                               /* 2560 */
    }

    return (ret != 0);                                                  /* 2564 */
}

static int EvGhostDisappear(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 2579 */
    u_char *dat_addr;
    u_char  ene_type;
    u_short ene_no;

    dat_addr = ctrl_addr->event_addr;                                   /* 2582 */

    ene_type = Get1Byte(dat_addr + 1);                                  /* 2585 */
    ene_no   = Get2Byte(dat_addr + 2);                                  /* 2588 */

    EneReleaseReq((int)ene_type, (int)ene_no);                    /* 2591 */

    return 1;                                                           /* 2594 */
}

/* Registers the ghost's model/motion files with the loader; the ghost itself
 * does not appear until GHOST_APPEAR. */
static int EvGhostRegist(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 2611 */
    u_char *dat_addr;
    u_char  ene_type;
    u_short ene_no;
    u_short room_no;

    dat_addr = ctrl_addr->event_addr;                                   /* 2614 */

    ene_type = Get1Byte(dat_addr + 1);                                  /* 2617 */
    ene_no   = Get2Byte(dat_addr + 2);                                  /* 2620 */
    room_no  = Get2Byte(dat_addr + 4);                                  /* 2623 */

    ev_eneRegisterFile((int)room_no, (int)ene_type, (int)ene_no); /* 2626 */

    return 1;                                                           /* 2629 */
}

static int EvGhostDelete(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 2646 */
    u_char *dat_addr;
    u_char  ene_type;
    u_short ene_no;
    u_short room_no;

    dat_addr = ctrl_addr->event_addr;                                   /* 2649 */

    ene_type = Get1Byte(dat_addr + 1);                                  /* 2652 */
    ene_no   = Get2Byte(dat_addr + 2);                                  /* 2655 */
    room_no  = Get2Byte(dat_addr + 4);                                  /* 2658 */

    ev_eneDeleteFile((int)room_no, (int)ene_type, (int)ene_no);   /* 2661 */

    return 1;                                                           /* 2664 */
}

/* The wandering "floating" ghosts that spawn on their own.  Locking is a
 * count, so an event that suppresses them can safely nest inside another. */
static int EvFloatageGhost(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 2679 */
    u_char sw;

    sw = Get1Byte(ctrl_addr->event_addr + 2);                           /* 2685 */

    if (sw == 1) {                                                      /* 2688 */
        fene_entry.Unlock();                                /* 2689 */
    } else if (sw == 0) {                                               /* 2691 */
        fene_entry.Lock();                                  /* 2692 */
    } else {
        printf("*************************************************************\n"); /* 2696 */
        printf("*     Error!! The value of the event data is illegal!!!     *\n"); /* 2697 */
        printf("*             FLOATAGE_GHOSTM    Event ID %3d               *\n", ctrl_addr->event_id); /* 2698 */
        printf("*************************************************************\n"); /* 2699 */
        PRINT_ASSERT("");                                               /* 2700 */
    }

    return 1;                                                           /* 2705 */
}

/* Per-area suppression, independent of the global lock above: one bit per area
 * in the entry's own flag set.  BIT_FLAGS asserts on its own for an
 * out-of-range index, so the check here is purely to name the event that did
 * it -- which is why both fire. */
static int EvLockAreaFloatageGhost(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 2721 */
    u_short area_label;

    area_label = Get2Byte(ctrl_addr->event_addr + 2);                   /* 2727 */

    if (area_label > 0x41) {                                            /* 2731 */
        PRINT_ASSERT("Error! EvLockAreaFloatageGhost event_id[%d] area_label %d",
                     ctrl_addr->event_id, area_label);                  /* 2732 */
    }

    fene_entry.mAreaLockFlg.FlgUp((int)area_label);                     /* 2741 */

    return 1;
}

static int EvUnLockAreaFloatageGhost(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 2756 */
    u_short area_label;

    area_label = Get2Byte(ctrl_addr->event_addr + 2);                   /* 2762 */

    fene_entry.mAreaLockFlg.FlgDown((int)area_label);                   /* 2770 */

    return 1;
}

/* ------------------------------------------------------------------------ *
 *  NPCs
 *
 *  None of these do anything.  The NPC system did not make it into this
 *  prototype, so the six opcodes survive only as operand readers -- the stream
 *  still has to be walked at the right width, and the events that use them
 *  still have to run.  Kept exactly as the ROM has them.
 * ------------------------------------------------------------------------ */

static int EvNpcPosSet(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 2793 */
    u_char *dat_addr;
    float   npc_pos[4];

    dat_addr = ctrl_addr->event_addr;

    MapLoadGetOffset(GetPlyrAreaNo());                                  /* 2796 */

    Get2Byte(dat_addr + 2);                                             /* 2799 */
    Get2Byte(dat_addr + 4);                                             /* 2803 */
    Get2Byte(dat_addr + 6);

    return 1;                                                           /* 2809 */
}

static int EvNpcHeightSet(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 2832 */
    u_char *dat_addr;
    float   npc_height[4];

    dat_addr = ctrl_addr->event_addr;

    MapLoadGetOffset(GetPlyrAreaNo());                                  /* 2835 */

    Get2Byte(dat_addr + 2);                                             /* 2838 */
    Get2Byte(dat_addr + 4);

    return 1;                                                           /* 2847 */
}

static int EvNpcRotSet(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 2865 */
    u_char   *dat_addr;
    short int rot360;
    float     set_npc_rot[4];

    dat_addr = ctrl_addr->event_addr;                                   /* 2868 */

    Get2Byte(dat_addr + 2);                                             /* 2871 */
    rot360 = Get2Byte(dat_addr + 4);                                    /* 2874 */

    EvGetRot360(rot360);                                                /* 2877 */

    return 1;                                                           /* 2890 */
}

static int EvNpcPosMove(EV_EXE_CTRL *ctrl_addr)
{
    return 1;                                                           /* 2901 */
}

static int EvNpcDisp(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 2917 */
    u_char *dat_addr;
    u_char  sw;

    dat_addr = ctrl_addr->event_addr;                                   /* 2920 */

    sw = Get1Byte(dat_addr + 1);                                        /* 2923 */
    Get2Byte(dat_addr + 2);                                             /* 2926 */

    if ((sw != 1) && (sw != 0)) {                                       /* 2929 */
        printf("*************************************************************\n"); /* 2935 */
        printf("*     Error!! The value of the event data is illegal!!!     *\n"); /* 2936 */
        printf("*                 NPC_DISPM    Event ID %3d                 *\n", ctrl_addr->event_id); /* 2937 */
        printf("*************************************************************\n"); /* 2938 */
        PRINT_ASSERT("");                                               /* 2939 */
    }

    return 1;                                                           /* 2944 */
}

static int EvNpcFloorChange(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 2959 */
    Get2Byte(ctrl_addr->event_addr + 2);                                /* 2965 */

    return 1;                                                           /* 2970 */
}

/* ------------------------------------------------------------------------ *
 *  Map objects
 *
 *  Each of these has to write the change twice: once into the live MDAT_OBJ,
 *  and once into the map save block.  The live record only exists while the
 *  object's room is loaded and being drawn -- which is why every handler
 *  tolerates a null pointer and still calls MapSaveSetStat().  Leaving and
 *  re-entering the room rebuilds the MDAT_OBJ from the save block, so the
 *  change survives regardless.
 * ------------------------------------------------------------------------ */

static int EvSetObjHitCheck(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 2987 */
    u_char   *dat_addr;
    u_char    sw;
    u_int     label;
    MDAT_OBJ *p_obj;

    dat_addr = ctrl_addr->event_addr;                                   /* 2990 */

    sw    = Get1Byte(dat_addr + 1);                                     /* 2993 */
    label = Get4Byte(dat_addr + 4);                                     /* 2998 */

    p_obj = (MDAT_OBJ *)RegDatGetStPtr4Label3(label, 3);                /* 3001 */

    if (MapLoadCheckDrawFlg(MapLoadGetBuffID4Label(label)) == 0) {      /* 3004 */
        p_obj = (MDAT_OBJ *)0;                                          /* 3008 */
    }

    if (p_obj != (MDAT_OBJ *)0) {                                       /* 3012 */
        p_obj->HitCheck = (int)(char)sw;
        MapObjSetHit(label, (int)(char)sw);                             /* 3016 */
    }

    MapSaveSetStat(label, 0, (int)(char)sw);                            /* 3019 */

    if (sw > 1) {                                                       /* 3021 */
        printf("*************************************************************\n");
        printf("*     Error!! The value of the event data is illegal!!!     *\n"); /* 3022 */
        printf("*            SET_OBJ_HITCHECKM    Event ID %3d              *\n", ctrl_addr->event_id); /* 3023 */
        printf("*************************************************************\n"); /* 3024 */
        PRINT_ASSERT("");                                               /* 3025 */
    }

    return 1;                                                           /* 3030 */
}

/* Photo-ability is the only one of these that also has to be registered with
 * the photo system, and de-registered first if it was already on -- hence the
 * PhotoAble read-before-write. */
static int EvSetObjPhotoAble(EV_EXE_CTRL *ctrl_addr)
{                                                                                                             /* 3047 */
    u_char   *dat_addr;
    u_short   photo_id;
    u_int     label;
    MDAT_OBJ *p_obj;

    dat_addr = ctrl_addr->event_addr;                                                                         /* 3050 */

    photo_id = Get2Byte(dat_addr + 2);                                                                        /* 3053 */
    label    = Get4Byte(dat_addr + 4);                                                                        /* 3056 */

    p_obj = (MDAT_OBJ *)RegDatGetStPtr4Label3(label, 3);                                                 /* 3060 */

    if (MapLoadCheckDrawFlg(MapLoadGetBuffID4Label(label)) == 0) {                                            /* 3063 */
        printf("********************************************************\n");                           /* 3064 */
        printf("label  buff_id == [%d]:\n", MapLoadGetBuffID4Label(label));                             /* 3065 */
        printf("player buff_id == [%d]:\n", MapLoadGetBuffID(GetPlyrAreaNo()));                         /* 3066 */
        /* The missing newline in this one is the ROM's. */
        printf("PHOTO_ABLE_NO_AREA label[%d] GetPlyrAreaNo[%d]\n", label, GetPlyrAreaNo());             /* 3068 */
        p_obj = (MDAT_OBJ *)0;                                                                                /* 3069 */
    }

    if (p_obj != (MDAT_OBJ *)0) {                                                                             /* 3071 */
        if (photo_id < 0 || (GetPhotoDatNum() <= photo_id)) {                                                 /* 3072 */
            printf("obj_id = %d\n", label);                                                             /* 3073 */
            PRINT_ASSERT("EvSetObjPhotoAble Illegal Id %d Max %d", photo_id, GetPhotoDatNum());               /* 3074 */
        } else {
            if (p_obj->PhotoAble != 0) {                                                                      /* 3076 */
                photo_datObjEnd(p_obj);                                                                       /* 3078 */
            }

            p_obj->PhotoAble = photo_id;

            if (photo_id != 0) {                                                                              /* 3081 */
                photo_datObjStart(p_obj);                                                                     /* 3082 */
            }
        }
    }

    MapSaveSetStat(label, 1, photo_id);                                                              /* 3087 */

    if (photo_id < 0) {                                                                                       /* 3090 */
        printf("*************************************************************\n");                      /* 3092 */
        printf("*     Error!! The value of the event data is illegal!!!     *\n");                      /* 3093 */
        printf("*            SET_OBJ_PHOTOABLEM    Event ID %3d             *\n", ctrl_addr->event_id); /* 3094 */
        printf("*************************************************************\n");                      /* 3095 */
        PRINT_ASSERT("");                                                                                     /* 3096 */
    }

    return 1;                                                                                                 /* 3101 */
}

/* SET_OBJ_EFFECT resolves the object and then does nothing with it -- the
 * per-object effect hook was never written.  The lookup is kept because it is
 * what the ROM does. */
static int EvSetObjEffect(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 3119 */
    u_char *dat_addr;
    u_int   label;

    dat_addr = ctrl_addr->event_addr;                                   /* 3122 */

    Get1Byte(dat_addr + 1);                                             /* 3125 */
    label = Get4Byte(dat_addr + 4);                                     /* 3130 */

    RegDatGetStPtr4Label(MapLoadGetRegBuffID(GetPlyrAreaNo(), plyr_wrk.cmn_wrk.floor), label); /* 3133 */

    return 1;                                                           /* 3140 */
}

static int EvSetObjVisible(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 3157 */

    u_char *dat_addr = ctrl_addr->event_addr;                                   /* 3160 */

    u_char sw = Get1Byte(dat_addr + 1);                                     /* 3163 */
    u_int label = Get4Byte(dat_addr + 4);                                     /* 3168 */

    MDAT_OBJ *p_obj = (MDAT_OBJ *) RegDatGetStPtr4Label3(label, 3);                /* 3172 */

    if (p_obj != (MDAT_OBJ *)0) {                                       /* 3173 */
        p_obj->Visible = sw;                                 /* 3175 */
    }

    MapSaveSetStat(label, 2, sw);                            /* 3178 */

    return 1;                                                           /* 3182 */
}

static int EvSetObjReqAction(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 3199 */
    u_char   *dat_addr;
    u_char    sw;
    u_int     label;
    MDAT_OBJ *p_obj;

    dat_addr = ctrl_addr->event_addr;                                   /* 3202 */

    sw    = Get1Byte(dat_addr + 1);                                     /* 3205 */
    label = Get4Byte(dat_addr + 4);                                     /* 3210 */

    p_obj = (MDAT_OBJ *)RegDatGetStPtr4Label3(label, 3);                /* 3214 */

    if (p_obj != (MDAT_OBJ *)0) {                                       /* 3217 */
        p_obj->Action = sw;                                  /* 3220 */
    }

    MapSaveSetStat(label, 4, sw);                            /* 3223 */

    return 1;                                                           /* 3226 */
}

static int EvSetObjActionType(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 3243 */
    u_char   *dat_addr;
    u_char    sw;
    u_int     label;
    MDAT_OBJ *p_obj;

    dat_addr = ctrl_addr->event_addr;                                   /* 3246 */

    sw    = Get1Byte(dat_addr + 1);                                     /* 3249 */
    label = Get4Byte(dat_addr + 4);                                     /* 3254 */

    p_obj = (MDAT_OBJ *)RegDatGetStPtr4Label3(label, 3);                /* 3258 */

    if (p_obj != (MDAT_OBJ *)0) {                                       /* 3260 */
        p_obj->ActionType = sw;                              /* 3263 */
    }

    MapSaveSetStat(label, 3, sw);                            /* 3266 */

    return 1;                                                           /* 3269 */
}

static int EvPhotoLock(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 3283 */
    PlayerFinderLock();

    return 1;                                                           /* 3285 */
}

static int EvPhotoUnlock(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 3297 */
    PlayerFinderUnlock();

    return 1;                                                           /* 3300 */
}

/* ------------------------------------------------------------------------ *
 *  Inventory
 *
 *  Every one of these validates its operands with an assert and then does the
 *  thing anyway -- the checks are there to catch bad event data during
 *  development, not to guard the call.
 * ------------------------------------------------------------------------ */

static int EvItemGet(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 3317 */
    u_char *dat_addr;
    u_char  item_id;
    u_char  get_num;

    dat_addr = ctrl_addr->event_addr;                                   /* 3320 */

    item_id = Get1Byte(dat_addr + 1);                                   /* 3323 */
    get_num = Get1Byte(dat_addr + 2);                                   /* 3325 */
    Get1Byte(dat_addr + 3);                                             /* 3327 */

    if (item_id >= PLYR_ITEM_MAX) {                                                                           /* 3331 */
        PRINT_ASSERT("Error! EvItemGet event_id[%d] item_id %d", ctrl_addr->event_id, item_id);               /* 3332 */
    }

    if (get_num > 99) {                                                                                       /* 3334 */
        PRINT_ASSERT("Error! EvItemGet event_id[%d] get_num %d", ctrl_addr->event_id, get_num);               /* 3335 */
    }

    /* Item 10 is film: receiving the first roll is also what selects the
     * camera's film type, since there is nothing loaded before that. */
    if ((GetPlyrItemHaveNum(item_id) == 0) && (item_id == 10)) { /* 3341 */
        m_plyr_camera.camera_film.mFilmType = 1;
    }

    ItemGet(item_id, get_num);                               /* 3346 */

    return 1;                                                           /* 3350 */
}

static int EvItemUse(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 3366 */
    u_char *dat_addr;
    u_char  item_id;
    u_char  use_num;

    dat_addr = ctrl_addr->event_addr;                                   /* 3369 */

    item_id = Get1Byte(dat_addr + 1);                                   /* 3372 */
    use_num = Get1Byte(dat_addr + 2);                                   /* 3374 */

    if (item_id >= PLYR_ITEM_MAX) {                                     /* 3378 */
        PRINT_ASSERT("Error! EvItemUse event_id[%d] item_id %d", ctrl_addr->event_id, item_id);               /* 3379 */
    }

    if (use_num > 99) {                                                 /* 3381 */
        PRINT_ASSERT("Error! EvItemUse event_id[%d] use_num %d", ctrl_addr->event_id, use_num);               /* 3382 */
    }

    ItemUse((int)(char)item_id, use_num);                               /* 3387 */

    return 1;                                                           /* 3390 */
}

/* Note the operand offsets: ITEM_LOST reads bytes 2 and 3, not 1 and 2 like
 * ITEM_GET / ITEM_USE above.  The ROM is laid out that way. */
static int EvItemLost(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 3406 */
    u_char *dat_addr;
    u_char  item_id;
    u_char  lost_num;

    dat_addr = ctrl_addr->event_addr;                                   /* 3409 */

    item_id  = Get1Byte(dat_addr + 2);                                  /* 3412 */
    lost_num = Get1Byte(dat_addr + 3);                                  /* 3414 */

    if (item_id >= PLYR_ITEM_MAX) {                                     /* 3418 */
        PRINT_ASSERT("Error! EvItemLost event_id[%d] item_id %d", ctrl_addr->event_id, item_id);              /* 3419 */
    }

    if (lost_num > 99) {                                                /* 3421 */
        PRINT_ASSERT("Error! EvItemLost event_id[%d] lost_num %d", ctrl_addr->event_id, lost_num);            /* 3422 */
    }

    ItemLost(item_id, lost_num);                             /* 3427 */

    return 1;                                                           /* 3430 */
}

static int EvFileGet(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 3445 */
    u_char *dat_addr;
    u_char  file_type;
    u_short file_id;

    dat_addr = ctrl_addr->event_addr;                                   /* 3448 */

    file_type = Get1Byte(dat_addr + 1);                                 /* 3451 */
    file_id   = Get2Byte(dat_addr + 2);                                 /* 3454 */

    if (file_type > 4) {                                          /* 3458 */
        PRINT_ASSERT("Error! EvFileGet event_id[%d] file_type %d", ctrl_addr->event_id, file_type);           /* 3459 */
    }

    if (file_id >= GetFileTypeMaxNum(file_type)) { /* 3461 */
        PRINT_ASSERT("Error! EvFileGet event_id[%d] file_id %d", ctrl_addr->event_id, file_id);               /* 3462 */
    }

    FileGet(file_type, (int)file_id);                        /* 3467 */

    return 1;                                                           /* 3470 */
}

static int EvFileLost(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 3485 */
    u_char *dat_addr;
    u_char  file_type;
    u_short file_id;

    dat_addr = ctrl_addr->event_addr;                                   /* 3488 */

    file_type = Get1Byte(dat_addr + 1);                                 /* 3491 */
    file_id   = Get2Byte(dat_addr + 2);                                 /* 3494 */

    if (file_type > 4) {                                          /* 3498 */
        PRINT_ASSERT("Error! EvFileLost event_id[%d] file_type %d", ctrl_addr->event_id, file_type);          /* 3499 */
    }

    if (file_id >= GetFileTypeMaxNum(file_type)) { /* 3501 */
        PRINT_ASSERT("Error! EvFileLost event_id[%d] file_id %d", ctrl_addr->event_id, file_id);              /* 3502 */
    }

    FileLost(file_type, (int)file_id);                       /* 3507 */

    return 1;                                                           /* 3510 */
}

/* Puts a file on screen and holds the event there until the player has read
 * it.  Unlike every other blocking opcode this one also drives the page
 * turning itself: MesStatusCheck() returning 1 means "more pages", and the
 * confirm button advances one page at a time. */
static int EvFileRead(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 3519 */
    u_char *dat_addr;
    u_char  file_type;
    u_short file_id;
    int     mes_status;

    dat_addr = ctrl_addr->event_addr;                                   /* 3528 */

    file_type = Get1Byte(dat_addr + 1);                                 /* 3531 */
    file_id   = Get2Byte(dat_addr + 2);                                 /* 3534 */

    /* Held for the whole opcode, not just the first frame -- ev_change.c uses
     * it to keep other events from re-entering while text is up. */
    SetEventWaitFlg(1);                                                 /* 3537 */

    if (file_type > 4) {                                          /* 3546 */
        PRINT_ASSERT("Error! EvFileRead event_id[%d] file_type %d", ctrl_addr->event_id, file_type);          /* 3547 */
    }

    if (file_id >= GetFileTypeMaxNum(file_type)) { /* 3549 */
        PRINT_ASSERT("Error! EvFileRead event_id[%d] file_id %d", ctrl_addr->event_id, file_id);              /* 3550 */
    }

    if (ctrl_addr->process == 0) {                                      /* 3554 */
        IngameEventFileDispReq(1);                                      /* 3558 */
        SetMsgFirstPage();                                              /* 3559 */
        ctrl_addr->process = 1;                                         /* 3561 */
    } else if (ctrl_addr->process == 1) {                               /* 3563 */
        DrawCmnFileWindow(file_type, (int)file_id, 0, 0x80, 'f');       /* 3564 */
        DrawCmnButton(0, 528.0f, 24.0f, 0x80, 0);                       /* 3567 */
        DrawCmnCaption(11, 557.0f, 26.0f, 0x80, 0);                     /* 3569 */

        mes_status = MesStatusCheck();                                  /* 3571 */

        if (mes_status == 0) {                                          /* 3573 */
            /* Nothing left to show: hand the pad back and let the phase
             * machine pick up whatever comes next. */
            SetEventWaitFlg(0);                                         /* 3574 */
            IngameEventMsgDispReq(0);                                   /* 3575 */
            SetNextGPhase(IngameDecideNextPhase());                     /* 3576 */
            return 1;                                                   /* 3577 */
        }

        if (mes_status == 1) {                                          /* 3580 */
            if (*paddat[3] == 1) {                                      /* 3581 */
                SystemBankPlay(6, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000); /* 3582 */
                MesSetNextPage();                                       /* 3583 */
            }
        }
    }

    return 0;                                                           /* 3599 */
}

static int EvCrystalGet(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 3613 */
    u_short crystal_id;

    crystal_id = Get2Byte(ctrl_addr->event_addr + 2);                   /* 3618 */

    if (crystal_id > 0x27) {                                            /* 3622 */
        PRINT_ASSERT("Error! EvCrystalGet event_id[%d] crystal_id %d", ctrl_addr->event_id, crystal_id);      /* 3623 */
    }

    GetCrystal((int)crystal_id);                                        /* 3628 */

    return 1;                                                           /* 3631 */
}

static int EvCrystalLost(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 3645 */

    u_short crystal_id = Get2Byte(ctrl_addr->event_addr + 2);                   /* 3650 */

    if (crystal_id > 0x27) {                                            /* 3654 */
        PRINT_ASSERT("Error! EvCrystalLost event_id[%d] crystal_id %d", ctrl_addr->event_id, crystal_id);     /* 3655 */
    }

    LostCrystal((int)crystal_id);                                       /* 3660 */

    return 1;                                                           /* 3663 */
}

/* There is only one level gem, so neither of these takes an operand. */
static int EvLevelGemGet(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 3675 */
    GetLevelGem();

    return 1;                                                           /* 3678 */
}

static int EvLevelGemLost(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 3690 */
    LostLevelGem();

    return 1;                                                           /* 3693 */
}

/* ------------------------------------------------------------------------ *
 *  Camera Obscura upgrades
 * ------------------------------------------------------------------------ */

/* Special lenses.  Receiving the *first* one is also what puts it into the
 * equip tray -- hence the count of lenses already held before the flag for
 * this one goes up. */
static int EvCamSpecialShotGet(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 3702 */
    u_short lens_id;
    int     have_num;
    int     i;
    char    aSubFuncArray[3];

    have_num = 0;

    lens_id = Get2Byte(ctrl_addr->event_addr + 2);                      /* 3707 */

    for (i = 0; i < 10; i++) {                                          /* 3712 */
        if (m_plyr_camera.camera_power_up.mTemperedRenzFlg.IsUp(i)) {
            have_num++;
        }
    }

    if (have_num == 0) {                                                /* 3717 */
        aSubFuncArray[0] = (char)lens_id;
        aSubFuncArray[1] = 0;
        aSubFuncArray[2] = 0;
        m_plyr_camera.eq_tray.SetSubFuncArray(aSubFuncArray); /* 3719 */
    }

    m_plyr_camera.camera_power_up.mTemperedRenzFlg.FlgUp((int)lens_id); /* 3724 */

    return 1;
}

static int EvCamAddFunctionGet(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 3738 */
    u_short func_id;

    func_id = Get2Byte(ctrl_addr->event_addr + 2);                      /* 3743 */

    m_plyr_camera.camera_power_up.mAdditionFlg.FlgUp((int)func_id);     /* 3748 */

    /* Function 2 is the multi-capture bonus; the tray has to be told the new
     * absorption rate as well as the flag. */
    if (func_id == 2) {                                                 /* 3750 */
        m_plyr_camera.eq_tray.SetAbsorbMultiRate(1.5f);
    }

    return 1;                                                           /* 3758 */
}

/* A camera part is both owned and fitted the moment the event grants it. */
static int EvCamEquipFunctionGet(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 3772 */
    u_short parts_id;

    parts_id = Get2Byte(ctrl_addr->event_addr + 2);                     /* 3777 */

    m_plyr_camera.camera_power_up.mCamPartsFlg.FlgUp((int)parts_id);
    m_plyr_camera.camera_power_up.mCamPartsSetFlg.FlgUp((int)parts_id);

    return 1;                                                           /* 3784 */
}

/* ------------------------------------------------------------------------ *
 *  Notes, puzzles
 * ------------------------------------------------------------------------ */

static int EvMemoUpdate(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 3799 */
    u_char *dat_addr;
    u_char  msg_step;
    u_short memo_label;

    dat_addr = ctrl_addr->event_addr;                                   /* 3801 */

    msg_step   = Get1Byte(dat_addr + 1);                                /* 3804 */
    memo_label = Get2Byte(dat_addr + 2);                                /* 3807 */

    if (memo_label > 0x13) {                                            /* 3810 */
        PRINT_ASSERT("Error! EvMemoUpdate event_id[%d] memo_label %d", ctrl_addr->event_id, memo_label);      /* 3811 */
    }

    if (msg_step > 1) {                                                 /* 3813 */
        PRINT_ASSERT("Error! %s event_id[%d] msg_step %d", __FUNCTION__, ctrl_addr->event_id, msg_step);      /* 3814 */
    }

    UpdateMemo((int)memo_label, msg_step);                              /* 3819 */

    return 1;                                                           /* 3822 */
}

static int EvPuzzleStart(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 3837 */
    u_short puzzle_id;

    puzzle_id = Get2Byte(ctrl_addr->event_addr + 2);                    /* 3843 */

    if (puzzle_id > 5) {                                                /* 3846 */
        PRINT_ASSERT("Error! EvPuzzleStart event_id[%d] puzzle_id %d", ctrl_addr->event_id, puzzle_id);       /* 3847 */
    }

    PuzzleStartReq((int)puzzle_id);                                     /* 3851 */

    return 1;                                                           /* 3854 */
}

/* ------------------------------------------------------------------------ *
 *  Event camera
 *
 *  EV_CAM hands the view to the scripted camera; the SET_EV_CAM_* opcodes that
 *  follow it configure that camera while it is running.  Positions here are
 *  authored in world space and are *not* room-relative -- unlike the character
 *  ones -- and the two angle opcodes carry hundredths.
 * ------------------------------------------------------------------------ */

static int EvEvCam(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 3863 */
    u_char sw;
    int    ret;

    ret = 1;

    sw = Get1Byte(ctrl_addr->event_addr + 2);                           /* 3866 */

    if (sw == 1) {                                                      /* 3870 */
        /* The finder owns the view, so it has to be out of the way before the
         * event camera can take over. */
        ret = EvWaitFinderOff(ctrl_addr);                               /* 3876 */

        if (ret == 1) {                                                 /* 3879 */
            printf("evevcam\n");                                        /* 3880 */
            EventCameraReq();                                           /* 3881 */
        }
    } else if (sw == 0) {                                               /* 3884 */
        EventCameraCut();                                               /* 3887 */
        ret = 1;                                                        /* 3889 */
    } else {
        printf("*************************************************************\n"); /* 3893 */
        printf("*     Error!! The value of the event data is illegal!!!     *\n"); /* 3894 */
        printf("*                  EV_CAMM    Event ID %3d                  *\n", ctrl_addr->event_id); /* 3895 */
        printf("*************************************************************\n"); /* 3896 */
        PRINT_ASSERT("");                                               /* 3897 */
    }

    return ret;                                                         /* 3902 */
}

static int EvSetEvCamVCI(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 3917 */
    u_short vci_no;

    vci_no = Get2Byte(ctrl_addr->event_addr + 2);                       /* 3923 */
    EventCameraVCIReq((int)vci_no);                                     /* 3927 */

    return 1;                                                           /* 3929 */
}

static int EvSetEvCamVP(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 3944 */
    u_char *dat_addr;
    float   cam_vp[4];

    dat_addr = ctrl_addr->event_addr;                                   /* 3947 */

    cam_vp[0] = (float)(short int)Get2Byte(dat_addr + 2);               /* 3950 */
    cam_vp[1] = (float)(short int)Get2Byte(dat_addr + 4);               /* 3952 */
    cam_vp[2] = (float)(short int)Get2Byte(dat_addr + 6);               /* 3954 */
    cam_vp[3] = 0.0f;                                                   /* 3955 */

    EventCameraSetPosition(cam_vp);                                     /* 3959 */

    return 1;                                                           /* 3962 */
}

static int EvSetEvCamVR(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 3977 */
    u_char *dat_addr;
    float   cam_vr[4];

    dat_addr = ctrl_addr->event_addr;                                   /* 3980 */

    cam_vr[0] = (float)(short int)Get2Byte(dat_addr + 2);               /* 3983 */
    cam_vr[1] = (float)(short int)Get2Byte(dat_addr + 4);               /* 3985 */
    cam_vr[2] = (float)(short int)Get2Byte(dat_addr + 6);               /* 3987 */
    cam_vr[3] = 0.0f;                                                   /* 3988 */

    EventCameraSetTarget(cam_vr);                                       /* 3992 */

    return 1;                                                           /* 3995 */
}

static int EvSetEvCamRot(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 4010 */
    u_short rot;

    rot = Get2Byte(ctrl_addr->event_addr + 2);                          /* 4016 */
    EventCameraSetRoll((float)(short int)rot / 100.0f);                 /* 4017 */

    return 1;                                                           /* 4023 */
}

static int EvSetEvCamProj(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 4038 */
    u_short proj;

    proj = Get2Byte(ctrl_addr->event_addr + 2);                         /* 4044 */
    EventCameraSetFov((float)(short int)proj / 100.0f);                 /* 4045 */

    return 1;                                                           /* 4052 */
}

/* Pin the camera position (or its target) to a moving object rather than a
 * fixed point: an object type/id pair plus an offset from it, and a margin the
 * camera keeps between itself and that object. */
static int EvCamVPSetObj(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 4070 */
    u_char *dat_addr;
    u_char  obj_type;
    u_short margin;
    u_int   obj_id;
    float   offset[4];

    dat_addr = ctrl_addr->event_addr;                                   /* 4073 */

    obj_type  = Get1Byte(dat_addr + 1);                                 /* 4076 */

    offset[0] = (float)(short int)Get2Byte(dat_addr + 2);               /* 4081 */
    offset[1] = (float)(short int)Get2Byte(dat_addr + 4);               /* 4083 */
    offset[2] = (float)(short int)Get2Byte(dat_addr + 6);               /* 4085 */
    offset[3] = 0.0f;

    margin    = Get2Byte(dat_addr + 8);                                 /* 4088 */
    obj_id    = Get4Byte(dat_addr + 0xc);                               /* 4094 */

    EventCameraSetPositionObjId((int)(char)obj_type, obj_id);           /* 4098 */
    EventCameraSetPositionOffset(offset);                               /* 4100 */
    EventCameraSetMargin((float)(int)margin);                           /* 4102 */

    return 1;                                                           /* 4105 */
}

static int EvCamVRSetObj(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 4123 */
    u_char *dat_addr;
    u_char  obj_type;
    u_short margin;
    u_int   obj_id;
    float   offset[4];

    dat_addr = ctrl_addr->event_addr;                                   /* 4126 */

    obj_type  = Get1Byte(dat_addr + 1);                                 /* 4129 */

    offset[0] = (float)(short int)Get2Byte(dat_addr + 2);               /* 4134 */
    offset[1] = (float)(short int)Get2Byte(dat_addr + 4);               /* 4136 */
    offset[2] = (float)(short int)Get2Byte(dat_addr + 6);               /* 4138 */
    offset[3] = 0.0f;

    margin    = Get2Byte(dat_addr + 8);                                 /* 4141 */
    obj_id    = Get4Byte(dat_addr + 0xc);                               /* 4147 */

    EventCameraSetTargetObjId((int)(char)obj_type, obj_id);             /* 4151 */
    EventCameraSetTargetOffset(offset);                                 /* 4153 */
    EventCameraSetMargin((float)(int)margin);                           /* 4155 */

    return 1;                                                           /* 4158 */
}

static int EvCamSetWorldSwitch(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 4173 */
    u_char sw;

    sw = Get1Byte(ctrl_addr->event_addr + 1);                           /* 4179 */
    EventCameraSetWorldFlg((int)(char)sw);                              /* 4182 */

    return 1;                                                           /* 4185 */
}

/* ------------------------------------------------------------------------ *
 *  Screen
 * ------------------------------------------------------------------------ */

/* Monochrome is two separate switches: the effect work block drives the 2D
 * layers, gra3dMonotoneDrawEnable() the 3D ones.  The prelight has to be
 * rebuilt afterwards because it bakes the current colour mode in. */
static int EvMonoDisp(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 4200 */
    u_char sw;
    int    bEnable;

    sw = Get1Byte(ctrl_addr->event_addr + 2);                           /* 4206 */

    if (sw == 1) {                                                      /* 4209 */
        EffWrkMonochroModeSet(1);                                       /* 4211 */
        bEnable = 1;
    } else if (sw == 0) {                                               /* 4213 */
        EffWrkMonochroModeSet(0);                                       /* 4215 */
        bEnable = 0;
    } else {
        printf("*************************************************************\n"); /* 4222 */
        printf("*     Error!! The value of the event data is illegal!!!     *\n"); /* 4223 */
        printf("*                 MONO_DISPM    Event ID %3d                *\n", ctrl_addr->event_id); /* 4224 */
        printf("*************************************************************\n"); /* 4225 */
        PRINT_ASSERT("");                                               /* 4226 */
        return 1;
    }

    gra3dMonotoneDrawEnable(bEnable);                                   /* 4217 */
    gra3dPrelight();                                                    /* 4218 */

    return 1;                                                           /* 4231 */
}

static int EvFadeIn(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 4247 */
    u_char *dat_addr;
    u_char  r;
    u_char  g;
    u_char  b;
    u_int   fade_in_time;

    dat_addr = ctrl_addr->event_addr;                                   /* 4250 */

    r            = Get1Byte(dat_addr + 1);                              /* 4253 */
    g            = Get1Byte(dat_addr + 2);                              /* 4254 */
    b            = Get1Byte(dat_addr + 3);                              /* 4255 */
    fade_in_time = Get4Byte(dat_addr + 4);                              /* 4256 */

    FadeInReq(r, g, b, fade_in_time);                                   /* 4260 */

    return 1;                                                           /* 4263 */
}

static int EvFadeOut(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 4279 */
    u_char *dat_addr;
    u_char  r;
    u_char  g;
    u_char  b;
    u_int   fade_out_time;

    dat_addr = ctrl_addr->event_addr;                                   /* 4282 */

    r             = Get1Byte(dat_addr + 1);                             /* 4285 */
    g             = Get1Byte(dat_addr + 2);                             /* 4286 */
    b             = Get1Byte(dat_addr + 3);                             /* 4287 */
    fade_out_time = Get4Byte(dat_addr + 4);                             /* 4288 */

    FadeOutReq(r, g, b, fade_out_time);                                 /* 4292 */

    return 1;                                                           /* 4295 */
}

/* ------------------------------------------------------------------------ *
 *  Sound effects
 * ------------------------------------------------------------------------ */

static int EvSoundLoad(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 4311 */
    u_char *dat_addr;
    u_char  room_no;
    u_short file_label;

    dat_addr = ctrl_addr->event_addr;                                   /* 4314 */

    room_no    = Get1Byte(dat_addr + 1);                                /* 4317 */
    file_label = Get2Byte(dat_addr + 2);                                /* 4320 */

    ev_seRegisterFile((int)(char)room_no, (int)file_label);             /* 4324 */

    return 1;                                                           /* 4327 */
}

static int EvSoundRelease(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 4343 */
    u_char *dat_addr;
    u_char  room_no;
    u_short file_label;

    dat_addr = ctrl_addr->event_addr;                                   /* 4346 */

    room_no    = Get1Byte(dat_addr + 1);                                /* 4349 */
    file_label = Get2Byte(dat_addr + 2);                                /* 4352 */

    ev_seDeleteFile((int)(char)room_no, (int)file_label);               /* 4356 */

    return 1;                                                           /* 4359 */
}

/* Shared by the three EV_SOUND*_PLAY opcodes.  Three bank ids are special --
 * the system bank, the player model bank and the sister model bank all have
 * their own play entry points -- and anything else is either an event sound
 * bank ev_se.c has registered, or a bank file played straight off disc.
 *
 * Only looping sounds are recorded in ev_sound_ctrl[]: a one-shot ends by
 * itself, so there is nothing for EV_SOUND_STOP to find and nothing to leak. */
static void EvBankSoundSub(int bd_file_no, int no, SND_3D_SET *set)
{                                                                       /* 4364 */
    int sound_id;
    int bank_id;

    if (bd_file_no == SISUTEMU_BD) {                                          /* 4368 */
        sound_id = SystemBankPlay(no, 0, 0, 0, set, 0x3200, 0x1000);    /* 4369 */

        if (SystemBankIsLoopSnd(no) != 0) {                             /* 4371 */
            Regist_SoundID(SISUTEMU_BD, no, sound_id);                        /* 4372 */
        }
    } else if (bd_file_no == MIO_SYS_BD_BD) {                                   /* 4374 */
        sound_id = plyr_mdlBankPlay(no, 0, 0, 0, set, 0x3200, 0x1000);  /* 4375 */

        /* Plays out of the player bank but asks the *sister* bank whether the
         * clip loops.  That mismatch is in the ROM; kept as-is. */
        if (sis_mdlBankIsLoopSnd(no) != 0) {                            /* 4376 */
            Regist_SoundID(MIO_SYS_BD_BD, no, sound_id);                        /* 4377 */
        }
    } else if (bd_file_no == ANE_SISUTEMU_BD) {                                   /* 4379 */
        /* Mayu's voice bank: held back while a subtitle is still running so
         * the two do not talk over each other. */
        if (SubTitleIsEnd() != 0) {                                     /* 4380 */
            sound_id = sis_mdlBankPlay(no, 0, 0, 0, set, 0x3200, 0x1000); /* 4381 */

            if (sis_mdlBankIsLoopSnd(no) != 0) {                        /* 4382 */
                Regist_SoundID(ANE_SISUTEMU_BD, no, sound_id);                    /* 4383 */
            }
        }
    } else {
        bank_id = ev_seGetBankID(bd_file_no);                           /* 4391 */

        if (bank_id == -1) {                                            /* 4393 */
            /* Not resident: stream the bank file straight off disc. */
            snd_utilAutoBDPlay(bd_file_no, bd_file_no - 1, 0, 0,
                               0x3200, 0x1000, 0, set);
        } else {
            sound_id = SndBankPlay(bank_id, no, 0, 0, 0x3200, 0x1000, 0, set); /* 4397 */

            if (SndBankIsLoopSnd(bank_id, no) != 0) {                   /* 4398 */
                Regist_SoundID(bd_file_no, no, sound_id);               /* 4399 */
            }
        }
    }
}

static int EvSoundPlay(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 4419 */
    u_char *dat_addr;
    u_short file_label;
    u_short pos;

    dat_addr = ctrl_addr->event_addr;                                   /* 4422 */

    file_label = Get2Byte(dat_addr + 2);                                /* 4425 */
    pos        = Get2Byte(dat_addr + 4);                                /* 4428 */

    EvBankSoundSub((int)file_label, (int)pos, (SND_3D_SET *)0);         /* 4431 */

    return 1;                                                           /* 4434 */
}

/* Also serves EV_SOUND3D_OBJ_STOP and EV_SOUND3D_POS_STOP -- stopping is the
 * same operation whichever way the sound was started, since ev_sound_ctrl[]
 * keys on the bank/slot pair rather than on how it was positioned.
 *
 * The scan does not stop at the first match: the last matching entry wins, so
 * when the same slot is looping more than once the most recent one is the one
 * that gets stopped. */
static int EvSoundStop(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 4443 */
    u_char *dat_addr;
    u_short file_label;
    u_short pos;
    int     id;
    int     i;

    id = -1;

    dat_addr = ctrl_addr->event_addr;                                   /* 4453 */

    file_label = Get2Byte(dat_addr + 2);                                /* 4456 */
    pos        = Get2Byte(dat_addr + 4);                                /* 4459 */
    Get2Byte(dat_addr + 6);                                             /* 4462 */

    for (i = 0; i < EV_SOUND_CTRL_MAX; i++) {                           /* 4465 */
        if ((ev_sound_ctrl[i].file_label == (int)file_label) &&
            (ev_sound_ctrl[i].pos == (int)pos)) {                       /* 4467 */
            id = ev_sound_ctrl[i].sound_id;                             /* 4469 */
        }
    }

    if (id != -1) {                                                     /* 4477 */
        SndBufStop(id);                                                 /* 4479 */
        Del_SoundID(id);                                                /* 4481 */
    }

    return 1;                                                           /* 4485 */
}

static int EvSound3DObjPlay(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 4494 */
    u_char    *dat_addr;
    u_char     obj_type;
    u_short    file_label;
    u_short    pos;
    u_int      obj_id;
    SND_3D_SET snd_3d_data;
    float      obj_pos[4];

    memset(&snd_3d_data, 0, sizeof(snd_3d_data));                       /* 4500 */
    memset(&obj_pos, 0, sizeof(obj_pos));                               /* 4501 */

    dat_addr = ctrl_addr->event_addr;                                   /* 4505 */

    obj_type   = Get1Byte(dat_addr + 1);                                /* 4508 */
    file_label = Get2Byte(dat_addr + 2);                                /* 4511 */
    pos        = Get2Byte(dat_addr + 4);                                /* 4514 */
    obj_id     = Get4Byte(dat_addr + 8);                                /* 4517 */

    if (GetObjectPos(obj_pos, obj_type, obj_id) == 0) {                 /* 4522 */
        printf("*************************************************************\n"); /* 4529 */
        printf("*     Error!! The value of the event data is illegal!!!     *\n"); /* 4530 */
        printf("*            SOUND3D_OBJ_PLAYM    Event ID %3d              *\n", ctrl_addr->event_id); /* 4531 */
        printf("*************************************************************\n"); /* 4532 */
        PRINT_ASSERT("");                                               /* 4533 */
    } else {
        snd_3d_data.pos = (sceVu0FVECTOR *)obj_pos;                                      /* 4524 */
        EvBankSoundSub((int)file_label, (int)pos, &snd_3d_data);        /* 4525 */
    }

    return 1;                                                           /* 4543 */
}

static int EvSound3DPosPlay(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 4551 */
    u_char    *dat_addr;
    u_short    file_label;
    u_short    pos;
    float      snd_pos[4];
    SND_3D_SET snd_3d_data;

    memset(&snd_3d_data, 0, sizeof(snd_3d_data));                       /* 4556 */

    dat_addr = ctrl_addr->event_addr;                                   /* 4560 */

    snd_pos[0] = (float)(short int)Get2Byte(dat_addr + 2);              /* 4566 */
    snd_pos[1] = (float)(short int)Get2Byte(dat_addr + 4);              /* 4568 */
    snd_pos[2] = (float)(short int)Get2Byte(dat_addr + 6);              /* 4570 */
    snd_pos[3] = 0.0f;                                                  /* 4572 */

    snd_3d_data.pos = (sceVu0FVECTOR *)snd_pos;                         /* 4573 */

    file_label = Get2Byte(dat_addr + 8);                                /* 4576 */
    pos        = Get2Byte(dat_addr + 10);                               /* 4579 */

    EvBankSoundSub((int)file_label, (int)pos, &snd_3d_data);            /* 4582 */

    return 1;                                                           /* 4585 */
}

/* ------------------------------------------------------------------------ *
 *  Streams
 *
 *  Only *looping* streams are tracked.  A one-shot stream ends on its own, so
 *  there is nothing to stop later and nothing to restore after a load -- which
 *  is why both the ev_stream_ctrl entry and the ev_save_* entry are written
 *  only when the loop flag is set.
 *
 *  A stream file's header lives in the file numbered one below it; that is the
 *  file_label - 1 every StreamAutoPlay() call here passes.
 * ------------------------------------------------------------------------ */

static int EvStreamPlay(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 4605 */
    u_char *dat_addr;
    u_char  loop;
    u_short file_label;
    u_short in_time;
    u_short volume;
    int     stream_id;

    dat_addr = ctrl_addr->event_addr;                                   /* 4608 */

    loop       = Get1Byte(dat_addr + 1);                                /* 4611 */
    file_label = Get2Byte(dat_addr + 2);                                /* 4614 */
    in_time    = Get2Byte(dat_addr + 4);                                /* 4617 */
    Get2Byte(dat_addr + 6);                                             /* 4620 */
    volume     = Get2Byte(dat_addr + 8);                                /* 4623 */

    /* Refuse to start a second copy of a stream already playing. */
    if (IsRegist_StreamID((int)file_label) == 0) {                      /* 4627 */
        stream_id = StreamAutoPlay((int)file_label, file_label - 1, 0x11, 0,
                                   (int)(char)loop, (int)volume, (int)in_time,
                                   (SND_3D_SET *)0);                    /* 4633 */

        if (loop == 1) {                                                /* 4638 */
            Regist_StreamID(stream_id, (int)file_label);                /* 4640 */
            SetEvSaveStream(stream_id, (int)file_label, (int)volume);   /* 4641 */
        }
    }

    return 1;                                                           /* 4645 */
}

/* Shared by EV_STREAM_STOP, EV_STREAM3D_OBJ_STOP and EV_STREAM3D_POS_STOP.
 * Which of the three ev_save_* tables the stream was recorded in is not known
 * here, so all three are cleared -- each Del is a no-op for an id it does not
 * hold. */
static void EvStreamStopSub(int file_label, int fade_out_time)
{                                                                       /* 4648 */
    int id;

    id = -1;

    for (int i = 0; i < EV_STREAM_CTRL_MAX; i++) {                      /* 4651 */
        if (ev_stream_ctrl[i].file_label == file_label) {               /* 4653 */
            id = ev_stream_ctrl[i].stream_id;
            break;
        }
    }

    StreamAutoFadeOut(id, fade_out_time);                               /* 4658 */

    Del_StreamID(id);                                                   /* 4662 */
    DelEvSaveStream(id);                                                /* 4663 */
    DelEvSaveObjStream(id);                                             /* 4665 */
    DelEvSavePosStream(id);                                             /* 4666 */
}                                                                       /* 4667 */

static int EvStreamStop(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 4683 */
    u_char *dat_addr;
    u_short file_label;
    u_short out_time;

    dat_addr = ctrl_addr->event_addr;                                   /* 4686 */

    file_label = Get2Byte(dat_addr + 2);                                /* 4689 */
    out_time   = Get2Byte(dat_addr + 4);                                /* 4692 */

    EvStreamStopSub((int)file_label, (int)out_time);                    /* 4695 */

    return 1;                                                           /* 4729 */
}

/* The map BGM is owned by map_bgm.c, not by this file: these three only
 * forward, and nothing is recorded in ev_stream_ctrl[] because the room's own
 * bookkeeping already survives a save. */
static int EvMapStreamPlay(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 4744 */
    u_short in_time;

    in_time = Get2Byte(ctrl_addr->event_addr + 2);                      /* 4750 */
    map_bgmFadeIn((int)in_time);                                        /* 4754 */

    return 1;                                                           /* 4759 */
}

static int EvMapStreamStop(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 4774 */
    u_short out_time;

    out_time = Get2Byte(ctrl_addr->event_addr + 2);                     /* 4780 */
    map_bgmFadeOut((int)out_time, 0);                                   /* 4784 */

    return 1;                                                           /* 4790 */
}

static int EvMapStreamChange(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 4806 */
    u_char *dat_addr;
    u_short bgm_no;
    u_int   iStrFileNo;

    dat_addr = ctrl_addr->event_addr;                                   /* 4809 */

    bgm_no     = Get2Byte(dat_addr + 2);                                /* 4812 */
    iStrFileNo = Get4Byte(dat_addr + 4);                                /* 4815 */

    map_bgmChangeTbl((int)bgm_no, iStrFileNo);                          /* 4819 */

    return 1;                                                           /* 4822 */
}

static int EvStream3DObjPlay(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 4831 */
    u_char    *dat_addr;
    u_char     obj_type;
    u_char     loop;
    u_short    file_label;
    u_short    in_time;
    u_short    volume;
    u_int      obj_id;
    int        stream_id;
    SND_3D_SET snd_3d_data;
    float      obj_pos[4];

    memset(&snd_3d_data, 0, sizeof(snd_3d_data));                       /* 4838 */
    memset(&obj_pos, 0, sizeof(obj_pos));                               /* 4839 */

    dat_addr = ctrl_addr->event_addr;                                   /* 4846 */

    obj_type   = Get1Byte(dat_addr + 1);                                /* 4849 */
    file_label = Get2Byte(dat_addr + 2);                                /* 4852 */
    in_time    = Get2Byte(dat_addr + 4);                                /* 4855 */
    Get2Byte(dat_addr + 6);                                             /* 4858 */
    volume     = Get2Byte(dat_addr + 8);                                /* 4861 */
    loop       = Get1Byte(dat_addr + 10);                               /* 4864 */
    obj_id     = Get4Byte(dat_addr + 0xc);                              /* 4867 */

    if (GetObjectPos(obj_pos, obj_type, obj_id) == 0) {                 /* 4872 */
        /* Banner names SOUND3D_OBJ_PLAY rather than STREAM3D_OBJ_PLAY -- the
         * ROM copied it from EvSound3DObjPlay(). */
        printf("*************************************************************\n"); /* 4879 */
        printf("*     Error!! The value of the event data is illegal!!!     *\n"); /* 4880 */
        printf("*            SOUND3D_OBJ_PLAYM    Event ID %3d              *\n", ctrl_addr->event_id); /* 4881 */
        printf("*************************************************************\n"); /* 4882 */
        PRINT_ASSERT("");                                               /* 4883 */
    } else {
        snd_3d_data.pos = (sceVu0FVECTOR *)obj_pos;                                      /* 4874 */

        if (IsRegist_StreamID((int)file_label) == 0) {                  /* 4890 */
            stream_id = StreamAutoPlay((int)file_label, file_label - 1, 0x11, 0,
                                       (int)(char)loop, (int)volume, (int)in_time,
                                       &snd_3d_data);                   /* 4896 */

            if (loop == 1) {                                            /* 4898 */
                Regist_StreamID(stream_id, (int)file_label);            /* 4900 */
                SetEvSaveObjStream(stream_id, (int)(char)obj_type, obj_id,
                                   (int)file_label, (int)volume);       /* 4901 */
            }
        }
    }

    return 1;                                                           /* 4905 */
}

static int EvStream3DPosPlay(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 4913 */
    u_char    *dat_addr;
    u_char     loop;
    u_short    file_label;
    u_short    in_time;
    u_short    volume;
    int        stream_id;
    float      snd_pos[4];
    SND_3D_SET snd_3d_data;

    memset(&snd_3d_data, 0, sizeof(snd_3d_data));                       /* 4919 */

    dat_addr = ctrl_addr->event_addr;                                   /* 4926 */

    loop       = Get1Byte(dat_addr + 1);                                /* 4929 */
    file_label = Get2Byte(dat_addr + 2);                                /* 4932 */
    in_time    = Get2Byte(dat_addr + 4);                                /* 4935 */

    snd_pos[0] = (float)(short int)Get2Byte(dat_addr + 6);              /* 4941 */
    snd_pos[1] = (float)(short int)Get2Byte(dat_addr + 8);              /* 4943 */
    snd_pos[2] = (float)(short int)Get2Byte(dat_addr + 10);             /* 4945 */
    snd_pos[3] = 0.0f;                                                  /* 4947 */

    snd_3d_data.pos = (sceVu0FVECTOR *)snd_pos;                                          /* 4948 */

    Get2Byte(dat_addr + 0xc);                                           /* 4951 */
    volume     = Get2Byte(dat_addr + 0xe);                              /* 4954 */

    if (IsRegist_StreamID((int)file_label) == 0) {                      /* 4958 */
        stream_id = StreamAutoPlay((int)file_label, file_label - 1, 0x11, 0,
                                   (int)(char)loop, (int)volume, (int)in_time,
                                   &snd_3d_data);                       /* 4964 */

        if (loop == 1) {                                                /* 4966 */
            Regist_StreamID(stream_id, (int)file_label);                /* 4968 */
            SetEvSavePosStream(stream_id, snd_pos, (int)file_label, (int)volume); /* 4969 */
        }
    }

    return 1;                                                           /* 4973 */
}

/* Stops everything, including streams this file never started -- so the
 * tracking tables are wiped wholesale rather than entry by entry. */
static int EvStreamAllStop(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 4985 */
    StreamAutoAllStop();
    EvStreamCtrlInit();                                                 /* 4988 */

    return 1;                                                           /* 4991 */
}

/* ------------------------------------------------------------------------ *
 *  Doors
 * ------------------------------------------------------------------------ */

/* Blocking, with two different waits behind one opcode: an unlocked door plays
 * the open animation (process 1), a locked one plays the rattle-and-fail
 * reaction instead (process 2).  Only the locked branch takes the message
 * display and the filament, because only it puts text on screen. */
static int EvDoorOpen(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 5000 */
    u_short door_id;
    int     ret;

    ret = 0;

    door_id = Get2Byte(ctrl_addr->event_addr + 2);                      /* 5010 */

    SetEventWaitFlg(1);                                                 /* 5016 */

    if (door_id > 0xcf) {                                               /* 5024 */
        PRINT_ASSERT("Error! EvDoorOpen event_id[%d] door_id %d",
                     ctrl_addr->event_id, door_id);                     /* 5025 */
    }

    if (ctrl_addr->process == 0) {                                      /* 5030 */
        if (GetDoorLockState((int)door_id) == 0) {                      /* 5032 */
            if (DoorOpenInit((int)door_id) < 0) {                       /* 5034 */
                /* The door refused -- treat the opcode as done rather than
                 * stranding the event on a door that will never open. */
                ret = 1;
                printf("ERR! CANNOT_OPEN_DOOR : ev_exe.c\n");           /* 5035 */
            } else {
                ctrl_addr->process = 1;                                 /* 5044 */
            }
        } else {
            IngameEventMsgDispReq(1);                                   /* 5046 */
            FilamentDrawLock();                                         /* 5048 */
            SetPlyrAnime(0, 10);                                        /* 5051 */
            DoorLockStateExeInit((int)door_id);                         /* 5053 */
            ctrl_addr->process = 2;                                     /* 5055 */
        }
    } else if (ctrl_addr->process == 1) {                               /* 5058 */
        if (DoorOpen((int)door_id) != 0) {                              /* 5062 */
            ret = 1;                                                    /* 5065 */
        }
    } else if (ctrl_addr->process == 2) {                               /* 5066 */
        if (DoorLockStateExe((int)door_id) != 0) {                      /* 5071 */
            ret = 1;                                                    /* 5073 */
            IngameEventMsgDispReq(0);
            FilamentDrawUnlock();                                       /* 5081 */
            SetNextGPhase(IngameDecideNextPhase());                     /* 5082 */
        }
    }

    if (ret == 1) {
        SetEventWaitFlg(0);
    }

    return ret;                                                         /* 5090 */
}

static int EvDoorClose(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 5105 */
    u_short door_id;

    door_id = Get2Byte(ctrl_addr->event_addr + 2);                      /* 5111 */

    if (door_id > 0xcf) {                                               /* 5114 */
        PRINT_ASSERT("Error! EvDoorClose event_id[%d] door_id %d",
                     ctrl_addr->event_id, door_id);                     /* 5115 */
    }

    DoorClose((int)door_id);                                            /* 5120 */

    return 1;                                                           /* 5123 */
}

static int EvDoorLock(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 5139 */
    u_char *dat_addr;
    u_char  lock_id;
    u_short door_id;

    dat_addr = ctrl_addr->event_addr;                                   /* 5142 */

    lock_id = Get1Byte(dat_addr + 1);                                   /* 5145 */
    door_id = Get2Byte(dat_addr + 2);                                   /* 5148 */

    /* lock_id selects which "it is locked" reaction the door plays, not
     * whether it is locked. */
    if (lock_id > 6) {                                                  /* 5152 */
        PRINT_ASSERT("Error! EvDoorLock event_id[%d] lock_id %d",
                     ctrl_addr->event_id, lock_id);                     /* 5153 */
    }

    if (door_id > 0xcf) {                                               /* 5155 */
        PRINT_ASSERT("Error! EvDoorLock event_id[%d] door_id %d",
                     ctrl_addr->event_id, door_id);                     /* 5156 */
    }

    DoorLock((int)door_id, lock_id);                                    /* 5161 */

    return 1;                                                           /* 5168 */
}

static int EvDoorUnlock(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 5183 */
    u_short door_id;

    door_id = Get2Byte(ctrl_addr->event_addr + 2);                      /* 5189 */

    if (door_id > 0xcf) {                                               /* 5193 */
        PRINT_ASSERT("Error! EvDoorUnlock event_id[%d] door_id %d",
                     ctrl_addr->event_id, door_id);                     /* 5194 */
    }

    DoorUnlock((int)door_id);                                           /* 5199 */

    return 1;                                                           /* 5206 */
}

/* ------------------------------------------------------------------------ *
 *  Map loading
 * ------------------------------------------------------------------------ */

/* MapLBuffSetLoadFile() answers 0 once the reference is held and -1 when the
 * map's 16 slots are all taken; nothing else is reachable, so the `res != -1`
 * complaint below is dead.  Only 0 lets the event move on.
 *
 * NOTE: -1 is what the shipped prototype always returns here -- MapLBuffInit()
 * is never called, so the slot table never gets its free markers.  See the
 * header comment in ingame/map/MapLBuff.c. */
static int EvLoadRequest(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 5224 */
    u_char *dat_addr;
    u_short map_id;
    u_short file_id;
    int     res;
    int     ret;

    dat_addr = ctrl_addr->event_addr;                                   /* 5227 */

    map_id  = Get2Byte(dat_addr + 2);                                   /* 5230 */
    file_id = Get2Byte(dat_addr + 4);                                   /* 5232 */

    if (map_id > 0x41) {                                                /* 5236 */
        PRINT_ASSERT("Error! EvLoadRequest event_id[%d] map_id %d",
                     ctrl_addr->event_id, map_id);                      /* 5237 */
    }

    res = MapLBuffSetLoadFile((int)(short int)map_id, (int)file_id);    /* 5241 */
    ret = 1;

    if (res != 0) {                                                     /* 5244 */
        ret = 0;

        if (res != -1) {
            printf("ERROR!! EvLoadRequest() is failure\n");             /* 5254 */
            ret = 0;                                                    /* 5255 */
        }
    }

    return ret;                                                         /* 5259 */
}

static int EvReleaseRequest(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 5277 */
    u_char *dat_addr;
    u_short map_id;
    u_short file_id;
    int     res;
    int     ret;

    dat_addr = ctrl_addr->event_addr;                                   /* 5280 */

    map_id  = Get2Byte(dat_addr + 2);                                   /* 5283 */
    file_id = Get2Byte(dat_addr + 4);                                   /* 5285 */

    if (map_id > 0x41) {                                                /* 5289 */
        PRINT_ASSERT("Error! EvReleaseRequest event_id[%d] map_id %d",
                     ctrl_addr->event_id, map_id);                      /* 5290 */
    }

    res = MapLBuffDeleteFile((int)(short int)map_id, (int)file_id);     /* 5295 */
    ret = 1;

    if (res != 0) {                                                     /* 5298 */
        ret = 0;

        if (res != -1) {
            printf("ERROR!! EvReleaseRequest() is failure\n");          /* 5308 */
            ret = 0;                                                    /* 5309 */
        }
    }

    return ret;                                                         /* 5313 */
}

/* Two frames minimum.  The first frame decides how the move is done -- a room
 * already in a buffer is just a draw-flag swap, an absent one needs a real
 * load -- and the second waits for the loader to go quiet before the player
 * and sister are actually moved across. */
static int EvAreaChange(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 5330 */
    u_short area;
    int     area_no;
    int     buff_id;
    int     ret;

    area    = Get2Byte(ctrl_addr->event_addr + 2);                      /* 5336 */
    area_no = (int)area;

    if (area_no >= 0x42) {                                              /* 5340 */
        PRINT_ASSERT("Error! EvAreaChange event_id[%d] map_id %d",
                     ctrl_addr->event_id, area_no);                     /* 5341 */
    }

    if (ctrl_addr->process == 0) {                                      /* 5345 */
        ctrl_addr->process = 1;
        ret = 0;

        if (MapLoadCheckLoadNow() != 0) {                               /* 5349 */
            /* Loader busy -- undo the step and try again next frame. */
            ctrl_addr->process = 0;                                     /* 5351 */
        } else {
            buff_id = MapLoadGetBuffID(area_no);                        /* 5361 */

            if (buff_id < 0) {                                          /* 5364 */
                MapLoadSetDrawFlg(3, 2);                                /* 5365 */
                MapLoadMoveRoom(area_no);                               /* 5366 */
                ev_seChangeRoom(area_no);                               /* 5368 */
                ev_sisChangeRoom(area_no);                              /* 5370 */
                ev_eneChangeRoom(area_no);                              /* 5373 */
            } else {
                /* Already resident: show it, hide the other buffer. */
                MapLoadSetDrawFlg2(buff_id ^ 1, 2);                     /* 5374 */
                MapLoadSetDrawFlg2(buff_id, 1);                         /* 5375 */
            }
        }
    } else {
        if (MapLoadCheckLoadNow() == 0) {                               /* 5379 */
            SetPlyrAreaNo(area_no);                                     /* 5384 */
            SetSisAreaNo(area_no);                                      /* 5388 */

            buff_id = MapLoadGetBuffID(area_no);                        /* 5389 */
            MapLoadDeleteRoom(buff_id ^ 1);                             /* 5392 */
            MapLoadSetNowRoom(area_no);                                 /* 5395 */

            ret = 1;                                                    /* 5398 */
        } else {
            ret = 0;
        }
    }

    return ret;                                                         /* 5399 */
}

static int EvBackGroundLoad(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 5413 */
    u_short area;
    int     room_no;

    area    = Get2Byte(ctrl_addr->event_addr + 2);                      /* 5419 */
    room_no = (int)area;

    if (room_no > 0x41) {                                               /* 5423 */
        PRINT_ASSERT("Error! EvBackGroundLoad event_id[%d] map_id %d",
                     ctrl_addr->event_id, room_no);                     /* 5424 */
    }

    MapLoadBg(room_no);                                                 /* 5430 */

    return 1;                                                           /* 5438 */
}

/* Completes the room swap a preceding AREA_CHANGE kicked off.  The player is
 * locked for the duration -- MapLoadSwitch() tears down and rebuilds the
 * room's data underneath them. */
static int EvAreaSwitch(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 5455 */
    int res;
    int room_no;

    if (ctrl_addr->process == 0) {                                      /* 5456 */
        PlayerLock();                                                   /* 5457 */
        ctrl_addr->process = 1;
    }

    res = MapLoadSwitch();                                              /* 5462 */

    if (res == 0) {
        room_no = (int)(short int)MapLoadGetRoomNoNow();                /* 5467 */

        ev_seChangeRoom(room_no);                                       /* 5468 */
        ev_sisChangeRoom(room_no);                                      /* 5469 */
        ev_eneChangeRoom(room_no);                                      /* 5470 */

        SetPlyrAreaNo(room_no);                                         /* 5473 */
        SetSisAreaNo(room_no);                                          /* 5474 */

        /* The sister is re-placed relative to where the player came from, so
         * she does not end up on the far side of the new room. */
        ChangeSisterExPos((u_int)plyr_wrk.cmn_wrk.pr_info.area_old,
                          (u_int)plyr_wrk.cmn_wrk.pr_info.area_no);     /* 5477 */

        PlayerUnlock();                                                 /* 5479 */
        MapAnimProc();                                                  /* 5498 */
    }

    return (res == 0);                                                  /* 5504 */
}

/* ------------------------------------------------------------------------ *
 *  Messages
 *
 *  Anything that puts text on screen follows the same shape: take the finder
 *  and the sister on the first frame, drive the message system until it says
 *  it is done, then hand everything back and let the phase machine re-decide
 *  what the game should be showing.  SetEventWaitFlg() is held for the whole
 *  time so no other event starts talking over this one.
 * ------------------------------------------------------------------------ */

static int EvDispMsg(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 5512 */
    u_char *dat_addr;
    u_char  msg_type;
    u_short msg_id;
    int     mes_status;

    dat_addr = ctrl_addr->event_addr;                                   /* 5521 */

    msg_type = Get1Byte(dat_addr + 1);                                  /* 5524 */
    msg_id   = Get2Byte(dat_addr + 2);                                  /* 5527 */

    SetEventWaitFlg(1);                                                 /* 5530 */

    if (msg_type > 0x52) {                                              /* 5539 */
        PRINT_ASSERT("Error! EvDispMsg event_id[%d] msg_type %d",
                     ctrl_addr->event_id, msg_type);                    /* 5540 */
    }

    if (((long)msg_id < 0) ||
        ((long)msg_id >= (long)GetMsgIDNumMax((int)(char)msg_type))) {  /* 5542 */
        printf("Error! EvDispMsg event_id[%d] msg_type %d, msg_id %d\n", ctrl_addr->event_id, msg_type, msg_id);            /* 5543 */
        PRINT_ASSERT("Error! EvDispMsg event_id[%d] msg_type %d, msg_id %d", ctrl_addr->event_id, msg_type, msg_id);      /* 5544 */
    }

    if (ctrl_addr->process == 0) {                                      /* 5548 */
        SetPlyrFinderEnd();                                             /* 5552 */
        PlayerFinderLock();                                             /* 5554 */
        IngameEventMsgDispReq(1);                                       /* 5556 */
        SisterLock();                                                   /* 5558 */
        ctrl_addr->process = 1;                                         /* 5560 */
    } else if (ctrl_addr->process == 1) {                               /* 5562 */
        PrintMsgDef_W(msg_type, msg_id);                /* 5564 */

        mes_status = MesStatusCheck();                                  /* 5565 */

        if (mes_status == 0) {                                          /* 5567 */
            SetEventWaitFlg(0);                                         /* 5570 */
            IngameEventMsgDispReq(0);                                   /* 5571 */
            SetNextGPhase(IngameDecideNextPhase());                     /* 5572 */
            PlayerFinderUnlock();                                       /* 5576 */
            SisterUnlock();                                             /* 5577 */
            return 1;                                                   /* 5579 */
        }

        if (mes_status == 1) {                                          /* 5582 */
            if (*paddat[3] == 1) {                                      /* 5585 */
                MesSetNextPage();                                       /* 5586 */
            }
        }
    }

    return 0;                                                           /* 5598 */
}

/* ------------------------------------------------------------------------ *
 *  Dialogue
 *
 *  A conversation is built up first -- TALK_TBL_INIT, then one TALK_DATA_ADD /
 *  TALK_SUBTITLE_ADD per line -- and only run when TALK_EXE is reached.  Both
 *  add opcodes feed the same TalkDataAdd(), differing only in which of the two
 *  ids is -1: a spoken line has a message id, a subtitle has a subtitle id.
 * ------------------------------------------------------------------------ */

static int EvTalkTblInit(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 5613 */
    u_char tbl_id;

    tbl_id = Get1Byte(ctrl_addr->event_addr + 2);                       /* 5619 */
    TalkTblInit(tbl_id);                                                /* 5623 */

    return 1;                                                           /* 5626 */
}

static int EvTalkDataAdd(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 5642 */
    u_char *dat_addr;
    u_char  tbl_id;
    u_short msg_id;

    dat_addr = ctrl_addr->event_addr;                                   /* 5645 */

    tbl_id = Get1Byte(dat_addr + 1);                                    /* 5648 */
    msg_id = Get2Byte(dat_addr + 2);                                    /* 5651 */

    TalkDataAdd(tbl_id, (int)msg_id, -1);                               /* 5655 */

    return 1;                                                           /* 5658 */
}

static int EvTalkSubtitleAdd(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 5674 */
    u_char *dat_addr;
    u_char  tbl_id;
    u_short subtitle_id;

    dat_addr = ctrl_addr->event_addr;                                   /* 5677 */

    tbl_id      = Get1Byte(dat_addr + 1);                               /* 5680 */
    subtitle_id = Get2Byte(dat_addr + 2);                               /* 5683 */

    TalkDataAdd(tbl_id, -1, (int)subtitle_id);                          /* 5687 */

    return 1;                                                           /* 5690 */
}

static int EvTalkTypeChange(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 5706 */
    u_char *dat_addr;
    u_char  tbl_id;
    u_short type;

    dat_addr = ctrl_addr->event_addr;                                   /* 5709 */

    tbl_id = Get1Byte(dat_addr + 1);                                    /* 5712 */
    type   = Get2Byte(dat_addr + 2);                                    /* 5715 */

    TalkTypeChange(tbl_id, (int)type);                                  /* 5719 */

    return 1;                                                           /* 5722 */
}

/* Unlike EvDispMsg this one does not touch the sister -- a conversation is
 * exactly the case where she should still be animating. */
static int EvTalkExe(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 5731 */
    u_char tbl_id;

    tbl_id = Get1Byte(ctrl_addr->event_addr + 2);                       /* 5738 */

    SetEventWaitFlg(1);                                                 /* 5744 */

    if (ctrl_addr->process == 0) {                                      /* 5747 */
        IngameEventMsgDispReq(1);                                       /* 5749 */
        SetPlyrFinderEnd();                                             /* 5752 */
        PlayerFinderLock();                                             /* 5757 */
        ctrl_addr->process = 1;                                         /* 5759 */
    } else if (ctrl_addr->process == 1) {                               /* 5761 */
        if (TalkExeMain(tbl_id) != 0) {                                 /* 5763 */
            IngameEventMsgDispReq(0);                                   /* 5767 */
            SetNextGPhase(IngameDecideNextPhase());                     /* 5768 */
            SetEventWaitFlg(0);                                         /* 5772 */
            PlayerFinderUnlock();                                       /* 5773 */
            return 1;                                                   /* 5775 */
        }
    } else {
        PRINT_ASSERT("Event Process Error! %s", "EvTalkExe");           /* 5781 */
    }

    return 0;                                                           /* 5785 */
}

static int EvTalkCam(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 5801 */
    u_char *dat_addr;
    u_char  tbl_id;
    u_char  on_off;

    dat_addr = ctrl_addr->event_addr;                                   /* 5804 */

    tbl_id = Get1Byte(dat_addr + 1);                                    /* 5807 */
    on_off = Get1Byte(dat_addr + 2);                                    /* 5810 */

    TalkCamSet(tbl_id, on_off);                                         /* 5814 */

    return 1;                                                           /* 5817 */
}

/* Up to three answers, terminated by a zero message id.  sel_max counts the
 * non-zero ones and is what the cursor wraps against, so a two-answer question
 * wraps at two.  The chosen index lands in ev_choice_ctrl.sel_num, which is
 * what the IF_SELECT_CHOICE predicate reads back. */
static int EvMsgChoice(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 5827 */
    u_char *dat_addr;
    u_short choice_msg;
    int     choice[3];
    int     sel_max;
    int     i;

    sel_max = 0;

    dat_addr = ctrl_addr->event_addr;                                   /* 5837 */

    choice_msg = Get2Byte(dat_addr + 2);                                /* 5840 */

    SetEventWaitFlg(1);                                                 /* 5843 */

    for (i = 0; i < 3; i++) {                                           /* 5846 */
        choice[i] = (int)Get2Byte(dat_addr + 4 + i * 2);                /* 5848 */

        if (choice[i] != 0) {                                           /* 5851 */
            sel_max++;                                                  /* 5852 */
        }
    }                                                                   /* 5855 */

    if (ctrl_addr->process == 0) {                                      /* 5859 */
        IngameEventMsgDispReq(1);                                       /* 5864 */
        ev_choice_ctrl.csr = 0;                                         /* 5869 */
        SetPlyrFinderEnd();                                             /* 5874 */
        PlayerFinderLock();                                             /* 5876 */
        SisterLock();                                                   /* 5878 */
        ctrl_addr->process = 1;                                         /* 5880 */
    } else if (ctrl_addr->process == 1) {                               /* 5883 */
        PrintChoice((DISP_STR *)0, (MSG_WIN_DAT *)0, (int)choice_msg,
                    choice, (int)ev_choice_ctrl.csr);                   /* 5886 */

        if (*paddat[0] == 1) {                                          /* 5887 */
            SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000); /* 5888 */
            ctrl_addr->process = 3;                                     /* 5892 */
        } else if (((pad[0].rpt & 0x8000) != 0) || (GetPadAnalogRpt(2) != 0)) { /* 5893 */
            SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000); /* 5894 */
            ev_choice_ctrl.csr = (char)((ev_choice_ctrl.csr + sel_max - 1) % sel_max); /* 5897 */
        } else if (((pad[0].rpt & 0x2000) != 0) || (GetPadAnalogRpt(3) != 0)) { /* 5898 */
            SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000); /* 5899 */
            ev_choice_ctrl.csr = (char)((ev_choice_ctrl.csr + 1) % sel_max); /* 5902 */
        }
    } else if (ctrl_addr->process == 3) {                               /* 5908 */
        /* Draw one last time so the highlighted answer is on screen for the
         * frame the choice is committed. */
        PrintChoice((DISP_STR *)0, (MSG_WIN_DAT *)0, (int)choice_msg,
                    choice, (int)ev_choice_ctrl.csr);                   /* 5910 */

        ev_choice_ctrl.sel_num = ev_choice_ctrl.csr;                    /* 5912 */
        ev_choice_ctrl.csr     = 0;                                     /* 5916 */

        IngameEventMsgDispReq(0);                                       /* 5919 */
        SetNextGPhase(IngameDecideNextPhase());                         /* 5921 */
        SetEventWaitFlg(0);                                             /* 5922 */
        PlayerFinderUnlock();                                           /* 5925 */
        SisterUnlock();
        return 1;
    } else {
        PRINT_ASSERT("Event Process Error! %s", __FUNCTION__);         /* 5927 */
    }

    return 0;                                                           /* 5931 */
}

/* GetNextEventCom() is called for its side effect of validating that the
 * opcode after this one exists -- its result is not used. */
static int EvMoviePlay(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 5946 */
    u_char scene_no;

    scene_no = Get1Byte(ctrl_addr->event_addr + 1);                     /* 5952 */

    PlyrSEStop();                                                       /* 5954 */
    IngameSceneReq((int)(char)scene_no);                                /* 5958 */
    GetNextEventCom(ctrl_addr);                                         /* 5961 */

    return 1;                                                           /* 5966 */
}

/* ------------------------------------------------------------------------ *
 *  2D overlays
 * ------------------------------------------------------------------------ */

/* Blocks while the picture loads -- the request is made once and then polled,
 * so an event never draws a still that is not resident yet. */
static int EvDisp2DStart(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 5975 */
    u_char *dat_addr;
    u_char  win_flg;
    u_short file_label;
    u_short x;
    u_short y;
    u_short fade_in_time;
    u_short base_label;
    int     ret;

    ret = 0;

    dat_addr = ctrl_addr->event_addr;                                   /* 5987 */

    win_flg      = Get1Byte(dat_addr + 1);                              /* 5990 */
    file_label   = Get2Byte(dat_addr + 2);                              /* 5993 */
    x            = Get2Byte(dat_addr + 4);                              /* 5996 */
    y            = Get2Byte(dat_addr + 6);                              /* 5998 */
    fade_in_time = Get2Byte(dat_addr + 8);                              /* 5999 */
    base_label   = Get2Byte(dat_addr + 10);                             /* 6002 */

    if (base_label > 1) {                                               /* 6010 */
        PRINT_ASSERT("Error! EvDisp2DStart event_id[%d] base_label %d\n",
                     ctrl_addr->event_id, base_label);                  /* 6011 */
    }

    if (ctrl_addr->process == 0) {                                      /* 6015 */
        EvDisp2DStartReq((int)x, (int)y, (int)file_label, (int)fade_in_time,
                         win_flg, (int)base_label);                     /* 6016 */
        ctrl_addr->process = 1;                                         /* 6018 */
    } else if (ctrl_addr->process == 1) {                               /* 6020 */
        ret = (CheckEvDisp2DDataLoad() != 0);                           /* 6021 */
    } else {
        ret = 0;
    }

    return ret;                                                         /* 6027 */
}

static int EvDisp2DEnd(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 6042 */
    u_short fade_out_time;

    fade_out_time = Get2Byte(ctrl_addr->event_addr + 2);                /* 6048 */
    EvDisp2DEndReq((int)fade_out_time);                                 /* 6051 */

    return 1;                                                           /* 6054 */
}

static int EvChapterDispStart(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 6070 */
    u_char chapter_num;

    chapter_num = Get1Byte(ctrl_addr->event_addr + 1);                  /* 6076 */

    if ((char)chapter_num > 10) {                                       /* 6080 */
        PRINT_ASSERT("Error! EvChapterDispStart event_id[%d] chapter_num %d",
                     ctrl_addr->event_id, chapter_num);                 /* 6081 */
    }

    EvChapterDispStartReq(chapter_num);                                 /* 6086 */

    return 1;                                                           /* 6090 */
}

/* ------------------------------------------------------------------------ *
 *  Event fog
 *
 *  All five opcodes read their operands and discard them.  Per-event fog
 *  override did not make it into this prototype -- the map's own fog data is
 *  what the renderer uses -- but the opcodes stayed in the table so existing
 *  event data keeps parsing.
 * ------------------------------------------------------------------------ */

static int EvEvFog(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 6106 */
    Get1Byte(ctrl_addr->event_addr + 2);                                /* 6112 */

    return 1;                                                           /* 6118 */
}

static int EvSetEvFogColor(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 6134 */
    u_char *dat_addr;

    dat_addr = ctrl_addr->event_addr;                                   /* 6137 */

    Get1Byte(dat_addr + 1);                                             /* 6140 */
    Get1Byte(dat_addr + 2);                                             /* 6141 */
    Get1Byte(dat_addr + 3);                                             /* 6142 */
    Get2Byte(dat_addr + 4);                                             /* 6144 */

    return 1;                                                           /* 6150 */
}

static int EvSetEvFogDistNear(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 6166 */
    u_char *dat_addr;

    dat_addr = ctrl_addr->event_addr;                                   /* 6169 */

    Get2Byte(dat_addr + 2);                                             /* 6172 */
    Get2Byte(dat_addr + 4);                                             /* 6174 */

    return 1;                                                           /* 6180 */
}

static int EvSetEvFogDistFar(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 6196 */
    u_char *dat_addr;

    dat_addr = ctrl_addr->event_addr;                                   /* 6199 */

    Get2Byte(dat_addr + 2);                                             /* 6202 */
    Get2Byte(dat_addr + 4);                                             /* 6204 */

    return 1;                                                           /* 6210 */
}

static int EvSetEvFogDist(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 6227 */
    u_char *dat_addr;

    dat_addr = ctrl_addr->event_addr;                                   /* 6230 */

    Get2Byte(dat_addr + 2);                                             /* 6233 */
    Get2Byte(dat_addr + 4);                                             /* 6235 */
    Get2Byte(dat_addr + 6);                                             /* 6237 */

    return 1;                                                           /* 6243 */
}

/* ------------------------------------------------------------------------ *
 *  Screen effects
 * ------------------------------------------------------------------------ */

static int EvOverLapStart(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 6258 */
    SetEffects_OVERLAP(2, Get2Byte(ctrl_addr->event_addr + 2));        /* 6264, 6268 */

    return 1;                                                           /* 6271 */
}

static int EvOverLapEnd(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 6280 */
    CutEffects(8);                                                      /* 6284 */

    return 1;                                                           /* 6286 */
}

/* The filament is the camera's viewfinder needle.  The timed form runs it for
 * a set number of frames; the plain form leaves it on until RELEASE. */
static int EvFilamentTimerCall(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 6301 */
    u_short time;

    time = Get2Byte(ctrl_addr->event_addr + 2);                         /* 6307 */
    RTFillamentModeOn(1, (int)time);                                    /* 6311 */

    return 1;                                                           /* 6314 */
}

static int EvFilamentCall(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 6324 */
    RTFillamentModeOn(0, 1);                                            /* 6328 */

    return 1;                                                           /* 6331 */
}

static int EvFilamentRelease(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 6345 */
    RTFillamentModeOff();

    return 1;                                                           /* 6348 */
}

/* ------------------------------------------------------------------------ *
 *  Behaviour overrides
 * ------------------------------------------------------------------------ */

/* Only two object classes actually accept an algorithm change: enemies (types
 * 0, 1, 2 and 5) and the sister (type 7).  An enemy that is not resident yet
 * makes the opcode block rather than fail. */
static int EvSetObjAlgorithm(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 6366 */
    u_char *dat_addr;
    u_char  obj_type;
    u_short algorithm;
    u_int   dat_no;
    int     ret;

    dat_addr = ctrl_addr->event_addr;                                   /* 6369 */

    obj_type  = Get1Byte(dat_addr + 1);                                 /* 6372 */
    algorithm = Get2Byte(dat_addr + 2);                                 /* 6375 */
    dat_no    = Get4Byte(dat_addr + 4);                                 /* 6378 */

    if (obj_type > 10) {                                                /* 6382 */
        PRINT_ASSERT("Error! EvSetObjAlgorithm event_id [%d] obj_type %d", ctrl_addr->event_id, obj_type);                    /* 6383 */
    }

    switch (obj_type) {                                                 /* 6387 */
    case 0:
    case 1:
    case 2:
    case 5:
        if (ChangeEneAlgorithm((int)(char)obj_type, dat_no, (int)algorithm) == 0) { /* 6390 */
            printf("EvSetObjAlgorithm Enemy Is Not Ready\n");           /* 6391 */
            ret = 0;
        } else {
            ret = 1;
        }
        break;

    case 7:
        ReqModeSisMotion((int)algorithm);                               /* 6406 */
        ret = 1;
        break;

    default:
        printf("Error!! SET_OBJ_ALGORITHM\n");                          /* 6398 */
        ret = 1;                                                        /* 6399 */
        break;
    }

    return ret;                                                         /* 6411 */
}

static int EvSetPlyrSisDistance(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 6425 */
    u_char type;

    type = Get1Byte(ctrl_addr->event_addr + 2);                         /* 6431 */
    ChangeSisTraceDist(type);                                           /* 6435 */

    return 1;                                                           /* 6438 */
}

/* ------------------------------------------------------------------------ *
 *  Event control
 * ------------------------------------------------------------------------ */

/* Forces another event into a given state.  Routed through ev_change.c's
 * request queue rather than applied here, because the target may be running
 * right now -- and tearing down a macro program from inside another one is
 * exactly what CheckEventExeEntry() exists to prevent. */
static int EvEvSetState(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 6455 */
    u_char *dat_addr;
    u_char  state;
    u_short req_id;

    dat_addr = ctrl_addr->event_addr;                                   /* 6458 */

    state  = Get1Byte(dat_addr + 1);                                    /* 6461 */
    req_id = Get2Byte(dat_addr + 2);                                    /* 6464 */

    if (state > 6) {                                                    /* 6468 */
        PRINT_ASSERT("EvEvSetState event id [%d] set_state %d", ctrl_addr->event_id, state);                       /* 6469 */
    }

    if (req_id >= 0x78b) {                                              /* 6471 */
        PRINT_ASSERT("EvEvSetState event id [%d] req_id %d", ctrl_addr->event_id, req_id);                      /* 6472 */
    }

    Req_CompulsionSetEventState(ctrl_addr->event_id, (int)req_id, state); /* 6477 */

    return 1;                                                           /* 6480 */
}

static int EvEvSetTimer(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 6497 */
    u_char *dat_addr;
    u_char  state;
    u_short req_id;
    u_int   timer;

    dat_addr = ctrl_addr->event_addr;                                   /* 6499 */

    state  = Get1Byte(dat_addr + 1);                                    /* 6502 */
    req_id = Get2Byte(dat_addr + 2);                                    /* 6505 */
    timer  = Get4Byte(dat_addr + 4);                                    /* 6508 */

    EvTimerRegist(ctrl_addr->event_id, timer, (int)req_id, state);      /* 6512 */

    return 1;                                                           /* 6515 */
}

/* Wait for a number of frames.  The count lives in ctrl->stop_timer rather
 * than in ctrl->process, so it survives the re-entries; a zero wait is a
 * mistake in the event data and completes immediately with a warning.
 *
 * The countdown is suspended while ev_wrk's wait flag is up -- text on screen
 * should not eat an event's timer. */
static int EvEvStop(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 6524 */
    u_int timer;
    int   ret;

    ret = 0;

    if (ctrl_addr->stop_timer == 0) {                                   /* 6530 */
        timer = Get4Byte(ctrl_addr->event_addr + 4);                    /* 6534 */
        ctrl_addr->stop_timer = timer;                                  /* 6538 */

        if (timer == 0) {                                               /* 6539 */
            PRINT_WARNING("EvEvStop() Wait Time Is ZERO!!\n");          /* 6542 */
            ret = 1;                                                    /* 6543 */
        }
    } else if (GetEvWrkWaitFlg() == 0) {                                /* 6547 */
        ctrl_addr->stop_timer--;                                        /* 6549 */
        ret = (ctrl_addr->stop_timer == 0);                             /* 6552 */
    }

    return ret;                                                         /* 6560 */
}

/* ------------------------------------------------------------------------ *
 *  Chapters
 * ------------------------------------------------------------------------ */

/* Unlike AREA_CHANGE this does not move between rooms -- it re-seeds
 * everything for a fresh chapter, so MapLoadInit() rather than
 * MapLoadMoveRoom(). */
static int EvChapterLoadRequest(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 6575 */
    u_short area;
    int     area_no;

    area    = Get2Byte(ctrl_addr->event_addr + 2);                      /* 6580 */
    area_no = (int)area;

    SetPlyrAreaNo(area_no);                                             /* 6583 */
    SetSisAreaNo(area_no);                                              /* 6584 */

    ev_seChangeRoom(area_no);                                           /* 6586 */
    ev_sisChangeRoom(area_no);                                          /* 6587 */
    ev_eneChangeRoom(area_no);                                          /* 6588 */

    MapLoadInit(area_no);                                               /* 6589 */

    return 1;                                                           /* 6592 */
}

static int EvChangeChapter(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 6606 */
    u_char chapter_label;

    chapter_label = Get1Byte(ctrl_addr->event_addr + 2);                /* 6611 */

    if (chapter_label > 10) {                                           /* 6615 */
        PRINT_ASSERT("Error! EvChangeChapter chapter_label %d", chapter_label); /* 6616 */
    }

    ingame_wrk.mChapterNo = chapter_label;

    return 1;                                                           /* 6624 */
}

static int EvGameDataSave(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 6637 */
    SavePointStartReq();

    return 1;                                                           /* 6640 */
}

/* Which ending plays is decided by the difficulty the run was played on. */
static int EvGameClear(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 6649 */
    if ((ingame_wrk.mDifficulty >= 0) &&
        (ingame_wrk.mDifficulty < 2)) {                          /* 6656 */
        SendIngameEndingNormal(1);                                      /* 6657 */
    } else if ((ingame_wrk.mDifficulty >= 2) &&
               (ingame_wrk.mDifficulty < 4)) {                   /* 6660 */
        SendIngameEndingHard(1);                                        /* 6661 */
    } else {
        PRINT_ASSERT("Error! %s", __FUNCTION__);                       /* 6663 */
    }

    return 1;                                                           /* 6667 */
}

/* Re-arms one side of this event's own condition set.  Re-opening also clears
 * the player's queued movement, so the event does not immediately re-trigger
 * off a stick input that was already in flight. */
static int EvSetConditionCheck(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 6682 */
    u_char *dat_addr;
    u_char  type;
    u_char  flg;

    dat_addr = ctrl_addr->event_addr;                                   /* 6684 */

    type = Get1Byte(dat_addr + 1);                                      /* 6687 */
    flg  = Get1Byte(dat_addr + 2);                                      /* 6690 */

    if (type == 0) {                                                    /* 6694 */
        SetOpenCondSwitch(flg);                                         /* 6695 */
        ClearPlyrMoveStatus();                                          /* 6696 */
    } else {
        SetEndCondSwitch(flg);                                          /* 6700 */
    }

    return 1;                                                           /* 6704 */
}

/* ------------------------------------------------------------------------ *
 *  Guiding butterflies
 *
 *  Positions here are 32-bit, not the 16-bit room-relative values the
 *  character opcodes use.
 * ------------------------------------------------------------------------ */

static int EvSetButterfly(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 6713 */
    u_char *dat_addr;
    u_char  butterfly_id;
    u_char  type;
    float   set_pos[4];

    memset(set_pos, 0, sizeof(set_pos));                                /* 6717 */

    dat_addr = ctrl_addr->event_addr;                                   /* 6721 */

    butterfly_id = Get1Byte(dat_addr + 1);                              /* 6723 */
    type         = Get1Byte(dat_addr + 2);                              /* 6726 */

    set_pos[0] = (float)(int)Get4Byte(dat_addr + 4);                    /* 6728 */
    set_pos[1] = (float)(int)Get4Byte(dat_addr + 8);                    /* 6733 */
    set_pos[2] = (float)(int)Get4Byte(dat_addr + 0xc);                  /* 6736 */
    set_pos[3] = 0.0f;                                                  /* 6741 */

    /* Start point and target are the same vector -- a butterfly set this way
     * appears where it is heading. */
    EffectButterflyReqTarget((int)(char)butterfly_id, (int)(char)type,
                             set_pos, set_pos);                         /* 6744 */

    return 1;                                                           /* 6747 */
}

static int EvMoveButterfly(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 6756 */
    u_char *dat_addr;
    u_char  butterfly_id;
    float   set_pos[4];

    memset(set_pos, 0, sizeof(set_pos));                                /* 6759 */

    dat_addr = ctrl_addr->event_addr;                                   /* 6763 */

    butterfly_id = Get1Byte(dat_addr + 1);                              /* 6765 */

    set_pos[0] = (float)(int)Get4Byte(dat_addr + 4);                    /* 6768 */
    set_pos[1] = (float)(int)Get4Byte(dat_addr + 8);                    /* 6773 */
    set_pos[2] = (float)(int)Get4Byte(dat_addr + 0xc);                  /* 6776 */
    set_pos[3] = 0.0f;                                                  /* 6781 */

    EffectButterflyChangeTarget((int)(char)butterfly_id, set_pos);      /* 6784 */

    return 1;                                                           /* 6787 */
}

static int EvReleaseButterfly(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 6802 */
    u_char butterfly_id;

    butterfly_id = Get1Byte(ctrl_addr->event_addr + 1);                 /* 6807 */
    EffectButterflyFadeOut((int)(char)butterfly_id);                    /* 6810 */

    return 1;                                                           /* 6813 */
}

/* ------------------------------------------------------------------------ *
 *  Full-screen effects
 * ------------------------------------------------------------------------ */

/* Effect 2 is the dither wash.  Its handle is kept in ev_eff_ctrl so
 * EV_EFF_DITHER_END can find it, and its parameters in the save block so a
 * load can bring it back. */
static int EvEffDitherStart(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 6833 */
    u_char *dat_addr;
    u_char  type;
    u_char  speed;
    u_char  alpha;
    u_char  alpha_max;
    u_char  col_max;
    void   *dither_id;

    dat_addr = ctrl_addr->event_addr;                                   /* 6835 */

    type      = Get1Byte(dat_addr + 1);                                 /* 6838 */
    speed     = Get1Byte(dat_addr + 2);                                 /* 6840 */
    alpha     = Get1Byte(dat_addr + 3);                                 /* 6842 */
    alpha_max = Get1Byte(dat_addr + 4);                                 /* 6844 */
    col_max   = Get1Byte(dat_addr + 5);                                 /* 6846 */

    /* alpha and speed landed in double-read variadic slots and stored as
     * exactly 0.0f; see EventMacroLoad_EffDitherInit().  ROM behaviour, kept. */
    dither_id = SetEffects_DITHER(2, type, 0.0f, 0.0f,                  /* 6850 */
                                  alpha_max, col_max, 0, 0, 0);
    SetEvEffDitherID(dither_id);                                        /* 6852 */
    SetEvSaveEffDither(type, speed, alpha, alpha_max, col_max);         /* 6855 */

    return 1;                                                           /* 6858 */
}

static int EvEffDitherEnd(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 6871 */
    void *p;

    p = GetEvEffDitherID();                                             /* 6874 */

    if (p != (void *)0) {                                               /* 6876 */
        ResetEffects(p);                                                /* 6878 */
        DelEvEffDitherID();                                             /* 6881 */
        EvSaveEffDitherInit();
    }

    return 1;                                                           /* 6885 */
}

static int EvEffThunderReq(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 6894 */
    u_char *dat_addr;
    u_short delay_time;
    float   lightning_direction[4];
    float   thunder_position[4];

    memset(lightning_direction, 0, sizeof(lightning_direction));        /* 6897 */
    memset(thunder_position, 0, sizeof(thunder_position));              /* 6898 */

    dat_addr = ctrl_addr->event_addr;                                   /* 6902 */

    delay_time = Get2Byte(dat_addr + 2);                                /* 6904 */

    lightning_direction[0] = (float)(int)Get4Byte(dat_addr + 4);        /* 6907 */
    lightning_direction[1] = (float)(int)Get4Byte(dat_addr + 8);        /* 6911 */
    lightning_direction[2] = (float)(int)Get4Byte(dat_addr + 0xc);      /* 6913 */
    lightning_direction[3] = 0.0f;                                      /* 6916 */

    thunder_position[0] = (float)(int)Get4Byte(dat_addr + 0x10);        /* 6917 */
    thunder_position[1] = (float)(int)Get4Byte(dat_addr + 0x14);        /* 6921 */
    thunder_position[2] = (float)(int)Get4Byte(dat_addr + 0x18);        /* 6924 */
    thunder_position[3] = 0.0f;                                         /* 6929 */

    EffectThunderLightReq(lightning_direction, (int)delay_time,
                          thunder_position);                            /* 6933 */

    return 1;                                                           /* 6936 */
}

static int EvSetScreenEffect(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 6951 */
    u_short eff_number;

    eff_number = Get2Byte(ctrl_addr->event_addr + 2);                   /* 6956 */

    EffectSetScreenEffectNo((int)eff_number);                           /* 6959 */
    SetEvSaveScreenEffect((int)eff_number);                             /* 6962 */

    return 1;                                                           /* 6964 */
}

/* ------------------------------------------------------------------------ *
 *  Synchro mode
 *
 *  The sequence where the player takes Mayu's place: the player model is
 *  replaced with the sister's, the world goes monochrome, and the flashlight
 *  moves from the hand to the step.  Running is locked out for the duration
 *  and the viewfinder filament is hidden, since neither belongs to her.
 *
 *  Both halves run the same lock / swap / wait machine the other model-change
 *  opcodes use, with process 4 as the "swap requested" step.
 * ------------------------------------------------------------------------ */

static int EvSynchroModeStart(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 6976 */
    int ret;

    if (ctrl_addr->process == 0) {                                      /* 6977 */
        m_plyr_camera.filament.DrawLock();                              /* 6978 */

        if ((plyr_wrk.cmn_wrk.st.sta & 0x8000) != 0) {                  /* 6979 */
            IgEffectRenzFlareDispFlgSet(0);                             /* 6981 */
        }

        PlayerDrawLock();                                               /* 6982 */
        PlayerRunLock();                                                /* 6986 */
        MapObjItemOff();                                                /* 6987 */
        ctrl_addr->process = 4;                                         /* 6989 */
    } else if (ctrl_addr->process == 4) {                               /* 6990 */
        SetupPlyrMdl(GetSisterMdlNo(), 5, 0x10, GetSisterAcsNo());      /* 6991 */

        EffWrkMonochroModeSet(1);                                       /* 6993 */
        gra3dMonotoneDrawEnable(1);                                     /* 6994 */
        gra3dPrelight();                                                /* 6995 */

        ctrl_addr->process = 1;                                         /* 7013 */
        EvBankSoundSub(0xc1d, 0, (SND_3D_SET *)0);
    }

    if (ctrl_addr->process == 1) {                                      /* 7016 */
        if (IsReadyPlyrMdl() == 0) {                                    /* 7017 */
            ret = 0;
        } else {
            playerSetFlashlightType(PFT_STEP);                          /* 7018 */
            PlayerDrawUnlock();                                         /* 7019 */
            synchro_mode_flg = 1;                                       /* 7022 */
            ret = 1;                                                    /* 7024 */
        }
    } else {
        ret = 0;
    }

    return ret;                                                         /* 7030 */
}

static int EvSynchroModeEnd(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 7041 */
    int ret;

    if (ctrl_addr->process == 0) {                                      /* 7042 */
        PlayerDrawLock();                                               /* 7043 */
        playerSetFlashlightType(PFT_HAND);                              /* 7045 */
        MapObjItemOn();                                                 /* 7046 */
        ctrl_addr->process = 4;                                         /* 7047 */
    } else if (ctrl_addr->process == 4) {                               /* 7050 */
        /* Back to Mio, with or without the flashlight depending on whether
         * she had it out when synchro mode started. */
        if ((plyr_wrk.cmn_wrk.st.sta & 0x8000) != 0) {                  /* 7051 */
            SetupPlyrMdl(GetPlyrMdlNo(), 1, 0x10, GetPlyrAcsNo());      /* 7053 */
        } else {
            SetupPlyrMdl(GetPlyrMdlNo(), 0, 0x10, GetPlyrAcsNo());      /* 7057 */
        }

        EffWrkMonochroModeSet(0);                                       /* 7058 */
        gra3dMonotoneDrawEnable(0);                                     /* 7059 */
        gra3dPrelight();                                                /* 7063 */

        ctrl_addr->process = 1;                                         /* 7064 */
    }

    if (ctrl_addr->process == 1) {                                      /* 7067 */
        if (IsReadyPlyrMdl() == 0) {                                    /* 7069 */
            ret = 0;
        } else {
            if ((plyr_wrk.cmn_wrk.st.sta & 0x8000) != 0) {              /* 7071 */
                ReqPlayerMim(0x19, 0);                                  /* 7072 */
                IgEffectRenzFlareDispFlgSet(1);                         /* 7075 */
            }

            PlayerRunUnlock();                                          /* 7078 */
            PlayerDrawUnlock();                                         /* 7080 */
            m_plyr_camera.filament.DrawUnlock();
            synchro_mode_flg = 0;                                       /* 7085 */
            ret = 1;
        }
    } else {
        ret = 0;
    }

    return ret;                                                         /* 7086 */
}

/* ------------------------------------------------------------------------ *
 *  Item-name banner, menu locks
 * ------------------------------------------------------------------------ */

static int EvItemNameDispStart(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 7102 */
    u_char *dat_addr;
    u_short msg_type;
    u_short msg_id;
    u_short fade_in_time;

    dat_addr = ctrl_addr->event_addr;                                   /* 7104 */

    msg_type  = Get2Byte(dat_addr + 2);                                 /* 7107 */
    msg_id    = Get2Byte(dat_addr + 4);                                 /* 7109 */
    fade_in_time = Get2Byte(dat_addr + 6);                              /* 7111 */

    if (msg_type > 0x52) {                                              /* 7115 */
        PRINT_ASSERT("Error! EvItemNameDispStart event_id[%d] msg_type %d", ctrl_addr->event_id, msg_type);                    /* 7116 */
    }

    if (msg_id < 0 || (GetMsgIDNumMax((int)msg_type) <= msg_id)) { /* 7118 */
        PRINT_ASSERT("Error! EvItemNameDispStart event_id[%d] msg_id %d", ctrl_addr->event_id, msg_id);                /* 7119 */
    }

    EvItemNameDispStartReq((int)msg_type, (int)msg_id,
                           (int)fade_in_time);                          /* 7124 */

    return 1;                                                           /* 7127 */
}

static int EvItemNameDispEnd(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 7142 */
    u_short fade_out_time;

    fade_out_time = Get2Byte(ctrl_addr->event_addr + 2);                /* 7147 */
    EvItemNameDispEndReq((int)fade_out_time);                           /* 7151 */

    return 1;                                                           /* 7154 */
}

/* All four are counted locks, so overlapping events cannot unlock each
 * other's menu or pause. */
static int EvMenuLock(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 7163 */
    ingame_wrk.MenuLock();                                  /* 7167 */

    return 1;                                                           /* 7168 */
}

static int EvMenuUnlock(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 7177 */
    ingame_wrk.MenuUnlock();                                /* 7181 */

    return 1;                                                           /* 7182 */
}

static int EvPauseLock(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 7191 */
    ingame_wrk.PauseLock();                                 /* 7194 */

    return 1;                                                           /* 7197 */
}

static int EvPauseUnlock(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 7206 */
    ingame_wrk.PauseUnlock();                               /* 7209 */

    return 1;                                                           /* 7212 */
}

/* Note the polarity: operand 0 *locks* the curse, anything else unlocks. */
static int EvSetPhotoCurse(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 7227 */
    u_char sw;

    sw = Get1Byte(ctrl_addr->event_addr + 1);                           /* 7232 */

    if (sw == 0) {                                                      /* 7235 */
        PlayerCurseLock();                                              /* 7236 */
    } else {
        PlayerCurseUnlock();                                            /* 7238 */
    }

    return 1;                                                           /* 7242 */
}

/* Blocks until the named pad action is pressed.  paddat[n] is that action's
 * hold count, so == 1 is the frame it went down. */
static int EvPadWait(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 7258 */
    u_short pad_label;

    pad_label = Get2Byte(ctrl_addr->event_addr + 2);                    /* 7263 */

    return (*paddat[pad_label] == 1);                                   /* 7267 */
}                                                                       /* 7272 */

static int EvMovieRoomRequest(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 7286 */
    /* Refused mid-battle -- the menu would take the player out of a fight
     * they cannot leave. */
    if (IsPlayerInBattle() == 0) {                                      /* 7287 */
        SetIngameMovieRoomMenu(1);
    }

    return 1;                                                           /* 7290 */
}

static int EvGameOverRequest(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 7299 */
    SendIngameGameOver(1);                                              /* 7302 */

    return 1;                                                           /* 7305 */
}

/* ------------------------------------------------------------------------ *
 *  Subtitles
 *
 *  Separate from the message system: subtitles are drawn over the world, and
 *  the 3D forms follow an object or a fixed point rather than sitting at the
 *  bottom of the screen.  None of the three block -- the subtitle runs on its
 *  own clock once requested.
 * ------------------------------------------------------------------------ */

static int EvSubTitleDispReq(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 7320 */
    u_short subtitle_label;

    subtitle_label = Get2Byte(ctrl_addr->event_addr + 2);               /* 7325 */

    if (subtitle_label > 0xf9) {                                        /* 7329 */
        PRINT_ASSERT("Error! %s event_id[%d] subtitle_label [%d]", __FUNCTION__, ctrl_addr->event_id, subtitle_label); /* 7330 */
    }

    SubTitleReq((int)(short int)subtitle_label);                        /* 7336 */

    return 1;                                                           /* 7339 */
}

static int EvSubTitle3DObjDispReq(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 7359 */
    u_char *dat_addr;
    u_char  obj_type;
    u_short subtitle_label;
    u_int   obj_id;

    dat_addr = ctrl_addr->event_addr;                                   /* 7361 */

    obj_type       = Get1Byte(dat_addr + 1);                            /* 7364 */
    subtitle_label = Get2Byte(dat_addr + 2);                            /* 7367 */
    obj_id         = Get4Byte(dat_addr + 4);                            /* 7370 */

    if (subtitle_label > 0xf9) {                                        /* 7374 */
        PRINT_ASSERT("Error! %s event_id[%d] subtitle_label [%d]",
                     __FUNCTION__, ctrl_addr->event_id,
                     subtitle_label);                                   /* 7375 */
    }

    if (obj_type > 10) {                                                /* 7378 */
        PRINT_ASSERT("Error! %s event_id [%d] obj_type %d",
                     __FUNCTION__, ctrl_addr->event_id, obj_type); /* 7379 */
    }

    SubTitleReq3DObj((int)(short int)subtitle_label, (int)(char)obj_type, obj_id); /* 7386 */

    return 1;                                                           /* 7394 */
}

static int EvSubTitle3DPosDispReq(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 7403 */
    u_char *dat_addr;
    u_short subtitle_label;
    float   pos[4];

    memset(pos, 0, sizeof(pos));                                        /* 7406 */

    dat_addr = ctrl_addr->event_addr;                                   /* 7410 */

    subtitle_label = Get2Byte(dat_addr + 2);                            /* 7412 */

    pos[0] = (float)(int)Get4Byte(dat_addr + 4);                        /* 7415 */
    pos[1] = (float)(int)Get4Byte(dat_addr + 8);                        /* 7419 */
    pos[2] = (float)(int)Get4Byte(dat_addr + 0xc);                      /* 7421 */

    if (subtitle_label > 0xf9) {                                        /* 7429 */
        PRINT_ASSERT("Error! %s event_id[%d] subtitle_label [%d]",
                     __FUNCTION__, ctrl_addr->event_id,
                     subtitle_label);                                   /* 7430 */
    }

    SubTitleReq3D((int)(short int)subtitle_label, pos);                 /* 7435 */

    return 1;                                                           /* 7438 */
}

static int EvSubTitleStop(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 7451 */
    SubTitleStop();

    return 1;                                                           /* 7454 */
}

/* ------------------------------------------------------------------------ *
 *  Ghost-sealed doors
 * ------------------------------------------------------------------------ */

static int EvSetGhostSealDoor(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 7469 */
    u_short seal_id;

    seal_id = Get2Byte(ctrl_addr->event_addr + 2);                      /* 7474 */
    GhostSealDoorAppear((int)seal_id);                                  /* 7478 */

    return 1;                                                           /* 7481 */
}

static int EvReleaseGhostSealDoor(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 7496 */
    u_short seal_id = Get2Byte(ctrl_addr->event_addr + 2);      /* 7501 */
    GhostSealDoorRelease((int)seal_id);                                 /* 7505 */

    return 1;                                                           /* 7508 */
}

/* ------------------------------------------------------------------------ *
 *  Mission mode
 *
 *  Both endings route through SendIngameEndingNormal(); clear and failed
 *  differ only in the type MisSetClearType() records.
 * ------------------------------------------------------------------------ */

static int EvMissionStart(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 7517 */
    SetNextGPhase(GID_STORY_MISSION_ST);                                /* 7521 */

    return 1;                                                           /* 7522 */
}

static int EvMissionClear(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 7531 */
    MisSetClearType(1);                                                 /* 7536 */
    MisDispDeleteFlg(3);                                                /* 7538 */
    SendIngameEndingNormal(1);                                       /* 7539 */

    return 1;                                                           /* 7540 */
}

static int EvMissionFailed(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 7549 */
    MisSetClearType(0);                                                 /* 7554 */
    MisDispDeleteFlg(3);                                                /* 7556 */
    SendIngameEndingNormal(1);                                       /* 7557 */

    return 1;                                                           /* 7558 */
}

/* Sets up whichever character the selected chapter starts with.  Blocks on the
 * model load; the third operand is read and unused. */
static int EvChapterSelInit(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 7575 */
    u_char *dat_addr;
    u_short mdl_id;
    u_short mot_id;
    int     ret;

    dat_addr = ctrl_addr->event_addr;                                   /* 7577 */

    mdl_id = Get2Byte(dat_addr + 2);                                    /* 7580 */
    mot_id = Get2Byte(dat_addr + 4);                                    /* 7582 */
    Get2Byte(dat_addr + 6);                                             /* 7584 */

    PRINT_WARNING("mdl_id = %d", mdl_id);                               /* 7586 */

    if (ctrl_addr->process == 0) {                                      /* 7587 */
        SetupPlyrMdl((int)mdl_id, (int)mot_id, 0x10, GetPlyrAcsNo());   /* 7588 */
        ctrl_addr->process = 1;                                         /* 7589 */
    }

    if (ctrl_addr->process == 1) {                                      /* 7593 */
        ret = (IsReadyPlyrMdl() != 0);                                  /* 7595 */
    } else {
        ret = 0;
    }

    return ret;                                                         /* 7602 */
}

/* ------------------------------------------------------------------------ *
 *  Branching
 *
 *  ctrl->if_state carries the whole of the interpreter's branch state, and it
 *  takes exactly four values:
 *
 *      0  not inside an IF at all
 *      1  inside a branch that is being executed
 *      2  inside a branch that is being skipped, none taken yet
 *      3  inside a branch that is being skipped because an earlier one already
 *         ran -- and so must keep being skipped to the ENDIF
 *
 *  The interpreter steps over every non-branch opcode while if_state is 2 or
 *  3.  The difference between the two only matters at ELSE / ELSEIF: a 2 may
 *  still be turned into a 1, a 3 never can.  There is no nesting -- one IF at
 *  a time per event.
 * ------------------------------------------------------------------------ */

static int EvEvIf(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 7616 */
    u_char *dat_addr;
    u_short if_label;

    dat_addr = ctrl_addr->event_addr;                                   /* 7619 */

    if_label = Get2Byte(dat_addr + 2);                                  /* 7622 */

    if (if_label >= IF_COND_MAX) {                                      /* 7626 */
        PRINT_ASSERT("Error! EvEvIf if label %d", if_label);            /* 7627 */
    }

    if (if_cond_wrk[(short int)if_label].ev_if_func != nullptr) /* 7633 */
    {
        if ((*if_cond_wrk[(short int)if_label].ev_if_func)(dat_addr + 4) == 0) { /* 7637 */
            ctrl_addr->if_state = 2;                                    /* 7638 */
        } else
        {
            ctrl_addr->if_state = 1;                                    /* 7641 */
        }
    } else {
        printf("*************************************************************\n"); /* 7646 */
        printf("*     Error!! The value of the event data is illegal!!!     *\n"); /* 7647 */
        printf("*                    IF_M    Event ID %3d                   *\n", ctrl_addr->event_id); /* 7648 */
        printf("*************************************************************\n"); /* 7649 */
        PRINT_ASSERT("");                                               /* 7650 */

    }

    return 1;                                                           /* 7656 */
}

static int EvEvElse(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 7670 */
    /* 1 -> 3 because the IF branch ran, so the ELSE must not; 3 stays 3 for
     * the same reason.  Only a 2 -- nothing taken yet -- becomes a 1. */
    if ((ctrl_addr->if_state == 1) || (ctrl_addr->if_state == 3)) {     /* 7673 */
        ctrl_addr->if_state = 3;
    } else {
        ctrl_addr->if_state = 1;
    }

    return 1;                                                           /* 7681 */
}

static int EvEvElseIf(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 7696 */
    u_char *dat_addr;
    u_short if_label;

    dat_addr = ctrl_addr->event_addr;                                   /* 7699 */

    if_label = Get2Byte(dat_addr + 2);                                  /* 7702 */

    if (if_label >= IF_COND_MAX) {                                      /* 7706 */
        PRINT_ASSERT("Error! EvEvElseIf if label %d", if_label);        /* 7707 */
    }

    if ((ctrl_addr->if_state == 1) || (ctrl_addr->if_state == 3)) {     /* 7713 */
        /* An earlier branch already ran -- the predicate is not even
         * evaluated. */
        ctrl_addr->if_state = 3;                                        /* 7716 */
    } else if (if_cond_wrk[(short int)if_label].ev_if_func != nullptr) {      /* 7720 */
        if ((*if_cond_wrk[(short int)if_label].ev_if_func)(dat_addr + 4) == 0) { /* 7725 */
            ctrl_addr->if_state = 2;                                    /* 7726 */
        } else {
            ctrl_addr->if_state = 1;                                    /* 7729 */
        }

    } else {
        printf("*************************************************************\n"); /* 7734 */
        printf("*     Error!! The value of the event data is illegal!!!     *\n"); /* 7735 */
        printf("*                  ELSEIF_M    Event ID %3d                 *\n", ctrl_addr->event_id); /* 7736 */
        printf("*************************************************************\n"); /* 7737 */
        PRINT_ASSERT("");                                               /* 7738 */
    }

    return 1;                                                           /* 7744 */
}

static int EvEvEndIf(EV_EXE_CTRL *ctrl_addr)
{                                                                       /* 7758 */
    ctrl_addr->if_state = 0;

    return 1;                                                           /* 7760 */
}

/* ------------------------------------------------------------------------ *
 *  IF predicates
 *
 *  Each reads its own operands from the two bytes after the if label onwards,
 *  and answers non-zero for "take the branch".
 * ------------------------------------------------------------------------ */

/* True when the player *cannot* take any more of this item -- ItemGetPossible()
 * answers whether there is room, so the sense is inverted here. */
static u_char EvIfItemMax(u_char *dat_addr)
{                                                                       /* 7818 */
    u_short item_id;
    u_short get_num;

    item_id = Get2Byte(dat_addr);                                       /* 7821 */
    get_num = Get2Byte(dat_addr + 2);                                   /* 7825 */

    if (item_id > 0x39) {                                               /* 7825 */
        PRINT_ASSERT("Error! EvIfItemMax item id %d", item_id);         /* 7826 */
    }

    if (get_num > 99) {                                                 /* 7828 */
        PRINT_ASSERT("Error! EvIfItemMax get num %d", get_num);         /* 7829 */
    }

    return (ItemGetPossible((int)(short int)item_id) == 0);             /* 7835 */
}                                                                       /* 7845 */

/* Reads back the answer the last MSG_CHOICE left in ev_choice_ctrl. */
static u_char EvIfChoiceSelect(u_char *dat_addr)
{                                                                       /* 7860 */
    u_short sel_num;

    sel_num = Get2Byte(dat_addr);                                       /* 7863 */

    return (ev_choice_ctrl.sel_num == sel_num);             /* 7871 */
}

static u_char EvIfRandom(u_char *dat_addr) {                /* 7887 */

    u_short percent = Get2Byte(dat_addr);                                       /* 7889 */

    return ((MioPan_Rand() % 100) < percent);               /* 7897 */
}

static u_char EvIfWithSister(u_char *dat_addr)
{                                                                       /* 7910 */
    return (GetSisJoinFlg() != 0);                                      /* 7918 */
}

static u_char EvIfPlyrFlashLightHave(u_char *dat_addr)
{                                                                       /* 7932 */
    return ((plyr_wrk.cmn_wrk.st.sta & 0x8000) != 0);                   /* 7940 */
}

static u_char EvIfPuzzleClear(u_char *dat_addr)
{                                                                       /* 7955 */
    u_short puzzle_id;

    puzzle_id = Get2Byte(dat_addr);                                     /* 7959 */

    if (puzzle_id > 5) {                                                /* 7959 */
        PRINT_ASSERT("Error! EvIfPuzzleClear puzzle id %d", puzzle_id); /* 7960 */
    }

    return (GetPuzzleClearInfo((int)puzzle_id) != 0);                   /* 7966 */
}                                                                       /* 7974 */

/* comparison_label picks the operator: 0 is >=, 1 is ==, 2 is <=.  Both this
 * and EvIfGameClearNum() below have the same shape. */
static u_char EvIfGameDifficulty(u_char *dat_addr)
{                                                                       /* 7990 */
    u_short difficulty_label;
    u_short comparison_label;
    u_char  res;

    difficulty_label = Get2Byte(dat_addr);                              /* 7993 */
    comparison_label = Get2Byte(dat_addr + 2);                          /* 7997 */

    if (difficulty_label > 3) {                                   /* 7997 */
        PRINT_ASSERT("Error! %s difficulty_label %d", __FUNCTION__, difficulty_label);     /* 7998 */
    }

    if (comparison_label > 2) {                                         /* 8000 */
        PRINT_ASSERT("Error! %s comparison_label %d", __FUNCTION__, comparison_label);           /* 8001 */
    }

    switch (comparison_label) {                                         /* 8007 */
    case 0:
        res = (difficulty_label <= ingame_wrk.mDifficulty); /* 8015 */
        break;

    case 1:
        res = (ingame_wrk.mDifficulty == difficulty_label); /* 8023 */
        break;

    case 2:
        res = (ingame_wrk.mDifficulty <= difficulty_label); /* 8031 */
        break;

    default:
        res = 0;
        break;
    }

    return res;                                                         /* 8037 */
}

static u_char EvIfGameClearNum(u_char *dat_addr)
{                                                                       /* 8053 */
    u_short clear_num;
    u_short comparison_label;
    u_char  res;

    clear_num        = Get2Byte(dat_addr);                              /* 8056 */
    comparison_label = Get2Byte(dat_addr + 2);                          /* 8060 */

    if (clear_num > 99) {                                         /* 8060 */
        PRINT_ASSERT("Error! %s clear_num %d", __FUNCTION__, clear_num);              /* 8061 */
    }

    if (comparison_label > 2) {                                         /* 8063 */
        PRINT_ASSERT("Error! %s comparison_label %d", __FUNCTION__, comparison_label);             /* 8064 */
    }

    switch (comparison_label) {                                         /* 8070 */
    case 0:
        res = (clear_num <= ingame_wrk.mClearCnt);         /* 8078 */
        break;

    case 1:
        res = (ingame_wrk.mClearCnt == clear_num);         /* 8086 */
        break;

    case 2:
        res = (ingame_wrk.mClearCnt <= clear_num);         /* 8094 */
        break;

    default:
        res = 0;
        break;
    }

    return res;                                                         /* 8100 */
}

/* ------------------------------------------------------------------------ *
 *  Bookkeeping
 * ------------------------------------------------------------------------ */

/* First byte of the command *after* the one the cursor is on.  Only
 * EvMoviePlay() uses it, and it discards the answer. */
static int GetNextEventCom(EV_EXE_CTRL *ctrl)
{                                                                       /* 8119 */
    return (int)ctrl->event_addr[ev_exe_wrk[*ctrl->event_addr].ev_dat_size]; /* 8123 */
}                                                                       /* 8126 */

static void Regist_SoundID(int file_label, int pos, int sound_id)
{                                                                       /* 8140 */
    int i;

    for (i = 0; i < EV_SOUND_CTRL_MAX; i++) {
        if (ev_sound_ctrl[i].sound_id == -1) {                          /* 8145 */
            ev_sound_ctrl[i].sound_id   = sound_id;
            ev_sound_ctrl[i].file_label = file_label;
            ev_sound_ctrl[i].pos        = pos;                          /* 8151 */
            break;
        }
    }

    if (i == EV_SOUND_CTRL_MAX) {                                       /* 8156 */
        printf("***************************************************\n"); /* 8158 */
        printf("*     Error!! The sound management is full!!!     *\n"); /* 8159 */
        printf("*          Regist_SoundID()                       *\n"); /* 8160 */
        printf("***************************************************\n"); /* 8161 */
        PRINT_ASSERT("");                                               /* 8162 */
    }
}

/* Not finding the id is only worth a printf: a one-shot sound was never
 * registered in the first place, so this is reached routinely. */
static void Del_SoundID(int sound_id)
{                                                                       /* 8174 */
    int i;

    for (i = 0; i < EV_SOUND_CTRL_MAX; i++) {
        if (ev_sound_ctrl[i].sound_id == sound_id) {                    /* 8179 */
            ev_sound_ctrl[i].sound_id   = -1;
            ev_sound_ctrl[i].file_label = -1;
            ev_sound_ctrl[i].pos        = -1;                           /* 8186 */
            break;
        }
    }

    if (i == EV_SOUND_CTRL_MAX) {                                       /* 8191 */
        printf("NOT FOUND SoundID %d\n", sound_id);                     /* 8192 */
    }
}

/* PORT NOTE: the ROM builds the "no empty slot" diagnostic by feeding each
 * resident stream's file name through CFileName -- SplitPath() into
 * drive/dir/file/ext, then GetFileExt() to get the base name back.  CFileName
 * (and the custom-allocator basic_string it is built on) is not part of the
 * port yet, so the base name is taken directly here.  Same text, no string
 * class. */
static const char *Regist_StreamID_BaseName(const char *path)
{
    const char *slash;
    const char *back;

    if (path == 0) {
        return "";
    }

    slash = strrchr(path, '/');
    back  = strrchr(path, '\\');

    if (back > slash) {
        slash = back;
    }

    return (slash != 0) ? (slash + 1) : path;
}

static void Regist_StreamID(int stream_id, int file_label)
{                                                                       /* 8202 */
    char temp[3000];
    char temp2[3000];
    int  i;

    for (i = 0; i < EV_STREAM_CTRL_MAX; i++) {
        if (ev_stream_ctrl[i].stream_id == -1) {                        /* 8206 */
            ev_stream_ctrl[i].stream_id  = stream_id;
            ev_stream_ctrl[i].file_label = file_label;                  /* 8211 */
            break;
        }
    }

    if (i == EV_STREAM_CTRL_MAX) {                                      /* 8213 */
        /* With only two slots this is worth naming: report which files are
         * holding them rather than just that the table is full. */
        memset(temp, 0, sizeof(temp));                                  /* 8216 */

        for (i = 0; i < EV_STREAM_CTRL_MAX; i++) {                      /* 8217 */
            sprintf(temp2, "\twrk[%d] file[%s] \n", i,
                    Regist_StreamID_BaseName(
                        GetFileName(ev_stream_ctrl[i].file_label)));    /* 8219 */
            strcat(temp, temp2);                                        /* 8223 */
        }

        PRINT_ASSERT("Event Stream No Empty\n%s!!", temp);              /* 8225 */
    }
}

static void Del_StreamID(int stream_id)
{                                                                       /* 8235 */
    int i;

    for (i = 0; i < EV_STREAM_CTRL_MAX; i++) {
        if (ev_stream_ctrl[i].stream_id == stream_id) {                 /* 8240 */
            ev_stream_ctrl[i].stream_id  = -1;
            ev_stream_ctrl[i].file_label = -1;                          /* 8244 */
            break;
        }
    }

    if (i == EV_STREAM_CTRL_MAX) {                                      /* 8249 */
        printf("NOT FOUND STREAM ID %d\n", stream_id);                  /* 8250 */
    }
}

static int IsRegist_StreamID(int file_label)
{                                                                       /* 8260 */
    int i;

    for (i = 0; i < EV_STREAM_CTRL_MAX; i++) {                          /* 8261 */
        if (ev_stream_ctrl[i].file_label == file_label) {
            return 1;                                                   /* 8266 */
        }
    }

    return 0;                                                           /* 8267 */
}                                                                       /* 8268 */

/* Driven by ingame.c when the menu opens and closes.  Only sounds this file
 * started are paused -- map and system sounds are somebody else's problem. */
void EvSoundPause(void)
{                                                                       /* 8273 */
    int i;

    for (i = 0; i < EV_SOUND_CTRL_MAX; i++) {
        if (ev_sound_ctrl[i].sound_id != -1) {                          /* 8276 */
            SndBufPause(ev_sound_ctrl[i].sound_id);
        }
    }
}                                                                       /* 8280 */

void EvSoundRestart(void)
{                                                                       /* 8286 */
    for (int i = 0; i < EV_SOUND_CTRL_MAX; i++) {
        if (ev_sound_ctrl[i].sound_id != -1) {                          /* 8289 */
            SndBufRestart(ev_sound_ctrl[i].sound_id);
        }
    }
}                                                                       /* 8293 */

/* ------------------------------------------------------------------------ *
 *  Event ghost registry
 *
 *  Public: the enemy code calls these as ghosts come and go, and the open /
 *  close condition evaluator asks GetEvGhostExist() whether a given ghost is
 *  still in the world.
 * ------------------------------------------------------------------------ */

void Set_EvGhostID(u_char ghost_type, int ghost_label, int wrk_id)
{                                                                       /* 8306 */
    for (int i = 0; i < EV_GHOST_CTRL_MAX; i++) {
        if (ev_ghost_ctrl[i].ghost_label == -1) {                       /* 8311 */
            ev_ghost_ctrl[i].ghost_label = ghost_label;
            ev_ghost_ctrl[i].ghost_type  = ghost_type;
            ev_ghost_ctrl[i].wrk_id      = wrk_id;
            return;                                                     /* 8316 */
        }
    }
}

/* Clears every matching entry rather than stopping at the first -- the same
 * type/label pair should be unique, and duplicates would otherwise linger. */
void Del_EvGhostID(u_char ghost_type, int ghost_label)
{                                                                       /* 8330 */
    for (int i = 0; i < EV_GHOST_CTRL_MAX; i++) {
        if ((ev_ghost_ctrl[i].ghost_type == ghost_type) &&
            (ev_ghost_ctrl[i].ghost_label == ghost_label)) {            /* 8335 */
            ev_ghost_ctrl[i].ghost_label = -1;
            ev_ghost_ctrl[i].ghost_type  = 0xff;
            ev_ghost_ctrl[i].wrk_id      = -1;                          /* 8342 */
        }
    }
}

int GetEvGhostExist(u_char ghost_type, int ghost_label)
{                                                                       /* 8355 */
    int exist = 0;

    for (int i = 0; i < EV_GHOST_CTRL_MAX; i++) {                           /* 8360 */
        if ((ev_ghost_ctrl[i].ghost_type == ghost_type) &&
            (ev_ghost_ctrl[i].ghost_label == ghost_label)) {            /* 8363 */
            exist = 1;                                                  /* 8368 */
        }
    }

    return exist;                                                       /* 8371 */
}

char GetSynchroModeFlg(void)
{
    return synchro_mode_flg;                                            /* 8382 */
}

static void SetEvEffDitherID(void *dither_id)
{
    ev_eff_ctrl.dither_id = dither_id;                                  /* 8396 */
}

static void DelEvEffDitherID(void)
{
    ev_eff_ctrl.dither_id = nullptr;                                  /* 8407 */
}

static void *GetEvEffDitherID(void)
{
    return ev_eff_ctrl.dither_id;                                       /* 8419 */
}

/* Called from EventEnd().  Fades rather than cuts, and clears the tracking
 * entries so the next event starts from an empty table. */
void EventEnd_StreamRelease(void)
{                                                                       /* 8429 */
    for (int i = 0; i < EV_STREAM_CTRL_MAX; i++) {
        if (ev_stream_ctrl[i].stream_id != -1) {                        /* 8433 */
            StreamAutoFadeOut(ev_stream_ctrl[i].stream_id, 0);
            ev_stream_ctrl[i].stream_id  = -1;
            ev_stream_ctrl[i].file_label = -1;
        }
    }
}                                                                       /* 8441 */

/* ------------------------------------------------------------------------ *
 *  Save blocks
 *
 *  Note what the three SetSave_* hooks below actually describe: one *entry*,
 *  not the whole two-entry array.  The ROM hard-codes sizeof(EV_SAVE_STREAM),
 *  sizeof(EV_SAVE_OBJ_STREAM) and sizeof(EV_SAVE_POS_STREAM) rather than the
 *  array sizes, so only slot 0 of each is ever written to the memory card and
 *  a second concurrent stream does not survive a save.  Kept faithful --
 *  widening these would change the save-block layout.
 *
 *  None of the three structures contains a pointer, so host sizeof matches the
 *  ROM's literal and the usual 64-bit widening problem does not arise here.
 * ------------------------------------------------------------------------ */

/* Fills the first free slot -- and keeps going, so a second free slot gets
 * written too.  The loop has no break in the ROM. */
static void SetEvSaveStream(int stream_id, int file_label, int volume)
{                                                                       /* 8453 */
    for (int i = 0; i < EV_SAVE_STREAM_MAX; i++) {
        if (ev_save_stream[i].set_flg == 0) {                           /* 8458 */
            ev_save_stream[i].set_flg    = 1;
            ev_save_stream[i].stream_id  = stream_id;
            ev_save_stream[i].file_label = file_label;
            ev_save_stream[i].volume     = volume;
        }
    }
}                                                                       /* 8466 */

static void DelEvSaveStream(int stream_id)
{                                                                       /* 8474 */
    for (int i = 0; i < EV_SAVE_STREAM_MAX; i++) {
        if (ev_save_stream[i].stream_id == stream_id) {                 /* 8479 */
            ev_save_stream[i].set_flg   = 0;
            ev_save_stream[i].stream_id = -1;
        }
    }
}                                                                       /* 8484 */

void SetSave_EvSaveStream(MC_SAVE_DATA *data)
{                                                                       /* 8496 */
    /// This is bugged! The original code did that but instead only saves 1 value
    data->size = sizeof(ev_save_stream[0]);
    data->addr = (u_char *)&ev_save_stream[0];                          /* 8497 */
}

static void SetEvSaveObjStream(int stream_id, int obj_type, int obj_id,
                               int file_label, int volume)
{                                                                       /* 8505 */
    for (int i = 0; i < EV_SAVE_STREAM_MAX; i++) {
        if (ev_save_obj_stream[i].set_flg == 0) {                       /* 8510 */
            ev_save_obj_stream[i].set_flg    = 1;
            ev_save_obj_stream[i].obj_type   = obj_type;
            ev_save_obj_stream[i].obj_id     = obj_id;
            ev_save_obj_stream[i].stream_id  = stream_id;
            ev_save_obj_stream[i].file_label = file_label;
            ev_save_obj_stream[i].volume     = volume;
        }
    }
}                                                                       /* 8520 */

static void DelEvSaveObjStream(int stream_id)
{                                                                       /* 8528 */
    for (int i = 0; i < EV_SAVE_STREAM_MAX; i++) {
        if (ev_save_obj_stream[i].stream_id == stream_id) {             /* 8533 */
            ev_save_obj_stream[i].set_flg   = 0;
            ev_save_obj_stream[i].stream_id = -1;
        }
    }
}                                                                       /* 8538 */

void SetSave_EvSaveObjStream(MC_SAVE_DATA *data)
{                                                                       /* 8550 */
    data->size = sizeof(ev_save_obj_stream[0]);
    data->addr = (u_char *)&ev_save_obj_stream[0];                      /* 8551 */
}

static void SetEvSavePosStream(int stream_id, float *pos, int file_label, int volume)
{                                                                       /* 8559 */
    for (int i = 0; i < EV_SAVE_STREAM_MAX; i++) {
        if (ev_save_pos_stream[i].set_flg == 0) {                       /* 8564 */
            ev_save_pos_stream[i].set_flg    = 1;
            ev_save_pos_stream[i].stream_id  = stream_id;
            ev_save_pos_stream[i].file_label = file_label;
            ev_save_pos_stream[i].volume     = volume;

            ev_save_pos_stream[i].pos[0] = pos[0];
            ev_save_pos_stream[i].pos[1] = pos[1];
            ev_save_pos_stream[i].pos[2] = pos[2];
            ev_save_pos_stream[i].pos[3] = pos[3];
        }
    }
}                                                                       /* 8573 */

static void DelEvSavePosStream(int stream_id)
{                                                                       /* 8581 */
    for (int i = 0; i < EV_SAVE_STREAM_MAX; i++) {
        if (ev_save_pos_stream[i].stream_id == stream_id) {             /* 8586 */
            ev_save_pos_stream[i].set_flg   = 0;
            ev_save_pos_stream[i].stream_id = -1;
        }
    }
}                                                                       /* 8591 */

void SetSave_EvSavePosStream(MC_SAVE_DATA *data)
{                                                                       /* 8603 */
    data->size = sizeof(ev_save_pos_stream[0]);
    data->addr = (u_char *)&ev_save_pos_stream[0];                      /* 8604 */
}

static void SetEvSaveEffDither(u_char type, u_char speed, u_char alpha,
                               u_char alpha_max, u_char col_max)
{                                                                       /* 8615 */
    ev_save_eff_dither.set_flg   = 1;                                   /* 8616 */
    ev_save_eff_dither.type      = type;                                /* 8617 */
    ev_save_eff_dither.speed     = speed;                               /* 8618 */
    ev_save_eff_dither.alpha     = alpha;                               /* 8619 */
    ev_save_eff_dither.alpha_max = alpha_max;
    ev_save_eff_dither.col_max   = col_max;                             /* 8620 */
}

void SetSave_EvSaveEffDither(MC_SAVE_DATA *data)
{                                                                       /* 8632 */
    data->addr = &ev_save_eff_dither.set_flg;
    data->size = sizeof(ev_save_eff_dither);                            /* 8633 */
}

static void SetEvSaveScreenEffect(int eff_number)
{
    ev_save_screen_effect.eff_number = eff_number;                      /* 8645 */
}

void SetSave_EvSaveScreenEffect(MC_SAVE_DATA *data)
{                                                                       /* 8657 */
    data->addr = (u_char *)&ev_save_screen_effect;
    data->size = sizeof(ev_save_screen_effect);                         /* 8658 */
}

/* Debug dump of the ghost registry.  ghost_type indexes the name table
 * directly and is not range-checked -- a stale 0xff would read past it. */
void EvDbg_EventGhostPrint(void)
{                                                                       /* 8670 */
    const char *ghost_type[4];

    ghost_type[0] = "JIBAKU";                                           /* 8673 */
    ghost_type[1] = "FUYU";
    ghost_type[2] = "AUTO";
    ghost_type[3] = "FLY_MOVE";

    printf("**************************************************\n");     /* 8683 */
    printf("*               Event Ghost Status               *\n");     /* 8684 */
    printf("**************************************************\n");     /* 8685 */

    for (int i = 0; i < EV_GHOST_CTRL_MAX; i++) {                           /* 8686 */
        if (ev_ghost_ctrl[i].ghost_label != -1) {
            printf("Ghost Type [%s], Ghost ID %d\n", ghost_type[ev_ghost_ctrl[i].ghost_type], ev_ghost_ctrl[i].ghost_label);
        }
    }
}                                                                       /* 8690 */
