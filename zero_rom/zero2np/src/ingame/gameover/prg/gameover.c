// FILE: /home/zero_rom/zero2np/src/ingame/gameover/prg/gameover.c
//
// The game-over sequence: the three phases between the player dying and the
// game-over menu opening.
//
//   GID_STORY_GAMEOVER_EFF    one frame of the room, then straight on to
//   GID_STORY_GAMEOVER_FADE   the same frame plus a 30-frame fade to black,
//                             then
//   GID_STORY_GAMEOVER_MOVIE  the "game over" movie, then GID_GAMEOVER_MENU_TOP
//
// one_Story_GameOver_Eff() and one_Story_GameOver_Fade() are the same fifteen
// calls -- the whole room: player, sister, ghosts, BGM, map hit, camera, play
// timer, motion, fog, the 3D draw, effects, the brightness filter and 2D --
// and then the fade module's own two calls on top.  This is savepoint.o's
// one_SavePoint_FadeIn()/FadeOut() pair with one call taken out: savepoint
// runs EvDispMain() between the brightness filter and Graph2dMain(), and this
// file does not.  The room is dead, so there is no event display left to run.
//
// The _EFF phase does exactly one frame of work and then hands on
// unconditionally; it exists to give the room one clean frame after the death
// animation before the fade counter starts.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), gameover.o.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.

#include "gameover.h"

#include "../../enemy/enemy.h"                      /* AutoEnemyMain          */
#include "../../ingame.h"                           /* IngameCameraMain       */
#include "../../map/map_bgm.h"                      /* map_bgmMain            */
#include "../../map/MapFog.h"                       /* MapFogProc             */
#include "../../map/MhCtl.h"                        /* MhCtlMain              */
#include "../../menu/anim_2d.h"                     /* Anim2D_CalcNowAlpha    */
#include "../../menu/play_data.h"                   /* PlayData_PlayTimeCount */
#include "../../plyr/player.h"                      /* PlayerMainCmn          */
#include "../../plyr/sis_mdl.h"                     /* sis_mdlMotionWork      */
#include "../../plyr/sister.h"                      /* SisterMain             */
#include "../../../common/variable.h"               /* plyr_wrk               */
#include "../../../graphics/effect/effect.h"        /* InitEffectsEF          */
#include "../../../graphics/graph2d/g2d_draw.h"     /* DISP_SQAR / SQAR_DAT   */
#include "../../../graphics/graph2d/graph2d.h"      /* Graph2dMain            */
#include "../../../graphics/graph3d/gra3d.h"        /* gra3dDraw              */
#include "../../../graphics/movie/movie.h"          /* InitMovieWithTitle     */
#include "../../../main/gphase.h"                   /* SetNextGPhase          */
#include "../../../system/eeiop/stream_auto.h"      /* StreamAutoAllStop      */

/* The "game over" movie.  A raw scene number, as outgame.c's own
 * InitMovieWithTitle(0x46, 1) is -- the ROM's debug info carries no enum for
 * the movie table. */
#define GAMEOVER_MOVIE_SCENE    0x45

/* Priority the black quad is drawn at.  Also gameover_bg's own `pri`, which
 * the caller then overwrites -- see GameOverScreenBgDisp(). */
#define GAMEOVER_BG_PRI         0xa0

/* types.txt.  Private to this file, as loadgame.c's LOAD_GAME_CTRL is:
 * globals.txt marks the one instance static and nothing else references it.
 * `short int` because every ROM access is an `lh`/`sh`. */
typedef struct                      /* 0x2 */
{
    /* 0x0 */ short int fade_timer;
} GAMEOVER_FADE_CTRL;

static GAMEOVER_FADE_CTRL gameover_fade_ctrl;               /* sbss 3f4c90 */

static void   GameOverFadeCtrlInit(void);
static void   GameOverFadeMain(void);
static void   GameOverFadeDispMain(void);
static u_char GameOverFadeAnimCtrl(short int timer);

