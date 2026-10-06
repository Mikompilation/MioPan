// FILE: main/gphase_trace.c
//
// PORT-ONLY diagnostic -- see gphase_trace.h.  Nothing here corresponds to
// anything in SLES_523.84; it is kept in its own translation unit so that
// gphase.c stays a faithful reconstruction with a single call site.

#include "gphase_trace.h"

#include <stdio.h>
#include <stdlib.h>                 // getenv

// Frames of no movement before the first stall report.  ~3s at 60fps: long
// enough that ordinary loads and fades do not trip it.
#define GPHASE_STALL_FRAMES 180

// After the first report, back off by doubling so a genuinely wedged phase
// keeps confirming itself without flooding the log.
#define GPHASE_STALL_BACKOFF 2

static const char *gphase_names[94] =
{
    /*  0 */ "SUPER",
    /*  1 */ "BOOT_INIT",
    /*  2 */ "BOOT_PADCHECK",
    /*  3 */ "LANGDATA_CHECK",
    /*  4 */ "LANGSEL_MAIN",
    /*  5 */ "AUTOLOAD_MAIN",
    /*  6 */ "UBI_MODE",
    /*  7 */ "OUTGAME_MAIN",
    /*  8 */ "STORY_MAIN",
    /*  9 */ "GAMEOVER_MENU",
    /* 10 */ "ENDING_MOVIE",
    /* 11 */ "GAMERESULT",
    /* 12 */ "CLEARMENU",
    /* 13 */ "SOFTRESETMAIN",
    /* 14 */ "DEBUG_MENU",
    /* 15 */ "TECMO_MODE",
    /* 16 */ "PROJECT_MODE",
    /* 17 */ "TITLE_MODE",
    /* 18 */ "TITLE_MOVIE_MODE",
    /* 19 */ "STORY_NOWLOADING",
    /* 20 */ "STORY_NORMAL",
    /* 21 */ "STORY_DAMAGE",
    /* 22 */ "STORY_DOOR_OPEN",
    /* 23 */ "STORY_DEBUG",
    /* 24 */ "STORY_DEBUG_CAM",
    /* 25 */ "STORY_PAUSE",
    /* 26 */ "STORY_PAUSE_MISSION",
    /* 27 */ "STORY_MENU",
    /* 28 */ "STORY_MAP",
    /* 29 */ "STORY_MISSION_ST",
    /* 30 */ "STORY_MISSION_RESULT",
    /* 31 */ "STORY_GAME_OVER_PRE",
    /* 32 */ "STORY_GAME_OVER",
    /* 33 */ "STORY_SCENE",
    /* 34 */ "STORY_MOVIE",
    /* 35 */ "STORY_EFFECT",
    /* 36 */ "EVENTMSG_DISP",
    /* 37 */ "EVENTFILE_DISP",
    /* 38 */ "STORY_PHOTO",
    /* 39 */ "STORY_ENE_DEAD",
    /* 40 */ "STORY_PUZZLE",
    /* 41 */ "STORY_SAVEPOINT",
    /* 42 */ "STORY_MOVIE_ROOM_SEL",
    /* 43 */ "GAMEOVER_MENU_TOP",
    /* 44 */ "GAMEOVER_MENU_LOAD",
    /* 45 */ "GAMEOVER_MENU_ALBUM",
    /* 46 */ "ENDING_NORMAL1",
    /* 47 */ "ENDING_NORMAL2",
    /* 48 */ "ENDING_HARD",
    /* 49 */ "GAMERESULT_TOP",
    /* 50 */ "CLEARMENU_TOP",
    /* 51 */ "CLEARMENU_SAVE",
    /* 52 */ "CLEARMENU_ALBUM",
    /* 53 */ "TITLE_TOP",
    /* 54 */ "TITLE_MENU",
    /* 55 */ "TITLE_NEWGAME",
    /* 56 */ "TITLE_LOADGAME",
    /* 57 */ "TITLE_SETUP",
    /* 58 */ "TITLE_ALBUM",
    /* 59 */ "TITLE_GALLERY",
    /* 60 */ "TITLE_OPTION",
    /* 61 */ "TITLE_FRAMERATE_SEL",
    /* 62 */ "TITLE_CHAPTER_SEL",
    /* 63 */ "TITLE_MOVE_MOVIE",
    /* 64 */ "STORY_LOAD_MISSION",
    /* 65 */ "STORY_LOAD_MISSION_EVENT",
    /* 66 */ "STORY_LOAD_MISSION_SAVE",
    /* 67 */ "STORY_GAMEOVER_EFF",
    /* 68 */ "STORY_GAMEOVER_FADE",
    /* 69 */ "STORY_GAMEOVER_MOVIE",
    /* 70 */ "STORY_SCENE_PRELOAD",
    /* 71 */ "STORY_SCENE_MAIN",
    /* 72 */ "STORY_MOVIE_PRELOAD",
    /* 73 */ "STORY_MOVIE_MAIN",
    /* 74 */ "PUZZLE_INCONF",
    /* 75 */ "PUZZLE_CROSSFADE",
    /* 76 */ "PUZZLE_HINA",
    /* 77 */ "PUZZLE_ROKU",
    /* 78 */ "PUZZLE_KAZA",
    /* 79 */ "PUZZLE_KAZA2",
    /* 80 */ "PUZZLE_KAI1",
    /* 81 */ "PUZZLE_KAI2",
    /* 82 */ "SAVEPOINT_FADEIN",
    /* 83 */ "SAVEPOINT_MAIN",
    /* 84 */ "SAVEPOINT_FADEOUT",
    /* 85 */ "TITLE_SETUPMENU",
    /* 86 */ "TITLE_MISSION",
    /* 87 */ "SAVEPOINT_TOP",
    /* 88 */ "SAVEPOINT_SAVE",
    /* 89 */ "SAVEPOINT_ALBUM",
    /* 90 */ "MISSION_SEL",
    /* 91 */ "MISSION_CAM",
    /* 92 */ "MISSION_ALBUM",
    /* 93 */ "MISSION_SAVE",
};

