// FILE: /home/zero_rom/zero2np/src/outgame/title_room.c
//
// PORT ADDITION -- the title screen's room backdrop.  title_room.h says what it
// is and how it hangs off the ROM's title; this is how it works.
//
// Nothing here is the ROM's.  It is built out of paths that already work
// outside normal play rather than invented, the same way debug/motion_viewer.c
// is:
//
//     load     MapLoadInit(room), then MapLoadMain() until it answers 0 --
//              scn_test.c's SceneTestLoad() (641/652).  The furniture,
//              doors and put-items register themselves on the way out
//              (MapLoadInitRoom -> MapLoadSetDrawFlg2 -> MapDrawInitFurn).
//     update   MhCtlMain(room): MapObjProc, MapAnimProc, the cloth, the
//              loader pump -- what one_Story_Normal() runs (885).
//     draw     MapFogProc, gra3dDraw, then the effect passes and the
//              brightness filter -- IngameDrawSub() (795..816), minus the
//              finder, the fade and the event display.
//     release  MapLoadDeleteRoomAll + MapDoorRelease + EffectSndEnd -- what
//              end_Story_Main() does with MapDrawDeleteRoomAll (719..727).
//
// THE SCENES are miopan_config.title.scenes[]: a room, two camera points and
// a path each.  A visit opens on one at random among the enabled, and after
// `passes` runs of its path (or on the next visit) the veil closes, another is
// picked -- never the same one twice running while there is a choice -- and it
// comes up out of black.  A scene in another room releases and requests the
// new one in the same frame, so the title's own picture never shows between.
// The pick has its own generator: MioPan_Rand() is the ROM's rand() stream.
//
// THE ROOM HAS TO BE GONE BEFORE IngameWrkInit() RUNS.  Every way off the
// title into a game resets the in-game state wholesale -- New Game, chapter
// select, the load screen as it opens, the mission menus, the event browser's
// chapter start -- and IngameWrkInit() clears MapPut, the effects and the
// cloth under a room it does not know is there.  The effects' sound list is
// re-initialised rather than stopped, so a looping cue (a room's river) would
// play on into the game, and deleting the room afterwards would walk handles
// already freed.  So IngameWrkInit() calls TitleRoomBeforeIngameReset() first
// (a one-line PORT hook in ingame.c), the room lets go while its state is
// still whole, and where the title carries on -- the load screen -- it loads
// again the frame after, over the reset.  The setup menus draw their own art,
// and the room stays released under them.
//
// THE PLAYER IS NOT THERE.  The room draw reads plyr_wrk in three places -- the
// flashlight MapDrawRoomOne() lights with, the area PlyrOutsideCheck() asks
// MapFogProc() about, and the shadow pass -- and the 3D sound a room's water
// and torches play needs a listener, which in a game IngameCameraMain() sets
// every frame and on the title nothing does (Snd3DCalc() asserts without
// one).  The listener follows the title's camera.  On the title plyr_wrk is
// whatever the last game or boot left.  TitleRoomDraw() puts in what a room
// with nobody in it would have for the length of the draw and puts the old
// values back, so nothing outside this file ever sees the difference.
//
// THE MUSIC is the ROM's own title stream id: init_Title_Mode() and five child
// phases start BGM000 and store its id with SetTitleStreamID(), and every
// keep-alive asks StreamAutoIsPlaying(GetTitleStreamID()).  A chosen track is
// started looped and stored there instead, so each of those finds it playing
// and leaves it alone.  Album and gallery still swap in their own and put
// BGM000 back on the way out; the next frame here notices the id is not ours
// and swaps it again.

#include "title_room.h"

#include "title.h"                              // SetTitleBgSendLock / Get/SetTitleStreamID
#include "title_movie.h"                        // LockMoveTitleMovie
#include "../common/variable.h"                 // plyr_wrk
#include "../debug/camera_menu.h"               // DebugCameraMenu / DebugFreeCamera / DebugCameraMenuMain
#include "../graphics/effect/effect.h"          // InitEffectsEF / EffectControl / BrightnessAdjustmentFilterDraw
#include "../graphics/effect/effect_sub.h"      // EffectSndEnd
#include "../graphics/graph2d/g2d_draw.h"       // SQAR_DAT / DISP_SQAR / CopySqrDToSqr / DispSqrD
#include "../graphics/graph3d/g3dLight.h"       // G3DLIGHTTYPE_FORCE_DWORD
#include "../graphics/graph3d/gra3d.h"          // camera / gra3dDraw
#include "../graphics/graph3d/gra3dSGDData.h"   // sgdGetBoundingBox
#include "../graphics/motion/accessory.h"       // acsInitRopeWork
#include "../graphics/obj_draw_ctrl.h"          // obj_draw_ctrl
#include "../ingame/ingame.h"                   // SetIngameListnerInfo
#include "../ingame/map/MapDoor.h"              // MapDoorRelease
#include "../ingame/map/MapFog.h"               // MapFogProc
#include "../ingame/map/MapLoad.h"              // MapLoadInit / Main / DeleteRoomAll / ...
#include "../ingame/map/MapObj.h"               // MapObjItemOff / On
#include "../ingame/map/MhCtl.h"                // MhCtlMain
#include "../ingame/map/RegDat.h"               // RegDatGetHead / MB_OUT_HEAD
#include "../main/gphase.h"                     // GPhaseIsActive / GID_*
#include "../main/phasefunc.h"                  // init_Title_Mode / pre_Title_Mode / end_Title_Mode
#include "../miopan/miopan_config.h"            // miopan_config.title
#include "../miopan/os/miopan_time.h"           // MioPan_GetPerformanceCounter
#include "../system/eeiop/cddat.h"              // GetFileSize / BGM* / KAN*
#include "../system/eeiop/stream_auto.h"        // StreamAutoPlay / FadeOut / IsPlaying / Fade

#include <SDL3/SDL_log.h>

#include <math.h>
#include <string.h>

#define TITLE_ROOM_NUM          66      /* RY00_PK2 .. RY13_PK2              */