/* ==========================================================================
 *  The fade to black
 * ======================================================================== */

static void GameOverFadeCtrlInit(void)                                  /* 91 */
{
    gameover_fade_ctrl.fade_timer = 0;                                  /* 94 */
}

/* The counter is advanced here rather than by the alpha helper, so the phase
 * change and the ramp stay in step even on a frame that does not draw. */
static void GameOverFadeMain(void)                                      /* 101 */
{
    gameover_fade_ctrl.fade_timer++;                                    /* 104 */

    if (gameover_fade_ctrl.fade_timer >= GAMEOVER_FADE_TIME) {          /* 106 */
        SetNextGPhase(GID_STORY_GAMEOVER_MOVIE);                        /* 107 */
    }
}

/* `alpha` leaves no stab -- the value goes straight from one call's return
 * into the next call's argument, so GCC coalesced it away.  The two $LM
 * markers (123 and 126) are what say it was two statements in the source;
 * savepoint.o's SavePointFadeInDispMain() is the same shape and the same
 * empty local list. */
static void GameOverFadeDispMain(void)                                  /* 116 */
{
    u_char alpha;

    alpha = GameOverFadeAnimCtrl(gameover_fade_ctrl.fade_timer);        /* 123 */

    GameOverScreenBgDisp(alpha, 0, 0, 0, GAMEOVER_BG_PRI);              /* 126 */
}

/* Past the end of the ramp Anim2D_CalcNowAlpha() would find no entry and
 * return 0x80 anyway; the early-out just saves the table walk. */
static u_char GameOverFadeAnimCtrl(short int timer)                     /* 136 */
{
    /* .rodata, so const in the source -- globals.txt never prints const, and
     * .rodata vs .data is the only tell. */
    static const ALPHA_ANIM_TBL fade_alpha_tbl[2] =                     /* rdata 3b3c70 */
    {
        {  0, 128,  0, GAMEOVER_FADE_TIME },
        { -1,  -1, -1, -1 }                     /* start_time -1 terminates */
    };

    u_char alpha = 128;                                                 /* 145 */

    if (timer < GAMEOVER_FADE_TIME) {                                   /* 147 */
        alpha = Anim2D_CalcNowAlpha(fade_alpha_tbl, timer);             /* 151 */
    }

    return alpha;                                                       /* 155 */
}

/* ==========================================================================
 *  The black quad itself
 * ======================================================================== */

/* gameover_bg's `pri` (160) is copied into dsq by CopySqrDToSqr() and then
 * immediately overwritten by the caller's, so only its w/h/x/y/rgb survive.
 * Kept as found. */
void GameOverScreenBgDisp(u_char alpha, u_char r, u_char g, u_char b,
                          u_int pri)                                    /* 166 */
{
    DISP_SQAR dsq;
    /* A .rodata blob copied into the frame, not a file-scope table --
     * globals.txt is right to omit it. */
    SQAR_DAT  gameover_bg = { 640, 448, 0, 0, GAMEOVER_BG_PRI,
                              255, 255, 255, 0 };                       /* 168 */
    int       i;

    CopySqrDToSqr(&dsq, &gameover_bg);                                  /* 173 */

    dsq.alpha = alpha;                                                  /* 174 */

    for (i = 0; i < 4; i++) {                                           /* 175 */
        dsq.r[i] = r;
        dsq.g[i] = g;
        dsq.b[i] = b;
    }

    dsq.pri = pri;  dsq.z = 0xfffff - (pri & 0xfffff);                  /* 176 */

    DispSqrD(&dsq);                                                     /* 180 */
}

/* ==========================================================================
 *  GID_STORY_GAMEOVER_EFF -- one last frame of the room
 * ======================================================================== */

void init_Story_GameOver_Eff(void)                                      /* 192 */
{
}

