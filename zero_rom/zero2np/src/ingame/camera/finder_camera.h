/* ==========================================================================
 *  ingame/camera/finder_camera.h
 *
 *  The Camera Obscura's first-person camera (finder_camera.o).
 *
 *  CameraMain() hands the frame over to these while plyr_wrk.cmn_wrk.mode is
 *  5 (raising the finder) or 6 (in finder view).  The finder camera sits at
 *  the player's eye, aims along plyr_wrk.frot_x / mbox.rot[1], and is steered
 *  by the d-pad or the analog stick; ReqPointSearchCamera() is the scripted
 *  swing that whips it onto a target before the player takes over.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), finder_camera.o.
 * ======================================================================== */

#ifndef _INGAME_CAMERA_FINDER_CAMERA_H
#define _INGAME_CAMERA_FINDER_CAMERA_H

#include "eetypes.h"

/* Transition into finder view (player mode 5).  Only runs the scripted swing;
 * the player has no control yet. */
void FinderInCameraCtrl(void);

/* First-person finder view (player mode 6).  Places and aims the camera, and
 * drives the reticle and the look controls. */
void FinderModeCameraCtrl(void);

/* Hands the frame to the finder camera; SetPlyrFinderIn() raises it as the
 * last thing it does, once the target ghost has been chosen. */
void ReqFinderCamera(void);

/* The scripted swing: 10 frames of the yaw speed already in mbox.rspd[1],
 * divided down so the whole remaining turn is spread over those frames.
 *
 * Both are exported from finder_camera.o but the ROM's only call sites are in
 * this file -- ReqFinderCamera() and the two Ctrl entry points above. */
void ReqPointSearchCamera(void);
void PointSearchCameraCtrl(void);

#endif /* _INGAME_CAMERA_FINDER_CAMERA_H */
