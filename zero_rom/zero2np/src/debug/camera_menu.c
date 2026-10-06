// FILE: /home/zero_rom/zero2np/src/debug/camera_menu.c
//
// Debug camera menu, and the free camera it arms.
//
// PORT ADDITION.  camera_menu.o in the Feb 6 2004 prototype has *no* .text at
// all: ZERO2.MAP gives it 0xc of .data and nothing else, and its stabs run
// 0x12d5e4..0x12d5e8 -- four bytes of alignment fill, with DebugCameraMenu the
// only symbol the object exports.  The whole menu was compiled out of this
// build, so there is nothing here to match against and everything below the
// DebugCameraMenu definition is ours.
//
// What the ROM *does* still carry is the three flags and every consumer of
// them, which is what fixes the design:
//
//   CameraDebugON   one_Story_Debug() sends the phase to GID_STORY_DEBUG_CAM
//                   (ingame.c 1341) and one_Story_Debug_Cam() drops it again
//                   on SELECT (1378).  That phase runs no player, no ghosts
//                   and no CameraMain() -- the world is frozen and the camera
//                   is ours.  Its ROM body has an eight-line hole between
//                   MhCtlMain() (1365) and SetIngameListnerInfo() (1374) that
//                   emits no code; the call that vanished with this file went
//                   there, and that is where DebugCameraMenuMain() is called.
//   FreeCameraON    Graph2dMain() (g2d_main.c) drops m_plyr_camera.Draw() when
//                   it is up, i.e. it hides the viewfinder HUD while the game
//                   itself keeps running.  So this one is the live free camera,
//                   driven from IngameCameraMain() after CameraMain() has had
//                   its say.
//   PlayerFollowON  qualifies both: the eye orbits the followed unit instead
//                   of flying.
//
// Worth knowing before using it: one_Story_Normal() gates PlayerMainCmn() and
// PlyrNormalCtrl() on CameraDebugON *only* (ingame.c 853), so under FREE
// CAMERA the player is still live and the stick that flies the camera also
// walks Mio.  That is the ROM's own gating and it is left alone -- CAMERA
// DEBUG is the mode that freezes the world, and it is the one to fly in.
//
// Controls, once FREE CAMERA or CAMERA DEBUG is up (the pad is only taken
// while the debug menu is closed -- DrawDbgMenu() uses L2/R1/R2 as its own
// edit modifiers).  The look stick is whichever one the finder aims with, so
// opt_wrk.ana_replace and opt_wrk.view_vertical are both honoured:
//
//     look          the finder's aim stick     keyboard W A S D
//     move          the other stick, or d-pad  keyboard arrow keys
//     rise / fall   R1 / L1                    keyboard F / R
//     slow / fast   L2 / R2  (x0.25 / x4)      keyboard Z / X
//     leave         SELECT (CAMERA DEBUG only) keyboard Tab
//
// The keyboard column is s_keyboard_map[] in miopan/io/miopan_input.cpp, which
// is indexed the same way key_now[] is.  That file binds keys to stick indices
// 0 and 1 only -- the *left* stick -- so on a keyboard the move stick is dead
// and the d-pad fallback below is what actually flies the camera.
//
// The up axis is -Y.  effect_rain.c's EffectDropParticleUpdate() keeps a drop
// alive while Position[1] < GroundHeight and *adds* Gravity to Velocity[1], so
// Y grows downwards; map_camera.c frames a head with itv[1] = ipos[1] - offy
// for a positive offy, and SetIngameListnerInfo() hands the sound listener a
// top vector of (0,-1,0).  Note scn_test.c's SceneTestPadCamera() -- the ROM's
// other free camera -- reads the opposite way round on the d-pad; that is its
// own inversion and is left alone.

#include "camera_menu.h"

#include "debug_menu.h"

#include "../common/variable.h"                 // pad / opt_wrk
#include "../graphics/graph3d/gra3d.h"          // gra3dcam* / gra3dApplyCamera
#include "../ingame/camera/map_camera.h"        // MapCamGetObjPosition
#include "../sdk/libvu0.h"                      // sceVu0CopyVector

#include <math.h>

DEBUG_CAMERA_MENU DebugCameraMenu;           // data 2d8048

