#ifndef MIOPAN_EVENT_JUMP_H
#define MIOPAN_EVENT_JUMP_H

/*
 * The event browser's engine: a catalogue of the game's 1931 events read out
 * of the resident event macro pak, and a per-frame driver that takes the game
 * to one of them.  No ImGui here -- miopan_ui_events.cpp draws the window.
 *
 * Every write goes through the ROM's own entry points: a chapter starts the
 * way chapter_sel.c starts one, prerequisites are forced through
 * CompulsionSetEventState() (the EV_SET_STATE opcode's own path), and a room
 * change is the memory card's load path run on the state already in memory.
 * See miopan_event_jump.cpp for how the pieces fit.
 */

#include <string>
#include <vector>

namespace MioPanEvents
{

/* ingame_wrk.mChapterNo runs 0..10: one chapter event each under ev1. */
constexpr int kChapterCount = 11;

/* Where an event's open conditions put the player. */
struct Place
{
    int label = -1; /* event rectangle, area_id * 1000 + record; -1 for none */
    int room = -1;  /* room number, -1 when unknown                          */
    int floor = -1; /* the label's floor ("kai"), -1 when unknown            */

    bool Known() const { return room >= 0; }
};

struct Event
{
    int parent = -1;
    std::vector<int> subs;

    /* The chapter tree this event sits in (ingame_wrk.mChapterNo), or -1 for
     * the room and system trees, which run in every chapter. */
    int chapter = -1;
    /* Closes on CHAPTER_LOAD_WAIT: the event that brings its chapter up. */
    bool load_event = false;
    /* A leaf that opens the moment its chapter's load event ends -- the
     * chapter's own set-up (start position, title card).  Never skipped. */
    bool intro_leaf = false;
    /* Has an init or main program that does something. */
    bool has_program = false;

    /* Its own trigger, from the first alternative of its open conditions. */
    Place place;
    /* Where a teleport for it goes: `place`, else where the event it waits on
     * happens, else its nearest ancestor's place or room. */
    Place go;
    /* Room of the nearest ancestor that opens on a single PLYR_ROOMIN. */
    int room_hint = -1;

    std::string trigger; /* "examine, area 63057 (room 65)" */
    std::string content; /* "movie, talk, camera"           */
    std::string search;  /* lower-case text the filter matches */
};

struct Catalogue
{
    std::vector<Event> events; /* indexed by event id */
    int chapter_event[kChapterCount];
    int room_root = -1;   /* ev2: per-room events */
    int system_root = -1; /* ev3: synchro mode and friends */
};

/* nullptr until the event macro pak is resident at EVENT_DATA_ADDR -- which
 * it is from a chapter or save load onwards, and not at the title. */
const Catalogue *GetCatalogue();

/* At the title: start the load chapter_sel.c would.  False when it cannot be
 * asked for from where the game is. */
bool CanRequestCatalogue();
void RequestCatalogue();

/* What the game will let a request do from here.  `why` gets a sentence when
 * the answer is no. */
bool CanStartChapter(const char **why);
bool CanTeleport(const char **why);

/* Difficulty used for a chapter started from the title; a chapter started
 * from a game in progress keeps that game's. */
int  TitleDifficulty();
void SetTitleDifficulty(int difficulty);

/* Restart into `chapter` from its beginning. */
void StartChapter(int chapter);
/* Restart into the event's chapter, settle everything its conditions wait on,
 * and take the player to where it starts. */
void JumpToEvent(int id);
/* Take the player to where the event starts, in the game as it stands. */
void TeleportToEvent(int id);

/* The ROM's forced state changes on one event.  Start now and re-arm go
 * through the deferred queue, so they run inside the next EventMain(). */
void ForceStart(int id);
void ForceEnd(int id);
void Rearm(int id);

/* The driver.  Step() once a frame, on the game thread. */
void Step();
bool Busy();
const std::string &Status();
/* True while a teleport is held back by an event still running. */
bool CanProceed();
void Proceed();
void Cancel();

/* Live state of an event, or -1 outside a game. */
int         StateOf(int id);
const char *StateName(int state);
/* An event waiting to open whose parent is running, so its conditions are
 * being polled. */
bool IsArmed(int id);

/* Full text of one condition stream (EV_TBL_OPEN_COND / EV_TBL_CLOSE_COND)
 * and of an event's three programs, for tooltips. */
std::string DescribeConditions(int id, int table);
std::string DescribePrograms(int id);
std::string DescribePlace(const Place &place);

/* Where the player is now, as a Place, and the chapter being played; -1 for
 * both outside a story game. */
Place PlayerPlace();
int   CurrentChapter();

/* The ROM chapter-select label for an mChapterNo. */
const char *ChapterLabel(int chapter);

} // namespace MioPanEvents

#endif