#define TITLE_ROOM_PI           3.14159265358979f

/* The ROM's BGM level and priority, from init_Title_Mode() (569). */
#define TITLE_ROOM_MUSIC_VOL    0x3200
#define TITLE_ROOM_MUSIC_PRI    0xc
/* Frames: a chosen track comes in softly when it replaces another. */
#define TITLE_ROOM_MUSIC_IN     30

/* Seconds for the room to come up out of black once it is in. */
#define TITLE_ROOM_FADE_SECONDS 1.0f

/* A frame longer than this is a load or a stall, not time to show the room
 * passing -- the camera would jump. */
#define TITLE_ROOM_MAX_STEP     0.1f

/* World units from a captured eye to the target stored with it: about two
 * character heights. */
#define TITLE_ROOM_CAPTURE_REACH 2000.0f

typedef enum
{
    TR_IDLE,                    /* nothing resident                          */
    TR_LOADING,                 /* MapLoadInit() issued                      */
    TR_READY,                   /* MapLoadMain() has answered 0              */
} TR_LOAD;

typedef struct
{
    int      active;            /* GID_TITLE_MODE is running                 */
    TR_LOAD  load;
    int      scene;             /* miopan_config.title.scenes[] on screen    */
    int      visited;           /* a scene has been picked before            */
    int      leaving;           /* fading out to the next scene              */
    int      held;              /* the overlay is editing: no moving on      */
    uint64_t rng;               /* the title's own; the ROM's rand() is untouched */
    int      room;              /* resident or loading                       */
    int      floor;             /* its first registration file's kai         */
    int      reload;            /* the overlay asked                         */
    int      failed;            /* a room that did not come up, -1 none      */
    int      released;          /* the current screen took the room away     */
    int      bg_locked;         /* this file holds SetTitleBgSendLock(1)     */
    float    fade;              /* 0 black .. 1 the room                     */
    double   path_time;         /* seconds along the camera path             */
    int      paused;
    int      preview;           /* -1 path, 0 / 1 hold that point            */
    int      fly;
    uint64_t last_tick;
    int      music_id;          /* our StreamAuto id, -1 none                */
    int      music_file;        /* what it plays                             */
    int      music_vol;         /* the level it was last set to              */
    uint64_t music_started;     /* ms; a track that will not start stops being retried */
} TITLE_ROOM_WRK;

/* Zero is "not on the title"; init_TitleRoom_Mode() sets the rest. */
static TITLE_ROOM_WRK title_room;

static void TitleRoomRelease(void);

/* The scene on screen, or loading to be. */
static MioPan_ConfigTitleScene *TitleRoomScene(void)
{
    MioPan_ConfigTitle *t = &miopan_config.title;

    if (title_room.scene < 0 || title_room.scene >= t->scene_count)
    {
        title_room.scene = 0;
    }

    return &t->scenes[title_room.scene];
}

// ──────────────────────────────────────────────────────────────────────
// The rooms.
//
// Named as the disc names their files.  The families are the house or area
// each prefix belongs to: RY is the village (rooms 0..12 are the ones
// MapSkyGetDatNowArea() gives a sky, and PlyrOutsideCheck() calls outdoors),
// OS / TB / RY(2) / KS the four houses, CH the rooms on floor 10.  The rooms
// the disc does not carry -- miopan_event_jump.cpp's area table finds no area
// in them -- are refused.

static const char *const title_room_code[TITLE_ROOM_NUM] =
{
    "RY00",  "RY01",  "RY02",  "RY03",  "RY04",  "RY05",  "RY06",  "RY07",
    "RY08",  "RY09",  "RY10",  "RY11",  "RY12",
    "ROS00", "ROS01", "ROS02", "ROS03", "ROS04", "ROS05",
    "RTB00", "RTB01", "RTB02", "RTB03", "RTB04", "RTB05", "RTB06", "RTB07",
    "RTB08", "RTB09", "RTB10",
    "RRY00", "RRY01", "RRY02", "RRY03", "RRY04", "RRY05", "RRY06", "RRY07",
    "RRY08", "RRY09",
    "RKS00", "RKS01", "RKS02", "RKS03", "RKS04", "RKS05", "RKS06", "RKS07",
    "RKS08", "RKS09", "RKS10", "RKS11", "RKS12", "RKS13", "RKS14",
    "RKH00", "RKC00", "RKR00",
    "RCH00", "RCH01", "RCH02", "RCH03", "RCH04", "RCH05", "RCH06",
    "RY13",
};

static const signed char title_room_missing[] = { 1, 3, 5, 7, 10, 19, 21, 63 };

int TitleRoomCount(void)
{
    return TITLE_ROOM_NUM;
}

const char *TitleRoomCode(int room)
{
    if ((unsigned int)room >= TITLE_ROOM_NUM)
    {
        return "?";
    }

    return title_room_code[room];
}

const char *TitleRoomPlace(int room)
{
    if (room < 0 || room >= TITLE_ROOM_NUM)
    {
        return "";
    }
    if (room <= 12 || room == 65)
    {
        return "Village";
    }
    if (room <= 18)
    {
        return "Osaka house";
    }
    if (room <= 29)
    {
        return "Tachibana house";
    }
    if (room <= 39)
    {
        return "Kiryu house";
    }
    if (room <= 57)
    {
        return "Kurosawa house";
    }

    return "Underground";
}

int TitleRoomIsOnDisc(int room)
{
    if ((unsigned int)room >= TITLE_ROOM_NUM)
    {
        return 0;
    }

    for (unsigned int i = 0; i < sizeof(title_room_missing); i++)
    {
        if (title_room_missing[i] == room)
        {
            return 0;
        }
    }

    /* +2 is the model pak MapLoadRegistReq() asks for first. */
    return GetFileSize(MapLoadGetLabel(room)) != 0 &&
           GetFileSize(MapLoadGetLabel(room) + 2) != 0;
}

