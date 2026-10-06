// FILE: /home/zero_rom/zero2np/src/ingame/savepoint/savepoint.c
//
// The save point's outer bracket: the request that opens one, the room-load
// hook that makes it possible, and the two phases either side of the menu
// where the room is still running.
//
// SavePointStartReq() is the gate.  Two things can refuse it: less than
// 0x2b1400 bytes free on the heap (the screen brings the album and the save
// screen with it, so it is not cheap), and a ghost being active -- and only
// the second warns, because the first is a silent no-op the event script
// retries.  Neither is an error the player ever sees.
//
// one_SavePoint_FadeIn() and one_SavePoint_FadeOut() are the same seventeen
// calls: the whole room -- player, sister, ghosts, BGM, map hit, camera, play
// timer, motion, fog, the 3D draw, effects, the brightness filter, the event
// display and 2D -- and then the fade module's own two calls on top.  The
// room never actually stops during a save; it only stops being drawn once the
// black quad is fully down and GID_SAVEPOINT_MAIN takes over.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), savepoint.o.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.

#include "savepoint.h"

#include "savepoint_fade_in.h"                      /* SavePointFadeIn*       */
#include "savepoint_fade_out.h"                     /* SavePointFadeOut*      */
#include "savepoint_main.h"                         /* SavePointMain*         */
#include "savepoint_top.h"                          /* SavePointTop*          */

#include "../enemy/enemy.h"                         /* IsEnemyOn / AutoEnemy  */
#include "../event/prg/ev_disp.h"                   /* EvDispMain             */
#include "../ingame.h"                              /* IngameCameraMain       */
#include "../map/map_bgm.h"                         /* map_bgmMain            */
#include "../map/MapFog.h"                          /* MapFogProc             */
#include "../map/MhCtl.h"                           /* MhCtlMain              */
#include "../menu/play_data.h"                      /* PlayData_PlayTimeCount */
#include "../plyr/player.h"                         /* PlayerMainCmn          */
#include "../plyr/sis_mdl.h"                        /* sis_mdlMotionWork      */
#include "../plyr/sister.h"                         /* SisterMain             */
#include "../../album/prg/album.h"                  /* AlbumBackGroundLoadReq */
#include "../../common/mem_util.h"                  /* mem_utilGetMem         */
#include "../../common/utility2.h"                  /* PRINT_WARNING          */
#include "../../common/variable.h"                  /* plyr_wrk               */
#include "../../graphics/effect/effect.h"           /* InitEffectsEF          */
#include "../../graphics/graph2d/graph2d.h"         /* Graph2dMain            */
#include "../../graphics/graph3d/gra3d.h"           /* gra3dDraw              */
#include "../../main/gphase.h"                      /* SetNextGPhase          */
#include "../../save_load/prg/game_data_save.h"     /* GameDataSave*          */

/* Heap the save-point screen needs free before it will open.  This is the
 * ROM's own literal and the test is a strict `>`: the compare is
 * `sltu 0x2b13ff, free`, so the screen wants 0x2b1400 bytes (2 823 168). */
#define SAVEPOINT_NEED_FREE_SIZE    0x2b13ff

/* ==========================================================================
 *  Opening a save point
 * ======================================================================== */

/* Called from the event script when the player uses a save point.  Lines
 * 91..103 hold no code -- 13 source lines that compiled to nothing. */
void SavePointStartReq(void)                                            /* 85 */
{
    if (mem_utilQueryTotalFreeSize() > SAVEPOINT_NEED_FREE_SIZE) {      /* 89 */
        if (IsEnemyOn() == 0) {                                         /* 90 */
            SetNextGPhase(GID_SAVEPOINT_FADEIN);                        /* 104 */
        }
        else {
            PRINT_WARNING("Warning! %s", __FUNCTION__);                 /* 107 */
        }
    }
}

/* ==========================================================================
 *  Room load
 * ======================================================================== */

/* Everything the save point needs, claimed at room load: the fade state, the
 * background pak, the language-dependent text pak, and both child screens'
 * assets.  The album and the save screen are handed mem_util's allocator
 * explicitly because they are also used from the outgame, where the heap is
 * a different one. */
