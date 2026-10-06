// FILE: /home/zero_rom/zero2np/src/ingame/event/prg/ev_phase.c
//
// The two GPhase states an event can park the game in while it shows something
// over the world: GID_STORY_EVENT_MSG (a message window) and
// GID_STORY_EVENT_FILE (a file / document page).  Both keep the room running so
// the world does not freeze behind the overlay -- player, sister, ghosts, bgm,
// camera and the whole draw chain still tick -- but neither reads the pad, so
// nothing the player does moves anything.  ev_disp.c owns what is actually
// drawn on top; EvDispMain() is the call that puts it there.
//
// The two bodies are the same sequence apart from two things: the message
// phase also runs PlyrMotionMovement() and asks IngameDecideNextPhase() where
// to go next, while the file phase does neither -- ev_disp.c closes the file
// page itself.  PlayerMainCmn's argument differs too (0 vs 1), which is what
// lets the message phase apply that movement.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "../../../main/phasefunc.h"         // GPHASE_ENUM + this module's phase callbacks
#include "../../../main/gphase.h"            // SetNextGPhase
#include "../../../common/variable.h"        // plyr_wrk

#include "ev_disp.h"                         // EvDispMain
#include "ev_main.h"                         // EventMain
#include "../../ingame.h"                    // IngameCameraMain / IngameDecideNextPhase
#include "../../enemy/enemy.h"               // AutoEnemyMain / EnemyMotionWork
#include "../../map/map_bgm.h"               // map_bgmMain
#include "../../map/MapFog.h"                // MapFogProc
#include "../../map/MhCtl.h"                 // MhCtlMain
#include "../../menu/play_data.h"            // PlayData_PlayTimeCount
#include "../../photo/m_plyr_camera.h"       // CNPlyrCamera::Main / m_plyr_camera
#include "../../photo/photo.h"               // photo_datObjMain
#include "../../plyr/player.h"               // PlayerMainCmn / PlyrMotionMovement / GetPlyrAreaNo
#include "../../plyr/plyr_mdl.h"             // plyr_mdlMotionWork
#include "../../plyr/sis_mdl.h"              // sis_mdlMotionWork
#include "../../plyr/sister.h"               // SisterMain
#include "../../subtitle/subtitle.h"         // SubTitleMain
#include "../../../graphics/effect/effect.h"     // InitEffectsEF / EffectControl / BrightnessAdjustmentFilterDraw
#include "../../../graphics/effect/effect_scr.h" // ScreenSaverDraw
#include "../../../graphics/graph2d/graph2d.h"   // Graph2dMain
#include "../../../graphics/graph3d/gra3d.h"     // gra3dDraw

/* -------------------------------------------------------------------------
 *  GID_STORY_EVENT_MSG -- an event is holding a message window open.
 * ---------------------------------------------------------------------- */

void init_EventMsg_Disp(void)                                           /* 33 */
{
}                                                                       /* 34 */

void end_EventMsg_Disp(void)                                            /* 36 */
{
}

GPHASE_ENUM one_EventMsg_Disp(GPHASE_ENUM dummy)
{
    photo_datObjMain();                                                 /* 40 */

    /* mode 0: no pad read, but the queued motion still gets applied, so a
     * walk-and-talk the event started keeps playing under the window. */
    PlayerMainCmn(0);                                                   /* 42 */
    PlyrMotionMovement();                                               /* 44 */
    SisterMain();                                                       /* 46 */
    AutoEnemyMain();                                                    /* 48 */
    map_bgmMain();                                                      /* 50 */

    MhCtlMain(GetPlyrAreaNo());                                         /* 53 */

    IngameCameraMain();                                                 /* 56 */

    m_plyr_camera.Main();                                               /* 59 */

    PlayData_PlayTimeCount();                                           /* 62 */

    EnemyMotionWork();                                                  /* 64 */
    plyr_mdlMotionWork();                                               /* 65 */
    sis_mdlMotionWork();                                                /* 66 */

    MapFogProc(GetPlyrAreaNo(), (int)(short)plyr_wrk.cmn_wrk.floor, plyr_wrk.cmn_wrk.mbox.pos); /* 69 */
    gra3dDraw();                                                        /* 70 */

    InitEffectsEF();                                                    /* 72 */
    EffectControl(5);                                                   /* 73 */
    BrightnessAdjustmentFilterDraw();                                   /* 74 */

    Graph2dMain();                                                      /* 76 */

    EvDispMain();                                                       /* 78 */

    EventMain();                                                        /* 80 */

    SubTitleMain(1);                                                    /* 82 */

    ScreenSaverDraw();                                                  /* 84 */

    /* Unconditional, unlike one_Story_Normal, which suppresses the call when
     * the answer is "stay put".  Here the phase is a temporary overlay, so
     * whatever ingame.c decides is where we go. */
    SetNextGPhase(IngameDecideNextPhase());                             /* 86 */

    return GPHASE_CONTINUE;                                             /* 88 */
}

/* -------------------------------------------------------------------------
 *  GID_STORY_EVENT_FILE -- an event is holding a file / document page open.
 * ---------------------------------------------------------------------- */

void init_EventFile_Disp(void)                                          /* 94 */
{
}                                                                       /* 95 */

void end_EventFile_Disp(void)                                           /* 97 */
{
}

GPHASE_ENUM one_EventFile_Disp(GPHASE_ENUM dummy)
{
    photo_datObjMain();                                                 /* 101 */

    /* mode 1 and no PlyrMotionMovement(): Mio stands still for the whole
     * read. */
    PlayerMainCmn(1);                                                   /* 103 */

    SisterMain();                                                       /* 106 */

    AutoEnemyMain();                                                    /* 109 */

    map_bgmMain();                                                      /* 112 */

    MhCtlMain(GetPlyrAreaNo());                                         /* 115 */

    IngameCameraMain();                                                 /* 118 */

    m_plyr_camera.Main();                                  /* 121 */

    PlayData_PlayTimeCount();                                           /* 124 */

    EnemyMotionWork();                                                  /* 126 */
    plyr_mdlMotionWork();                                               /* 127 */
    sis_mdlMotionWork();                                                /* 128 */

    MapFogProc(GetPlyrAreaNo(), (int)(short)plyr_wrk.cmn_wrk.floor,     /* 131 */
               plyr_wrk.cmn_wrk.mbox.pos);
    gra3dDraw();                                                        /* 132 */

    InitEffectsEF();                                                    /* 134 */
    EffectControl(5);                                                   /* 135 */
    BrightnessAdjustmentFilterDraw();                                   /* 136 */

    Graph2dMain();                                                      /* 138 */

    EvDispMain();                                                       /* 140 */

    EventMain();                                                        /* 142 */

    SubTitleMain(1);                                                    /* 144 */

    ScreenSaverDraw();                                                  /* 146 */

    /* No IngameDecideNextPhase() here -- the file page closes itself through
     * ev_disp.c, which requests the transition when the player dismisses it. */
    return GPHASE_CONTINUE;                                             /* 148 */
}