// ──────────────────────────────────────────────────────────────────────
// The tracks.
//
// Every stream on the disc that is music or a room's ambience: the BGM*
// themes and the KAN* beds map_bgm.c gives rooms.  A note is given only where
// the file's own name says what it is.

typedef struct
{
    int         file;
    const char *name;
    const char *note;
} TITLE_ROOM_TRACK;

static const TITLE_ROOM_TRACK title_room_track[] =
{
    { 0,                      "Original",            "the title theme, as the game plays it" },
    { BGM000_TITLE_STR,       "BGM000_TITLE",        "title theme, looped" },
    { BGM004_MENU1_STR,       "BGM004_MENU1",        "menu" },
    { BGM005_MENU2_STR,       "BGM005_MENU2",        "Mission Mode menu" },
    { BGM010_OMAKE_STR,       "BGM010_OMAKE",        "extras" },
    { BGM009_START_STR,       "BGM009_START",        "" },
    { BGM006_CLEAR_STR,       "BGM006_CLEAR",        "game clear" },
    { BGM007_WIN_STR,         "BGM007_WIN",          "" },
    { BGM008_LOSE_STR,        "BGM008_LOSE",         "" },
    { BGM011_GAMEOVER_STR,    "BGM011_GAMEOVER",     "game over" },
    { BGM010_PRM90_STR,       "BGM010_PRM90",        "" },
    { KAN028_MURA1_STR,       "KAN028_MURA1",        "the village" },
    { KAN029_SANDOU_STR,      "KAN029_SANDOU",       "" },
    { KAN035_MENNIWA_STR,     "KAN035_MENNIWA",      "" },
    { KAN010_HASINO_UE_STR,   "KAN010_HASINO_UE",    "" },
    { KAN004_OUSAKA_STR,      "KAN004_OUSAKA",       "Osaka house" },
    { KAN006_TACHIBANA_STR,   "KAN006_TACHIBANA",    "Tachibana house" },
    { KAN005_KIRYUU_STR,      "KAN005_KIRYUU",       "Kiryu house" },
    { KAN007_KUROSAWA_STR,    "KAN007_KUROSAWA",     "Kurosawa house" },
    { KAN023_OKUNAI1_STR,     "KAN023_OKUNAI1",      "indoors" },
    { KAN021_HIROI_STR,       "KAN021_HIROI",        "" },
    { KAN022_HOSOI_STR,       "KAN022_HOSOI",        "" },
    { KAN044_CHIKA_STR,       "KAN044_CHIKA",        "underground" },
    { KAN018_SAE_STR,         "KAN018_SAE",          "" },
    { KAN003_KUREHA_STR,      "KAN003_KUREHA",       "" },
    { KAN002_KUTIKI_STR,      "KAN002_KUTIKI",       "" },
    { KAN025_MISONO_STR,      "KAN025_MISONO",       "" },
    { KAN020_TYOU_STR,        "KAN020_TYOU",         "" },
    { KAN032_OKYOU_STR,       "KAN032_OKYOU",        "" },
    { KAN033_NAZO_STR,        "KAN033_NAZO",         "" },
    { KAN036_NANIKA_STR,      "KAN036_NANIKA",       "" },
    { KAN_36_NANIKA2_STR,     "KAN_36_NANIKA2",      "" },
    { KAN037_CONTE_STR,       "KAN037_CONTE",        "" },
    { KAN017_RHYTHM_STR,      "KAN017_RHYTHM",       "" },
    { KAN_WASAN_00_STR,       "KAN_WASAN_00",        "" },
    { KAN_WASAN_01_STR,       "KAN_WASAN_01",        "" },
    { KAN_WASAN_02_STR,       "KAN_WASAN_02",        "" },
    { KAN_FUKAMITI_WASAN_STR, "KAN_FUKAMITI_WASAN",  "" },
    { KAN_NIEZA_STR,          "KAN_NIEZA",           "" },
    { KAN_UTURO_STR,          "KAN_UTURO",           "" },
    { KAN_AME_00_STR,         "KAN_AME_00",          "rain" },
    { KAN042_KAMINARI_STR,    "KAN042_KAMINARI",     "thunder" },
    { KAN043_RAKURAI_STR,     "KAN043_RAKURAI",      "lightning strike" },
};

#define TITLE_ROOM_TRACK_NUM ((int)(sizeof(title_room_track) / sizeof(title_room_track[0])))

int TitleRoomMusicCount(void)
{
    return TITLE_ROOM_TRACK_NUM;
}

int TitleRoomMusicFile(int index)
{
    return (index >= 0 && index < TITLE_ROOM_TRACK_NUM) ? title_room_track[index].file : 0;
}

const char *TitleRoomMusicName(int index)
{
    return (index >= 0 && index < TITLE_ROOM_TRACK_NUM) ? title_room_track[index].name : "?";
}

const char *TitleRoomMusicNote(int index)
{
    return (index >= 0 && index < TITLE_ROOM_TRACK_NUM) ? title_room_track[index].note : "";
}

int TitleRoomMusicIndex(int file)
{
    for (int i = 0; i < TITLE_ROOM_TRACK_NUM; i++)
    {
        if (title_room_track[i].file == file)
        {
            return i;
        }
    }

    return -1;
}

// ──────────────────────────────────────────────────────────────────────
// Where the title is.

static int TitleRoomBackdropOn(void)
{
    return miopan_config.title.backdrop == MIOPAN_TITLE_BACKDROP_ROOM &&
           TitleRoomIsOnDisc(TitleRoomScene()->room);
}

/* The setup menus are Story / Mission Mode, and the mission side resets the
 * in-game state (MissionSelInit -> MissionKeepSaveData + IngameWrkInit) under
 * its own full-screen art.  Everything else under GID_TITLE_MODE draws over
 * the title's backdrop. */
static int TitleRoomScreenTakesRoom(void)
{
    return GPhaseIsActive(GID_TITLE_SETUP);
}

/* Every screen whose phase keeps the title stream alive: the ones that call
 * StreamAutoPlay(BGM000_TITLE_STR...) when it stops, plus the top and the
 * frame-rate and chapter selects, which play under it.  Album, gallery and
 * setup play music of their own. */