void SavePointBackGroundLoadReq(void)                                   /* 117 */
{
    SavePointFadeInCtrlInit();                                          /* 121 */

    SavePointMainBackGroundLoadReq();                                   /* 124 */
    SavePointTopBackGroundLoadReq();                                    /* 127 */

    GameDataSaveBackGroundLoadReq(mem_utilGetMem, mem_utilFreeMem);     /* 130 */
    AlbumBackGroundLoadReq(mem_utilGetMem, mem_utilFreeMem);            /* 133 */
}

void SavePointEnd(void)                                                 /* 141 */
{
    SavePointMainMemFree();                                             /* 145 */
    SavePointTopMemFree();                                              /* 148 */

    GameDataSaveTexMemFree();                                           /* 151 */
    AlbumEnd();                                                         /* 154 */
}

/* ==========================================================================
 *  GID_SAVEPOINT_FADEIN -- the prompt
 * ======================================================================== */

void init_SavePoint_FadeIn(void)                                        /* 166 */
{
}

GPHASE_ENUM one_SavePoint_FadeIn(GPHASE_ENUM dummy)                     /* 169 */
{
    (void)dummy;

    PlayerMainCmn(1);                                                   /* 170 */
    SisterMain();                                                       /* 173 */
    AutoEnemyMain();                                                    /* 176 */
    map_bgmMain();                                                      /* 179 */
    MhCtlMain(GetPlyrAreaNo());                                         /* 182 */
    IngameCameraMain();                                                 /* 185 */

    PlayData_PlayTimeCount();                                           /* 190 */

    EnemyMotionWork();                                                  /* 192 */
    sis_mdlMotionWork();                                                /* 193 */

    MapFogProc(GetPlyrAreaNo(), (int)(short)plyr_wrk.cmn_wrk.floor,
               plyr_wrk.cmn_wrk.mbox.pos);                              /* 196 */
    gra3dDraw();                                                        /* 197 */

    InitEffectsEF();                                                    /* 199 */
    EffectControl(5);                                                   /* 200 */
    BrightnessAdjustmentFilterDraw();                                   /* 201 */

    EvDispMain();                                                       /* 203 */
    Graph2dMain();                                                      /* 205 */

    SavePointFadeInMain();                                              /* 208 */
    SavePointFadeInDispMain();                                          /* 210 */

    return GPHASE_CONTINUE;                                             /* 212 */
}

void end_SavePoint_FadeIn(void)                                         /* 215 */
{
}

/* ==========================================================================
 *  GID_SAVEPOINT_FADEOUT -- back into the room
 * ======================================================================== */

void init_SavePoint_FadeOut(void)                                       /* 221 */
{
    SavePointFadeOutInit();                                             /* 223 */
}

/* The fade-in body less SavePointFadeInMain(): there is no control step on
 * the way out, only the ramp. */
GPHASE_ENUM one_SavePoint_FadeOut(GPHASE_ENUM dummy)                    /* 226 */
{
    (void)dummy;

    PlayerMainCmn(1);                                                   /* 227 */
    SisterMain();                                                       /* 230 */
    AutoEnemyMain();                                                    /* 233 */
    map_bgmMain();                                                      /* 236 */
    MhCtlMain(GetPlyrAreaNo());                                         /* 239 */
    IngameCameraMain();                                                 /* 242 */

    PlayData_PlayTimeCount();                                           /* 247 */

    EnemyMotionWork();                                                  /* 249 */
    sis_mdlMotionWork();                                                /* 250 */

    MapFogProc(GetPlyrAreaNo(), (int)(short)plyr_wrk.cmn_wrk.floor,
               plyr_wrk.cmn_wrk.mbox.pos);                              /* 253 */
    gra3dDraw();                                                        /* 254 */

    InitEffectsEF();                                                    /* 256 */
    EffectControl(5);                                                   /* 257 */
    BrightnessAdjustmentFilterDraw();                                   /* 258 */

    EvDispMain();                                                       /* 260 */
    Graph2dMain();                                                      /* 262 */

    SavePointFadeOutDispMain();                                         /* 265 */

    return GPHASE_CONTINUE;                                             /* 267 */
}

void end_SavePoint_FadeOut(void)                                        /* 270 */
{
}