/* --------------------------------------------------------------------------
 *  PORT ADDITION from here down.
 * ------------------------------------------------------------------------ */

/* key_now[]/cnt[] indices, resolved through sce_pad[16] in system/pad/pad.c.
 * That table is a third ordering -- neither the SCE bit order nor paddat[]'s
 * logical labels -- so these are read off it rather than assumed. */
#define DBGCAM_KEY_UP    0
#define DBGCAM_KEY_DOWN  1
#define DBGCAM_KEY_LEFT  2
#define DBGCAM_KEY_RIGHT 3
#define DBGCAM_KEY_L1    8
#define DBGCAM_KEY_L2    9
#define DBGCAM_KEY_R1   10
#define DBGCAM_KEY_R2   11

/* Stick centre is 0x80 and full deflection 0x7f, as everywhere else that reads
 * pad[0].analog[].  The deadzone is ours: player.c's own camera-turn test
 * ((u_char)(analog[1] + 0xd0) > 0xa0) ignores anything inside +/-0x50, which is
 * far too coarse to fly with. */
#define DBGCAM_STICK_DEAD    16.0f
#define DBGCAM_STICK_RANGE  111.0f      /* 127 - DBGCAM_STICK_DEAD */

/* Leave a sliver either side of straight up/down so the forward axis never
 * becomes parallel to the up hint g3dMatrixSetDirection() rebuilds X from. */
#define DBGCAM_PITCH_LIMIT   1.5533431f /* PI/2 - 1 degree */

#define DBGCAM_MOVE_SPEED_DEF   40.0f
#define DBGCAM_TURN_SPEED_DEF    0.04f
#define DBGCAM_FOLLOW_DIST_DEF 700.0f   /* the approach camera's own distance */
#define DBGCAM_FOLLOW_HEIGHT_DEF 600.0f /* and its own head-framing lift      */
#define DBGCAM_FOV_DEF           1.0471975f     /* 60 degrees */

DEBUG_FREE_CAMERA DebugFreeCamera;

static char s_dbg_camera_title[] = "CAMERA";
static char s_dbg_free_camera[] = "FREE CAMERA";
static char s_dbg_camera_debug[] = "CAMERA DEBUG";
static char s_dbg_player_follow[] = "PLAYER FOLLOW";
static char s_dbg_cam_move_speed[] = "MOVE SPEED";
static char s_dbg_cam_turn_speed[] = "TURN SPEED";
static char s_dbg_cam_fov[] = "FOV";
static char s_dbg_cam_follow_dist[] = "FOLLOW DIST";
static char s_dbg_cam_follow_height[] = "FOLLOW HEIGHT";
static char s_dbg_cam_x[] = "CAM X";
static char s_dbg_cam_y[] = "CAM Y";
static char s_dbg_cam_z[] = "CAM Z";
static char s_dbg_cam_active[] = "ACTIVE";
static char s_dbg_cam_end[] = "_end_";

/* Reachable from the CAMERA row of dbg_menu_main.  The three CAM rows are the
 * live eye, so they read out as a position display and edit as a teleport; the
 * menu re-clamps every VALUE row on display, which is why their limits are
 * wide rather than tight. */