static int TitleRoomScreenHasTitleMusic(void)
{
    return GPhaseIsActive(GID_TITLE_TOP) || GPhaseIsActive(GID_TITLE_MENU) ||
           GPhaseIsActive(GID_TITLE_NEWGAME) || GPhaseIsActive(GID_TITLE_LOADGAME) ||
           GPhaseIsActive(GID_TITLE_OPTION) || GPhaseIsActive(GID_TITLE_FRAMERATE_SEL) ||
           GPhaseIsActive(GID_TITLE_CHAPTER_SEL);
}

/* The backdrop has the screen: a room resident or loading. */
static int TitleRoomOwnsScreen(void)
{
    return title_room.load != TR_IDLE;
}

void TitleRoomBeforeIngameReset(void)
{
    if (title_room.active != 0 && title_room.load != TR_IDLE)
    {
        TitleRoomRelease();
    }
}

int TitleRoomHidesTitleArt(void)
{
    return title_room.active != 0 && TitleRoomOwnsScreen() != 0;
}

TITLE_ROOM_STATE TitleRoomGetState(void)
{
    if (title_room.active == 0 ||
        miopan_config.title.backdrop != MIOPAN_TITLE_BACKDROP_ROOM)
    {
        return TITLE_ROOM_OFF;
    }
    if (TitleRoomIsOnDisc(TitleRoomScene()->room) == 0)
    {
        return TITLE_ROOM_MISSING;
    }
    if (title_room.failed == TitleRoomScene()->room)
    {
        return TITLE_ROOM_MISSING;
    }
    if (title_room.released != 0)
    {
        return TITLE_ROOM_RELEASED;
    }
    if (title_room.load == TR_READY)
    {
        return TITLE_ROOM_SHOWN;
    }

    return TITLE_ROOM_LOADING;
}

// ──────────────────────────────────────────────────────────────────────
// Loading and releasing.

static void TitleRoomBeginLoad(void)
{
    title_room.room = TitleRoomScene()->room;
    title_room.floor = 11;
    title_room.fade = 0.0f;
    title_room.load = TR_LOADING;

    SDL_Log("[port] title room: loading %d (%s)", title_room.room,
            TitleRoomCode(title_room.room));

    MapLoadInit(title_room.room);
}

/* Drop what is resident.  Safe mid-load: a buffer whose registration files
 * have not landed yet has nothing to delete, and the loads still in flight
 * finish into memory the next MapLoadInit() writes over.
 *
 * The ropes -- hanging cloths, bell cords, anything MapObjReg.c registers
 * with acsRopeSetWork() -- are let go by hand.  MapDrawDeleteRoom() clears
 * the room's reg_id[] before MapObjBornDelete() reads it to find them, so a
 * room change never frees its rope slots: in a game IngameWrkInit() sweeps
 * them on every way in, but the title changes rooms on its own, and the
 * tenth rope overran rope_spring[]'s nine.  Nothing else on the title holds
 * one. */
static void TitleRoomRelease(void)
{
    if (title_room.load != TR_IDLE)
    {
        SDL_Log("[port] title room: releasing %d", title_room.room);

        MapLoadDeleteRoomAll();
        MapDoorRelease();
        EffectSndEnd();
        MapObjItemOn();
        acsInitRopeWork();
    }

    title_room.load = TR_IDLE;
    title_room.fade = 0.0f;
}

/* Place both points from the room model's bounding box: eye height in the
 * long side's first and last quarter, both looking at the middle, so any room
 * opens on something.  The box is in the model's own units; the room is
 * parked with g_matConvertSI2PS (25, -25, -25) plus its origin
 * (MapDrawCalcRoomCoord), and so is the box here.  Y grows downwards, so the
 * floor is the box's largest y. */
static void TitleRoomFrame(MioPan_ConfigTitleScene *t, const MLOAD_HEAD *hp)
{
    float bb[8][4];
    float lo[3] = {  1.0e30f,  1.0e30f,  1.0e30f };
    float hi[3] = { -1.0e30f, -1.0e30f, -1.0e30f };

    if (hp == NULL || hp->model_addr == 0)
    {
        return;
    }

    sgdGetBoundingBox((SGDFILEHEADER *)hp->model_addr, bb);

    for (int i = 0; i < 8; i++)
    {
        const float w[3] =
        {
             25.0f * bb[i][0] + hp->pos[0],
            -25.0f * bb[i][1] + hp->pos[1],
            -25.0f * bb[i][2] + hp->pos[2],
        };

        for (int k = 0; k < 3; k++)
        {
            lo[k] = fminf(lo[k], w[k]);
            hi[k] = fmaxf(hi[k], w[k]);
        }
    }

    const float floor_y = hi[1];
    const float height = hi[1] - lo[1];
    /* A standing character is about a thousand units tall; an eye a little
     * under that, or half the room in a low one. */
    const float eye_y = floor_y - fminf(height * 0.5f, 900.0f);
    const float cx = (lo[0] + hi[0]) * 0.5f;
    const float cz = (lo[2] + hi[2]) * 0.5f;
    const float dx = hi[0] - lo[0];
    const float dz = hi[2] - lo[2];

    if (dx >= dz)
    {
        t->a_eye[0] = lo[0] + dx * 0.25f;
        t->b_eye[0] = hi[0] - dx * 0.25f;
        t->a_eye[2] = t->b_eye[2] = lo[2] + dz * 0.15f;
    }
    else
    {
        t->a_eye[2] = lo[2] + dz * 0.25f;
        t->b_eye[2] = hi[2] - dz * 0.25f;
        t->a_eye[0] = t->b_eye[0] = lo[0] + dx * 0.15f;
    }
    t->a_eye[1] = t->b_eye[1] = eye_y;

    t->a_target[0] = t->b_target[0] = cx;
    /* Near level, a touch down: a tall room (an outdoor one's box takes in
     * the trees and roofs) would otherwise have it staring at the floor. */
    t->a_target[1] = t->b_target[1] = eye_y + fminf(height * 0.08f, 150.0f);
    t->a_target[2] = t->b_target[2] = cz;
    t->a_fov = t->b_fov = 50.0f;
    t->camera_set = 1;

    SDL_Log("[port] title room: framed %d, box (%.0f %.0f %.0f)..(%.0f %.0f %.0f)",
            title_room.room, (double)lo[0], (double)lo[1], (double)lo[2],
            (double)hi[0], (double)hi[1], (double)hi[2]);
}

