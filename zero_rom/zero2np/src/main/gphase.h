/* ==========================================================================
 *  gphase.h
 *
 *  Global Phase System (GPhase) - the hierarchical game-state machine that
 *  drives the whole title.  Six layers (0 = root "GID_SUPER", deeper = child
 *  sub-phases); each layer holds a current phase id and a queued-next id, and
 *  every frame GPhaseSysMain() advances the tree.
 *
 *  Public interface for src/main/gphase.c.  The phase ids and the per-phase
 *  tables live here because every state module references them (each module's
 *  init/pre/after/end callbacks are registered against a GPHASE_ID_ENUM).
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GPHASE_H
#define _GPHASE_H

#define layer_num 6

/* --------------------------------------------------------------------------
 *  Result returned by a phase's per-frame callbacks.
 * ------------------------------------------------------------------------ */
enum GPHASE_ENUM
{
    GPHASE_CONTINUE = 0,        /* stay in this phase                       */
    GPHASE_END      = 1,        /* this phase is finished                   */
    GPHASE_NOFRAME  = 2         /* skip the rest of the frame               */
};

/* --------------------------------------------------------------------------
 *  Every game state has a stable id here.  Layer 0 is GID_SUPER; the rest are
 *  arranged as children of their owning state.  GPHASE_ID_NONE (-1) marks an
 *  empty layer slot.
 * ------------------------------------------------------------------------ */
enum GPHASE_ID_ENUM
{
    GID_SUPER                    = 0,
    GID_BOOT_INIT                = 1,
    GID_BOOT_PADCHECK            = 2,
    GID_LANGDATA_CHECK           = 3,
    GID_LANGSEL_MAIN             = 4,
    GID_AUTOLOAD_MAIN            = 5,
    GID_UBI_MODE                 = 6,
    GID_OUTGAME_MAIN             = 7,
    GID_STORY_MAIN               = 8,
    GID_GAMEOVER_MENU            = 9,
    GID_ENDING_MOVIE             = 10,
    GID_GAMERESULT               = 11,
    GID_CLEARMENU                = 12,
    GID_SOFTRESETMAIN            = 13,
    GID_DEBUG_MENU               = 14,
    GID_TECMO_MODE               = 15,
    GID_PROJECT_MODE             = 16,
    GID_TITLE_MODE               = 17,
    GID_TITLE_MOVIE_MODE         = 18,
    GID_STORY_NOWLOADING         = 19,
    GID_STORY_NORMAL             = 20,
    GID_STORY_DAMAGE             = 21,
    GID_STORY_DOOR_OPEN          = 22,
    GID_STORY_DEBUG              = 23,
    GID_STORY_DEBUG_CAM          = 24,
    GID_STORY_PAUSE              = 25,
    GID_STORY_PAUSE_MISSION      = 26,
    GID_STORY_MENU               = 27,
    GID_STORY_MAP                = 28,
    GID_STORY_MISSION_ST         = 29,
    GID_STORY_MISSION_RESULT     = 30,
    GID_STORY_GAME_OVER_PRE      = 31,
    GID_STORY_GAME_OVER          = 32,
    GID_STORY_SCENE              = 33,
    GID_STORY_MOVIE              = 34,
    GID_STORY_EFFECT             = 35,
    GID_EVENTMSG_DISP            = 36,
    GID_EVENTFILE_DISP           = 37,
    GID_STORY_PHOTO              = 38,
    GID_STORY_ENE_DEAD           = 39,
    GID_STORY_PUZZLE             = 40,
    GID_STORY_SAVEPOINT          = 41,
    GID_STORY_MOVIE_ROOM_SEL     = 42,
    GID_GAMEOVER_MENU_TOP        = 43,
    GID_GAMEOVER_MENU_LOAD       = 44,
    GID_GAMEOVER_MENU_ALBUM      = 45,
    GID_ENDING_NORMAL1           = 46,
    GID_ENDING_NORMAL2           = 47,
    GID_ENDING_HARD              = 48,
    GID_GAMERESULT_TOP           = 49,
    GID_CLEARMENU_TOP            = 50,
    GID_CLEARMENU_SAVE           = 51,
    GID_CLEARMENU_ALBUM          = 52,
    GID_TITLE_TOP                = 53,
    GID_TITLE_MENU               = 54,
    GID_TITLE_NEWGAME            = 55,
    GID_TITLE_LOADGAME           = 56,
    GID_TITLE_SETUP              = 57,
    GID_TITLE_ALBUM              = 58,
    GID_TITLE_GALLERY            = 59,
    GID_TITLE_OPTION             = 60,
    GID_TITLE_FRAMERATE_SEL      = 61,
    GID_TITLE_CHAPTER_SEL        = 62,
    GID_TITLE_MOVE_MOVIE         = 63,
    GID_STORY_LOAD_MISSION       = 64,
    GID_STORY_LOAD_MISSION_EVENT = 65,
    GID_STORY_LOAD_MISSION_SAVE  = 66,
    GID_STORY_GAMEOVER_EFF       = 67,
    GID_STORY_GAMEOVER_FADE      = 68,
    GID_STORY_GAMEOVER_MOVIE     = 69,
    GID_STORY_SCENE_PRELOAD      = 70,
    GID_STORY_SCENE_MAIN         = 71,
    GID_STORY_MOVIE_PRELOAD      = 72,
    GID_STORY_MOVIE_MAIN         = 73,
    GID_PUZZLE_INCONF            = 74,
    GID_PUZZLE_CROSSFADE         = 75,
    GID_PUZZLE_HINA              = 76,
    GID_PUZZLE_ROKU              = 77,
    GID_PUZZLE_KAZA              = 78,
    GID_PUZZLE_KAZA2             = 79,
    GID_PUZZLE_KAI1              = 80,
    GID_PUZZLE_KAI2              = 81,
    GID_SAVEPOINT_FADEIN         = 82,
    GID_SAVEPOINT_MAIN           = 83,
    GID_SAVEPOINT_FADEOUT        = 84,
    GID_TITLE_SETUPMENU          = 85,
    GID_TITLE_MISSION            = 86,
    GID_SAVEPOINT_TOP            = 87,
    GID_SAVEPOINT_SAVE           = 88,
    GID_SAVEPOINT_ALBUM          = 89,
    GID_MISSION_SEL              = 90,
    GID_MISSION_CAM              = 91,
    GID_MISSION_ALBUM            = 92,
    GID_MISSION_SAVE             = 93,
    GPHASE_ID_NONE               = -1
};