DEBUG_MENU dbg_camera_main =
{
    &dbg_menu_main,
    nullptr,
    s_dbg_camera_title,
    {
        { s_dbg_free_camera,   DBM_ATTR_SWITCH, &DebugCameraMenu.FreeCameraON,
                                                          0.0f,       1.0f,   1.0f },
        { s_dbg_camera_debug,  DBM_ATTR_SWITCH, &DebugCameraMenu.CameraDebugON,
                                                          0.0f,       1.0f,   1.0f },
        { s_dbg_player_follow, DBM_ATTR_SWITCH, &DebugCameraMenu.PlayerFollowON,
                                                          0.0f,       1.0f,   1.0f },
        { s_dbg_cam_move_speed, DBM_ATTR_VALUE | DBM_ATTR_FLOAT,
                               &DebugFreeCamera.fMoveSpeed,     1.0f,     500.0f,   1.0f },
        { s_dbg_cam_turn_speed, DBM_ATTR_VALUE | DBM_ATTR_FLOAT,
                               &DebugFreeCamera.fTurnSpeed,     0.005f,     0.5f,   0.005f },
        { s_dbg_cam_fov,        DBM_ATTR_VALUE | DBM_ATTR_FLOAT,
                               &DebugFreeCamera.fFov,           0.1f,       3.0f,   0.01f },
        { s_dbg_cam_follow_dist, DBM_ATTR_VALUE | DBM_ATTR_FLOAT,
                               &DebugFreeCamera.fFollowDist,   10.0f,   10000.0f,  10.0f },
        { s_dbg_cam_follow_height, DBM_ATTR_VALUE | DBM_ATTR_FLOAT,
                               &DebugFreeCamera.fFollowHeight, -5000.0f, 5000.0f,  10.0f },
        { s_dbg_cam_x,          DBM_ATTR_VALUE | DBM_ATTR_FLOAT,
                               &DebugFreeCamera.fEye[0], -1000000.0f, 1000000.0f, 10.0f },
        { s_dbg_cam_y,          DBM_ATTR_VALUE | DBM_ATTR_FLOAT,
                               &DebugFreeCamera.fEye[1], -1000000.0f, 1000000.0f, 10.0f },
        { s_dbg_cam_z,          DBM_ATTR_VALUE | DBM_ATTR_FLOAT,
                               &DebugFreeCamera.fEye[2], -1000000.0f, 1000000.0f, 10.0f },
        /* Switching this off drops the pose; the next frame re-seeds it from
         * wherever the game camera is, which is the only way back once the
         * free camera has been flown somewhere useless. */
        { s_dbg_cam_active,    DBM_ATTR_SWITCH, &DebugFreeCamera.bActive,
                                                          0.0f,       1.0f,   1.0f },
        { s_dbg_cam_end,       0,               nullptr,   0.0f,       0.0f,   0.0f },
    },
    0,
    0,
    0,
    0,
};

/* --------------------------------------------------------------------------
 *  DebugCameraMenuInit
 *
 *  Called once from DebugInit().  Only the tunables are seeded -- the pose
 *  comes from the live camera the first frame the free camera is armed.
 * ------------------------------------------------------------------------ */
void DebugCameraMenuInit(void)
{
    DEBUG_FREE_CAMERA *fc = &DebugFreeCamera;

    DebugCameraMenu.PlayerFollowON = 0;
    DebugCameraMenu.FreeCameraON   = 0;
    DebugCameraMenu.CameraDebugON  = 0;

    fc->bActive       = 0;
    fc->fEye[0]       = 0.0f;
    fc->fEye[1]       = 0.0f;
    fc->fEye[2]       = 0.0f;
    fc->fEye[3]       = 1.0f;
    fc->fYaw          = 0.0f;
    fc->fPitch        = 0.0f;
    fc->fMoveSpeed    = DBGCAM_MOVE_SPEED_DEF;
    fc->fTurnSpeed    = DBGCAM_TURN_SPEED_DEF;
    fc->fFov          = DBGCAM_FOV_DEF;
    fc->fFollowDist   = DBGCAM_FOLLOW_DIST_DEF;
    fc->fFollowHeight = DBGCAM_FOLLOW_HEIGHT_DEF;
}

/* --------------------------------------------------------------------------
 *  DbgCamAxis
 *
 *  One analog axis as [-1, 1] with the deadzone taken out rather than clipped,
 *  so the first live sample is a nudge and not a jump.
 * ------------------------------------------------------------------------ */
static float DbgCamAxis(int iIndex)
{
    float v = (float)pad[0].analog[iIndex] - 128.0f;

    if (v > DBGCAM_STICK_DEAD)
    {
        v -= DBGCAM_STICK_DEAD;
    }
    else if (v < -DBGCAM_STICK_DEAD)
    {
        v += DBGCAM_STICK_DEAD;
    }
    else
    {
        return 0.0f;
    }

    v /= DBGCAM_STICK_RANGE;

    if (v > 1.0f)
    {
        v = 1.0f;
    }
    else if (v < -1.0f)
    {
        v = -1.0f;
    }

    return v;
}

/* analog[0..1] is the right stick and [2..3] the left, and opt_wrk.ana_replace
 * picks which of them aims -- exactly as finder_camera.c's PlyrCamAnalogChk()
 * reads them.  The free camera looks with whichever stick the player has the
 * finder set to, and moves with the other, so the aiming hand never changes. */
