// FILE: /home/zero_rom/zero2np/src/graphics/scene/IngameScene.c
//
// In-game scene / movie playback phase callbacks.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "IngameScene.h"
#include "../../main/phasefunc.h"    // GPHASE_ENUM + this module's phase-callback prototypes
#include "../../common/heapctrl.h"   // SAFE_MALLOC / heapCtrlFree
#include "../../common/mem_util.h"
#include "../../common/variable.h"   // plyr_wrk
#include "../effect/effect.h"        // SetDebugMenuSwitch
#include "../../ingame/ingame.h"     // IngameCameraMain / IngameDrawSub / IngameSceneReq / IngameLoopSE*
#include "../../ingame/event/prg/ev_disp.h"  // EvChapterDispEndRelease / EvDisp2DEndReq
#include "../../ingame/map/MapDoor.h"
#include "../../ingame/map/MapLoad.h"
#include "../../ingame/map/MapObjReg.h"
#include "../../ingame/map/MhCtl.h"          // MhCtlGetRoomNo
#include "../../ingame/plyr/player.h"        // GetPlyrAreaNo / SetPlyrFinderQEnd
#include "../../system/eeiop/cddat.h"        // GetFileSize
#include "../../system/eeiop/stream_auto.h"  // StreamAutoIsAllStop / StreamAutoSetPlayNum / ...ExclusiveMode
#include "../../system/os/eecdvd.h"          // LoadReq / IsLoadEndAll
#include "../../system/os/system.h"          // GetSystemHeapWrkP
#include "../movie/movie.h"                  // *MovieWithTitle / MovieCountGet
#include "scene.h"
#include "scene_effect.h"                    // SceneEffectEnd

static INGAME_SCENE_CTRL ingame_scene;      /* data 2c3e10 */

/* Scene-effect script for the movie currently playing.  Allocated by
 * init_Story_Movie_PreLoad(), released by end_Story_Movie_Main(). */
static u_int *pSceneMovieEffect;            /* sdata 3eec40 */

/* --------------------------------------------------------------------------
 *  IngameSceneInit
 *
 *  Latches the scene number and picks the entry phase.  The chapter caption
 *  and any 2D overlay are torn down first so they do not survive into the
 *  scene.
 * ------------------------------------------------------------------------ */
GPHASE_ID_ENUM IngameSceneInit(int scene_no)
{                                                                       /* 106 */
    ingame_scene.SceneNo = scene_no;                                    /* 110 */

    EvChapterDispEndRelease();                                          /* 114 */
    EvDisp2DEndReq(0);                                                  /* 115 */

    if (SceneDecisionMovie(scene_no) != 0)                              /* 117 */
    {
        return GID_STORY_MOVIE_PRELOAD;
    }

    return GID_STORY_SCENE_PRELOAD;
}

/* --------------------------------------------------------------------------
 *  IngameSceneRoomLoadCheck
 *
 *  Non-zero when the room needs no load.  A negative room number means "no
 *  room", which is trivially satisfied.
 *
 *  The loop polarity is the ROM's and is deliberately preserved: it clears
 *  ret on the *first* buffer that does not hold room_no, so the answer is
 *  "yes" only when both buffers hold it.  That is stricter than the obvious
 *  "is it resident in either buffer?" reading, and it costs a redundant
 *  MapLoadMoveRoom() when the room is already in one buffer -- but it never
 *  skips a load that is actually needed, so the phase machine still behaves.
 *  Do not flip it without deciding that deviation deliberately.
 * ------------------------------------------------------------------------ */
static int IngameSceneRoomLoadCheck(int room_no)
{
    int i;                                                              /* 139 */
    int ret = 1;                                                        /* 140 */

    if (room_no >= 0)                                                   /* 143 */
    {
        for (i = 0; i < 2; i++)                                         /* 144 */
        {
            if (room_no != MapLoadGetRoomNo4BuffID(i))                  /* 145 */
            {
                ret = 0;                                                /* 146 */
                break;
            }
        }                                                               /* 149 */
    }

    return ret;                                                         /* 152 */
}

/* --------------------------------------------------------------------------
 *  IngameSceneLoadRoom
 *
 *  Drives the sub room in first, then the main room, one MapLoad request at a
 *  time.  Answers non-zero while still busy.
 *
 *  SceneRoomNoGet() / SceneSubRoomNoGet() are re-called rather than cached;
 *  that is the ROM's shape (its only local is `ret`) and the getters are pure
 *  reads of the scene header.
 * ------------------------------------------------------------------------ */