static const MLOAD_HEAD *TitleRoomHead(void)
{
    const int buff_id = MapLoadGetBuffID(title_room.room);

    return buff_id >= 0 ? MapLoadGetHeadPtr(buff_id) : NULL;
}

static void TitleRoomLoadMain(void)
{
    if (MapLoadMain() != 0 || MapLoadCheckLoadNow() != 0)
    {
        return;
    }

    const MLOAD_HEAD *hp = TitleRoomHead();
    if (hp == NULL || hp->model_addr == 0)
    {
        SDL_Log("[port] title room: %d did not come up", title_room.room);
        TitleRoomRelease();
        title_room.failed = title_room.room;
        return;
    }

    /* The floor the fog asks the registration data about. */
    MB_OUT_HEAD *rp = RegDatGetHead((int)hp->reg_id[0]);
    title_room.floor = rp != NULL ? rp->kai : 11;

    /* Item sparkles mark pick-ups, and there is nobody here to pick them up --
     * what SceneInitializeIngame() does for the same reason. */
    MapObjItemOff();

    if (TitleRoomScene()->camera_set == 0)
    {
        TitleRoomFrame(TitleRoomScene(), hp);
    }

    title_room.load = TR_READY;
    title_room.path_time = 0.0;
    title_room.fade = 0.0f;
}

/* Keep what is resident in step with the settings and the screen. */
static void TitleRoomFollow(void)
{
    if (title_room.reload != 0)
    {
        TitleRoomRelease();
        title_room.reload = 0;
        title_room.failed = -1;
    }

    title_room.released = TitleRoomScreenTakesRoom();

    if (TitleRoomBackdropOn() == 0 || title_room.released != 0)
    {
        TitleRoomRelease();
        return;
    }

    if (title_room.load != TR_IDLE && title_room.room != TitleRoomScene()->room)
    {
        TitleRoomRelease();
    }

    if (title_room.load == TR_IDLE && title_room.failed != TitleRoomScene()->room)
    {
        TitleRoomBeginLoad();
    }
}

// ──────────────────────────────────────────────────────────────────────
// The camera.

static float TitleRoomEase(float x)
{
    if (TitleRoomScene()->easing == MIOPAN_TITLE_EASING_LINEAR)
    {
        return x;
    }

    return x * x * (3.0f - 2.0f * x);
}

/* Seconds in one whole cycle of the path. */
static double TitleRoomCycle(void)
{
    const MioPan_ConfigTitleScene *t = TitleRoomScene();
    const double travel = t->travel_seconds > 0.5f ? t->travel_seconds : 0.5;
    const double hold = t->hold_seconds > 0.0f ? t->hold_seconds : 0.0;

    switch (t->motion)
    {
    case MIOPAN_TITLE_MOTION_LOOP:
    case MIOPAN_TITLE_MOTION_ONCE:
        return hold * 2.0 + travel;

    default:
        return (hold + travel) * 2.0;
    }
}

/* How far from A to B the camera is at `time`, 0..1, eased. */
static float TitleRoomPathAt(double time)
{
    const MioPan_ConfigTitleScene *t = TitleRoomScene();
    const double travel = t->travel_seconds > 0.5f ? t->travel_seconds : 0.5;
    const double hold = t->hold_seconds > 0.0f ? t->hold_seconds : 0.0;
    const double cycle = TitleRoomCycle();
    double x;

    if (t->motion == MIOPAN_TITLE_MOTION_ONCE)
    {
        x = time < cycle ? time : cycle;
    }
    else
    {
        x = fmod(time, cycle);
    }

    if (x < hold)
    {
        return 0.0f;
    }
    x -= hold;
    if (x < travel)
    {
        return TitleRoomEase((float)(x / travel));
    }
    x -= travel;
    if (x < hold || t->motion != MIOPAN_TITLE_MOTION_PINGPONG)
    {
        return 1.0f;
    }
    x -= hold;

    return TitleRoomEase((float)(1.0 - x / travel));
}

static float TitleRoomLerp(float a, float b, float e)
{
    return a + (b - a) * e;
}

static void TitleRoomCameraMain(void)
{
    const MioPan_ConfigTitleScene *t = TitleRoomScene();

    if (title_room.fly != 0)
    {
        /* The free camera, steered by hand.  Its own switches are the in-game
         * Assists menu's, so they are put back as found; following the player
         * makes no sense with nobody here. */
        const int free_on = DebugCameraMenu.FreeCameraON;
        const int follow = DebugCameraMenu.PlayerFollowON;

        DebugCameraMenu.FreeCameraON = 1;
        DebugCameraMenu.PlayerFollowON = 0;
        DebugCameraMenuMain(1);
        DebugCameraMenu.FreeCameraON = free_on;
        DebugCameraMenu.PlayerFollowON = follow;
        return;
    }

    float e;
    if (title_room.preview == 0)
    {
        e = 0.0f;
    }
    else if (title_room.preview == 1)
    {
        e = 1.0f;
    }
    else
    {
        e = TitleRoomPathAt(title_room.path_time);
    }

    float eye[4];
    float target[4];
    for (int k = 0; k < 3; k++)
    {
        eye[k] = TitleRoomLerp(t->a_eye[k], t->b_eye[k], e);
        target[k] = TitleRoomLerp(t->a_target[k], t->b_target[k], e);
    }
    eye[3] = 1.0f;
    target[3] = 1.0f;

    const float fov = TitleRoomLerp(t->a_fov, t->b_fov, e);

    gra3dcamSetFov(fov * TITLE_ROOM_PI / 180.0f);
    gra3dcamSetRoll(0.0f);
    gra3dcamSetPosition(eye);
    gra3dcamSetTarget(target, 1);
    gra3dApplyCamera((GRA3DCAMERA *)0, 1);
}

