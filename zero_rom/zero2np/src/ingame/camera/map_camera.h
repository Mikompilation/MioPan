/* ==========================================================================
 *  ingame/camera/map_camera.h
 *
 *  Story-mode camera controller.  CameraMain() is the single per-frame entry
 *  point IngameCameraMain() calls; everything else here is a request the rest
 *  of the game raises against it (approach / damage / talk / death framing,
 *  the earthquake shake, and the "fin" camera the event system pins).
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), map_camera.o
 *  0x001d9260..0x001db7d7.
 * ======================================================================== */

#ifndef _INGAME_CAMERA_MAP_CAMERA_H
#define _INGAME_CAMERA_MAP_CAMERA_H

#include "eetypes.h"

struct ENE_WRK;

/* app_camera.mode.  Anything other than NORMAL routes CameraMain() through
 * PlyrApproachCameraCtrl() instead of the rectangle/special map cameras.
 * Only NORMAL and APPROACH are ever stored -- the FINDER states are selected
 * from plyr_wrk.cmn_wrk.mode instead. */
enum MAP_CAM_MODE
{
    MAP_CAM_MODE_NORMAL    = 0,
    MAP_CAM_MODE_FINDER_IN = 1,
    MAP_CAM_MODE_FINDER    = 2,
    MAP_CAM_MODE_APPROACH  = 3
};

/* app_camera.flow -- the approach camera's state machine.  The three groups
 * (0..3 approach/damage, 10..11 talk, 20..21 death) are entered by the
 * matching ReqPlyr*CameraCtrl() request. */
enum APPROACH_CAMERA_FLOW
{
    APPROACH_CAMERA_FLOW_INIT      = 0,
    APPROACH_CAMERA_FLOW_MOVE      = 1,
    APPROACH_CAMERA_FLOW_KEEP      = 2,
    APPROACH_CAMERA_FLOW_SHOULDER  = 3,
    APPROACH_CAMERA_FLOW_TALK_INIT = 10,
    APPROACH_CAMERA_FLOW_TALK_KEEP = 11,
    APPROACH_CAMERA_FLOW_DEAD_INIT = 20,
    APPROACH_CAMERA_FLOW_DEAD_KEEP = 21
};

/* Which over-the-shoulder offset the damage camera uses.  Picked from the
 * attacking ghost's model number, because the twins and the oni children are
 * much shorter than the adult ghosts. */
enum PLYR_SHOULDER_CAMERA_TYPE
{
    PLYR_SHOULDER_CAMERA_TYPE_DEFAULT   = 0,
    PLYR_SHOULDER_CAMERA_TYPE_TWINS     = 1,
    PLYR_SHOULDER_CAMERA_TYPE_ONIKODOMO = 2
};

/* ---- frame ------------------------------------------------------------ */
void CameraMain(void);
void CameraMainInit(void);

/* ---- event camera hand-off -------------------------------------------- */
void MapCamSetEventCameraFlg(int Flg);
int  MapCamGetEventCameraFlg(void);

/* Which unit the map cameras follow: 0 = player, otherwise the sister. */
void MapCamTargetChange(int no);
/* Fills pos with the followed unit's position.  Always returns 0; obj_id is
 * accepted for symmetry with the event camera's object lookup but ignored. */
int  MapCamGetObjPosition(float *pos, int obj_id);

/* ---- room-offset aware camera setters --------------------------------- */
/* Map data stores camera positions relative to the room origin, so every
 * placement goes through these to add MapLoadGetOffsetVector(). */
void MapCamGra3dcamSetPositionAddOffset(const float *Vector, int RoomNo);
void MapCamGra3dcamSetTargetAddOffset(const float *Vector, int RoomNo);
/* Follow with slack: the camera only moves once it is further than margin
 * from the requested point, and then only far enough to close the gap. */
void MapCamGra3dcamSetPositionMargin(const float *Position, const float *NowPosition, float margin);
void MapCamGra3dcamSetTargetMargin(const float *Target, const float *NowTarget, float margin);

/* ---- "fin" camera ------------------------------------------------------ */
/* Pins the camera at rvPosition looking at *pvTarget.  The target pointer is
 * retained, so CameraMain() keeps re-aiming at it every frame until
 * MapCamCutFinCamera(). */
void MapCamSetFinCamera(const float (&rvPosition)[3], const float (*pvTarget)[3]);
void MapCamCutFinCamera(void);

/* ---- approach camera requests ------------------------------------------ */
void ReqPlyrApproachCameraCtrl(float *ipos, float offy, float dist);
void ReqPlyrDamageCameraCtrl(float *ipos, float offy, float dist, ENE_WRK *pEneWrk);
void ReqPlyrDeadCameraCtrl(float *ipos, float offy, float offy2, float dist, float *trot);
void ReqPlyrTalkCameraCtrl(float offy, float dist);
void EndPlyrApproachCameraCtrl(void);

void ApproachCameraCrossFadeSW(int sw);
int  GetApproachCameraCrossFade(void);

/* Runs the finder-entry screen overlap for `time` frames. */
void ReqFinderInOverRap(u_short time);

/* ---- earthquake shake --------------------------------------------------- */
void QuakeCameraStop(void);
void QuakeCameraReq(float Power, u_int Time, u_int LoopNum);
int  QuakeCameraGetReq(void);

#endif /* _INGAME_CAMERA_MAP_CAMERA_H */