static int IngameSceneLoadRoom(INGAME_SCENE_CTRL *pIngameScene)
{                                                                       /* 162 */
    int ret = 1;

    switch (pIngameScene->RoomLoadStatus)                               /* 166 */
    {
    case INGAME_SCENE_ROOM_LOAD_WAIT:
        if (MapLoadCheckLoadNow() != 0)                                 /* 168 */
        {
            break;
        }

        pIngameScene->RoomLoadStatus = INGAME_SCENE_ROOM_SUB_LOAD_REQ;  /* 169 */
        /* fall through */

    case INGAME_SCENE_ROOM_SUB_LOAD_REQ:
        if (IngameSceneRoomLoadCheck(SceneSubRoomNoGet()) == 0)         /* 176 */
        {
            MapLoadMoveRoom(SceneSubRoomNoGet());                       /* 177 */
            pIngameScene->RoomLoadStatus = INGAME_SCENE_ROOM_SUB_LOAD;  /* 178 */
        }
        else
        {
            pIngameScene->RoomLoadStatus = INGAME_SCENE_ROOM_MAIN_LOAD_REQ; /* 183 */
        }
        break;

    case INGAME_SCENE_ROOM_SUB_LOAD:
        if (MapLoadCheckLoadNow() != 0)                                 /* 186 */
        {
            break;
        }

        pIngameScene->RoomLoadStatus = INGAME_SCENE_ROOM_MAIN_LOAD_REQ; /* 187 */
        /* fall through */

    case INGAME_SCENE_ROOM_MAIN_LOAD_REQ:
        if (IngameSceneRoomLoadCheck(SceneRoomNoGet()) == 0)            /* 193 */
        {
            MapLoadMoveRoom(SceneRoomNoGet());                          /* 194 */
            pIngameScene->RoomLoadStatus = INGAME_SCENE_ROOM_MAIN_LOAD; /* 195 */
        }
        else
        {
            ret = 0;
        }
        break;

    case INGAME_SCENE_ROOM_MAIN_LOAD:
        ret = (MapLoadCheckLoadNow() != 0);                             /* 203 */
        break;
    }

    MapLoadMain();                                                      /* 212 */

    if (ret == 0)                                                       /* 214 */
    {
        /* Both rooms are resident -- pre-render the doors between them so the
         * first drawn frame of the scene does not pop. */
        if (SceneSubRoomNoGet() != -1)                                  /* 216 */
        {
            MapDoorAllPreRender(SceneRoomNoGet());                       /* 217 */
        }
    }

    return ret;                                                         /* 221 */
}

/* --------------------------------------------------------------------------
 *  IngameSceneDrawRoomInit
 *
 *  Marks the scene's rooms drawable.  MapObjRegSetSceneLoad() brackets the
 *  work so the object registration inside knows it is running for a scene and
 *  not for ordinary play.
 * ------------------------------------------------------------------------ */
static void IngameSceneDrawRoomInit(void)
{                                                                       /* 228 */
    MapObjRegSetSceneLoad(1);                                           /* 230 */

    MapLoadSetNowRoom(SceneRoomNoGet());                                /* 232 */

    if (SceneRoomNoGet() >= 0)                                          /* 234 */
    {
        MapLoadSetDrawFlg3(SceneRoomNoGet(), 1);                        /* 235 */
    }

    if (SceneSubRoomNoGet() >= 0)                                       /* 237 */
    {
        MapLoadSetDrawFlg3(SceneSubRoomNoGet(), 1);                     /* 238 */
    }

    MapObjRegSetSceneLoad(0);                                           /* 242 */
}

/* --------------------------------------------------------------------------
 *  init_Story_Scene / end_Story_Scene
 *
 *  Outer bracket of the whole scene phase group.  Reserves the 2 MB staging
 *  buffer the scene data is read into, and puts streaming audio into
 *  exclusive mode (one slot) so a scene's own audio is not fighting the map
 *  BGM.
 * ------------------------------------------------------------------------ */
void init_Story_Scene(void)
{                                                                       /* 248 */
    INGAME_SCENE_CTRL *pIngameScene = &ingame_scene;                    /* 249 */

    pIngameScene->pLoadAdrs = (u_int *)mem_utilGetMem(0x200000);        /* 251 */
    StreamAutoSetExclusiveMode(1, 3);                                   /* 252 */
}

void end_Story_Scene(void)
{                                                                       /* 255 */
    INGAME_SCENE_CTRL *pIngameScene = &ingame_scene;                    /* 256 */

    if (pIngameScene->pLoadAdrs != (u_int *)0)                          /* 258 */
    {
        mem_utilFreeMem(pIngameScene->pLoadAdrs);                       /* 259 */
        pIngameScene->pLoadAdrs = (u_int *)0;                           /* 260 */
    }

    StreamAutoSetExclusiveMode(0, 3);                                   /* 262 */
}