/* --------------------------------------------------------------------------
 *  Per-phase static configuration: where the phase sits in the tree and how
 *  many child sub-phases it owns.  One GPHASE_DAT per GPHASE_ID_ENUM value.
 * ------------------------------------------------------------------------ */
typedef struct _GPHASE_DAT
{
    int layer;                  /* 0x0  depth (0..5)                        */
    int superID;                /* 0x4  parent phase id                     */
    int son_ID;                 /* 0x8  first child phase id                */
    int son_num;                /* 0xc  number of child sub-phases          */
} GPHASE_DAT;

/* --------------------------------------------------------------------------
 *  Runtime state of the six-layer machine.
 * ------------------------------------------------------------------------ */
typedef struct _GPHASE_SYS
{
    GPHASE_ID_ENUM now[layer_num];      /* 0x00  phase running this frame, per layer */
    GPHASE_ID_ENUM next[layer_num];     /* 0x18  phase queued for next frame         */
    int            ini_flg[layer_num];  /* 0x30  set when now[] != next[] this frame */
} GPHASE_SYS;

/* --------------------------------------------------------------------------
 *  Public API (gphase.c)
 * ------------------------------------------------------------------------ */
void InitGPhaseSys(void);                   /* clear all layers, queue GID_SUPER */
void GPhaseSysMain(void);                   /* advance the tree one frame        */
void SetNextGPhase(GPHASE_ID_ENUM id);      /* queue a transition to phase `id`  */

#endif /* _GPHASE_H */
