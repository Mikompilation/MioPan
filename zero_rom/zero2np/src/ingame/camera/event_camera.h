/* ==========================================================================
 *  ingame/camera/event_camera.h
 *
 *  Scripted event camera.  The SET_EV_CAM_* macro opcodes in
 *  ingame/event/prg/ev_macro.c drive this; MapCamNormalCameraCtrl() calls
 *  EventCameraMain() every frame while the event-camera flag is up.
 *
 *  Two modes share the controller:
 *
 *    manual  - the macro sets position / target / fov / roll directly (or
 *              binds either end to an object id) and EventCameraMain()
 *              re-applies them each frame until EventCameraCut().
 *    VCI     - EventCameraVCIReq(no) points the controller at one camera
 *              path out of the VCI pak, and EventCameraVCIPlay() walks it
 *              a frame at a time with a trapezoidal speed profile.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), event_camera.o
 *  0x0018c660..0x0018e5ab.
 * ======================================================================== */

#ifndef _INGAME_CAMERA_EVENT_CAMERA_H
#define _INGAME_CAMERA_EVENT_CAMERA_H

#include "eetypes.h"

/* --------------------------------------------------------------------------
 *  VCI camera path file.
 *
 *  One header followed by DataNum points.  Both are quadword-aligned, so the
 *  first point sits at pHead[1] and the whole file can be walked with plain
 *  array arithmetic.
 *
 *  Frame is the length of the move in frames.  AccelTime / EqualTime /
 *  BrakeTime are *ratios*, not frame counts: the three are summed and each is
 *  scaled by Frame to get the real phase lengths, which is what lets the same
 *  path be retimed by editing Frame alone.
 * ------------------------------------------------------------------------ */
typedef struct                          /* 0x10 */
{
    /* 0x0 */ char     FileId[4];       /* "EVEC"                            */
    /* 0x4 */ u_short  Frame;           /* total length of the move          */
    /* 0x6 */ u_short  DataNum;         /* number of VCI_CAMERA_POINT after  */
    /* 0x8 */ u_short  HokanType;       /* 0 linear, 1 Bezier, 2 Hermite     */
    /* 0xa */ u_short  AccelTime;       /* speed-profile ramp-up ratio       */
    /* 0xc */ u_short  EqualTime;       /* speed-profile constant ratio      */
    /* 0xe */ u_short  BrakeTime;       /* speed-profile ramp-down ratio     */
} VCI_CAMERA_HEADER;

typedef struct                          /* 0x30 */
{
    /* 0x00 */ float  LocalPosition[4]; /* eye, room-local                   */
    /* 0x10 */ float  LocalTarget[4];   /* look-at, room-local               */
    /* 0x20 */ float  Roll;
    /* 0x24 */ float  Proj;             /* field of view, radians            */

    /* PORT: on the EE a float[4] member carries quadword alignment, which
     * padded this struct out to 0x30.  The host gives float[4] only 4-byte
     * alignment, so without this the struct is 0x28 and every pEveCam[i]
     * past the first reads the wrong point.  The stride is spelled out
     * rather than relying on an alignment attribute. */
    /* 0x28 */ u_char _pad028[8];
} VCI_CAMERA_POINT;

/* Which of the two point streams an interpolation call is working on.  The
 * eye and the look-at are advanced independently, each against its own
 * accumulated arc length. */
enum EVENT_CAMERA_POINT_TYPE
{
    EVECAM_POINT_POSITION = 0,
    EVECAM_POINT_TARGET   = 1
};


/* ---- per-frame driver -------------------------------------------------- */

/* Called from MapCamNormalCameraCtrl() while the event camera flag is up.
 * Always returns 0 in this build -- the scripted move is ended by
 * EventCameraCut(), not by the driver deciding it is finished. */
int  EventCameraMain(void);

/* Resets the controller so the next EventCameraReq() starts clean.  Called by
 * MapCamSetEventCameraFlg(0) as well as by the macro. */
