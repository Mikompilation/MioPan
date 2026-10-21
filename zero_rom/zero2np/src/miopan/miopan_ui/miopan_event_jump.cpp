/*
 * The event browser's engine: what the game's events are, and how to get the
 * game to one of them.
 *
 * THE DATA.  Every event lives in the event macro pak (EVENT_OBJ, loaded to
 * EVENT_DATA_ADDR by EventDataLoadReq()): seven tables of one stream per
 * event id -- open conditions, init / main / end programs, close conditions,
 * parent and sub-event lists (ev_get.h has the layout).  The ids form one tree:
 *
 *     ev0 ─┬─ ev1 ── one chapter event per chapter, each opening on
 *          │         EV_NOW_CHAPTER(n): ev4 .. ev13, then ev446 for 10
 *          ├─ ev2 ── the room events: groups opening on PLYR_ROOMIN, then the
 *          │         examine points, doors and pick-ups of each room
 *          └─ ev3 ── the handful that run in every chapter (synchro mode)
 *
 * Measured on the disc, every chapter event has the same shape.  Its first
 * child is a load event -- SET_CONDITION_CHECK(open, off), CHAPTER_LOAD_REQUEST
 * of the start room, closing on CHAPTER_LOAD_WAIT -- and the rest are the
 * chapter's parts, chained by their open conditions: a part opens on
 * EV_STATE(previous part = END) and closes on the END of one of its own story
 * events.  So "everything before this event" is not a list anyone wrote down:
 * it is the closure of the EV_STATE references in the event's open conditions
 * and its ancestors', which is what FastForward() computes.
 *
 * THE DRIVER.  A jump is three things done in order by Step(), once a frame
 * from the overlay (after_super(), on the game thread):
 *
 *   1. Restart the chapter.  From a game in play the way out is
 *      GID_SOFTRESETMAIN for one frame -- leaving GID_STORY_MAIN is what runs
 *      end_Story_Main()'s releases, which a jump straight to
 *      GID_STORY_LOAD_MISSION would skip, both being under GID_STORY_MAIN --
 *      and then exactly what chapter_sel.c does: IngameWrkInit(),
 *      EventDataLoadReq(), GID_STORY_LOAD_MISSION.  From the title it is
 *      chapter_sel.c to the letter.
 *
 *   2. Fast-forward.  On the frame the chapter's load event ends -- its close
 *      condition passed, its end program has run, and the open conditions it
 *      switched off are only polled again on the next frame -- every
 *      prerequisite of the target is forced to the state its condition asks
 *      for through CompulsionSetEventState(), the EV_SET_STATE opcode's own
 *      path.  The parts before the target's are among them, so they never
 *      open and their scenes never play.  Two kinds of event are left to run:
 *      load events, and the chapter's set-up leaves that open on the load
 *      event alone (start position, title card).
 *
 *      The target's own direct prerequisites are held back until the player
 *      is in place.  An event that waits only on an EV_STATE would otherwise
 *      fire the moment its part opens -- in the chapter's start room.
 *
 *      A skipped event can be the one that hands back a lock: chapter 1's own
 *      init takes the pad and only its opening movie's event returns it, with
 *      the fade-in.  So once the game has sat in event mode, or black, for a
 *      second with no event program running, ReleaseStrandedLocks() undoes
 *      what the skipped tail would have (EvMacroReleaseLocks(), FADE_IN).
 *
 *   3. Teleport.  Each frame Advance() finds the first event on the path down
 *      to the target that has not opened and, if its conditions name a place
 *      the player is not in, takes the player there.  Within the room this is
 *      a SetPlayerPos().  To another room it is the memory card's load path
 *      run on the state already in memory: GID_SOFTRESETMAIN for a frame, the
 *      player's area set to the new room, then GID_STORY_LOAD_MISSION_SAVE --
 *      which reloads the world around whatever ingame_wrk, the event tables
 *      and plyr_wrk say, as it does after a game over.  The point inside the
 *      room comes from the live registration data once it has loaded.
 *
 * The driver only teleports while the game is quiet: GID_STORY_NORMAL, no
 * phase change queued, and no event program running -- the save point's
 * rule, more or less.  When one is, the status line says which, and Proceed()
 * overrides it.
 */
#include "miopan_event_jump.h"

#include "../miopan_memory.h"                       /* MioPan_GetHostPointer */

