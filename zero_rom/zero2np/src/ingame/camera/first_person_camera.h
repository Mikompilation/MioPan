/* ==========================================================================
 *  ingame/camera/first_person_camera.h
 *
 *  PORT ADDITION -- no ROM counterpart.  The first-person view: the game seen
 *  through Mio's eyes while she walks, as the Xbox Director's Cut allowed.
 *  Options > Special switches it ([game] first_person in miopan.ini).
 *
 *  It is the finder camera's eye without the finder.  The eye sits where
 *  finder_camera.c puts it, 709 units up the body; the body faces where the
 *  view looks, so mbox.rot[1] is the view's yaw exactly as it is in the finder;
 *  and the view's pitch is handed to plyr_wrk.frot_x as the finder comes up
 *  and taken back as it goes down.  Raising the Camera Obscura therefore keeps
 *  the aim, and lowering it leaves the view where it was.
 *
 *  It never replaces a camera the game composed.  CameraMain() runs the map
 *  camera as it always has -- its rectangles, its cuts and everything that
 *  hangs off them (the floating ghosts are re-seeded on a cut) -- and only
 *  then does FirstPersonCameraCtrl() move the eye, and only when nothing else
 *  holds the view: an event camera, the approach camera (talking, a ghost's
 *  grab, the death), a camera pinned on a target, or the camera following
 *  Mayu all keep their own shots, with Mio in them.  The game's camera is put
 *  back as it left it before it runs again, so it never sees the eye.
 *
 *  Movement is player.c's (PlyrMoveChkFirstPerson()): the left stick moves and
 *  sidesteps relative to the view rather than turning her, at the pace of the
 *  walk and run clips as ever.  This file owns the look -- right stick and
 *  mouse -- and the eye.
 * ======================================================================== */

#ifndef _INGAME_CAMERA_FIRST_PERSON_CAMERA_H
#define _INGAME_CAMERA_FIRST_PERSON_CAMERA_H

/* 1 while the player has asked for the first-person view. */
int   FirstPersonEnabled(void);

/* 1 while the view is Mio's own: asked for, and the last CameraMain() placed
 * the eye (or kept it, raising the finder).  Her model is not drawn, her
 * steering is first-person and her torch follows the view while this holds. */
int   FirstPersonViewActive(void);

/* The view's pitch, radians, + looks up -- plyr_wrk.frot_x's sense. */
float FirstPersonGetPitch(void);

/* Player side, from PlyrNModeCtrl() while the view is active: the right stick
 * and the mouse turn mbox.rot[1] and the pitch.  Asks for the mouse each tick. */
void  FirstPersonLookCtrl(void);

/* Camera side, all from CameraMain().
 *
 *   Restore     before the game's own camera runs: give it back the camera it
 *               left, if the eye has been over it since.
 *   Ctrl        after it: place the eye if `may_take`, else let the view go.
 *   FinderCtrl  each finder frame: the pitch follows frot_x, and the finder's
 *               camera is now the one the game left.
 *   Yield       something else placed the camera this frame (a pinned target,
 *               the debug free camera): the view is not Mio's. */
void  FirstPersonCameraRestore(void);
void  FirstPersonCameraCtrl(int may_take);
void  FirstPersonFinderCtrl(void);
void  FirstPersonCameraYield(void);

#endif /* _INGAME_CAMERA_FIRST_PERSON_CAMERA_H */