GPHASE_ENUM pre_Story_Scene(GPHASE_ENUM dummy)
{
    (void)dummy;

    return GPHASE_CONTINUE;                                             /* 266 */
}

GPHASE_ENUM after_Story_Scene(GPHASE_ENUM dummy)
{
    (void)dummy;

    return GPHASE_CONTINUE;                                             /* 270 */
}

/* --------------------------------------------------------------------------
 *  init_Story_Scene_PreLoad / end_Story_Scene_PreLoad
 * ------------------------------------------------------------------------ */
void init_Story_Scene_PreLoad(void)
{
    INGAME_SCENE_CTRL *pIngameScene = &ingame_scene;                    /* 278 */

    pIngameScene->Status = INGAME_SCENE_DATA_LOAD_REQ;                  /* 280 */
    pIngameScene->RoomLoadStatus = INGAME_SCENE_ROOM_LOAD_WAIT;         /* 281 */
}

void end_Story_Scene_PreLoad(void)
{
}                                                                       /* 285 */

/* --------------------------------------------------------------------------
 *  one_Story_Scene_PreLoad
 *
 *  Loads the scene script, then whatever rooms it needs.  The camera and the
 *  sub-draw keep running throughout so the frame behind the fade is still
 *  being produced.
 * ------------------------------------------------------------------------ */
GPHASE_ENUM one_Story_Scene_PreLoad(GPHASE_ENUM dummy)
{                                                                       /* 287 */
    INGAME_SCENE_CTRL *pIngameScene = &ingame_scene;                    /* 288 */

    (void)dummy;

    IngameCameraMain();                                                 /* 291 */
    IngameDrawSub();                                                    /* 292 */

    switch (pIngameScene->Status)                                       /* 294 */
    {
    case INGAME_SCENE_DATA_LOAD_REQ:
        InitSceneWork();                                                /* 296 */
        pIngameScene->Status = INGAME_SCENE_DATA_LOAD_WAIT;             /* 297 */
        /* fall through */

    case INGAME_SCENE_DATA_LOAD_WAIT:
        if (SceneAllLoad(pIngameScene->SceneNo, pIngameScene->pLoadAdrs) != 0) /* 301 */
        {
            if ((IngameSceneRoomLoadCheck(SceneRoomNoGet()) != 0) &&    /* 302 */
                (IngameSceneRoomLoadCheck(SceneSubRoomNoGet()) != 0))
            {
                SetNextGPhase(GID_STORY_SCENE_MAIN);                    /* 306 */
            }
            else
            {
                pIngameScene->Status = INGAME_SCENE_ROOM_LOAD;          /* 313 */
            }
        }
        break;

    case INGAME_SCENE_ROOM_LOAD:
        if (IngameSceneLoadRoom(pIngameScene) == 0)                     /* 316 */
        {
            SetNextGPhase(GID_STORY_SCENE_MAIN);                        /* 318 */
        }
        break;
    }

    return GPHASE_CONTINUE;                                             /* 327 */
}

/* --------------------------------------------------------------------------
 *  init_Story_Scene_Main / end_Story_Scene_Main
 * ------------------------------------------------------------------------ */
void init_Story_Scene_Main(void)
{
    IngameSceneDrawRoomInit();                                          /* 332 */
    SceneInitializeIngame();                                            /* 333 */

    SetDebugMenuSwitch(0);                                              /* 335 */
    IngameLoopSEPause();                                                /* 336 */
}

void end_Story_Scene_Main(void)
{
    SceneEndProc();                                                     /* 342 */
    SetDebugMenuSwitch(1);                                              /* 343 */
    IngameLoopSERestart();                                              /* 344 */

    /* -2 is MhCtl's "not in any room" answer.  When the player did land in a
     * room, the *other* room buffer is the one the scene left force-drawn, so
     * put it back under normal draw control.  Kept as one nested expression
     * split over two lines: the ROM has no local here (functions.txt), and
     * that is how its 350 / 352 line attribution falls out. */
    if (MhCtlGetRoomNo((int)(short)plyr_wrk.cmn_wrk.floor,               /* 348 */
                       plyr_wrk.cmn_wrk.mbox.pos) != -2)
    {
        MapLoadSetDrawFlg2(MapLoadGetBuffID(GetPlyrAreaNo()) ^ 1,       /* 350 */
                           2);                                          /* 352 */
    }
}

/* --------------------------------------------------------------------------
 *  one_Story_Scene_Main
 * ------------------------------------------------------------------------ */