#include "../../common/variable.h"                  /* ingame_wrk / plyr_wrk */
#include "../../graphics/graph2d/fade.h"            /* fade_ctrl / FadeInReq */
#include "../../ingame/event/prg/ev_change.h"       /* CompulsionSetEventState */
#include "../../ingame/event/prg/ev_exe.h"          /* EventEnd_SubStateChange */
#include "../../ingame/event/prg/ev_get.h"          /* Get*Byte / EV_TBL_* / GetEvState */
#include "../../ingame/event/prg/ev_macro.h"        /* EV_MACRO_LABEL / EvMacroGet* */
#include "../../ingame/event/prg/ev_main.h"         /* EV_STATE_* / EventDataLoadReq */
#include "../../ingame/event/prg/ev_open.h"         /* EvCondGet* */
#include "../../ingame/ingame.h"                    /* IngameWrkInit / InitCostume */
#include "../../ingame/loading/loading.h"           /* LoadingTexLoadWait */
#include "../../ingame/map/MapLoad.h"               /* MapLoadGetBuffID / MapLoadGetHeadPtr */
#include "../../ingame/map/MhCtl.h"                 /* MhCtlGetMapHeight */
#include "../../ingame/map/RegDat.h"                /* RegDatGetHead / RegDatVecFind4Label */
#include "../../ingame/map/map_rectangle.h"         /* MrecIsInEvent */
#include "../../ingame/plyr/player.h"               /* SetPlayerPos / SetPlyrAreaNo */
#include "../../ingame/plyr/sister.h"               /* SetSisterPos / SetSisAreaNo */
#include "../../main/gphase.h"                      /* SetNextGPhase / GPhaseIsActive */
#include "../../outgame/title.h"                    /* GetTitleSoundID / SetTitleLoadFlg */
#include "../../system/eeiop/snd_buffer.h"          /* SndBufIsPlaying */
#include "../../system/os/system.h"                 /* EVENT_DATA_ADDR */

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace MioPanEvents
{
namespace
{

constexpr int kEventCount = EVENT_STATE_MAX;

/* EVENT_DATA_ADDR's region in the memory map (system.h); EVENT_OBJ itself is
 * 0x448d4.  Every offset read out of the pak is checked against it, so a
 * stale or foreign block at that address reads as "not resident" instead of
 * sending a walk off the end of emulated RAM. */
constexpr u_int kPakSize = 0x50000;
constexpr u_int kPakHeader = EV_TBL_MAX * 4;
constexpr u_int kPakTables = kPakHeader + EV_TBL_MAX * kEventCount * 4;

/* The three roots under ev0. */
constexpr int kChapterRoot = 1;
constexpr int kRoomRoot = 2;
constexpr int kSystemRoot = 3;

/* Condition labels, in ev_open.c's ev_cond_wrk[] numbering -- which is the
 * stream's own, so these are data, not the port's choice.  The two structural
 * bytes are ev_open.c's EV_COND_OR / EV_COND_END. */
enum
{
    kCondPlyrAreaIn = 0x00,
    kCondPlyrAreaInDef = 0x01,
    kCondPlyrAreaOut = 0x02,
    kCondPlyrAreaOutDef = 0x03,
    kCondPlyrRoomIn = 0x04,
    kCondPlyrRoomOut = 0x05,
    kCondPlyrIntoSight = 0x07,
    kCondSisAreaIn = 0x08,
    kCondSisAreaInDef = 0x09,
    kCondSisIntoSight = 0x0d,
    kCondGhostExist = 0x0e,
    kCondGhostLost = 0x0f,
    kCondGhostReleaseType = 0x10,
    kCondNpcAreaIn = 0x11,
    kCondNpcDistance = 0x16,
    kCondPushPad = 0x17,
    kCondItemUse = 0x18,
    kCondItemHave = 0x19,
    kCondEvState = 0x1a,
    kCondEvNotState = 0x1b,
    kCondEvRandom = 0x1c,
    kCondPhotoObj = 0x1d,
    kCondFinderMode = 0x1f,
    kCondObjIntoFinder = 0x20,
    kCondObjIntoPlyrSight = 0x21,
    kCondBattleEnd = 0x22,
    kCondBattleMode = 0x23,
    kCondChapterLoadWait = 0x24,
    kCondPuzzleSuccess = 0x27,
    kCondNowChapter = 0x28,
    kCondOr = 0x30,
    kCondEnd = 0xff,
};

/* The ROM's chapter-select labels (chapter_sel.c), by mChapterNo. */
const char *const kChapterNames[kChapterCount] = {
    "Chapter 1", "Chapter 2", "Chapter 3",      "Chapter 4",
    "Chapter 5", "Chapter 6", "Chapter 7",      "Chapter 8",
    "Chapter 9", "Chapter 10-1", "Chapter 10-2",
};

/* ---------------------------------------------------------------------------
 *  Area -> room
 *
 *  An event rectangle's label is area_id * 1000 + record (RegDatGetStID4Label),
 *  and nothing in the ROM says which room an area belongs to: the registration
 *  data is only ever looked at once its room is loaded.  A teleport has to know
 *  before, so this is measured off the disc -- every room's RYxx_PZB_PAK (file
 *  RY00_PK2 + room * 5 + 2) holds one "pzb" block per area, whose MB_OUT_HEAD
 *  names area_id and kai.  Area 24 is in no room on this disc, and the rooms
 *  the disc does not carry (1, 3, 5, 7, 10, 19, 21, 63) own none.
 *
 *  Being wrong here costs a teleport, nothing more: PlacePlayer() looks the
 *  label up in the live registration data after the load and says so when it
 *  is not there.
 * ------------------------------------------------------------------------ */

struct AreaRoom
{
    signed char room;
    signed char floor;
};

const AreaRoom kAreaRoom[] = {
    {13, 11}, {14, 11}, {15, 11}, {16, 11}, {17, 11}, {18, 12}, /* 0..   */
    {14, 12}, { 0, 11}, { 2, 11}, { 4, 11}, {12, 11}, {11, 11}, /* 6..   */
    {40, 11}, {41, 11}, {42, 11}, {43, 11}, {44, 11}, {44, 12}, /* 12..  */
    {45, 11}, {45, 12}, {46, 10}, {46, 11}, {47, 10}, {47, 11}, /* 18..  */
    {-1, -1}, {48, 10}, {48, 11}, {49, 10}, {49, 11}, {50, 12}, /* 24..  */
    {51, 12}, {52, 12}, {53, 10}, {53, 11}, {54, 12}, {30, 12}, /* 30..  */
    {31, 11}, {32, 11}, {33, 11}, {34, 11}, {35, 11}, {36, 11}, /* 36..  */
    {37, 11}, {37, 12}, {38, 11}, {38, 12}, {39, 11}, {39, 12}, /* 42..  */
    {20, 11}, {22, 11}, {23, 11}, {24, 11}, {25, 11}, {26, 11}, /* 48..  */
    {26, 12}, {27, 11}, {27, 12}, {28, 11}, {28, 12}, {29, 12}, /* 54..  */
    { 6, 11}, { 8, 11}, { 9, 11}, {65, 11}, {56, 11}, {55, 11}, /* 60..  */
    {57, 11}, {58, 10}, {59, 10}, {60, 10}, {61, 10}, {62, 10}, /* 66..  */
    {64, 10},                                                   /* 72    */
};
constexpr int kAreaCount = (int)(sizeof(kAreaRoom) / sizeof(kAreaRoom[0]));

Place PlaceOfLabel(int label)
{
    Place place;
    const int area = label / 1000;

    if (label >= 0 && area < kAreaCount && kAreaRoom[area].room >= 0)
    {
        place.label = label;
        place.room = kAreaRoom[area].room;
        place.floor = kAreaRoom[area].floor;
    }
    return place;
}

Place PlaceOfRoom(int room)
{
    Place place;
    place.room = room;
    return place;
}

/* ---------------------------------------------------------------------------
 *  Reading the pak
 * ------------------------------------------------------------------------ */

u_char *PakTop()
{
    return (u_char *)MioPan_GetHostPointer(EVENT_DATA_ADDR);
}

u_int PakWord(const u_char *top, u_int offset)
{
    if (offset > kPakSize - 4)
    {
        return 0;
    }
    return Get4Byte(const_cast<u_char *>(top) + offset);
}

/* The header of a resident EVENT_OBJ is fixed: seven table offsets, each one
 * event-count of words after the last.  Nothing else that is ever loaded to
 * this address -- MISSION_OBJ shares it -- starts that way. */
bool PakResident(const u_char *top)
{
    for (u_int t = 0; t < EV_TBL_MAX; t++)
    {
        if (PakWord(top, t * 4) != kPakHeader + t * kEventCount * 4)
        {
            return false;
        }
    }
    return true;
}

/* Event `id`'s stream in `table`, or nullptr. */
const u_char *Stream(const u_char *top, int table, int id)
{
    const u_int offset =
        PakWord(top, PakWord(top, (u_int)table * 4) + (u_int)id * 4);

    if (offset < kPakTables || offset >= kPakSize)
    {
        return nullptr;
    }
    return top + offset;
}

struct Cond
{
    int label;
    const u_char *p; /* the entry, label byte first */
};
using Group = std::vector<Cond>;

/* A condition stream as its alternatives: entries within a group AND,
 * groups OR (ev_open.c).  A stream with no conditions is one empty group. */
std::vector<Group> ParseConds(const u_char *top, const u_char *s)
{
    std::vector<Group> groups(1);
    const u_char *const end = top + kPakSize;

    for (int guard = 0; s != nullptr && s < end && guard < 256; guard++)
    {
        const int label = *s;
        if (label == kCondEnd)
        {
            break;
        }
        if (label == kCondOr)
        {
            groups.emplace_back();
            s += 4;
            continue;
        }

        const int size = EvCondGetSize(label);
        if (size <= 0 || s + size > end)
        {
            break;
        }
        groups.back().push_back({label, s});
        s += size;
    }
    return groups;
}

/* A program's opcodes up to EV_END. */
std::vector<int> ParseProgram(const u_char *top, const u_char *s)
{
    std::vector<int> ops;
    const u_char *const end = top + kPakSize;

    for (int guard = 0; s != nullptr && s < end && guard < 1024; guard++)
    {
        const int com = *s;
        if (com == EV_END)
        {
            break;
        }

        const int size = EvMacroGetSize(com);
        if (size <= 0 || s + size > end)
        {
            break;
        }
        ops.push_back(com);
        s += size;
    }
    return ops;
}

std::vector<int> ParseIdList(const u_char *top, const u_char *s)
{
    std::vector<int> ids;
    const u_char *const end = top + kPakSize;

    for (int i = 0; s != nullptr && s + 4 <= end && i < EV_SUB_ID_MAX; i++)
    {
        const u_int id = Get4Byte(const_cast<u_char *>(s));
        if (id == 0xffffffffu)
        {
            break;
        }
        if (id < (u_int)kEventCount)
        {
            ids.push_back((int)id);
        }
        s += 4;
    }
    return ids;
}

/* Payloads, as ev_open.c's tests read them. */
int CondStateOf(const Cond &c) { return c.p[1]; }
int CondEventOf(const Cond &c) { return Get2Byte(const_cast<u_char *>(c.p) + 2); }
int CondRoomOf(const Cond &c) { return Get2Byte(const_cast<u_char *>(c.p) + 2) & 0xff; }
int CondLabelOf(const Cond &c) { return (int)Get4Byte(const_cast<u_char *>(c.p) + 4); }

bool IsAreaIn(int label)
{
    return label == kCondPlyrAreaIn || label == kCondPlyrAreaInDef;
}

bool IsSisAreaIn(int label)
{
    return label == kCondSisAreaIn || label == kCondSisAreaInDef;
}

/* EV_STATE references that ask for an END or a forced end -- the ones a
 * fast-forward can satisfy. */
struct Ref
{
    int id;
    u_char state;
};

std::vector<Ref> DoneRefs(const Group &group)
{
    std::vector<Ref> refs;
    for (const Cond &c : group)
    {
        const int state = CondStateOf(c);
        const int id = CondEventOf(c);
        if (c.label == kCondEvState && id < kEventCount &&
            (state == EV_STATE_END || state == EV_STATE_LOCK))
        {
            refs.push_back({id, (u_char)state});
        }
    }
    return refs;
}

/* ---------------------------------------------------------------------------
 *  Describing
 * ------------------------------------------------------------------------ */

std::string Format(const char *fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return buf;
}

std::string PlaceText(const Place &place)
{
    if (place.label >= 0)
    {
        return Format("area %d (room %d)", place.label, place.room);
    }
    if (place.room >= 0)
    {
        return Format("room %d", place.room);
    }
    return "";
}

void Append(std::string &out, const std::string &part, const char *sep = ", ")
{
    if (part.empty())
    {
        return;
    }
    if (!out.empty())
    {
        out += sep;
    }
    out += part;
}

/* One short line for a group: the conditions a player can act on, in words.
 * The ones that only gate (difficulty, clear count, NOT_*) are left to the
 * tooltip. */
std::string Summarise(const Group &group)
{
    if (group.empty())
    {
        return "auto";
    }

    std::string action;
    std::string where;
    std::string rest;
    std::string after;
    int after_count = 0;

    for (const Cond &c : group)
    {
        switch (c.label)
        {
        case kCondPushPad:
            action = "examine";
            break;
        case kCondPlyrAreaIn:
        case kCondPlyrAreaInDef:
        {
            const Place place = PlaceOfLabel(CondLabelOf(c));
            Append(where, place.Known() ? PlaceText(place)
                                        : Format("area %d", CondLabelOf(c)));
            break;
        }
        case kCondPlyrAreaOut:
        case kCondPlyrAreaOutDef:
            Append(rest, Format("leave area %d", CondLabelOf(c)));
            break;
        case kCondPlyrRoomIn:
            Append(where, Format("room %d", CondRoomOf(c)));
            break;
        case kCondPlyrRoomOut:
            Append(rest, Format("leave room %d", CondRoomOf(c)));
            break;
        case kCondSisAreaIn:
        case kCondSisAreaInDef:
            Append(where, Format("Mayu in area %d", CondLabelOf(c)));
            break;
        case kCondNpcAreaIn:
        case kCondNpcDistance:
            Append(rest, "NPC");
            break;
        case kCondPlyrIntoSight:
        case kCondSisIntoSight:
        case kCondObjIntoPlyrSight:
            Append(rest, "in sight");
            break;
        case kCondGhostExist:
            Append(rest, "ghost present");
            break;
        case kCondGhostLost:
            Append(rest, "ghost gone");
            break;
        case kCondGhostReleaseType:
            Append(rest, "ghost released");
            break;
        case kCondItemUse:
            Append(rest, "use item");
            break;
        case kCondItemHave:
            Append(rest, "carrying item");
            break;
        case kCondEvRandom:
            Append(rest, "random");
            break;
        case kCondPhotoObj:
            action = "photograph";
            break;
        case kCondFinderMode:
        case kCondObjIntoFinder:
            Append(rest, "camera raised");
            break;
        case kCondBattleEnd:
            Append(rest, "after a fight");
            break;
        case kCondBattleMode:
            Append(rest, "in a fight");
            break;
        case kCondPuzzleSuccess:
            Append(rest, "puzzle solved");
            break;
        case kCondChapterLoadWait:
            Append(rest, "chapter loaded");
            break;
        case kCondNowChapter:
            Append(rest, Format("in %s", c.p[1] < kChapterCount
                                             ? kChapterNames[c.p[1]]
                                             : "chapter ?"));
            break;
        case kCondEvState:
            if (CondStateOf(c) == EV_STATE_END ||
                CondStateOf(c) == EV_STATE_LOCK)
            {
                if (after_count++ < 2)
                {
                    Append(after, Format("ev%d", CondEventOf(c)));
                }
            }
            break;
        default:
            break;
        }
    }

    std::string text = action;
    Append(text, where);
    Append(text, rest);
    if (after_count != 0)
    {
        Append(text, "after " + after + (after_count > 2 ? ", ..." : ""));
    }
    return text.empty() ? "gated" : text;
}

struct Tag
{
    int com;
    const char *tag;
};

/* What a program's opcodes are worth calling out, most telling first. */
const Tag kTags[] = {
    {MOVIE_PLAY, "movie"},
    {EV_CAM, "cutscene"},
    {TALK_EXE, "talk"},
    {MSG_DISP, "message"},
    {MSG_CHOICE, "choice"},
    {GHOST_APPEAR, "ghost"},
    {FLOATAGE_GHOST, "ghost"},
    {PUZZLE_START, "puzzle"},
    {ITEM_GET, "item"},
    {FILE_GET, "file"},
    {CRYSTAL_GET, "crystal"},
    {LEVELGEM_GET, "gem"},
    {CAM_SPECIALSHOT_GET, "camera upgrade"},
    {CAM_ADD_FUNCTION_GET, "camera upgrade"},
    {CAM_EQUIP_FUNCTION_GET, "camera upgrade"},
    {MEMO_UPDATE, "memo"},
    {CHAPTER_DISP_START, "chapter title"},
    {SUBTITLE_DISP_REQ, "voice"},
    {SUBTITLE3D_OBJ_DISP_REQ, "voice"},
    {SUBTITLE3D_POS_DISP_REQ, "voice"},
    {EV_STREAM_PLAY, "stream"},
    {EV_STREAM3D_OBJ_PLAY, "stream"},
    {EV_STREAM3D_POS_PLAY, "stream"},
    {SET_BUTTERFLY, "butterfly"},
    {SYNCHRO_MODE_START, "synchro"},
    {EV_MOVIE_ROOM_REQUEST, "projector"},
    {AREA_CHANGE, "room change"},
    {CHAPTER_LOAD_REQUEST, "chapter load"},
    {CHANGE_CHAPTER, "chapter change"},
    {EV_DOOR_UNLOCK, "unlocks door"},
    {EV_DOOR_LOCK, "locks door"},
    {SET_GHOST_SEAL_DOOR, "seals door"},
    {GAME_DATA_SAVE, "save"},
    {GAMEOVER_REQUEST, "game over"},
    {GAME_CLEAR, "game clear"},
};

std::string Tags(const std::vector<int> &ops)
{
    std::string text;
    for (const Tag &tag : kTags)
    {
        if (std::find(ops.begin(), ops.end(), tag.com) != ops.end() &&
            text.find(tag.tag) == std::string::npos)
        {
            Append(text, tag.tag);
        }
    }
    return text;
}

std::string Lower(std::string text)
{
    for (char &c : text)
    {
        c = (char)std::tolower((unsigned char)c);
    }
    return text;
}

/* ---------------------------------------------------------------------------
 *  The catalogue
 * ------------------------------------------------------------------------ */

Catalogue g_catalogue;
std::vector<u_char> g_catalogue_tables; /* the pak's tables it was built from */
bool g_catalogue_valid = false;

Place InheritedPlace(int id);

void Build(const u_char *top)
{
    Catalogue &cat = g_catalogue;
    cat.events.assign(kEventCount, Event{});
    std::fill(std::begin(cat.chapter_event), std::end(cat.chapter_event), -1);
    cat.room_root = kRoomRoot;
    cat.system_root = kSystemRoot;

    std::vector<std::vector<Group>> open(kEventCount);

    for (int id = 0; id < kEventCount; id++)
    {
        Event &ev = cat.events[id];
        ev.subs = ParseIdList(top, Stream(top, EV_TBL_SUB_ID, id));
        open[id] = ParseConds(top, Stream(top, EV_TBL_OPEN_COND, id));

        for (const Group &group : ParseConds(top, Stream(top, EV_TBL_CLOSE_COND, id)))
        {
            for (const Cond &c : group)
            {
                ev.load_event |= c.label == kCondChapterLoadWait;
            }
        }

        /* Its place: the first alternative that names one.  The player's own
         * rectangle wins over Mayu's, either over a bare room. */
        for (const Group &group : open[id])
        {
            Place player, sister, room;
            for (const Cond &c : group)
            {
                if (IsAreaIn(c.label) && player.room < 0)
                {
                    player = PlaceOfLabel(CondLabelOf(c));
                }
                else if (IsSisAreaIn(c.label) && sister.room < 0)
                {
                    sister = PlaceOfLabel(CondLabelOf(c));
                }
                else if (c.label == kCondPlyrRoomIn && room.room < 0)
                {
                    room = PlaceOfRoom(CondRoomOf(c));
                }
            }
            ev.place = player.Known() ? player : sister.Known() ? sister : room;
            if (ev.place.Known())
            {
                break;
            }
        }

        const std::vector<int> init = ParseProgram(top, Stream(top, EV_TBL_INIT_PRG, id));
        std::vector<int> ops = ParseProgram(top, Stream(top, EV_TBL_EXE_PRG, id));
        ev.has_program = !init.empty() || !ops.empty();
        ops.insert(ops.end(), init.begin(), init.end());

        ev.trigger = Summarise(open[id][0]);
        if (open[id].size() > 1)
        {
            ev.trigger += Format(" (+%d more)", (int)open[id].size() - 1);
        }
        ev.content = Tags(ops);
    }

    for (int id = 0; id < kEventCount; id++)
    {
        for (int s : cat.events[id].subs)
        {
            cat.events[s].parent = id;
        }
    }

    for (int s : cat.events[kChapterRoot].subs)
    {
        for (const Cond &c : open[s][0])
        {
            if (c.label == kCondNowChapter && c.p[1] < kChapterCount)
            {
                cat.chapter_event[c.p[1]] = s;
            }
        }
    }

    /* Chapter and room hint flow down the tree.  The visited set is only a
     * guard: the disc's tree has no cycles. */
    std::vector<char> visited(kEventCount, 0);
    struct Item
    {
        int id;
        int chapter;
        int room;
    };
    std::vector<Item> stack = {{0, -1, -1}};
    while (!stack.empty())
    {
        Item item = stack.back();
        stack.pop_back();
        if (visited[item.id]++)
        {
            continue;
        }

        Event &ev = cat.events[item.id];
        for (int c = 0; c < kChapterCount; c++)
        {
            if (cat.chapter_event[c] == item.id)
            {
                item.chapter = c;
            }
        }
        const std::vector<Group> &groups = open[item.id];
        if (groups.size() == 1 && groups[0].size() == 1 &&
            groups[0][0].label == kCondPlyrRoomIn)
        {
            item.room = CondRoomOf(groups[0][0]);
        }
        ev.chapter = item.chapter;
        ev.room_hint = item.room;

        for (int s : ev.subs)
        {
            stack.push_back({s, item.chapter, item.room});
        }
    }

    for (int id = 0; id < kEventCount; id++)
    {
        Event &ev = cat.events[id];
        const Group &group = open[id][0];

        bool intro = ev.subs.empty() && !group.empty();
        for (const Cond &c : group)
        {
            intro = intro && c.label == kCondEvState &&
                    CondEventOf(c) < kEventCount &&
                    cat.events[CondEventOf(c)].load_event;
        }
        ev.intro_leaf = intro;

        ev.search = Lower(Format("ev%d ", id) + ev.trigger + " " + ev.content);
    }

    for (int id = 0; id < kEventCount; id++)
    {
        Event &ev = cat.events[id];
        ev.go = ev.place.Known() ? ev.place : InheritedPlace(id);
    }
}

/* ---------------------------------------------------------------------------
 *  Where the game is
 * ------------------------------------------------------------------------ */

bool InStoryPlay()
{
    return GPhaseIsActive(GID_STORY_NORMAL) != 0 ||
           GPhaseIsActive(GID_STORY_DEBUG) != 0;
}

bool AtTitle()
{
    return GPhaseIsActive(GID_TITLE_TOP) != 0 ||
           GPhaseIsActive(GID_TITLE_MENU) != 0;
}

bool InStory()
{
    return GPhaseIsActive(GID_STORY_MAIN) != 0;
}

bool Running(int state)
{
    return state == EV_STATE_INIT || state == EV_STATE_EXE ||
           state == EV_STATE_WAIT_END;
}

bool Waiting(int state)
{
    return state == EV_STATE_WAIT_OPEN || state == EV_STATE_SUSPEND;
}

/* No event program on the execution list.  EXE is a main program running;
 * an end program runs with its event already END, so that half has to ask
 * the list itself. */
bool EventsQuiet(int *busy_id)
{
    for (int id = 0; id < kEventCount; id++)
    {
        const int state = GetEvState(id);
        if (state == EV_STATE_INIT || state == EV_STATE_EXE ||
            (state == EV_STATE_END && CheckEventExeEntry(id) == 1))
        {
            *busy_id = id;
            return false;
        }
    }
    *busy_id = -1;
    return GetEvWrkWaitFlg() == 0;
}

bool PhaseReady()
{
    return GPhaseIsActive(GID_STORY_NORMAL) != 0 && GPhaseIsSettled() != 0;
}

bool Here(const Place &place)
{
    if (GetPlyrAreaNo() != place.room)
    {
        return false;
    }
    if (place.label < 0)
    {
        return true;
    }
    return MrecIsInEvent(plyr_wrk.cmn_wrk.mbox.pos, place.label,
                         GetPlyrFloor()) != 0;
}

/* ---------------------------------------------------------------------------
 *  Placing the player
 * ------------------------------------------------------------------------ */

void Centre(const MB_OUT_RECT *rec, float *out)
{
    out[0] = (rec->vec[0][0] + rec->vec[1][0] + rec->vec[2][0] + rec->vec[3][0]) * 0.25f;
    out[1] = 0.0f;
    out[2] = (rec->vec[0][2] + rec->vec[1][2] + rec->vec[2][2] + rec->vec[3][2]) * 0.25f;
    out[3] = 0.0f;
}

/* Twice the XZ area of a rectangle's quad. */
float QuadArea(const MB_OUT_RECT *rec)
{
    float sum = 0.0f;
    for (int i = 0; i < 4; i++)
    {
        const float *a = rec->vec[i];
        const float *b = rec->vec[(i + 1) % 4];
        sum += a[0] * b[2] - b[0] * a[2];
    }
    return sum < 0.0f ? -sum : sum;
}

/* A point for `place` out of the registration data now resident: the centre
 * of the label's first rectangle, or for a bare room the centre of its
 * largest outline rectangle.  `floor` gets the area's kai. */
bool FindPoint(const Place &place, float *out, int *floor)
{
    if (place.label >= 0)
    {
        for (int b = 0; b < REG_DAT_BUFF_NUM; b++)
        {
            MB_OUT_HEAD *hp = RegDatGetHead(b);
            if (hp == nullptr || hp->area_id != place.label / 1000 ||
                RegDatVecFind4Label(b, place.label) < 0)
            {
                continue;
            }
            const MB_OUT_RECT *rec = RegDatVecNextFind(b);
            if (rec != nullptr)
            {
                Centre(rec, out);
                *floor = hp->kai;
                return true;
            }
        }
        return false;
    }

    const int buff_id = MapLoadGetBuffID(place.room);
    if (buff_id < 0)
    {
        return false;
    }

    MLOAD_HEAD *mh = MapLoadGetHeadPtr(buff_id);
    const MB_OUT_RECT *best = nullptr;
    float best_area = 0.0f;
    for (int j = 0; j < 4; j++)
    {
        const int reg_id = mh->reg_id[j];
        if (reg_id < 0 || RegDatGetHead(reg_id) == nullptr)
        {
            continue;
        }

        const MB_OUT_RECT *rec = RegDatGetVecPtr(reg_id, RECORD_TYPE_ROOM_OUTLINE);
        const int num = RegDatGetVecNum(reg_id, RECORD_TYPE_ROOM_OUTLINE);
        for (int i = 0; rec != nullptr && i < num; i++)
        {
            if (best == nullptr || QuadArea(rec) > best_area)
            {
                best = rec;
                best_area = QuadArea(rec);
                *floor = RegDatGetHead(reg_id)->kai;
            }
            rec = RegDatGetNextVecPtr(const_cast<MB_OUT_RECT *>(rec));
        }
    }
    if (best == nullptr)
    {
        return false;
    }
    Centre(best, out);
    return true;
}

/* Stand the player on `place` in the room already loaded, the way
 * EvPlyrPosSet() does: the floor height under the point, seeded from where
 * she stands now.  Mayu comes along when she is following. */
bool PlacePlayer(const Place &place)
{
    float point[4];
    float placed[4];
    int floor = place.floor;

    if (!FindPoint(place, point, &floor))
    {
        return false;
    }

    if (floor >= 0)
    {
        SetPlayerFloor(floor);
    }
    point[1] = plyr_wrk.cmn_wrk.mbox.pos[1];
    if (MhCtlGetMapHeight(placed, point, place.room, 0) < 0)
    {
        std::memcpy(placed, point, sizeof(placed));
    }
    SetPlayerPos(placed);

    if (GetSisJoinFlg() != 0 && GetSisAreaNo() == place.room)
    {
        SetSisterPos(placed);
        SetSisterFloorPL();
    }
    return true;
}

/* ---------------------------------------------------------------------------
 *  Forcing states
 * ------------------------------------------------------------------------ */

/* CompulsionSetEventState() on an event that is not running.  An END of a
 * part also parks its sub-events, as a real end would (SetEventEndExeStatus);
 * a forced end seals the whole sub-tree by itself. */
bool Force(int id, u_char state)
{
    const int now = GetEvState(id);
    if (Running(now))
    {
        return false;
    }
    if (now == EV_STATE_END || now == EV_STATE_LOCK)
    {
        return true;
    }

    CompulsionSetEventState(id, state);
    if (state == EV_STATE_END && !g_catalogue.events[id].subs.empty())
    {
        EventEnd_SubStateChange(id);
    }
    return true;
}

/* Events a fast-forward leaves alone: they bring the chapter up. */
bool RunsByItself(int id)
{
    const Event &ev = g_catalogue.events[id];
    return ev.load_event || ev.intro_leaf;
}

/* ---------------------------------------------------------------------------
 *  The job
 * ------------------------------------------------------------------------ */

enum class Kind
{
    Chapter,
    Jump,
    Teleport,
};

enum class Stage
{
    Idle,
    LeaveForChapter, /* out of GID_STORY_MAIN through GID_SOFTRESETMAIN   */
    TitleStart,      /* at the title, waiting as chapter_sel.c does        */
    ChapterLoading,  /* until the chapter's load event ends                */
    Advance,         /* walking the path down to the target                */
    LeaveForRoom,    /* out of GID_STORY_MAIN for a room change            */
    RoomLoading,     /* GID_STORY_LOAD_MISSION_SAVE until the room is in   */
};

struct Job
{
    Kind kind = Kind::Chapter;
    Stage stage = Stage::Idle;
    int chapter = -1;
    int target = -1;
    int difficulty = 1;
    int clear_cnt = 0;
    bool from_title = false;
    bool left = false;          /* GID_SOFTRESETMAIN has been asked for   */
    std::vector<Ref> held;      /* the target's own prerequisites         */
    bool released = false;
    bool placed = false;        /* the target's place has been dealt with */
    int teleported_for = -1;    /* path event the last teleport was for   */
    Place dest;
    bool proceed = false;
    bool waiting_on_events = false;
    int quiet_frames = 0;       /* locked with no event program running   */
    int frames = 0;
};

Job g_job;
std::string g_status = "Pick a chapter or an event.";
int g_title_difficulty = 1;

void SetStage(Stage stage)
{
    g_job.stage = stage;
    g_job.frames = 0;
    g_job.left = false;
    g_job.waiting_on_events = false;
}

void Finish(const std::string &status)
{
    g_status = status;
    g_job = Job{};
}

const Event &Ev(int id)
{
    return g_catalogue.events[id];
}

std::string ChapterName(int chapter)
{
    return chapter >= 0 && chapter < kChapterCount ? kChapterNames[chapter]
                                                   : "the chapter";
}

/* The path from just below the chapter event down to `id`. */
std::vector<int> PathBelowChapter(int id)
{
    std::vector<int> path;
    const int chapter_event = g_catalogue.chapter_event[Ev(id).chapter];
    for (int e = id; e >= 0 && e != chapter_event; e = Ev(e).parent)
    {
        path.push_back(e);
    }
    std::reverse(path.begin(), path.end());
    return path;
}

std::vector<int> AncestorsAndSelf(int id)
{
    std::vector<int> ids;
    for (int e = id; e >= 0; e = Ev(e).parent)
    {
        ids.push_back(e);
    }
    return ids;
}

std::vector<Ref> OpenRefs(int id)
{
    const u_char *top = PakTop();
    return DoneRefs(ParseConds(top, Stream(top, EV_TBL_OPEN_COND, id))[0]);
}

/* Where to put the player for an event with no place of its own: where the
 * nearest event it waits on happens -- the story beat just before it -- else
 * the nearest ancestor's place, else its room. */
Place InheritedPlace(int id)
{
    std::vector<int> queue;
    std::set<int> seen = {id};
    for (const Ref &ref : OpenRefs(id))
    {
        queue.push_back(ref.id);
    }

    for (size_t i = 0; i < queue.size() && i < 64; i++)
    {
        const int e = queue[i];
        if (!seen.insert(e).second || Ev(e).load_event)
        {
            continue;
        }
        if (Ev(e).place.Known())
        {
            return Ev(e).place;
        }
        for (const Ref &ref : OpenRefs(e))
        {
            queue.push_back(ref.id);
        }
    }

    for (int e = Ev(id).parent; e >= 0; e = Ev(e).parent)
    {
        if (Ev(e).place.Known())
        {
            return Ev(e).place;
        }
    }
    return Ev(id).room_hint >= 0 ? PlaceOfRoom(Ev(id).room_hint) : Place{};
}

Place TargetPlace(int id)
{
    return Ev(id).go;
}

/* Step 2 of the banner. */
void FastForward()
{
    const int target = g_job.target;
    std::set<int> keep;
    for (int e : AncestorsAndSelf(target))
    {
        keep.insert(e);
    }

    std::map<int, u_char> want;
    std::vector<int> work = PathBelowChapter(target);
    std::set<int> seen;
    while (!work.empty())
    {
        const int e = work.back();
        work.pop_back();
        if (!seen.insert(e).second)
        {
            continue;
        }

        for (const Ref &ref : OpenRefs(e))
        {
            if (keep.count(ref.id) != 0 || RunsByItself(ref.id))
            {
                continue;
            }
            /* A part of another tree is somebody else's business. */
            if (Ev(ref.id).chapter != g_job.chapter && !Ev(ref.id).subs.empty())
            {
                continue;
            }
            want.emplace(ref.id, ref.state);
            work.push_back(ref.id);
        }
    }

    g_job.held.clear();
    for (const Ref &ref : OpenRefs(target))
    {
        if (want.count(ref.id) != 0)
        {
            g_job.held.push_back(ref);
        }
    }

    int forced = 0;
    for (const auto &[id, state] : want)
    {
        const bool held = std::any_of(g_job.held.begin(), g_job.held.end(),
                                      [&](const Ref &r) { return r.id == id; });
        if (!held && Force(id, state))
        {
            forced++;
        }
    }

    SDL_Log("MioPan events: ev%d -- %d prerequisite(s) settled, %d held "
            "until the player is placed",
            target, forced, (int)g_job.held.size());
}

void Release()
{
    for (const Ref &ref : g_job.held)
    {
        if (Waiting(GetEvState(ref.id)))
        {
            Force(ref.id, ref.state);
        }
    }
    g_job.released = true;
}

/* Into the room already loaded, or out and back in through the load path. */
void Teleport(const Place &place)
{
    if (GetPlyrAreaNo() == place.room && MapLoadGetBuffID(place.room) >= 0)
    {
        if (!PlacePlayer(place))
        {
            g_status = "No rectangle for " + PlaceText(place) +
                       " in the room's registration data.";
        }
        if (g_job.kind == Kind::Teleport)
        {
            Finish("Moved to " + PlaceText(place) + ".");
        }
        return;
    }

    g_job.dest = place;
    SetStage(Stage::LeaveForRoom);
    g_status = "Leaving for " + PlaceText(place) + "...";
}

/* Blocked only by an event program?  Say which and let Proceed() through. */
bool HeldByEvents()
{
    int busy = -1;
    if (EventsQuiet(&busy) || g_job.proceed)
    {
        g_job.waiting_on_events = false;
        return false;
    }
    g_job.waiting_on_events = true;
    g_status = busy >= 0
                   ? Format("Waiting for ev%d to finish before teleporting.", busy)
                   : std::string("Waiting for a message to close before teleporting.");
    return true;
}

/* A skipped event can be the one that hands back a lock an earlier one took
 * (EvMacroReleaseLocks() has the chapter 1 case).  Stranded means: the game
 * held in event mode, or the screen left black, with no event program
 * running for a second.  The ROM's own sequences go quiet for a frame or two
 * where one event chains into the next, never for thirty.  True while it is
 * dealing with one, so the caller waits. */
bool ReleaseStrandedLocks()
{
    int busy = -1;
    const bool held = GPhaseIsActive(GID_EVENTMSG_DISP) != 0;
    const bool black = (held || GPhaseIsActive(GID_STORY_NORMAL) != 0) &&
                       fade_ctrl.fade_state == 0 && fade_ctrl.alpha != 0;

    if (!(held || black) || GPhaseIsSettled() == 0 || !EventsQuiet(&busy))
    {
        g_job.quiet_frames = 0;
        return false;
    }
    if (++g_job.quiet_frames < 30)
    {
        return true;
    }

    g_job.quiet_frames = 0;
    if (held)
    {
        EvMacroReleaseLocks();
        /* What EvPlyrPad() does after its PLYR_PAD(1). */
        SetNextGPhase(IngameDecideNextPhase());
    }
    if (black)
    {
        /* FADE_IN(0, 0, 0, 30), as the chapter openings write it. */
        FadeInReq(0, 0, 0, 30);
    }
    SDL_Log("MioPan events: released the locks a skipped event would have "
            "(event mode %d, black %d)", held, black);
    return true;
}

void EnterChapter()
{
    if (g_job.from_title)
    {
        InitCostume();
    }
    IngameWrkInit(g_job.chapter, g_job.difficulty);
    if (g_job.from_title)
    {
        /* chapter_sel.c's hand-over: the title's bank goes. */
        SetTitleLoadFlg(0);
    }
    else
    {
        ingame_wrk.mClearCnt.Set((char)g_job.clear_cnt);
    }
    EventDataLoadReq();
    SetNextGPhase(GID_STORY_LOAD_MISSION);
    SetStage(Stage::ChapterLoading);
    g_status = "Loading " + ChapterName(g_job.chapter) + "...";
}

void StepAdvance()
{
    if (g_job.kind == Kind::Teleport)
    {
        if (!PhaseReady())
        {
            g_status = "Waiting for normal play...";
            return;
        }
        if (HeldByEvents())
        {
            return;
        }
        g_job.proceed = false;
        if (Here(g_job.dest))
        {
            Finish("Already at " + PlaceText(g_job.dest) + ".");
            return;
        }
        Teleport(g_job.dest);
        return;
    }

    const int target = g_job.target;
    const int state = GetEvState(target);
    if (!Waiting(state))
    {
        Finish(Format("ev%d has started.", target));
        return;
    }
    if (ReleaseStrandedLocks())
    {
        return;
    }
    if (!PhaseReady())
    {
        g_status = "Waiting for normal play...";
        return;
    }

    const std::vector<int> path = PathBelowChapter(target);
    int node = -1;
    for (int e : path)
    {
        if (Waiting(GetEvState(e)))
        {
            node = e;
            break;
        }
    }
    if (node < 0)
    {
        return;
    }

    const int parent = Ev(node).parent;
    if (parent >= 0 && !Running(GetEvState(parent)))
    {
        Finish(Format("ev%d cannot open: ev%d, its parent, has already ended.",
                      node, parent));
        return;
    }
    if (GetEvState(node) == EV_STATE_SUSPEND)
    {
        Finish(Format("ev%d was parked when its part ended.", node));
        return;
    }

    if (node != target)
    {
        /* A part on the way down has not opened.  If it opens on a place,
         * go there -- to the deepest place below it in the same room, so one
         * load serves both. */
        Place aim = Ev(node).place;
        if (aim.Known() && !Here(aim) && g_job.teleported_for != node)
        {
            if (HeldByEvents())
            {
                return;
            }
            g_job.proceed = false;
            for (auto it = std::find(path.begin(), path.end(), node);
                 it != path.end(); ++it)
            {
                const Place &below = Ev(*it).place;
                if (below.label >= 0 && below.room == aim.room)
                {
                    aim = below;
                }
            }
            g_job.teleported_for = node;
            Teleport(aim);
            return;
        }
        g_status = Format("Waiting for ev%d to open: %s.", node,
                          Ev(node).trigger.c_str());
        return;
    }

    if (!g_job.placed)
    {
        const Place aim = TargetPlace(target);
        if (aim.Known() && !Here(aim))
        {
            if (HeldByEvents())
            {
                return;
            }
            g_job.proceed = false;
            g_job.placed = true;
            Teleport(aim);
            return;
        }
        g_job.placed = true;
    }

    if (!g_job.released)
    {
        Release();
    }
    g_status = Format("At ev%d -- waiting for it: %s.", target,
                      Ev(target).trigger.c_str());
}

} // namespace

/* ---------------------------------------------------------------------------
 *  Public
 * ------------------------------------------------------------------------ */

const Catalogue *GetCatalogue()
{
    const u_char *top = PakTop();
    if (!PakResident(top))
    {
        g_catalogue_valid = false;
        return nullptr;
    }

    /* The tables are all the identity a pak has; a reload of the same file is
     * the same catalogue. */
    if (!g_catalogue_valid ||
        std::memcmp(g_catalogue_tables.data(), top, kPakTables) != 0)
    {
        g_catalogue_tables.assign(top, top + kPakTables);
        Build(top);
        g_catalogue_valid = true;
    }
    return &g_catalogue;
}

bool CanRequestCatalogue()
{
    return AtTitle() && GPhaseIsSettled() != 0 && !PakResident(PakTop());
}

void RequestCatalogue()
{
    if (CanRequestCatalogue())
    {
        /* What chapter_sel.c does before it starts a chapter.  The address is
         * MISSION_OBJ's too, which mission_sel.c reloads on its way in. */
        EventDataLoadReq();
    }
}

bool CanStartChapter(const char **why)
{
    const char *reason = nullptr;
    if (Busy())
    {
        reason = "A jump is already under way.";
    }
    else if (InStoryPlay())
    {
        if (CheckIngameMission() != 0)
        {
            reason = "Not from Mission Mode.";
        }
    }
    else if (!AtTitle())
    {
        reason = "Only from the title screen or from normal play.";
    }

    if (why != nullptr)
    {
        *why = reason;
    }
    return reason == nullptr;
}

bool CanTeleport(const char **why)
{
    const char *reason = nullptr;
    if (Busy())
    {
        reason = "A jump is already under way.";
    }
    else if (!InStoryPlay())
    {
        reason = "Only from normal play.";
    }
    else if (CheckIngameMission() != 0)
    {
        reason = "Not from Mission Mode.";
    }

    if (why != nullptr)
    {
        *why = reason;
    }
    return reason == nullptr;
}

int TitleDifficulty()
{
    return g_title_difficulty;
}

void SetTitleDifficulty(int difficulty)
{
    g_title_difficulty = std::clamp(difficulty, 0, 3);
}

namespace
{

void Begin(Kind kind, int chapter, int target)
{
    g_job = Job{};
    g_job.kind = kind;
    g_job.chapter = chapter;
    g_job.target = target;

    if (AtTitle())
    {
        g_job.from_title = true;
        g_job.difficulty = g_title_difficulty;
        SetStage(Stage::TitleStart);
    }
    else
    {
        g_job.difficulty = ingame_wrk.mDifficulty.Get();
        g_job.clear_cnt = ingame_wrk.mClearCnt.Get();
        SetStage(Stage::LeaveForChapter);
    }
    g_status = "Starting " + ChapterName(chapter) + "...";
}

} // namespace

void StartChapter(int chapter)
{
    if (chapter < 0 || chapter >= kChapterCount || !CanStartChapter(nullptr))
    {
        return;
    }
    Begin(Kind::Chapter, chapter, -1);
}

void JumpToEvent(int id)
{
    if (GetCatalogue() == nullptr || id < 0 || id >= kEventCount ||
        !CanStartChapter(nullptr))
    {
        return;
    }

    const Event &ev = Ev(id);
    if (ev.chapter < 0)
    {
        return;
    }

    /* The chapter event and its load event are the chapter's start. */
    const bool start_only = id == g_catalogue.chapter_event[ev.chapter] ||
                            ev.load_event;
    Begin(start_only ? Kind::Chapter : Kind::Jump, ev.chapter,
          start_only ? -1 : id);
}

void TeleportToEvent(int id)
{
    if (GetCatalogue() == nullptr || id < 0 || id >= kEventCount ||
        !CanTeleport(nullptr))
    {
        return;
    }

    const Place place = TargetPlace(id);
    if (!place.Known())
    {
        g_status = Format("ev%d names no place to go to.", id);
        return;
    }

    g_job = Job{};
    g_job.kind = Kind::Teleport;
    g_job.target = id;
    g_job.dest = place;
    SetStage(Stage::Advance);
    g_status = "Going to " + PlaceText(place) + "...";
}

/* The deferred queue only takes an event that is waiting or done and off the
 * execution list (CompulsionSetEventStateMain()), which is what keeps a
 * program from being torn down under itself. */
void ForceStart(int id)
{
    if (InStory() && id >= 0 && id < kEventCount)
    {
        Req_CompulsionSetEventState(id, id, EV_STATE_INIT);
        g_status = Format("ev%d will start on the next frame.", id);
    }
}

void ForceEnd(int id)
{
    if (InStory() && GetCatalogue() != nullptr && id >= 0 && id < kEventCount)
    {
        g_status = Force(id, EV_STATE_END)
                       ? Format("ev%d marked as ended.", id)
                       : Format("ev%d is running; it was left alone.", id);
    }
}

void Rearm(int id)
{
    if (InStory() && id >= 0 && id < kEventCount)
    {
        Req_CompulsionSetEventState(id, id, EV_STATE_WAIT_OPEN);
        g_status = Format("ev%d will be waiting again from the next frame.", id);
    }
}

void Step()
{
    /* Every status line goes to the log too, once: a jump spans loads, and
     * the log is where to read back what it did. */
    static std::string logged;
    if (g_status != logged)
    {
        logged = g_status;
        SDL_Log("MioPan events: %s", logged.c_str());
    }

    if (g_job.stage == Stage::Idle)
    {
        return;
    }
    g_job.frames++;

    switch (g_job.stage)
    {
    case Stage::Idle:
        break;

    case Stage::LeaveForChapter:
    case Stage::LeaveForRoom:
        if (!g_job.left)
        {
            if (!InStoryPlay())
            {
                Finish("The game left normal play before the jump could start.");
            }
            else if (GPhaseIsSettled() != 0)
            {
                SetNextGPhase(GID_SOFTRESETMAIN);
                g_job.left = true;
            }
            break;
        }

        /* One frame in GID_SOFTRESETMAIN, its first: init_SoftResetMain() has
         * cancelled the loads, and one_SoftResetMain() only moves on to the
         * logos on its third.  Leaving now overrides nothing. */
        if (GPhaseIsActive(GID_SOFTRESETMAIN) != 0 && GPhaseIsSettled() != 0)
        {
            if (g_job.stage == Stage::LeaveForChapter)
            {
                EnterChapter();
                break;
            }

            /* The memory card's load path, on the state in memory: it loads
             * the room plyr_wrk names and puts Mayu and the room's ghosts and
             * sounds there with her -- EvChapterLoadRequest()'s set. */
            const Place &dest = g_job.dest;
            SetPlyrAreaNo(dest.room);
            SetSisAreaNo(dest.room);
            if (dest.floor >= 0)
            {
                SetPlayerFloor(dest.floor);
            }
            SetSisterFloorPL();
            SetNextGPhase(GID_STORY_LOAD_MISSION_SAVE);
            SetStage(Stage::RoomLoading);
            g_status = "Loading room " + std::to_string(dest.room) + "...";
        }
        else if (g_job.frames > 30)
        {
            Finish("The game did not leave play; jump abandoned.");
        }
        break;

    case Stage::TitleStart:
        if (!AtTitle())
        {
            Finish("The title screen closed before the chapter could start.");
        }
        else if (GPhaseIsSettled() != 0 && LoadingTexLoadWait() != 0 &&
                 SndBufIsPlaying(GetTitleSoundID()) == 0)
        {
            /* chapter_sel.c's two waits: the loading screen's pak, and the
             * title jingle, whose bank SetTitleLoadFlg(0) frees. */
            EnterChapter();
        }
        break;

    case Stage::ChapterLoading:
    {
        if (!InStory() || GetCatalogue() == nullptr)
        {
            if (g_job.frames > 30 * 60)
            {
                Finish("The chapter never came up.");
            }
            break;
        }

        /* The frame the load event ends is the one to act on: the parts open
         * on its END, but its own init switched open conditions off and only
         * its end program, which has just run, switches them back -- so they
         * are first polled next frame. */
        const int chapter_event = g_catalogue.chapter_event[g_job.chapter];
        int load = -1;
        if (chapter_event >= 0)
        {
            for (int s : Ev(chapter_event).subs)
            {
                if (Ev(s).load_event)
                {
                    load = s;
                }
            }
        }
        const int state = load >= 0 ? GetEvState(load) : EV_STATE_END;
        if (state != EV_STATE_END && state != EV_STATE_LOCK)
        {
            break;
        }

        if (g_job.kind == Kind::Chapter)
        {
            Finish(ChapterName(g_job.chapter) + " started.");
            break;
        }
        FastForward();
        SetStage(Stage::Advance);
        g_status = Format("Heading for ev%d...", g_job.target);
        break;
    }

    case Stage::RoomLoading:
        /* Place the player on the frame the load ends -- the one
         * one_Story_Load_Mission_Save() queues GID_STORY_NORMAL on -- so the
         * first frame of play already has her there and no condition sees
         * her standing at the old room's coordinates. */
        if ((GPhaseIsActive(GID_STORY_LOAD_MISSION_SAVE) != 0 &&
             GPhaseIsSettled() == 0) ||
            GPhaseIsActive(GID_STORY_NORMAL) != 0)
        {
            const bool placed = PlacePlayer(g_job.dest);
            if (g_job.kind == Kind::Teleport)
            {
                Finish(placed ? "Moved to " + PlaceText(g_job.dest) + "."
                              : "Loaded room " + std::to_string(g_job.dest.room) +
                                    ", but found no rectangle for " +
                                    PlaceText(g_job.dest) + ".");
                break;
            }
            SetStage(Stage::Advance);
        }
        else if (!InStory() && g_job.frames > 30)
        {
            Finish("The room load did not start.");
        }
        break;

    case Stage::Advance:
        StepAdvance();
        break;
    }
}

bool Busy()
{
    return g_job.stage != Stage::Idle;
}

const std::string &Status()
{
    return g_status;
}

bool CanProceed()
{
    return Busy() && g_job.waiting_on_events && !g_job.proceed;
}

void Proceed()
{
    g_job.proceed = true;
}

void Cancel()
{
    if (Busy())
    {
        Finish("Cancelled.");
    }
}

int StateOf(int id)
{
    if (!InStory() || id < 0 || id >= kEventCount)
    {
        return -1;
    }
    return GetEvState(id);
}

const char *StateName(int state)
{
    /* EvDbg_EventStatePrint()'s labels, in words. */
    switch (state)
    {
    case EV_STATE_WAIT_OPEN:
        return "waiting";
    case EV_STATE_INIT:
        return "starting";
    case EV_STATE_EXE:
        return "running";
    case EV_STATE_WAIT_END:
        return "closing";
    case EV_STATE_SUSPEND:
        return "parked";
    case EV_STATE_END:
        return "done";
    case EV_STATE_LOCK:
        return "sealed";
    default:
        return "";
    }
}

bool IsArmed(int id)
{
    /* Per row per frame from the browser, so the catalogue the window was
     * drawn from is trusted rather than re-checked against the pak. */
    if (!g_catalogue_valid || StateOf(id) != EV_STATE_WAIT_OPEN)
    {
        return false;
    }
    const int parent = Ev(id).parent;
    return parent < 0 || Running(GetEvState(parent));
}

std::string DescribeConditions(int id, int table)
{
    const u_char *top = PakTop();
    if (!PakResident(top) || id < 0 || id >= kEventCount)
    {
        return "";
    }

    std::string text;
    for (const Group &group : ParseConds(top, Stream(top, table, id)))
    {
        std::string line;
        for (const Cond &c : group)
        {
            const char *name = EvCondGetName(c.label);
            std::string item = name != nullptr ? name : "?";
            if (c.label == kCondEvState || c.label == kCondEvNotState)
            {
                item += Format("(ev%d %s)", CondEventOf(c),
                               StateName(CondStateOf(c)));
            }
            else if (IsAreaIn(c.label) || IsSisAreaIn(c.label) ||
                     c.label == kCondPlyrAreaOut || c.label == kCondPlyrAreaOutDef)
            {
                const Place place = PlaceOfLabel(CondLabelOf(c));
                item += place.Known() ? "(" + PlaceText(place) + ")"
                                      : Format("(%d)", CondLabelOf(c));
            }
            else if (c.label == kCondPlyrRoomIn || c.label == kCondPlyrRoomOut)
            {
                item += Format("(room %d)", CondRoomOf(c));
            }
            else if (c.label == kCondNowChapter)
            {
                item += "(" + ChapterName(c.p[1]) + ")";
            }
            Append(line, item, " and ");
        }
        Append(text, line.empty() ? "nothing (starts at once)" : line, "\n  or ");
    }
    return text;
}

std::string DescribePrograms(int id)
{
    const u_char *top = PakTop();
    if (!PakResident(top) || id < 0 || id >= kEventCount)
    {
        return "";
    }

    const struct
    {
        int table;
        const char *name;
    } programs[] = {
        {EV_TBL_INIT_PRG, "init"},
        {EV_TBL_EXE_PRG, "main"},
        {EV_TBL_END_PRG, "end"},
    };

    std::string text;
    for (const auto &program : programs)
    {
        const std::vector<int> ops = ParseProgram(top, Stream(top, program.table, id));
        std::string line;
        for (size_t i = 0; i < ops.size();)
        {
            size_t run = 1;
            while (i + run < ops.size() && ops[i + run] == ops[i])
            {
                run++;
            }
            const char *name = EvMacroGetName(ops[i]);
            std::string item = name != nullptr ? name : "?";
            if (run > 1)
            {
                item += Format(" x%d", (int)run);
            }
            Append(line, item);
            i += run;
        }
        if (!line.empty())
        {
            Append(text, std::string(program.name) + ": " + line, "\n");
        }
    }
    return text.empty() ? "no programs" : text;
}

std::string DescribePlace(const Place &place)
{
    return PlaceText(place);
}

const char *ChapterLabel(int chapter)
{
    return chapter >= 0 && chapter < kChapterCount ? kChapterNames[chapter] : "?";
}

int CurrentChapter()
{
    return InStory() ? (int)ingame_wrk.mChapterNo.Get() : -1;
}

Place PlayerPlace()
{
    Place place;
    if (InStory())
    {
        place.room = GetPlyrAreaNo();
        place.floor = GetPlyrFloor();
    }
    return place;
}

} // namespace MioPanEvents