void EventCameraInitCtrlReq(void);

/* Reset the controller and raise the event-camera flag. */
void EventCameraReq(void);

/* Lower the flag (which resets the controller via MapCamSetEventCameraFlg). */
void EventCameraCut(void);


/* ---- manual mode ------------------------------------------------------- */

/* Position/Target are room-local unless SetWorldFlg(1) was called, in which
 * case they go straight to gra3dcam without the room offset. */
void EventCameraSetPosition(const float *Position);
void EventCameraSetTarget(const float *Target);

/* Bind either end of the camera to a live object instead of a fixed point.
 * Type is the GetObjectPos() object class, Id its index; (-1, -1) means
 * "unbound", which is what the fixed-point setters restore. */
void EventCameraSetPositionObjId(int Type, int Id);
void EventCameraSetTargetObjId(int Type, int Id);

/* Added to the object's position once it has been fetched. */
void EventCameraSetPositionOffset(float *Offset);
void EventCameraSetTargetOffset(float *Offset);

void EventCameraSetFov(float Fov);
void EventCameraSetRoll(float Roll);

/* Dead zone the object-bound look-at may drift inside before the camera
 * bothers to re-aim (MapCamGra3dcamSetTargetMargin). */
void EventCameraSetMargin(float Margin);

/* Non-zero: treat Position/Target as world space, skipping the room offset. */
void EventCameraSetWorldFlg(int Flg);


/* ---- VCI mode ---------------------------------------------------------- */

/* Pull camera path CamNo out of the VCI pak and start playing it. */
void EventCameraVCIReq(int CamNo);

/* Point the controller at an already-resolved path (no pak lookup). */
void EventCameraSetVCIAddress(u_int *pData);

/* Rewind the playback cursor onto pDataTop. */
void EventCameraVCICtrlInit(u_int *pDataTop);

/* Advance one frame.  Non-zero only on the "no data" error path. */
int  EventCameraVCIPlay(void);

/* Non-zero once the cursor has run past the path's frame count -- also
 * non-zero when no path is loaded, so a caller waiting on it never hangs. */
int  EventCameraVCIPlayIsEnd(void);


/* ---- VCI data readers -------------------------------------------------- */

/* All three take the raw pak file pointer, not a VCI_CAMERA_HEADER *. */
int  EventCameraGetDataNum(u_int *pData);
void EventCameraGetPosition(float *Position, u_int *pData, int PointNo);
void EventCameraGetTarget(float *Target, u_int *pData, int PointNo);
void EventCameraGetOneData(VCI_CAMERA_POINT *pOut, const u_int *pData, int PointNo);

/* Non-zero when pData starts with the "EVEC" magic. */
int  EventCameraCheckFileId(const u_int *pData);


/* ---- curve primitives -------------------------------------------------- */

/* De Casteljau evaluation of an n-degree Bezier over pControlPoint[0..n].
 * Writes Point[3] = 1.0f so the result is usable as a homogeneous position.
 *
 * ZERO2.MAP demangles the last parameter as float (*)[3]; that is the GNU v2
 * array mangling, which encodes the *upper bound*, so the element really is
 * float[4].  The 16-byte stride is visible in the ROM's index arithmetic. */
void CalcBezierPoint(float *Point, int n, float t, float (*pControlPoint)[4]);

/* Cubic Hermite between Point0 and Point1 with end tangents Direction0 and
 * Direction1, evaluated at ParamU in [0,1]. */
void CalcHermitePoint(float *HermiteVector, float *Point0, float *Point1,
                      float *Direction0, float *Direction1, float ParamU);


/* ---- debug ------------------------------------------------------------- */

/* Pad-driven smoke test for the manual mode: SELECT-adjacent bit 0x20 starts
 * a camera pinned to object (0,0), bit 0x40 cuts it.  Not called from
 * anywhere in the shipped build. */
void EventCameraTest(void);

#endif /* _INGAME_CAMERA_EVENT_CAMERA_H */
