/* ==========================================================================
 *  ingame/plyr/player.h
 *
 *  Player module (player.c) cross-module interface.  player.c is a large game
 *  module that is NOT yet reconstructed; only the entry points other
 *  reconstructed files link against are declared here.  In particular the
 *  message system drives the player's idle animation while text is on screen:
 *  ClearPlyrMoveStatus() halts movement input, SetPlyrAnime() forces an
 *  animation clip/frame.
 *
 *  Bodies live in the stub player.c until the module is reconstructed; expand
 *  this header as more of player.c comes online.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_PLYR_PLAYER_H
#define _INGAME_PLYR_PLAYER_H

#include "eetypes.h"
#include "../../common/variable.h"
#include "plyr_mdl.h"

/* The shot-scoring chain hands these around by pointer.  ENE_WRK comes from
 * enemy.h and BONUS_SHOT_SCORE from m_plyr_camera.h; forward declarations
 * keep this header out of both dependency chains. */
struct ENE_WRK;
struct BONUS_SHOT_SCORE;
struct SUBJECT_WRK;
struct HINT_PHOTO_REQ;

enum PLAYERFLASHLIGHTTYPE
{
    PFT_HAND = 0,
    PFT_STEP = 1,
    NUM_PLAYERFLASHLIGHTTYPE = 2
};

/* Area / room the player is currently in. */
int  GetPlyrAreaNo(void);
int  GetPlyrOldAreaNo(void);
int  GetPlyrRoomID(void);
void SetPlyrAreaNo(int area_no);
int  GetPlyrFloor(void);
int  GetPlyrEquipmentFilmType(void);
float PlayerGetNowHPPercentage(void);
float PlayerGetNowMPPercentage(void);
int  InFinderMode(void);
/* Non-zero while the player is in one of the damage-reaction modes; the
 * PUSH_PAD event condition suppresses input during those. */
int  InDamageState(void);
int  IsPlayerInBattle(void);
int  PlayerGameOver(void);
int  PlyrDamageCtrl(void);
int  PlayerModeIsFinder(void);
int  PlyrOutsideCheck(void);
void playerSetFlashlightType(PLAYERFLASHLIGHTTYPE pft);
PLAYERFLASHLIGHTTYPE playerGetFlashlightType(void);

/* Message-driven idle-animation hooks. */
void ClearPlyrMoveStatus(void);
void SetPlyrAnime(u_char anime_no, u_char frame);
void SetPlayerFloor(int floor);
void PlayerMainCmn(int mode);
void PlyrNormalCtrl(void);
void PlyrMotionMovement(void);
void PlyrSEStop(void);
void ReleasePlayer(void);
void InitPlayer(void);
void PlyrPosSet(MOVE_BOX *mb, float *tv);
void PlyrDebug(void);
/* Clears the per-frame enemy contact bits before the movement/damage passes
 * re-raise them. */
void ClrEneSta(void);

/* ---- position / rotation ---- */
void GetPlayerPos(float *pos);
/* Viewfinder reticle position, in screen pixels. */
void GetPlayerFinderPos(float *fx, float *fy);
void SetPlayerPos(float *pos);
void SetPlayerRot(float *rot);
void SetPlayerHitRadius(float fLen);

/* ---- mode / doors / stairs ---- */
/* Every mode change goes through here: it traces the transition and runs the
 * finder teardown when the new mode leaves finder mode. */
void PlayerChangeMode(int iMode);
void ReqPlyrDead(int mode);
void PlyrRoomCheck(void);
void PlyrCondCheck(void);
void MovePlyrStairs(void);
void PlyrMepachiCtrl(void);
/* Distance to the nearest live ghost; 99999.0 when there is none. */
float GetNearestDistFromPlyrToEnemy(void);
/* phase 0 opens the door, phase 1 walks the player through it. */
void ReqPlyrDoorMotion(int door_type, int phase);
int  PlyrOpenDoor(void);
int  DoorMotionIsEnd(void);
void ClearPlyrDoorFlg(void);
u_char GetPlyrDoorFlg(void);
int  GetPlyrDoorMoveFlg(void);
int  CheckPlyrAnimeEnd(int anime_no);