GPHASE_ENUM one_Story_Scene_Main(GPHASE_ENUM dummy)
{                                                                       /* 357 */
    INGAME_SCENE_CTRL *pIngameScene = &ingame_scene;                    /* 358 */

    (void)dummy;

    SceneDraw(pIngameScene->SceneNo);                                   /* 360 */

    if (SceneIsEnd() != 0)                                              /* 362 */
    {
        /* These three scenes leave the player holding the camera obscura up;
         * drop the finder without the usual fade before handing control back. */
        if ((pIngameScene->SceneNo == 15) ||                            /* 363 */
            (pIngameScene->SceneNo == 54) ||
            (pIngameScene->SceneNo == 9))
        {
            SetPlyrFinderQEnd();                                        /* 366 */
        }

        IngameSceneReq(-1);                                             /* 368 */

        SetNextGPhase(IngameDecideNextPhase());                         /* 373 */
    }

    return GPHASE_CONTINUE;                                             /* 376 */
}

/* Streams are throttled to a single slot with a short fade for the duration of
 * a movie, and released again on the way out. */
void init_Story_Movie(void)
{                                                                       /* 380 */
    StreamAutoSetPlayNum(0, 3);                                         /* 381 */
}

void end_Story_Movie(void)
{                                                                       /* 384 */
    StreamAutoSetPlayNum(2, 100);                                       /* 385 */
}

GPHASE_ENUM pre_Story_Movie(GPHASE_ENUM dummy)
{
    (void)dummy;

    return GPHASE_CONTINUE;                                             /* 389 */
}

GPHASE_ENUM after_Story_Movie(GPHASE_ENUM dummy)
{
    (void)dummy;

    return GPHASE_CONTINUE;                                             /* 393 */
}

/* The scene-effect script that runs alongside the movie -- lightning cues and
 * the like, keyed off the movie's frame counter.  It is loaded here so
 * playback never waits on disc. */
void init_Story_Movie_PreLoad(void)
{                                                                       /* 399 */
    int file_no;

    file_no = SceneEffectDataFileNoGet(ingame_scene.SceneNo);           /* 400 */

    pSceneMovieEffect = (u_int *)SAFE_MALLOC(GetSystemHeapWrkP(), (void *)0,
                                             GetFileSize(file_no));     /* 403 */

    if (pSceneMovieEffect != (u_int *)0)                                /* 405 */
    {
        LoadReq(file_no, (uintptr_t)pSceneMovieEffect);                 /* 406 */
    }
}                                                                       /* 407 */

void end_Story_Movie_PreLoad(void)
{
}                                                                       /* 412 */

/* Waits for two things before handing over to the player: every stream has
 * actually stopped (the movie brings its own audio), and the effect script has
 * finished loading.  The camera and the sub-draw keep running so the frame
 * behind the fade is still being produced. */
GPHASE_ENUM one_Story_Movie_PreLoad(GPHASE_ENUM dummy)
{                                                                       /* 415 */
    (void)dummy;

    IngameCameraMain();                                                 /* 416 */
    IngameDrawSub();

    if ((StreamAutoIsAllStop() != 0) && (IsLoadEndAll() != 0))          /* 418 */
    {
        SetNextGPhase(GID_STORY_MOVIE_MAIN);                            /* 419 */
    }

    return GPHASE_CONTINUE;                                             /* 422 */
}

void init_Story_Movie_Main(void)
{                                                                       /* 427 */
    InitMovieWithTitle(ingame_scene.SceneNo, 1);                        /* 428 */
    SetDebugMenuSwitch(0);                                              /* 431 */
    IngameLoopSEPause();                                                /* 433 */
}                                                                       /* 434 */

void end_Story_Movie_Main(void)
{
    EndMovieWithTitle();                                                /* 439 */
    SetDebugMenuSwitch(1);                                              /* 442 */
    IngameLoopSERestart();                                              /* 443 */

    if (pSceneMovieEffect != (u_int *)0)                                /* 444 */
    {
        heapCtrlFree(GetSystemHeapWrkP(), pSceneMovieEffect);
    }

    pSceneMovieEffect = (u_int *)0;
    SceneEffectEnd();                                                   /* 446 */
}

/* PlayMovieWithTitle() answers non-zero once the movie has finished.  In this
 * port it answers non-zero on the first call -- movie.c has no decoder yet --
 * so the scene is reported played and the phase machine moves straight on to
 * whatever IngameDecideNextPhase() picks. */
GPHASE_ENUM one_Story_Movie_Main(GPHASE_ENUM dummy)
{
    (void)dummy;

    if (PlayMovieWithTitle() != 0)                                      /* 450 */
    {
        IngameSceneReq(-1);                                             /* 451 */

        SetNextGPhase(IngameDecideNextPhase());                         /* 456 */
    }

    SceneMovieEffectMain(MovieCountGet(), pSceneMovieEffect);           /* 459 */

    return GPHASE_CONTINUE;                                             /* 461 */
}