// ──────────────────────────────────────────────────────────────────────
// The rotation.

/* xorshift64*, seeded off the clock the first time.  The scene pick must not
 * come out of MioPan_Rand(): that is the ROM's rand() stream, and a draw here
 * would shift every random value the game takes after it. */
static unsigned int TitleRoomRandom(void)
{
    uint64_t x = title_room.rng;

    if (x == 0)
    {
        x = MioPan_GetPerformanceCounter() | 1;
    }
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    title_room.rng = x;

    return (unsigned int)((x * 2685821657736338717ULL) >> 32);
}

/* The scenes the rotation may land on: enabled, and their room on the disc. */
static int TitleRoomPool(int *pool)
{
    const MioPan_ConfigTitle *t = &miopan_config.title;
    int n = 0;

    for (int i = 0; i < t->scene_count; i++)
    {
        if (t->scenes[i].enabled != 0 && TitleRoomIsOnDisc(t->scenes[i].room) != 0)
        {
            pool[n++] = i;
        }
    }

    return n;
}

/* A scene at random, never `avoid` while there is any other to pick.  With
 * none enabled, the first one -- the backdrop has to show something. */
static int TitleRoomPickScene(int avoid)
{
    int pool[MIOPAN_TITLE_SCENE_MAX];
    int n = TitleRoomPool(pool);

    if (n == 0)
    {
        return 0;
    }
    if (n > 1)
    {
        for (int i = 0; i < n; i++)
        {
            if (pool[i] == avoid)
            {
                pool[i] = pool[--n];
                break;
            }
        }
    }

    return pool[TitleRoomRandom() % (unsigned int)n];
}

/* Time to fade out: `passes` runs of the path, less the fade, so the black
 * lands as the last run ends. */
static int TitleRoomTimeToMove(void)
{
    const MioPan_ConfigTitle *t = &miopan_config.title;
    int pool[MIOPAN_TITLE_SCENE_MAX];

    if (title_room.held != 0 || t->change != MIOPAN_TITLE_CHANGE_PASS ||
        TitleRoomPool(pool) < 2)
    {
        return 0;
    }

    return title_room.path_time >=
           TitleRoomCycle() * t->passes - TITLE_ROOM_FADE_SECONDS;
}

/* Take up `scene`.  Another room is released and requested in one go, so
 * there is no frame in between for the title's own picture to show in. */
static void TitleRoomEnterScene(int scene)
{
    title_room.scene = scene;
    title_room.leaving = 0;
    title_room.path_time = 0.0;
    title_room.preview = -1;

    SDL_Log("[port] title room: scene %d, %s", scene, TitleRoomScene()->name);

    if (title_room.load == TR_IDLE)
    {
        return;
    }
    if (TitleRoomScene()->room != title_room.room)
    {
        TitleRoomRelease();
        TitleRoomBeginLoad();
    }
    else if (title_room.load == TR_READY && TitleRoomScene()->camera_set == 0)
    {
        TitleRoomFrame(TitleRoomScene(), TitleRoomHead());
    }
}

// ──────────────────────────────────────────────────────────────────────
// Drawing.

static void TitleRoomDraw(void)
{
    const OBJ_DRAW_FLG draw_flg = obj_draw_ctrl;
    const G3DLIGHTTYPE fl_type = plyr_wrk.fl.Type;
    const G3DLIGHTTYPE fl2_type = plyr_wrk.fl2.Type;
    const u_char area_no = plyr_wrk.cmn_wrk.pr_info.area_no;

    /* A room with nobody in it: no characters, no shadows cast from them, no
     * flashlight (MapLightSetPlayerReal() turns both lamps off on these two
     * types), and the area the fog's outdoor test reads is the room's own.
     * The layer switches the game itself flips -- a game over leaves only the
     * player -- are forced on for the room's parts. */
    obj_draw_ctrl.player = 0;
    obj_draw_ctrl.sister = 0;
    obj_draw_ctrl.enemy = 0;
    obj_draw_ctrl.shadow = 0;
    obj_draw_ctrl.room = 1;
    obj_draw_ctrl.obj = 1;
    obj_draw_ctrl.sky = 1;
    obj_draw_ctrl.effect = 1;
    plyr_wrk.fl.Type = G3DLIGHTTYPE_FORCE_DWORD;
    plyr_wrk.fl2.Type = G3DLIGHTTYPE_FORCE_DWORD;
    plyr_wrk.cmn_wrk.pr_info.area_no = (u_char)title_room.room;

    MhCtlMain(title_room.room);
    TitleRoomCameraMain();
    SetIngameListnerInfo();

    MapFogProc(title_room.room, title_room.floor, gra3dcamGetPosition());
    gra3dDraw();
    InitEffectsEF();
    EffectControl(5);
    EffectControl(7);
    BrightnessAdjustmentFilterDraw();
    EffectControl(8);

    obj_draw_ctrl = draw_flg;
    plyr_wrk.fl.Type = fl_type;
    plyr_wrk.fl2.Type = fl2_type;
    plyr_wrk.cmn_wrk.pr_info.area_no = area_no;
}

/* Black over the room and under everything the title draws after it: the
 * fade up out of black, and the dimming that keeps the logo readable. */
static void TitleRoomDrawVeil(void)
{
    const float dim = miopan_config.title.dim;
    const float black = 1.0f - (1.0f - dim) * title_room.fade;
    const int alpha = (int)(black * 128.0f + 0.5f);

    if (alpha <= 0)
    {
        return;
    }

    SQAR_DAT veil = { 640, 448, 0, 0, 0, 0, 0, 0, 0x80 };
    DISP_SQAR dsq;

    CopySqrDToSqr(&dsq, &veil);
    dsq.alpha = (u_char)(alpha > 0x80 ? 0x80 : alpha);
    DispSqrD(&dsq);
}

