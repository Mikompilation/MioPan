/* ==========================================================================
 *  debug/camera_menu.h
 *
 *  Debug camera menu state shared with the 2D frame driver.
 *
 *  DEBUG_CAMERA_MENU is the ROM's, recovered from camera_menu.c's stabs
 *  (DEBUG_CAMERA_MENU:t1573=s12PlayerFollowON:1,0,32;FreeCameraON:1,32,32;
 *  CameraDebugON:1,64,32).  Everything below it is a PORT ADDITION -- see the
 *  banner in camera_menu.c for why there is nothing to match.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _DEBUG_CAMERA_MENU_H
#define _DEBUG_CAMERA_MENU_H

#include "debug_menu.h"

typedef struct DEBUG_CAMERA_MENU             /* 0xc */
{
    /* 0x0 */ int PlayerFollowON;
    /* 0x4 */ int FreeCameraON;
    /* 0x8 */ int CameraDebugON;
} DEBUG_CAMERA_MENU;

extern DEBUG_CAMERA_MENU DebugCameraMenu;    /* data 2d8048 */

/* --------------------------------------------------------------------------
 *  PORT ADDITION from here down.
 *
 *  The free camera's own state.  Angles are radians; the world's up axis is
 *  -Y (a falling water drop in effect_rain.c is alive while Position[1] <
 *  GroundHeight and its Gravity is *added* to Velocity[1]), so fPitch > 0
 *  looks up and fEye[1] decreases as the camera rises.
 * ------------------------------------------------------------------------ */
typedef struct DEBUG_FREE_CAMERA
{
    int   bActive;          /* latch: 0 re-seeds fEye/fYaw/fPitch from the
                             * live camera on the next frame                */
    float fEye[4];          /* world-space eye, also the menu's X/Y/Z rows  */
    float fYaw;             /* about world Y; 0 looks along +Z              */
    float fPitch;           /* + looks up                                   */
    float fMoveSpeed;       /* world units per frame at full stick          */
    float fTurnSpeed;       /* radians per frame at full stick              */
    float fFov;             /* radians, pushed every frame                  */
    float fFollowDist;      /* PLAYER FOLLOW: eye distance from the unit    */
    float fFollowHeight;    /* PLAYER FOLLOW: pivot lift above the unit     */
} DEBUG_FREE_CAMERA;

extern DEBUG_FREE_CAMERA DebugFreeCamera;
extern DEBUG_MENU        dbg_camera_main;

void DebugCameraMenuInit(void);

/* One frame of the free camera.  Returns non-zero when it took the camera
 * over, so a caller can tell whether the map camera's placement survived.
 * bAcceptPad is 0 while the debug menu owns the pad -- the camera then holds
 * its pose and only the menu's own rows move it. */
int  DebugCameraMenuMain(int bAcceptPad);

#endif /* _DEBUG_CAMERA_MENU_H */