static int DbgCamLookStick(void)
{
    return (opt_wrk.ana_replace == 1) ? 0 : 2;
}

static int DbgCamMoveStick(void)
{
    return (opt_wrk.ana_replace == 1) ? 2 : 0;
}

/* --------------------------------------------------------------------------
 *  DbgCamSeed
 *
 *  Take the pose from wherever the game camera currently is, so arming the
 *  free camera never moves the picture.
 * ------------------------------------------------------------------------ */
static void DbgCamSeed(void)
{
    DEBUG_FREE_CAMERA *fc = &DebugFreeCamera;
    float (&rPos)[4] = gra3dcamGetPosition();
    float (&rDir)[4] = gra3dcamGetDirection();
    float y;

    sceVu0CopyVector(fc->fEye, rPos);
    fc->fEye[3] = 1.0f;

    /* matCoord[2] is normalised by g3dMatrixSetDirection(), so rDir[1] is
     * already a sine -- clamp anyway, because asinf() of 1 + 1ulp is a NaN and
     * a NaN here poisons the pose for the rest of the session. */
    y = rDir[1];
    if (y < -1.0f)
    {
        y = -1.0f;
    }
    else if (y > 1.0f)
    {
        y = 1.0f;
    }

    fc->fYaw   = atan2f(rDir[0], rDir[2]);
    fc->fPitch = asinf(-y);                 /* -Y is up */
    fc->fFov   = gra3dcamGetFov();
}

/* --------------------------------------------------------------------------
 *  DebugCameraMenuMain
 * ------------------------------------------------------------------------ */