/* ---- HP / SP damage requests.  Nothing subtracts stamina directly: these
 *      queue an amount that PlyrHPdownCtrl() / PlyrSPdownCtrl() drain over
 *      the following frames.  The *P variants take a percentage of max. ---- */
void ReqPlyrHPSPdownP(PLCMN_WRK *cmn, u_short per);
void ReqPlyrHPdownP(PLCMN_WRK *cmn, u_short per);
void ReqPlyrSPdownP(PLCMN_WRK *cmn, u_short per);
void ReqPlyrHPSPdown(PLCMN_WRK *cmn, u_short deg);
void ReqPlyrHPdown(PLCMN_WRK *cmn, u_short deg);
void ReqPlyrSPdown(PLCMN_WRK *cmn, u_short deg);
void PlyrSPdownCtrl(PLCMN_WRK *cmn);
void PlyrHPdownCtrl(PLCMN_WRK *cmn);
void RTSpiritsDownModeOn(int time);
void RTSpiritsDownModeOff(void);
/* Non-zero once the whole dodge window has elapsed. */
u_char PlyrAvoidCheck(void);
/* Non-zero on each full-deflection stick direction change -- how the player
 * shakes free of a grab. */
u_char LeverGachaChk(void);

/* ---- finder mode.  0 -> 5 (raising) -> 6 (in finder) -> 7 (lowering) -> 0;
 *      QEnd is the abrupt exit that skips mode 7. ---- */
void SetPlyrFinderIn(void);
void SetPlyrFinder(void);
void SetPlyrFinderEnd(void);
void SetPlyrFinderEnd1(void);
void SetPlyrFinderEnd2(void);
void SetPlyrFinderQEnd(void);
void PlyrFinderIn(void);
void PlyrFinderCtrl(void);
void PlyrFinderEnd(void);

/* Which ghosts are legal camera targets this frame, and where they sit in the
 * viewfinder.  Writes ENE_WRK::st.sta bits 0x40 / 0x80 / 0x200 / 0x400 and the
 * screen position in ENE_WRK::fp. */
void EneFrameHitChk(void);
/* Reads the shutter buttons; non-zero when a picture was taken. */
int  PlyrPhotoChk2(void);
/* Non-zero when something photographable is in frame.  Referenced only from a
 * ROM data table -- kept because it is a player.o export. */
int  ChkPhotoAble(void);

/* ---- shot scoring.  PlyrPhotoChk2Sub() drives the whole chain once per
 *      shutter press; bWithLenz distinguishes the burst-shot button from the
 *      plain shutter throughout. ---- */
/* Total damage the shot does, summed over every ghost it caught.  The damage
 * is written to each ghost's st.dmg for enemy.c to apply later in the frame. */
int PhotoDmgChk2(int bWithLenz);
/* One ghost's share of it.  *pbParticleFlg comes back non-zero when the spirit
 * particles should fly home to the equip tray rather than disperse. */
int PhotoDmgChkSub2(ENE_WRK *ew, int bWithLenz, int *pbParticleFlg);
/* One ghost's base points, accumulating the five per-ghost bonuses into
 * *bonus and filling in its album subject entry. */
int PhotoPointCulcEne2(ENE_WRK *ew, BONUS_SHOT_SCORE *bonus,
                       SUBJECT_WRK *subjects, HINT_PHOTO_REQ *hint_picture,
                       int bWithLenz);

/* Is the world point tv inside the viewfinder frame?  Projects it with
 * GetCamI2DPos() -- tx / ty come back set either way -- and tests the result
 * against the rectangle plyr_wrk.fp / photo_frame_tbl describe. */
int FrameInsideChk(float *tv, float *tx, float *ty);

/* ---- lock counters.  All counted, so nested holders are safe. ---- */
int  IsFinderLocked(void);
/* Returns -1 when taken while the finder was already up, so the caller knows
 * it still has to force the finder down. */
int  PlayerFinderLock(void);
void PlayerFinderUnlock(void);
void PlayerShutterLock(void);
void PlayerShutterUnlock(void);
void PlayerMoveLock(void);
void PlayerMoveUnlock(void);
void PlayerActionLock(void);
void PlayerActionUnlock(void);
void PlayerRunLock(void);
void PlayerRunUnlock(void);
void PlayerCurseLock(void);
void PlayerCurseUnlock(void);
void PlayerLock(void);
void PlayerUnlock(void);

