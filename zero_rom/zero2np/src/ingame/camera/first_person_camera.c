// FILE: ingame/camera/first_person_camera.c
//
// PORT ADDITION -- no ROM counterpart.  The first-person view; the header's
// banner says how it hangs off the game's cameras.
//
// Three pieces of state, all this file's own:
//
//   * fp_view, whether the last CameraMain() left the eye in place.  Everything
//     that changes with the view -- Mio's model, her steering, her torch --
//     asks FirstPersonViewActive(), so they all switch on the same frame the
//     picture does, and none of them while a cutscene holds the camera.
//   * fp_pitch, the view's pitch.  The yaw needs no state: it is the body's
//     own mbox.rot[1], as it is in the finder.
//   * fp_game_camera, the camera the game placed this frame before the eye
//     went over it.  The map camera's follow modes start from wherever the
//     camera is now (MapCamGra3dcamSetPositionMargin()), so it is handed back
//     before the game's camera runs again; the game's camera then moves on
//     from its own last shot, as if the eye had never been there, and is
//     where it should be the moment the view goes back to it.

#include "first_person_camera.h"

#include <math.h>                               /* sqrtf / fabsf               */

#include "eetypes.h"
#include "libvu0.h"                             /* sceVu0AddVector             */

#include "../../common/utility.h"               /* RotFvector                  */
#include "../../common/variable.h"              /* plyr_wrk / opt_wrk / pad    */
#include "../../graphics/graph3d/gra3d.h"       /* gra3dcam*                   */
#include "../../miopan/io/miopan_input.h"       /* mouse look                  */
#include "../../miopan/miopan_config.h"         /* [game] first_person         */
#include "../../system/os/system.h"             /* GetPALMode                  */
#include "../../system/pad/pad.h"               /* pad                         */
#include "../plyr/unit_ctl.h"                   /* RotLimitChk                 */

/* The ROM's PI, as finder_camera.c spells it. */
#define FP_PI               3.1415925f

/* The eye, up the body's own -Y axis: SetPlyrFinderIn()'s eye line, and where
 * the finder camera sits, so raising it does not move the picture. */
#define FP_EYE_HEIGHT       709.0f

/* How far ahead the target is put.  Only the direction matters. */
#define FP_LOOK_DISTANCE    1000.0f

/* The finder's own pitch limit, so the hand-over in either direction never
 * has to clamp. */
#define FP_PITCH_LIMIT      (60.0f * FP_PI / 180.0f)

/* The right stick's turn at full tilt, per frame: 150 and 90 degrees a second
 * at the game's 30.  The finder's 1.676 degrees is for aiming a photograph;
 * walking wants to look round.  Past the dead zone the travel is squared, so
 * a small push still gives a slow, fine turn. */
#define FP_YAW_SPEED        (5.0f * FP_PI / 180.0f)
#define FP_PITCH_SPEED      (3.0f * FP_PI / 180.0f)
#define FP_STICK_DEAD_ZONE  0.2f

/* An axis this close to centre is taken as centred, so a push straight
 * sideways does not creep the pitch (nor straight up the yaw).  A resting
 * stick is not quite on 128 either: the port's own centre is 127. */
#define FP_STICK_AXIS_SNAP  0.1f

/* DualShock analog-pad id.  Without one, analog[] means nothing. */
#define FP_PAD_ID_ANALOG    0x79

static int         fp_view;
static float       fp_pitch;
static GRA3DCAMERA fp_game_camera;
static int         fp_game_camera_held;

int FirstPersonEnabled(void)
{
    return miopan_config.game.first_person != 0;
}

int FirstPersonViewActive(void)
{
    return FirstPersonEnabled() != 0 && fp_view != 0;
}

float FirstPersonGetPitch(void)
{
    return fp_pitch;
}

/* --------------------------------------------------------------------------
 *  FirstPersonLookCtrl
 *
 *  The right stick (analog[0..1]; the left one walks) and the mouse.  The
 *  stick turns by rate, the mouse by distance -- the input layer has already
 *  scaled its motion to radians -- so the mouse is not scaled for PAL, as in
 *  finder_camera.c's SetFinderRot().
 * ------------------------------------------------------------------------ */