GPHASE_ENUM one_Story_GameOver_Eff(GPHASE_ENUM dummy)                   /* 195 */
{
    (void)dummy;

    PlayerMainCmn(1);                                                   /* 196 */
    SisterMain();                                                       /* 199 */
    AutoEnemyMain();                                                    /* 202 */
    map_bgmMain();                                                      /* 204 */
    MhCtlMain(GetPlyrAreaNo());                                         /* 207 */
    IngameCameraMain();                                                 /* 210 */

    PlayData_PlayTimeCount();                                           /* 215 */

    EnemyMotionWork();                                                  /* 217 */
    sis_mdlMotionWork();                                                /* 218 */

    MapFogProc(GetPlyrAreaNo(), (int)(short)plyr_wrk.cmn_wrk.floor,
               plyr_wrk.cmn_wrk.mbox.pos);                              /* 221 */
    gra3dDraw();                                                        /* 222 */

    InitEffectsEF();                                                    /* 224 */
    EffectControl(5);                                                   /* 225 */
    BrightnessAdjustmentFilterDraw();                                   /* 226 */

    Graph2dMain();                                                      /* 228 */

    SetNextGPhase(GID_STORY_GAMEOVER_FADE);                             /* 231 */

    return GPHASE_CONTINUE;                                             /* 233 */
}

void end_Story_GameOver_Eff(void)                                       /* 236 */
{
}

/* ==========================================================================
 *  GID_STORY_GAMEOVER_FADE -- the same frame, going black
 * ======================================================================== */

void init_Story_GameOver_Fade(void)                                     /* 242 */
{
    GameOverFadeCtrlInit();                                             /* 244 */
}

/* The _EFF body with the phase change replaced by the fade's own two calls. */
GPHASE_ENUM one_Story_GameOver_Fade(GPHASE_ENUM dummy)                  /* 247 */
{
    (void)dummy;

    PlayerMainCmn(1);                                                   /* 248 */
    SisterMain();                                                       /* 251 */
    AutoEnemyMain();                                                    /* 254 */
    map_bgmMain();                                                      /* 256 */
    MhCtlMain(GetPlyrAreaNo());                                         /* 259 */
    IngameCameraMain();                                                 /* 262 */

    PlayData_PlayTimeCount();                                           /* 267 */

    EnemyMotionWork();                                                  /* 269 */
    sis_mdlMotionWork();                                                /* 270 */

    MapFogProc(GetPlyrAreaNo(), (int)(short)plyr_wrk.cmn_wrk.floor,
               plyr_wrk.cmn_wrk.mbox.pos);                              /* 273 */
    gra3dDraw();                                                        /* 274 */

    InitEffectsEF();                                                    /* 276 */
    EffectControl(5);                                                   /* 277 */
    BrightnessAdjustmentFilterDraw();                                   /* 278 */

    Graph2dMain();                                                      /* 280 */

    GameOverFadeMain();                                                 /* 283 */
    GameOverFadeDispMain();                                             /* 286 */

    return GPHASE_CONTINUE;                                             /* 288 */
}

void end_Story_GameOver_Fade(void)                                      /* 291 */
{
}

/* ==========================================================================
 *  GID_STORY_GAMEOVER_MOVIE
 * ======================================================================== */

/* Every stream is stopped rather than faded: the screen is already black by
 * the time this runs, and the movie brings its own audio. */
void init_Story_GameOver_Movie(void)                                    /* 297 */
{
    StreamAutoAllStop();                                                /* 299 */

    InitMovieWithTitle(GAMEOVER_MOVIE_SCENE, 1);                        /* 301 */
}

GPHASE_ENUM one_Story_GameOver_Movie(GPHASE_ENUM dummy)                 /* 304 */
{
    (void)dummy;

    if (PlayMovieWithTitle() != 0) {                                    /* 305 */
        SetNextGPhase(GID_GAMEOVER_MENU_TOP);                           /* 306 */
    }

    return GPHASE_CONTINUE;                                             /* 309 */
}

void end_Story_GameOver_Movie(void)                                     /* 312 */
{
    EndMovieWithTitle();                                                /* 313 */
}