void SetSave_PlyrWrk(MC_SAVE_DATA *data);

/* Status blocks the HP/SP drain runs over: [0] player, [1] sister.  Static
 * data in the ROM (&plyr_wrk / &sis_wrk); never reassigned at runtime.
 * PlyrHPdownCtrl() tells the two apart by comparing against pl_sta[1]. */
extern PLCMN_WRK *pl_sta[2];

/* ---- AWAITING RECONSTRUCTION: real functions in player.o, stubbed in
 *      player.c until the rest of the module is reversed. ---- */
/* Advances the death state machine (plyr_wrk.modedead).  Non-zero on the
 * frame the sequence finishes and the game-over screen takes over. */
int  PlyrDead(void);

/* ---- aim quality, 0..100.  CulcEP() scores yaw and pitch, CulcEP2() yaw
 *      alone, CulcEP3() yaw with a distance falloff. ---- */
float CulcEP(float *v0, float *v1);
float CulcEP2(float *v0, float *v1);
float CulcEP3(float *v1);

/* Non-zero when `pos` is inside both the sight cone and the viewfinder frame.
 * `p_dist` is the projected point's screen distance from the finder centre --
 * meaningful only on a hit, see the note on the body. */
int InFinderFrameSub(float *pos, float *p_dist, float sight_range,
                     float limit_dist);
/* Aim score for the nearest visible ghost, 0 when none is in range. */
float GetEnePowerDegree(void);
void  FModeScreenEffect(void);

/* ---- enemy proximity / camera tracing ---- */
void PlyrBattleCheck(void);
/* Closest ordinary (type < 2) ghost.  NearAllEneInfo() is the variant that
 * counts every class. */
void NearEneInfo(PLCMN_WRK *cmn);
/* Turns the player towards `ew`, or towards the nearest ghost when NULL. */
void ReqCamTraceNearEne(void *ew);
void PlyrCamTurnChk(void);

/* ---- flashlight ---- */
/* move_sw non-zero lets the right stick re-aim the beam this frame. */
void PlyrFlashlight(int move_sw);
void PlyrSpotMoveCtrl(void);

/* ---- controller vibration.  Two channels, each with its own countdown, so
 *      a one-shot request survives the frames after it was made. ---- */
void PlyrVibCheck(void);
void PlyrVibCtrl(u_char time);
void PlyrVibCtrlBig(u_char pow, u_char time);

/* ---- movement / animation (reconstructed) ---- */
void  PlyrNModeCtrl(void);
void  PlyrFModeMoveCtrl(void);
float PlyrMovePad(void);
void  PlyrMoveChk(MOVE_BOX *mb, float *tv, float rot);
void  PlyrMoveChkV(MOVE_BOX *mb, float *tv, float mrot);
void  PlyrMovePadFind(MOVE_BOX *mb, float *tv);
/* Stick heading in radians; 10.0 means centred. */
float GetMovePad(u_char id);
u_char PlyrMoveStaChk(float pad_chk);
u_int PlyrLeverInputChk(void);
void  GetMoveSpeed(float *tv);
void  PlyrHeightCtrl(float *tv);
void  PadInfoTmpSave(u_char *dir_save, u_char dir_now, float *rot_save, float rot_now);
void  CngPlyrRotRapid(MOVE_BOX *mb, float rot0);
void  PlyrNAnimeCtrl(void);
void  PlyrDWalkTmCtrl(PLCMN_WRK *cmn);
void  NearAllEneInfo(PLCMN_WRK *cmn);
/* Non-zero when the look-at system may drive the neck for this clip; the
 * 0x32..0x3b range animates the neck itself. */
int   CheckPlayerNeckSW(u_char anime_no);

/* Midpoint of t1 and t2 (w forced to 1).  The talk camera frames the point
 * halfway between the two characters' heads with it. */
void GetCenterPoint(float *center, float *t1, float *t2);


#endif /* _INGAME_PLYR_PLAYER_H */