// ──────────────────────────────────────────────────────────────────────
// The music.

static int TitleRoomMusicVolume(void)
{
    return (int)(miopan_config.title.music_volume * (float)TITLE_ROOM_MUSIC_VOL + 0.5f);
}

static void TitleRoomMusicMain(void)
{
    const int file = miopan_config.title.music;

    if (file == 0 || TitleRoomMusicIndex(file) < 0)
    {
        /* The game's own: if ours is what the title is playing, hand the
         * stream back the way init_Title_Mode() starts it. */
        if (title_room.music_id >= 0)
        {
            if (GetTitleStreamID() == title_room.music_id &&
                TitleRoomScreenHasTitleMusic() != 0)
            {
                StreamAutoFadeOut(title_room.music_id, TITLE_ROOM_MUSIC_IN);
                SetTitleStreamID(StreamAutoPlay(BGM000_TITLE_STR, BGM000_TITLE_HXD,
                                                TITLE_ROOM_MUSIC_PRI, 0, 0,
                                                TITLE_ROOM_MUSIC_VOL, 0,
                                                (SND_3D_SET *)0));
            }
            title_room.music_id = -1;
        }
        return;
    }

    if (TitleRoomScreenHasTitleMusic() == 0)
    {
        return;
    }

    const int vol = TitleRoomMusicVolume();
    const int ours = title_room.music_id >= 0 &&
                     GetTitleStreamID() == title_room.music_id &&
                     title_room.music_file == file;

    if (ours != 0 && StreamAutoIsPlaying(title_room.music_id) != 0)
    {
        if (vol != title_room.music_vol)
        {
            StreamAutoFade(title_room.music_id, vol, 10);
            title_room.music_vol = vol;
        }
        return;
    }

    /* A file that stops as soon as it starts is not retried every frame. */
    const uint64_t now = MioPan_GetTicksMs();
    if (ours != 0 && now - title_room.music_started < 1000)
    {
        return;
    }

    /* Whatever the title is playing now -- BGM000 just started by one of the
     * ROM's keep-alives, or our own stopped track -- goes. */
    StreamAutoFadeOut(GetTitleStreamID(), 0);
    if (title_room.music_id >= 0 && title_room.music_id != GetTitleStreamID())
    {
        StreamAutoFadeOut(title_room.music_id, 0);
    }

    SDL_Log("[port] title room: music %s (%d)",
            TitleRoomMusicName(TitleRoomMusicIndex(file)), file);

    title_room.music_id = StreamAutoPlay(file, file - 1, TITLE_ROOM_MUSIC_PRI, 0, 1,
                                         vol, TITLE_ROOM_MUSIC_IN, (SND_3D_SET *)0);
    title_room.music_file = file;
    title_room.music_vol = vol;
    title_room.music_started = now;
    SetTitleStreamID(title_room.music_id);
}

void TitleRoomMusicChanged(void)
{
    if (title_room.active != 0)
    {
        TitleRoomMusicMain();
    }
}

// ──────────────────────────────────────────────────────────────────────
// For the overlay.

void TitleRoomReload(void)
{
    title_room.reload = 1;
}

int TitleRoomCurrentScene(void)
{
    return title_room.scene;
}

void TitleRoomShowScene(int scene)
{
    if (scene < 0 || scene >= miopan_config.title.scene_count ||
        (scene == title_room.scene && title_room.leaving == 0))
    {
        return;
    }

    TitleRoomEnterScene(scene);
}

void TitleRoomHoldScene(int hold)
{
    title_room.held = hold != 0;

    /* A fade already under way when the hold came down finishes as a fade
     * back in, on the scene it was leaving. */
    if (hold != 0)
    {
        title_room.leaving = 0;
    }
}

void TitleRoomFrameAutomatically(int scene)
{
    if (scene < 0 || scene >= miopan_config.title.scene_count)
    {
        return;
    }
    if (title_room.active != 0 && title_room.load == TR_READY &&
        scene == title_room.scene)
    {
        TitleRoomFrame(TitleRoomScene(), TitleRoomHead());
        title_room.path_time = 0.0;
        return;
    }

    miopan_config.title.scenes[scene].camera_set = 0;
}

int TitleRoomCaptureView(int scene, int point)
{
    if (scene < 0 || scene >= miopan_config.title.scene_count)
    {
        return 0;
    }

    MioPan_ConfigTitleScene *t = &miopan_config.title.scenes[scene];
    float (&pos)[4] = gra3dcamGetPosition();
    float (&aim)[4] = gra3dcamGetTarget();
    const float fov = gra3dcamGetFov() * 180.0f / TITLE_ROOM_PI;
    int room_change = 0;

    /* Only the direction is kept, at a fixed reach.  The free camera's own
     * target is 100 units out and a map camera's is wherever Mio stands, and
     * a path between a near target and a far one swings round mid-way, since
     * the two ends are interpolated as points. */
    float dir[3] = { aim[0] - pos[0], aim[1] - pos[1], aim[2] - pos[2] };
    float len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (len < 1.0e-3f)
    {
        float (&view)[4] = gra3dcamGetDirection();

        dir[0] = view[0];
        dir[1] = view[1];
        dir[2] = view[2];
        len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    }
    if (len < 1.0e-3f)
    {
        dir[2] = len = 1.0f;
    }

    float target[3];
    for (int k = 0; k < 3; k++)
    {
        target[k] = pos[k] + dir[k] / len * TITLE_ROOM_CAPTURE_REACH;
    }

    if (GPhaseIsActive(GID_STORY_MAIN) != 0)
    {
        const int room = MapLoadGetRoomNoNow();

        if (room >= 0 && room < TITLE_ROOM_NUM && room != t->room)
        {
            /* A path cannot run between two rooms, so the other point takes
             * this view as well and gets moved from there. */
            t->room = room;
            room_change = 1;
        }
    }

    for (int i = 0; i < 2; i++)
    {
        if (i != point && room_change == 0)
        {
            continue;
        }

        float *eye = i == 0 ? t->a_eye : t->b_eye;
        float *aim = i == 0 ? t->a_target : t->b_target;

        for (int k = 0; k < 3; k++)
        {
            eye[k] = pos[k];
            aim[k] = target[k];
        }
        if (i == 0)
        {
            t->a_fov = fov;
        }
        else
        {
            t->b_fov = fov;
        }
    }
    t->camera_set = 1;

    return room_change;
}