const char *GPhaseIdName(int id)
{
    if (id == (int)GPHASE_ID_NONE)
    {
        return "NONE";
    }

    if (id < 0 || id >= (int)(sizeof(gphase_names) / sizeof(gphase_names[0])))
    {
        return "?";
    }

    return gphase_names[id];
}

// Deepest layer that is not GPHASE_ID_NONE -- the phase actually doing the
// work this frame, which is the one worth naming in a stall report.
static int GPhaseTraceLeaf(const GPHASE_ID_ENUM *now)
{
    int i;
    int leaf = 0;

    for (i = 0; i < layer_num; i++)
    {
        if (now[i] != GPHASE_ID_NONE)
        {
            leaf = i;
        }
    }

    return leaf;
}

static void GPhaseTracePath(const GPHASE_ID_ENUM *now)
{
    int leaf = GPhaseTraceLeaf(now);
    int i;

    for (i = 0; i <= leaf; i++)
    {
        printf("%s%s", i ? " > " : "", GPhaseIdName((int)now[i]));
    }

    printf("(%d)\n", (int)now[leaf]);
}

static int GPhaseTraceEnabled(void)
{
    static int cached = -1;

    if (cached < 0)
    {
        const char *env = getenv("MIOPAN_GPHASE_TRACE");
        cached = (env != NULL && env[0] == '0' && env[1] == '\0') ? 0 : 1;
    }

    return cached;
}

void GPhaseTraceMain(const GPHASE_ID_ENUM *now)
{
    static GPHASE_ID_ENUM last[layer_num];
    static int            primed;
    static unsigned int   frame;
    static unsigned int   still;
    static unsigned int   next_report = GPHASE_STALL_FRAMES;
    int                   changed = 0;
    int                   i;

    if (!GPhaseTraceEnabled())
    {
        return;
    }

    frame++;

    for (i = 0; i < layer_num; i++)
    {
        if (!primed || last[i] != now[i])
        {
            changed = 1;
        }
    }

    if (changed)
    {
        primed = 1;
        still = 0;
        next_report = GPHASE_STALL_FRAMES;

        for (i = 0; i < layer_num; i++)
        {
            last[i] = now[i];
        }

        printf("[gphase] %8u  ", frame);
        GPhaseTracePath(now);
        fflush(stdout);
        return;
    }

    still++;

    if (still >= next_report)
    {
        int leaf = GPhaseTraceLeaf(now);

        printf("[gphase]  STALL   after %u frames in %s(%d)\n",
               still, GPhaseIdName((int)now[leaf]), (int)now[leaf]);
        fflush(stdout);

        next_report = still * GPHASE_STALL_BACKOFF;
    }
}
