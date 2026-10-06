// FILE: /home/zero_rom/zero2np/src/graphics/graph2d/g2d_main.c
//
// 2D graphics subsystem entry points.  Three init hooks tie the 2D layer into
// the engine boot / per-frame flow, and Graph2dMain() is the per-frame driver
// that draws the player camera, runs the 2D effect passes and the screen fade.
//
//   * InitGraph2dON()     - power-on hook (nothing to do in this build).
//   * InitGraph2dBoot()   - one-time boot: reset the work block and bring up the
//                           2D packet ring, the message system and the fader.
//   * InitGraph2dEFrame() - per-effect-frame hook: message + heat-haze refresh.
//   * Graph2dMain()       - per-frame: player-camera draw, 2D effects, fade,
//                           pad vibration.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "graph2d.h"                // this subsystem's public API + G2D_WRK

#include "../graph3d/ctl/fixed_array.h"     // fixed_array<> template (inlined per TU)

#include "../effect/effect.h"       // EffectControl / InitHeatHaze
#include "../../ingame/photo/n_plyr_camera.h"  // CNPlyrCamera / m_plyr_camera
#include "../../debug/camera_menu.h"        // DEBUG_CAMERA_MENU / DebugCameraMenu
#include "message.h"                // InitMessage / InitMessageEF
#include "fade.h"                   // FadeCtrlInit / FadeMain
#include "../../system/pad/vib_manage.h"    // CallVibrate

// ──────────────────────────────────────────────────────────────────────
// File-scope work block.

// Font texture-bank descriptors: real FF2 TEX0 values recovered from the PS2
// ELF (see tools/extract_font_data.py).  Previously a zero-filled stub, which
// left every font bank pointing at GS VRAM address 0 with no glyph atlas.
#include "fntdat_data.h"            // SPRT_DAT fntdat[6]
// Effect texture-bank descriptors, same provenance (see
// tools/reverse/extract_effdat.py).  Read by the effect modules through
// Set3DPosTexure() and friends.
#include "effdat_data.h"            // SPRT_DAT effdat[98]
G2D_WRK g2d_wrk;                    // sdata 3f0a88 : { init, flow }

// the per-TU fixed_array<> template instances (_fixed_array_assert /
// _fixed_array_verifyrange<T>) are emitted here by the compiler; see
// "../graph3d/ctl/fixed_array.h" for their definitions.

// ──────────────────────────────────────────────────────────────────────
// Power-on hook.  Empty in this build.

void InitGraph2dON(void)
{
}

// ──────────────────────────────────────────────────────────────────────
// One-time boot of the 2D layer.

void InitGraph2dBoot(void)
{
    g2d_wrk.flow = 0;
    InitPK2Dbuf();
    InitMessage();
    FadeCtrlInit();
}

// ──────────────────────────────────────────────────────────────────────
// Per-effect-frame refresh.

void InitGraph2dEFrame(void)
{
    InitMessageEF();
    InitHeatHaze();
}

// ──────────────────────────────────────────────────────────────────────
// Per-frame 2D driver.  Skip the player-camera draw while a debug camera is
// active; otherwise draw it, then run the two 2D effect passes, the fade and
// the controller vibration.

void Graph2dMain(void)
{
    if ((DebugCameraMenu.CameraDebugON == 0) && (DebugCameraMenu.FreeCameraON == 0))
    {
        m_plyr_camera.Draw();
    }

    EffectControl(7);
    EffectControl(8);
    FadeMain();
    CallVibrate();
}
