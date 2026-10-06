/* ==========================================================================
 *  ingame/photo/freq_camera.h
 *
 *  The finder camera's kick (freq_camera.o).
 *
 *  A one-shot recoil played over the camera the finder has already placed: the
 *  eye and the look-at point jolt vertically along a damped oscillation while
 *  the field of view pulses in and back out.  The effect code raises it when a
 *  shot lands on a ghost; FinderModeCameraCtrl() plays it out, and the camera
 *  HUD reads the current offset back so the overlay shakes with the view.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), freq_camera.o.
 * ======================================================================== */

#ifndef _INGAME_PHOTO_FREQ_CAMERA_H
#define _INGAME_PHOTO_FREQ_CAMERA_H

/* The two halves run independently once armed -- each has its own state
 * (0 idle, 1 armed, 2 running) and its own cursor into its table.  fov_bak
 * holds the field of view to restore, captured when the kick was requested. */
typedef struct                      /* 0x14 */
{
    /* 0x00 */ int   frq_flow;
    /* 0x04 */ int   frq_cnt;
    /* 0x08 */ int   fov_flow;
    /* 0x0c */ int   fov_cnt;
    /* 0x10 */ float fov_bak;
} FREQ_CAM;

/* Clears the vertical half only -- ReqFinderFadeIn() calls it as the finder
 * comes up, and a FOV pulse still in flight is left to finish and restore
 * itself. */
void  FreqCameraInit(void);

/* Arms both halves, unless either is already running.  Raised by the enemy
 * damage and zero-shot effect code. */
void  ReqFreqCamera(void);

/* Applies this frame's kick; finder_camera.c calls it between aiming the
 * camera and setting the FOV. */
void  FreqCamera(void);

/* The vertical offset FreqCamera() applied this frame, 0 when idle.  The
 * camera HUD draw uses it to shake the overlay in step with the view. */
float GetFreqCamera(void);

#endif /* _INGAME_PHOTO_FREQ_CAMERA_H */