int DebugCameraMenuMain(int bAcceptPad)
{
    DEBUG_FREE_CAMERA *fc = &DebugFreeCamera;
    float vFwd[4];
    float vRight[4];
    float vTarget[4];
    float cp;
    float sp;
    float cy;
    float sy;
    float step;
    float ax;
    float ay;

    if (DebugCameraMenu.FreeCameraON == 0 && DebugCameraMenu.CameraDebugON == 0)
    {
        /* Disarmed: forget the pose so the next arming re-seeds. */
        fc->bActive = 0;
        return 0;
    }

    if (fc->bActive == 0)
    {
        DbgCamSeed();
        fc->bActive = 1;
    }

    /* L2 slows, R2 speeds.  Both at once is the slow one -- the R2+L2 gesture
     * already means "zero this" to the menu, so it should not fly fast. */
    step = fc->fMoveSpeed;
    if (*key_now[DBGCAM_KEY_L2] != 0)
    {
        step *= 0.25f;
    }
    else if (*key_now[DBGCAM_KEY_R2] != 0)
    {
        step *= 4.0f;
    }

    if (bAcceptPad != 0)
    {
        /* ---- look ---- */
        ax = DbgCamAxis(DbgCamLookStick());
        ay = DbgCamAxis(DbgCamLookStick() + 1);

        /* opt_wrk.view_vertical is the game's own invert-pitch option; the
         * finder honours it, so the free camera does too. */
        if (opt_wrk.view_vertical == 1)
        {
            ay = -ay;
        }

        fc->fYaw   += ax * fc->fTurnSpeed;
        fc->fPitch -= ay * fc->fTurnSpeed;

        if (fc->fPitch > DBGCAM_PITCH_LIMIT)
        {
            fc->fPitch = DBGCAM_PITCH_LIMIT;
        }
        else if (fc->fPitch < -DBGCAM_PITCH_LIMIT)
        {
            fc->fPitch = -DBGCAM_PITCH_LIMIT;
        }
    }

    /* The basis is rebuilt from the angles every frame rather than carried, so
     * the CAM X/Y/Z menu rows can move the eye without invalidating it. */
    cp = cosf(fc->fPitch);
    sp = sinf(fc->fPitch);
    cy = cosf(fc->fYaw);
    sy = sinf(fc->fYaw);

    vFwd[0] = sy * cp;
    vFwd[1] = -sp;                          /* -Y is up */
    vFwd[2] = cy * cp;
    vFwd[3] = 0.0f;

    /* X the way g3dMatrixSetDirection() builds it: normalize((0,1,0) x Z),
     * which keeps strafing level however far the camera is pitched. */
    vRight[0] = cy;
    vRight[1] = 0.0f;
    vRight[2] = -sy;
    vRight[3] = 0.0f;

    if (DebugCameraMenu.PlayerFollowON != 0)
    {
        /* Orbit the unit the map cameras follow -- MapCamGetObjPosition()
         * answers for the sister after MapCamTargetChange(), so the free
         * camera follows whoever the game was following. */
        float vPivot[4];

        MapCamGetObjPosition(vPivot, 0);
        vPivot[1] -= fc->fFollowHeight;
        vPivot[3] = 1.0f;

        if (bAcceptPad != 0)
        {
            float md = DbgCamAxis(DbgCamMoveStick() + 1);

            if (*key_now[DBGCAM_KEY_DOWN] != 0)
            {
                md += 1.0f;
            }
            if (*key_now[DBGCAM_KEY_UP] != 0)
            {
                md -= 1.0f;
            }

            fc->fFollowDist += md * step;
            if (fc->fFollowDist < 10.0f)
            {
                fc->fFollowDist = 10.0f;
            }

            if (*key_now[DBGCAM_KEY_R1] != 0)
            {
                fc->fFollowHeight += step;
            }
            if (*key_now[DBGCAM_KEY_L1] != 0)
            {
                fc->fFollowHeight -= step;
            }
        }

        fc->fEye[0] = vPivot[0] - vFwd[0] * fc->fFollowDist;
        fc->fEye[1] = vPivot[1] - vFwd[1] * fc->fFollowDist;
        fc->fEye[2] = vPivot[2] - vFwd[2] * fc->fFollowDist;
        fc->fEye[3] = 1.0f;

        sceVu0CopyVector(vTarget, vPivot);
    }
    else
    {
        if (bAcceptPad != 0)
        {
            float mf;
            float ms;

            ax = DbgCamAxis(DbgCamMoveStick());
            ay = DbgCamAxis(DbgCamMoveStick() + 1);

            /* Stick up is a smaller byte, so forward is -ay. */
            mf = -ay;
            ms = ax;

            /* The d-pad moves as well, and on a keyboard it is the only thing
             * that does: miopan_input.cpp binds keys to stick indices 0 and 1
             * only (s_stick_keyboard_neg/pos), which are the *left* stick, so
             * the move stick is permanently centred there.  Adding rather than
             * choosing is the same shape finder_camera.c's PlyrCamAnalogChk()
             * uses for its own d-pad/stick pair. */
            if (*key_now[DBGCAM_KEY_UP] != 0)
            {
                mf += 1.0f;
            }
            if (*key_now[DBGCAM_KEY_DOWN] != 0)
            {
                mf -= 1.0f;
            }
            if (*key_now[DBGCAM_KEY_RIGHT] != 0)
            {
                ms += 1.0f;
            }
            if (*key_now[DBGCAM_KEY_LEFT] != 0)
            {
                ms -= 1.0f;
            }

            fc->fEye[0] += (vFwd[0] * mf + vRight[0] * ms) * step;
            fc->fEye[1] += (vFwd[1] * mf) * step;
            fc->fEye[2] += (vFwd[2] * mf + vRight[2] * ms) * step;

            if (*key_now[DBGCAM_KEY_R1] != 0)
            {
                fc->fEye[1] -= step;        /* rise */
            }
            if (*key_now[DBGCAM_KEY_L1] != 0)
            {
                fc->fEye[1] += step;        /* fall */
            }
        }

        fc->fEye[3] = 1.0f;

        vTarget[0] = fc->fEye[0] + vFwd[0] * 100.0f;
        vTarget[1] = fc->fEye[1] + vFwd[1] * 100.0f;
        vTarget[2] = fc->fEye[2] + vFwd[2] * 100.0f;
        vTarget[3] = 1.0f;
    }

    /* Nothing restores the fov when the free camera is disarmed, and nothing
     * has to: every other controller (map / event / finder / scene) writes it
     * from its own data on the frame it next runs. */
    gra3dcamSetFov(fc->fFov);
    gra3dcamSetPosition(fc->fEye);
    gra3dcamSetTarget(vTarget, 1);
    gra3dApplyCamera(NULL, 1);

    return 1;
}