float TitleRoomGetProgress(void)
{
    const double cycle = TitleRoomCycle();
    double x = title_room.path_time;

    if (TitleRoomScene()->motion == MIOPAN_TITLE_MOTION_ONCE)
    {
        x = x < cycle ? x : cycle;
    }
    else
    {
        x = fmod(x, cycle);
    }

    return (float)(x / cycle);
}

void TitleRoomSetProgress(float progress)
{
    if (progress < 0.0f)
    {
        progress = 0.0f;
    }
    if (progress > 1.0f)
    {
        progress = 1.0f;
    }

    title_room.path_time = (double)progress * TitleRoomCycle();
}

int TitleRoomGetPaused(void)
{
    return title_room.paused;
}

void TitleRoomSetPaused(int paused)
{
    title_room.paused = paused != 0;
}

int TitleRoomGetPreview(void)
{
    return title_room.preview;
}

void TitleRoomSetPreview(int point)
{
    title_room.preview = (point == 0 || point == 1) ? point : -1;
}

int TitleRoomGetFly(void)
{
    return title_room.fly;
}

void TitleRoomSetFly(int fly)
{
    fly = fly != 0;

    /* Re-seeded from wherever the path has the camera now. */
    if (fly != 0 && title_room.fly == 0)
    {
        DebugFreeCamera.bActive = 0;
    }
    title_room.fly = fly;
}

// ──────────────────────────────────────────────────────────────────────
// GPhase callbacks -- GID_TITLE_MODE's, wrapping title.c's.

void init_TitleRoom_Mode(void)
{
    init_Title_Mode();

    title_room.active = 1;
    title_room.load = TR_IDLE;

    /* A fresh pick every visit -- never the last visit's while there are
     * others -- whichever way the rotation moves on after that. */
    title_room.scene = TitleRoomPickScene(title_room.visited != 0 ? title_room.scene : -1);
    title_room.visited = 1;
    title_room.leaving = 0;
    title_room.preview = -1;
    title_room.reload = 0;
    title_room.failed = -1;
    title_room.released = 0;
    title_room.bg_locked = 0;
    title_room.path_time = 0.0;
    title_room.last_tick = 0;
    title_room.music_id = -1;
}

GPHASE_ENUM pre_TitleRoom_Mode(GPHASE_ENUM dummy)
{
    GPHASE_ENUM result;
    float dt = 0.0f;

    {
        const uint64_t now = MioPan_GetPerformanceCounter();
        const uint64_t freq = MioPan_GetPerformanceFrequency();

        if (title_room.last_tick != 0 && freq != 0)
        {
            dt = (float)((double)(now - title_room.last_tick) / (double)freq);
        }
        if (dt > TITLE_ROOM_MAX_STEP)
        {
            dt = TITLE_ROOM_MAX_STEP;
        }
        title_room.last_tick = now;
    }

    TitleRoomFollow();

    /* Ahead of the ROM's pre, which draws the scrolling background unless
     * this is up.  Re-asserted every frame while the room is the backdrop:
     * the gallery lowers it on its way out of the picture viewer, and that
     * would put the old art back in front of the room. */
    const int shown = TitleRoomOwnsScreen();
    if (shown != 0)
    {
        SetTitleBgSendLock(1);
        title_room.bg_locked = 1;
    }
    else if (title_room.bg_locked != 0)
    {
        SetTitleBgSendLock(0);
        title_room.bg_locked = 0;
    }

    result = pre_Title_Mode(dummy);

    TitleRoomMusicMain();

    /* The attract movie would take the room away every fifteen idle
     * seconds; with the lock up CheckMoveTitleMovie() keeps the title music
     * alive instead of counting. */
    if (TitleRoomBackdropOn() != 0 && miopan_config.title.attract_movie == 0 &&
        GPhaseIsActive(GID_TITLE_TOP) != 0)
    {
        LockMoveTitleMovie();
    }

    if (title_room.load == TR_LOADING)
    {
        TitleRoomLoadMain();
    }

    if (title_room.load == TR_READY)
    {
        const int running = title_room.paused == 0 && title_room.fly == 0 &&
                            title_room.preview < 0;

        if (running != 0)
        {
            title_room.path_time += dt;
        }

        if (title_room.leaving != 0)
        {
            title_room.fade -= dt / TITLE_ROOM_FADE_SECONDS;
            if (title_room.fade <= 0.0f)
            {
                title_room.fade = 0.0f;
                TitleRoomEnterScene(TitleRoomPickScene(title_room.scene));
            }
        }
        else
        {
            title_room.fade += dt / TITLE_ROOM_FADE_SECONDS;
            if (title_room.fade > 1.0f)
            {
                title_room.fade = 1.0f;
            }
            if (running != 0 && TitleRoomTimeToMove() != 0)
            {
                title_room.leaving = 1;
            }
        }
    }

    /* Entering a scene in another room has just put it back to loading. */
    if (title_room.load == TR_READY)
    {
        TitleRoomDraw();
        TitleRoomDrawVeil();
    }

    return result;
}

void end_TitleRoom_Mode(void)
{
    end_Title_Mode();

    /* Whole even on the way into a game: IngameWrkInit() has let the room
     * go before resetting anything under it. */
    TitleRoomRelease();

    if (title_room.bg_locked != 0)
    {
        SetTitleBgSendLock(0);
        title_room.bg_locked = 0;
    }

    /* end_Title_Mode() faded the title stream out, and that was ours. */
    title_room.music_id = -1;
    title_room.fly = 0;
    title_room.active = 0;
}