void FirstPersonLookCtrl(void)
{
    MOVE_BOX *pMoveBox = &plyr_wrk.cmn_wrk.mbox;
    float     yaw   = 0.0f;
    float     pitch = 0.0f;
    float     mouse_yaw;
    float     mouse_pitch;
    float     x;
    float     y;
    float     mag;
    float     scale;

    /* Free to look, so the mouse is captured.  Asked every tick, as the
     * finder asks: the capture lapses by itself once the asking stops. */
    MioPan_InputRequestWalkLook();

    /* Paralysed or dead: not hers to turn, as in the finder. */
    if (plyr_wrk.cmn_wrk.st.cond == 1 || plyr_wrk.cmn_wrk.st.cond == 3)
    {
        return;
    }

    if (pad[0].id == FP_PAD_ID_ANALOG)
    {
        x = ((float)pad[0].analog[0] - 128.0f) / 127.0f;
        y = ((float)pad[0].analog[1] - 128.0f) / 127.0f;
        if (fabsf(x) < FP_STICK_AXIS_SNAP)
        {
            x = 0.0f;
        }
        if (fabsf(y) < FP_STICK_AXIS_SNAP)
        {
            y = 0.0f;
        }
        mag = sqrtf(x * x + y * y);

        if (mag > FP_STICK_DEAD_ZONE)
        {
            scale = (mag - FP_STICK_DEAD_ZONE) / (1.0f - FP_STICK_DEAD_ZONE);
            if (scale > 1.0f)
            {
                scale = 1.0f;
            }
            scale = scale * scale / mag;

            /* Stick up is a negative y, and looks up -- unless the game's
             * own "viewfinder vertical" says otherwise. */
            yaw   = x * scale * FP_YAW_SPEED;
            pitch = -y * scale * FP_PITCH_SPEED;
            if (opt_wrk.view_vertical == 1)
            {
                pitch = -pitch;
            }

            /* 50 frames a second turn less per second; the finder's 1.2. */
            if (GetPALMode() != 0)
            {
                yaw   *= 1.2f;
                pitch *= 1.2f;
            }
        }
    }

    if (MioPan_InputMouseLook(&mouse_yaw, &mouse_pitch) != 0)
    {
        yaw   += mouse_yaw;
        pitch += mouse_pitch;
    }

    pMoveBox->rot[1] += yaw;
    RotLimitChk(&pMoveBox->rot[1]);

    fp_pitch += pitch;
    if (FP_PITCH_LIMIT < fp_pitch)
    {
        fp_pitch = FP_PITCH_LIMIT;
    }
    else if (fp_pitch < -FP_PITCH_LIMIT)
    {
        fp_pitch = -FP_PITCH_LIMIT;
    }
}

/* --------------------------------------------------------------------------
 *  The camera.
 * ------------------------------------------------------------------------ */

void FirstPersonCameraRestore(void)
{
    if (fp_game_camera_held != 0)
    {
        *gra3dGetCamera()   = fp_game_camera;
        fp_game_camera_held = 0;
    }
}

/* Placed as FinderModeCameraCtrl() places the finder: the eye up the body,
 * then a point ahead along the yaw and pitch. */
void FirstPersonCameraCtrl(int may_take)
{
    MOVE_BOX *pMoveBox = &plyr_wrk.cmn_wrk.mbox;
    float     Eye[4];
    float     Position[4];
    float     Target[4];
    float     RotVec[4];

    if (FirstPersonEnabled() == 0 || may_take == 0)
    {
        /* The game's camera is its own again, and the view comes back
         * level after whatever held it. */
        fp_view  = 0;
        fp_pitch = 0.0f;
        return;
    }

    fp_game_camera      = *gra3dGetCamera();
    fp_game_camera_held = 1;

    Position[0] = 0.0f; Position[1] = -FP_EYE_HEIGHT;
    Position[2] = 0.0f; Position[3] = 1.0f;
    RotFvector(pMoveBox->rot, Position);
    sceVu0AddVector(Eye, pMoveBox->pos, Position);
    Eye[3] = 1.0f;
    gra3dcamSetPosition(Eye);

    Target[0] = 0.0f; Target[1] = 0.0f;
    Target[2] = FP_LOOK_DISTANCE; Target[3] = 1.0f;
    RotVec[0] = fp_pitch;
    RotVec[1] = pMoveBox->rot[1];
    RotVec[2] = 0.0f; RotVec[3] = 0.0f;
    RotFvector(RotVec, Target);
    sceVu0AddVector(Target, Eye, Target);
    Target[3] = 1.0f;

    /* Fixed up: a roll the map camera's record carried does not survive. */
    gra3dcamSetTarget(Target, 1);
    gra3dcamSetFov(miopan_config.game.first_person_fov * FP_PI / 180.0f);
    gra3dApplyCamera(NULL, 1);

    fp_view = 1;
}

void FirstPersonFinderCtrl(void)
{
    /* The finder's camera is the one the game left now, and the game's own
     * picks up from it when the finder comes down -- as in the ROM. */
    fp_game_camera_held = 0;

    if (FirstPersonEnabled() != 0)
    {
        fp_pitch = plyr_wrk.frot_x;
    }
}

void FirstPersonCameraYield(void)
{
    fp_view             = 0;
    fp_pitch            = 0.0f;
    fp_game_camera_held = 0;
}
