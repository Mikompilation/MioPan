#include "miopan_config.h"

#include "io/miopan_file.h"
#include "io/miopan_input.h"
#include "io/miopan_paths.h"
#include "io/miopan_undub.h"
#include "miopan_ui/miopan_ui.h"
#include "rendering/miopan_renderer.h"
#include "iop_voice.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

/* One built-in title scene: a name, a room, and the eye, target and field of
 * view at each end.  The rest is the same for all of them -- in the rotation,
 * placed by hand, thirty seconds from A to B and four at each end, there and
 * back. */
#define TITLE_SCENE(name, room, ax, ay, az, atx, aty, atz, afov,             \
                    bx, by, bz, btx, bty, btz, bfov)                          \
    {                                                                         \
        name, 1, room, 1,                                                     \
        { ax, ay, az }, { atx, aty, atz }, afov,                              \
        { bx, by, bz }, { btx, bty, btz }, bfov,                              \
        30.0f, 4.0f, MIOPAN_TITLE_MOTION_PINGPONG, MIOPAN_TITLE_EASING_SMOOTH, \
    }

/* Defaults.  The window size is the one EnsureRenderer() used before this
 * existed -- twice the ROM's 640x448 -- so a first run with no file behaves
 * exactly as the port did before settings were persisted at all. */
MioPan_Config miopan_config = {
    /* .renderer = */ {
        /* .window_mode       = */ MIOPAN_WINDOW_MODE_WINDOWED,
        /* .window_width      = */ 1280,
        /* .window_height     = */ 896,
        /* .render_mode       = */ MIOPAN_RENDER_RES_MATCH_WINDOW,
        /* .render_scale      = */ 1.0f,
        /* .upscale_filter    = */ MIOPAN_RENDER_FILTER_LINEAR,
        /* .smooth_2d         = */ 1,
        /* Both off by default, and for the same reason HDR is: they are
         * additions rather than reconstructions, and each has a cost the port
         * should not spend on a player's behalf.  MSAA in particular is not
         * the near-free 4x of a single-pass forward renderer here -- this game
         * samples the frame buffer for its effects, and every one of those
         * samples now resolves the multisample target.  See the blocks above
         * MioPan_RendererSetMsaaSamples() and ...SetAnisotropy() in
         * rendering/miopan_renderer.h. */
        /* .msaa_samples      = */ 1,
        /* .anisotropy        = */ 1,
        /* .aspect_mode       = */ MIOPAN_ASPECT_AUTO,
        /* .aspect_ratio      = */ 16.0f / 9.0f,
        /* .lighting_mode     = */ MIOPAN_LIGHTING_VERTEX,
        /* .shadow_filter     = */ MIOPAN_SHADOW_FILTER_SOFT,
        /* .extra_presents    = */ 0,
        /* .interpolate_presents = */ 1,
        /* .interpolate_geometry = */ 1,
        /* .smoothing_mode    = */ MIOPAN_SMOOTHING_FIELD,
        /* HDR is off by default and deliberately so: it is the one setting
         * here that cannot be judged from the code, only from the panel in
         * front of the player, and a first run that silently re-grades the
         * picture is worse than one that asks. */
        /* .hdr_mode          = */ MIOPAN_HDR_OFF,
        /* .hdr_paper_white   = */ 200.0f,
        /* .hdr_peak          = */ 0.0f,
        /* .hdr_expansion     = */ 0.35f,
        /* .hdr_expansion_knee = */ 0.75f,
        /* .grade_brightness  = */ 1.0f,
        /* .grade_contrast    = */ 1.0f,
        /* .grade_gamma       = */ 1.0f,
        /* .grade_saturation  = */ 1.0f,
        /* Native by default.  The grain itself is the ROM's -- it is part of
         * how the game was photographed, and turning it off is the deviation --
         * but its 128x128 sheet was drawn one texel to a PS2 pixel, and there
         * is no reading of that which survives a 1080p window: either the
         * grain magnifies into dots or it stops being one texel per pixel.
         * NATIVE keeps the density and gives up only the sheet's size, which
         * is the part of the original that was a memory budget rather than a
         * look.  PS2 is here for anyone who wants the dots. */
        /* .film_grain        = */ MIOPAN_FILM_GRAIN_NATIVE,
        /* On by default.  It is an addition rather than a reconstruction, so
         * the first instinct was to ship it off the way HDR is -- but HDR is a
         * property of the display and this is a property of the viewfinder,
         * and something that only ever appears while the camera is raised is
         * not a thing a player should have to go and find a menu for.  It also
         * does nothing at all on a 4:3 window, where there is no world outside
         * the frame to treat. */
        /* .finder_mask       = */ 1,
        /* .finder_mask_blur  = */ 0.75f,
        /* .finder_mask_darken = */ 0.45f,
        /* .finder_mask_tint  = */ 0.55f,
        /* .finder_mask_scale = */ 1.0f,
        /* Off by default: a deliberate deviation from the GS, so a
         * player who has not asked for it gets the hardware's own
         * edges.  0.75 is a good starting point at 3x native. */
        /* .alpha_sharpen     = */ 0.0f,
        /* Off for the same reason, and it is the blunter of the two:
         * it aliases the edge and can erase fine geometry. */
        /* .alpha_cutoff      = */ 0.0f,
        /* On: it changes where the vertices live, not what is drawn, and
         * off is kept only as the A/B. */
        /* .resident_meshes   = */ 1,
        /* On, for the same reason: the textures are the ones the GS path
         * resolves, only no longer re-sent. */
        /* .resident_textures = */ 1,
        /* On: the same bytes reach every read, only no longer rearranged
         * into GS memory on every send.  Off is the A/B. */
        /* .gs_by_reference   = */ 1,
        /* On: the same triangles, each vertex sent once instead of once per
         * triangle it is a corner of.  Off is the A/B. */
        /* .indexed_streams   = */ 1,
        /* On: a deviation from what the port did before -- it asked for
         * IMMEDIATE and tore on every display that granted it -- but tearing
         * was never a choice anyone made.  Presenting at 30 or 60 Hz against
         * a 60 Hz or faster display, the wait it can add is absorbed by the
         * field wait the frame does anyway; see MioPan_RendererSetVsync(). */
        /* .vsync             = */ MIOPAN_VSYNC_ON,
        /* Off: a game that stops by itself is a surprise the first time,
         * however welcome after that.  Mute is the gentler default. */
        /* .pause_in_background = */ 0,
    },
    /* .audio = */ {
        /* .volume            = */ 1.0f,
        /* On: alt-tabbing away to read a walkthrough should not leave the
         * Manor's ambience playing over it. */
        /* .mute_in_background = */ 1,
        /* On: nobody imports the Japanese voices to keep the English ones,
         * and until they are imported it does nothing. */
        /* .japanese_voices   = */ 1,
    },
    /* .ui = */ {
        /* .scale             = */ 0.0f,
    },
    /* .paths = */ {},
    /* .developer = */ {
        /* Off: see MioPan_ConfigDeveloper. */
        /* .debug_menu        = */ 0,
    },
    /* .title = */ {
        /* Room: it is what the setting exists for, and the logo, the menu and
         * every screen behind them are the original's either way. */
        /* .backdrop          = */ MIOPAN_TITLE_BACKDROP_ROOM,
        /* .change            = */ MIOPAN_TITLE_CHANGE_PASS,
        /* .passes            = */ 1,
        /* .dim               = */ 0.2f,
        /* 0: the game's own title music, played the way the ROM plays it. */
        /* .music             = */ 0,
        /* .music_volume      = */ 1.0f,
        /* Off: the room is the thing to look at while the title sits idle,
         * and the movie would take it away every fifteen seconds. */
        /* .attract_movie     = */ 0,
        /* .scene_count       = */ 21,
        /* .scenes            = */ {
            /* Every scene but the courtyard starts from one of the room's
             * own map cameras -- the shots the game frames Mio with -- and
             * moves a little way from there.  Each path was rendered at nine
             * points along it, at 16:9 and at 5120x1417, and nothing crosses
             * the lens at either: no post, wall, net or lantern passes in
             * front of it. */

            /* RKS00, the lantern courtyard outside the Kurosawa house: a slow
             * truck across it, both ends looking at the same point among the
             * lanterns, so the shot turns as it travels.  At eye height the
             * lanterns along the path passed right by the lens on a wide
             * window; at y -2700 the camera goes over them. */
            TITLE_SCENE("Lantern courtyard", 40,
                         8500.0f, -2700.0f, -2600.0f,   9874.0f, -800.0f,   581.0f,  50.0f,
                        11200.0f, -2700.0f, -2600.0f,   9874.0f, -800.0f,   581.0f,  50.0f),

            /* The village. */

            /* RY04: in towards a shrine gate between two stone lanterns. */
            TITLE_SCENE("Shrine gate", 4,
                        18040.0f,  4960.0f, 35511.0f,  18985.0f,  4088.0f, 39038.0f,  44.7f,
                        18368.0f,  4658.0f, 36733.0f,  18985.0f,  4088.0f, 39038.0f,  44.7f),
            /* RY06: down from above the torii onto the stone stairs, the
             * lanterns and the jizo along them. */
            TITLE_SCENE("Torii stairs", 6,
                        -5818.0f,  1235.0f, 27649.0f,  -6471.0f,  1488.0f, 29573.0f,  47.0f,
                        -5818.0f,  1835.0f, 27649.0f,  -6471.0f,  1488.0f, 29573.0f,  47.0f),
            /* RY08: between two of the map's own cameras on the steps up
             * through the stone lanterns. */
            TITLE_SCENE("Lantern steps", 8,
                        -6101.0f, -1164.0f, 33311.0f,  -7436.0f, -1344.0f, 34861.0f,  49.3f,
                        -6597.0f,  -864.0f, 33518.0f,  -7812.0f, -1239.0f, 35128.0f,  46.4f),
            /* RY09: in among the trees towards the wooden grave markers. */
            TITLE_SCENE("Grave markers", 9,
                        -10729.0f,  778.0f, 14855.0f, -11349.0f,   767.0f, 16502.0f,  56.7f,
                        -10975.0f,  774.0f, 15510.0f, -11349.0f,   767.0f, 16502.0f,  56.7f),
            /* RY11: in towards a great gate lit by its two lanterns. */
            TITLE_SCENE("Lantern gate", 11,
                        19304.0f,  4487.0f, 53810.0f,  18845.0f,  4344.0f, 56118.0f,  39.0f,
                        19071.0f,  4414.0f, 54985.0f,  18845.0f,  4344.0f, 56118.0f,  39.0f),
            /* RY13: the overlook's braziers against the dusk, through the
             * frame of a broken torii. */
            TITLE_SCENE("Braziers at dusk", 65,
                        -1317.0f, -3564.0f, 11871.0f,   1005.0f, -3723.0f, 11998.0f,  44.1f,
                         -918.0f, -3591.0f, 11892.0f,   1005.0f, -3723.0f, 11998.0f,  44.1f),

            /* The Osaka house. */

            /* ROS00: along a lattice with red paper lanterns behind it. */
            TITLE_SCENE("Red lanterns", 13,
                        10279.0f,  -713.0f,  6012.0f,  10288.0f,  -693.0f,  4174.0f,  58.8f,
                         9479.0f,  -713.0f,  6008.0f,   9488.0f,  -693.0f,  4170.0f,  58.8f),
            /* ROS02: down past a mosquito net hung over the bedding. */
            TITLE_SCENE("Mosquito net", 15,
                         4833.0f, -1546.0f,  4964.0f,   5119.0f,  -200.0f,  3249.0f,  40.1f,
                         4833.0f,  -946.0f,  4964.0f,   5119.0f,  -200.0f,  3249.0f,  40.1f),

            /* The Tachibana house. */

            /* RTB04: in towards the altar shelves and their candles. */
            TITLE_SCENE("Candle altar", 23,
                        20091.0f,  -458.0f,  6628.0f,  20839.0f,  -915.0f,  9365.0f,  48.7f,
                        20325.0f,  -601.0f,  7485.0f,  20839.0f,  -915.0f,  9365.0f,  48.7f),
            /* RTB06: two corners of the room's own four-corner camera, across
             * the shapes under sheets and the candles between them. */
            TITLE_SCENE("Draped room", 25,
                        19785.0f, -1214.0f, 12864.0f,  21140.0f,  -778.0f, 11610.0f,  45.3f,
                        21472.0f, -1061.0f, 13207.0f,  20852.0f,  -880.0f, 11468.0f,  52.7f),

            /* The Kiryu house. */

            /* RRY04: in towards the doll heads and kimono on their stands. */
            TITLE_SCENE("Doll room", 34,
                         3947.0f,  -865.0f, 13962.0f,   5235.0f,  -710.0f, 13021.0f,  50.0f,
                         4510.0f,  -798.0f, 13551.0f,   5235.0f,  -710.0f, 13021.0f,  50.0f),
            /* RRY05: in towards the film projector. */
            TITLE_SCENE("Projector", 35,
                         8469.0f,  -875.0f, 10953.0f,   7149.0f,  -500.0f, 12158.0f,  53.9f,
                         8108.0f,  -772.0f, 11283.0f,   7149.0f,  -500.0f, 12158.0f,  53.9f),
            /* RRY06: down onto the red-draped tiers of dolls and candles. */
            TITLE_SCENE("Doll altar", 36,
                         9998.0f, -1642.0f, 12946.0f,   8796.0f,  -573.0f, 14295.0f,  44.7f,
                         9998.0f, -1092.0f, 12946.0f,   8796.0f,  -573.0f, 14295.0f,  44.7f),

            /* The Kurosawa house. */

            /* RKS01: in towards a round window full of moonlight. */
            TITLE_SCENE("Moon window", 41,
                         8893.0f, -1243.0f,  6816.0f,   8916.0f, -1420.0f,  8528.0f,  47.0f,
                         8905.0f, -1336.0f,  7711.0f,   8916.0f, -1420.0f,  8528.0f,  47.0f),
            /* RKS06: a little way into the candle-lit library.  Further in,
             * a shelf crowds the left of a wide window. */
            TITLE_SCENE("Library", 46,
                        21957.0f, -1251.0f,  -736.0f,  20551.0f, -1456.0f,  -983.0f,  41.3f,
                        21616.0f, -1301.0f,  -796.0f,  20551.0f, -1456.0f,  -983.0f,  41.3f),
            /* RKS07: down the aisle of glowing memorial tablets to the altar. */
            TITLE_SCENE("Memorial tablets", 47,
                        14016.0f, -1515.0f,  4931.0f,  14311.0f, -1750.0f,  7855.0f,  60.2f,
                        14136.0f, -1611.0f,  6121.0f,  14311.0f, -1750.0f,  7855.0f,  60.2f),
            /* RKS12: in towards the tiers of hina dolls and their lanterns. */
            TITLE_SCENE("Hina dolls", 52,
                         5300.0f, -4109.0f,  6020.0f,   5911.0f, -3408.0f,  8298.0f,  49.3f,
                         5548.0f, -3824.0f,  6946.0f,   5911.0f, -3408.0f,  8298.0f,  49.3f),
            /* RKH00: down the hall to the offering table. */
            TITLE_SCENE("Offering hall", 55,
                         2070.0f,  -627.0f,  1714.0f,   1996.0f,  -334.0f,  5069.0f,  53.3f,
                         2044.0f,  -523.0f,  2909.0f,   1996.0f,  -334.0f,  5069.0f,  53.3f),

            /* Underground. */

            /* RKC00: across the cave shrine, its candles and red spider
             * lilies.  The map camera at the far end puts a jizo against the
             * lens of a wide window, so the path stops short of it. */
            TITLE_SCENE("Cave shrine", 56,
                         1399.0f,  -636.0f,  2331.0f,   2351.0f,  -671.0f,  4158.0f,  47.0f,
                         2526.0f,  -636.0f,  2333.0f,   2254.0f,  -671.0f,  4114.0f,  47.0f),
            /* RCH06: down over the field of candles and the seal. */
            TITLE_SCENE("Candle field", 64,
                         -547.0f, -1650.0f,  3367.0f,   -706.0f,  -496.0f,  1268.0f,  41.3f,
                         -547.0f,  -950.0f,  3367.0f,   -706.0f,  -496.0f,  1268.0f,  41.3f),
        },
    },
};

#undef TITLE_SCENE

namespace
{

/* ------------------------------------------------------------------------
 *  Enumerated settings are stored by name so the file can be edited by hand.
 *  Each table is terminated by a null label and every lookup falls back to
 *  strtol, so a file written by an older build -- or by someone who typed the
 *  number -- still loads.
 * --------------------------------------------------------------------- */

struct EnumName
{
    const char *label;
    int value;
};

const EnumName kWindowModes[] = {
    {"windowed", MIOPAN_WINDOW_MODE_WINDOWED},
    {"borderless", MIOPAN_WINDOW_MODE_BORDERLESS},
    /* Accepted on read, never written: what someone would type by hand. */
    {"fullscreen", MIOPAN_WINDOW_MODE_BORDERLESS},
    {nullptr, 0},
};

const EnumName kRenderModes[] = {
    {"match_window", MIOPAN_RENDER_RES_MATCH_WINDOW},
    {"native", MIOPAN_RENDER_RES_NATIVE_PS2},
    {"window_scale", MIOPAN_RENDER_RES_WINDOW_SCALE},
    {nullptr, 0},
};

const EnumName kFilters[] = {
    {"nearest", MIOPAN_RENDER_FILTER_NEAREST},
    {"linear", MIOPAN_RENDER_FILTER_LINEAR},
    {nullptr, 0},
};

const EnumName kAspectModes[] = {
    {"auto", MIOPAN_ASPECT_AUTO},
    {"original", MIOPAN_ASPECT_ORIGINAL},
    {"4:3", MIOPAN_ASPECT_4_3},
    {"16:9", MIOPAN_ASPECT_16_9},
    {"16:10", MIOPAN_ASPECT_16_10},
    {"custom", MIOPAN_ASPECT_CUSTOM},
    {nullptr, 0},
};

const EnumName kLightingModes[] = {
    {"vertex", MIOPAN_LIGHTING_VERTEX},
    {"fragment", MIOPAN_LIGHTING_FRAGMENT},
    {"fragment-all", MIOPAN_LIGHTING_FRAGMENT_ALL},
    {nullptr, 0},
};

const EnumName kFilmGrain[] = {
    {"off", MIOPAN_FILM_GRAIN_OFF},
    {"ps2", MIOPAN_FILM_GRAIN_PS2},
    {"native", MIOPAN_FILM_GRAIN_NATIVE},
    /* What the 0/1 an older build wrote meant.  "on" was the ROM's sheet. */
    {"on", MIOPAN_FILM_GRAIN_PS2},
    {nullptr, 0},
};

const EnumName kShadowFilters[] = {
    {"none", MIOPAN_SHADOW_FILTER_NONE},
    {"soft", MIOPAN_SHADOW_FILTER_SOFT},
    {nullptr, 0},
};


const EnumName kHdrModes[] = {
    {"off", MIOPAN_HDR_OFF},
    {"auto", MIOPAN_HDR_AUTO},
    {"scrgb", MIOPAN_HDR_SCRGB},
    {"hdr10", MIOPAN_HDR_HDR10},
    /* Accepted on read, never written: what someone would type by hand. */
    {"on", MIOPAN_HDR_AUTO},
    {"pq", MIOPAN_HDR_HDR10},
    {"st2084", MIOPAN_HDR_HDR10},
    {nullptr, 0},
};

const EnumName kSmoothingModes[] = {
    {"field", MIOPAN_SMOOTHING_FIELD},
    {"display", MIOPAN_SMOOTHING_DISPLAY},
    {nullptr, 0},
};

const EnumName kVsyncModes[] = {
    {"on", MIOPAN_VSYNC_ON},
    {"off", MIOPAN_VSYNC_OFF},
    {"mailbox", MIOPAN_VSYNC_MAILBOX},
    /* Accepted on read, never written: the SDL names for the same modes. */
    {"vsync", MIOPAN_VSYNC_ON},
    {"fifo", MIOPAN_VSYNC_ON},
    {"immediate", MIOPAN_VSYNC_OFF},
    {nullptr, 0},
};

const EnumName kTitleBackdrops[] = {
    {"room", MIOPAN_TITLE_BACKDROP_ROOM},
    {"classic", MIOPAN_TITLE_BACKDROP_CLASSIC},
    /* Accepted on read, never written: what someone would type by hand. */
    {"original", MIOPAN_TITLE_BACKDROP_CLASSIC},
    {nullptr, 0},
};

const EnumName kTitleMotions[] = {
    {"pingpong", MIOPAN_TITLE_MOTION_PINGPONG},
    {"loop", MIOPAN_TITLE_MOTION_LOOP},
    {"once", MIOPAN_TITLE_MOTION_ONCE},
    {nullptr, 0},
};

const EnumName kTitleEasings[] = {
    {"smooth", MIOPAN_TITLE_EASING_SMOOTH},
    {"linear", MIOPAN_TITLE_EASING_LINEAR},
    {nullptr, 0},
};

const EnumName kTitleChanges[] = {
    {"pass", MIOPAN_TITLE_CHANGE_PASS},
    {"visit", MIOPAN_TITLE_CHANGE_VISIT},
    {nullptr, 0},
};

const EnumName kGyroTurnAxes[] = {
    {"yaw", MIOPAN_GYRO_TURN_YAW},
    {"roll", MIOPAN_GYRO_TURN_ROLL},
    {nullptr, 0},
};

const EnumName kMoveSchemes[] = {
    {"game", MIOPAN_MOVE_SCHEME_GAME},
    {"camera", MIOPAN_MOVE_SCHEME_CAMERA},
    {"tank", MIOPAN_MOVE_SCHEME_TANK},
    {nullptr, 0},
};

const EnumName kFinderAimSticks[] = {
    {"game", MIOPAN_FINDER_AIM_GAME},
    {"left", MIOPAN_FINDER_AIM_LEFT},
    {"right", MIOPAN_FINDER_AIM_RIGHT},
    {nullptr, 0},
};

const char *EnumToName(const EnumName *table, int value)
{
    for (const EnumName *e = table; e->label != nullptr; e++)
    {
        if (e->value == value)
        {
            return e->label;
        }
    }
    return table[0].label;
}

/* ------------------------------------------------------------------------
 *  A very small INI reader.  Sections, `key = value`, `;` or `#` comments,
 *  and nothing else -- there is no escaping or continuation to get wrong, and
 *  every value this file stores is a number or a short identifier.
 * --------------------------------------------------------------------- */

struct IniEntry
{
    std::string key; /* "section.name", lower-cased */
    std::string value;
    std::string section; /* lower-cased; the title's scenes are found by it */
};

std::vector<IniEntry> g_entries;
char g_path[1024];
bool g_path_resolved;

std::string Trim(const std::string &s)
{
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
    {
        return std::string();
    }
    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::string Lower(std::string s)
{
    for (char &c : s)
    {
        if (c >= 'A' && c <= 'Z')
        {
            c = (char)(c - 'A' + 'a');
        }
    }
    return s;
}

void ParseIni(const std::string &text)
{
    g_entries.clear();

    std::string section;
    size_t pos = 0;
    while (pos <= text.size())
    {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos)
        {
            end = text.size();
        }
        std::string line = Trim(text.substr(pos, end - pos));
        pos = end + 1;

        if (line.empty() || line[0] == ';' || line[0] == '#')
        {
            continue;
        }
        if (line[0] == '[')
        {
            size_t close = line.find(']');
            if (close != std::string::npos)
            {
                section = Lower(Trim(line.substr(1, close - 1)));
            }
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos)
        {
            continue;
        }
        IniEntry entry;
        entry.key = section + "." + Lower(Trim(line.substr(0, eq)));
        entry.value = Trim(line.substr(eq + 1));
        entry.section = section;
        g_entries.push_back(entry);
    }
}

const char *Lookup(const char *section, const char *name)
{
    std::string key = Lower(std::string(section) + "." + name);
    for (const IniEntry &entry : g_entries)
    {
        if (entry.key == key)
        {
            return entry.value.c_str();
        }
    }
    return nullptr;
}

void ReadInt(const char *section, const char *name, int *dst)
{
    const char *value = Lookup(section, name);
    if (value != nullptr && value[0] != '\0')
    {
        *dst = (int)std::strtol(value, nullptr, 10);
    }
}

void ReadFloat(const char *section, const char *name, float *dst)
{
    const char *value = Lookup(section, name);
    if (value != nullptr && value[0] != '\0')
    {
        *dst = (float)std::atof(value);
    }
}

/* Three numbers, separated by spaces or commas -- a camera point.  Anything
 * short of three leaves `dst` as it was, so a half-typed line cannot move the
 * camera to the origin. */
void ReadVec3(const char *section, const char *name, float *dst)
{
    const char *value = Lookup(section, name);
    if (value == nullptr || value[0] == '\0')
    {
        return;
    }

    float parsed[3];
    const char *cursor = value;
    for (int i = 0; i < 3; i++)
    {
        while (*cursor == ' ' || *cursor == '\t' || *cursor == ',')
        {
            cursor++;
        }
        char *end = nullptr;
        parsed[i] = (float)std::strtod(cursor, &end);
        if (end == cursor)
        {
            SDL_Log("MioPan: [%s] %s: wants three numbers, got \"%s\"; "
                    "keeping the default", section, name, value);
            return;
        }
        cursor = end;
    }
    dst[0] = parsed[0];
    dst[1] = parsed[1];
    dst[2] = parsed[2];
}

/* Verbatim, less surrounding whitespace, which ParseIni has already trimmed.
 * A path is the one setting that must not be interpreted -- no case folding, no
 * separator rewriting -- because on Android it can be a content:// tree URI
 * rather than a filesystem path at all. */
void ReadString(const char *section, const char *name, char *dst, size_t size)
{
    const char *value = Lookup(section, name);
    if (value != nullptr && value[0] != '\0')
    {
        SDL_strlcpy(dst, value, size);
    }
}

/* A name from the table, else a number.  Returns false, leaving `dst` alone,
 * for anything else. */
bool ParseEnum(const EnumName *table, const char *value, int *dst)
{
    for (const EnumName *e = table; e->label != nullptr; e++)
    {
        if (SDL_strcasecmp(value, e->label) == 0)
        {
            *dst = e->value;
            return true;
        }
    }
    char *end = nullptr;
    const long number = std::strtol(value, &end, 10);
    if (end == value)
    {
        return false;
    }
    *dst = (int)number;
    return true;
}

void ReadEnum(const char *section, const char *name, const EnumName *table,
              int *dst)
{
    const char *value = Lookup(section, name);
    if (value == nullptr || value[0] == '\0')
    {
        return;
    }
    /* Not a name we know: a number from an older file, or a typo.  Only a
     * number is taken.  A typo keeps what the field already holds, which at
     * load time is the default -- it used to store strtol's 0, which was the
     * same thing only while 0 was every enum's default, and vsync's is not. */
    if (!ParseEnum(table, value, dst))
    {
        SDL_Log("MioPan: [%s] %s: unrecognised value \"%s\"; keeping the "
                "default", section, name, value);
    }
}

/*
 * "W:H" as a ratio, e.g. "21:9" -> 2.333.  Returns false if the text is not
 * that shape.  The named aspects cover the common cases, but this file is meant
 * to be hand-edited and a colon form is what someone will type for anything
 * else; without it "21:9" parses as the number 21 and clamps to nonsense.
 */
bool ParseRatio(const char *text, float *out)
{
    const char *colon = std::strchr(text, ':');
    if (colon == nullptr)
    {
        return false;
    }
    double w = std::atof(text);
    double h = std::atof(colon + 1);
    if (!(w > 0.0) || !(h > 0.0))
    {
        return false;
    }
    *out = (float)(w / h);
    return true;
}

/* Aspect needs its own reader: a name, then W:H, then a bare number. */
void ReadAspect(const char *section, const char *name, int *mode, float *ratio)
{
    const char *value = Lookup(section, name);
    if (value == nullptr || value[0] == '\0')
    {
        return;
    }
    for (const EnumName *e = kAspectModes; e->label != nullptr; e++)
    {
        if (SDL_strcasecmp(value, e->label) == 0)
        {
            *mode = e->value;
            return;
        }
    }
    float parsed = 0.0f;
    if (ParseRatio(value, &parsed) ||
        (parsed = (float)std::atof(value)) > 0.0f)
    {
        *mode = MIOPAN_ASPECT_CUSTOM;
        *ratio = parsed;
    }
}

/* The renderer environment overrides, by name.  Listed here rather than asked
 * of the renderer so the rule stays visible next to the code it governs; the
 * cost is that a new override has to be added in both places, which the comment
 * on each getenv in miopan_renderer.cpp points out. */
/* ------------------------------------------------------------------------
 *  [input] -- the rebindable controls.
 *
 *  Unlike everything else in this file these do not live in MioPan_Config:
 *  miopan_input owns them, and this section is only the text form.  Keeping
 *  one copy is what stops the file and the live bindings drifting apart.
 * --------------------------------------------------------------------- */

const char *const kInputButtonNames[MIOPAN_PAD_BUTTON_COUNT] = {
    "up",    "down", "left", "right", "triangle", "cross",  "square", "circle",
    "l1",    "l2",   "r1",   "r2",    "start",    "select", "l3",     "r3"
};

const char *const kInputStickNames[MIOPAN_PAD_STICK_COUNT] = {
    "leftx", "lefty", "rightx", "righty"
};

/*
 * Split a source list.
 *
 * Sources are located by their prefixes rather than by splitting on commas,
 * because a key's own name may BE a comma: SDL_GetScancodeName() answers ","
 * for SDL_SCANCODE_COMMA, so "key:," is a perfectly legal binding that a comma
 * split would tear in half.  A prefix only opens a new source at a list
 * boundary -- start of line, or after a comma -- so "key:none" stays one
 * source rather than becoming two.
 */
std::vector<std::string> SplitSources(const std::string &value)
{
    static const char *const kPrefixes[] = {
        "key:", "pad:", "axis+:", "axis-:", "mouse:", "wheel:", "none"
    };

    std::vector<size_t> starts;
    for (size_t i = 0; i < value.size(); i++)
    {
        for (const char *prefix : kPrefixes)
        {
            size_t n = std::strlen(prefix);
            if (value.compare(i, n, prefix) != 0)
            {
                continue;
            }
            size_t back = i;
            while (back > 0 && (value[back - 1] == ' ' || value[back - 1] == '\t'))
            {
                back--;
            }
            if (back == 0 || value[back - 1] == ',')
            {
                starts.push_back(i);
            }
            break;
        }
    }

    std::vector<std::string> out;
    for (size_t s = 0; s < starts.size(); s++)
    {
        bool last = (s + 1 == starts.size());
        size_t end = last ? value.size() : starts[s + 1];
        std::string frag = Trim(value.substr(starts[s], end - starts[s]));

        /* Drop the separator, but only where there is one to drop.  A comma
         * ends the fragment in two quite different ways: in "key:,, pad:a" the
         * last character separates two sources, while in "key:," it IS the
         * source -- the comma key.  Only a fragment with another source after
         * it can be carrying a separator, so the last one is left alone and
         * ParseSourceList forgives a stray trailing comma instead. */
        if (!last && !frag.empty() && frag.back() == ',')
        {
            frag.pop_back();
            frag = Trim(frag);
        }
        if (!frag.empty())
        {
            out.push_back(frag);
        }
    }
    return out;
}

/*
 * Parse a source list into `dst`, up to `max` entries.  Returns how many
 * parsed.  Unrecognised sources are skipped with a warning rather than
 * clearing the slot: a settings file written by a newer build, or a typo,
 * should cost one binding and not the whole line.
 */
int ParseSourceList(const char *label, const char *value,
                    MioPan_InputSource *dst, int max)
{
    int count = 0;
    for (const std::string &frag : SplitSources(value))
    {
        if (count >= max)
        {
            SDL_Log("MioPan: [input] %s lists more than %d sources; "
                    "the rest are ignored", label, max);
            break;
        }
        MioPan_InputSource src;
        std::memset(&src, 0, sizeof(src));
        bool ok = MioPan_InputSourceFromString(frag.c_str(), &src) != 0;

        /* A trailing separator with nothing after it -- "key:A," -- is a
         * common enough typo to forgive, and it can only be reached here,
         * because SplitSources cannot tell it from the comma key. */
        if (!ok && frag.size() > 1 && frag.back() == ',')
        {
            ok = MioPan_InputSourceFromString(frag.substr(0, frag.size() - 1).c_str(),
                                              &src) != 0;
        }

        if (ok)
        {
            dst[count++] = src;
        }
        else
        {
            SDL_Log("MioPan: [input] %s: unrecognised source \"%s\", skipped",
                    label, frag.c_str());
        }
    }
    return count;
}

void LoadInputBindings()
{
    /* Which physical pad to use.  "auto" (or absent) keeps the first device SDL
     * lists, which is what a single-controller desk always wants; a GUID pins
     * one, so a wheel or a second pad enumerating first cannot take the slot. */
    const char *pad = Lookup("input", "gamepad");
    if (pad != nullptr && pad[0] != '\0' && SDL_strcasecmp(pad, "auto") != 0)
    {
        MioPan_InputSetPreferredGamepad(pad);
    }

    for (int i = 0; i < MIOPAN_PAD_BUTTON_COUNT; i++)
    {
        const char *value = Lookup("input", kInputButtonNames[i]);
        if (value == nullptr || value[0] == '\0')
        {
            continue; /* absent key keeps the default */
        }

        MioPan_InputButtonBinding binding;
        std::memset(&binding, 0, sizeof(binding));
        int got = ParseSourceList(kInputButtonNames[i], value, binding.src,
                                  MIOPAN_INPUT_MAX_SOURCES);

        /* Nothing at all parsed: the line is a typo rather than an intent, and
         * honouring it would silently leave the button dead.  "none" parses,
         * so deliberately clearing a button still works. */
        if (got == 0)
        {
            SDL_Log("MioPan: [input] %s had no usable source; keeping the "
                    "default", kInputButtonNames[i]);
            continue;
        }
        MioPan_InputSetButtonBinding(i, &binding);
    }

    for (int i = 0; i < MIOPAN_PAD_STICK_COUNT; i++)
    {
        MioPan_InputStickBinding binding;
        char name[64];

        if (!MioPan_InputGetStickBinding(i, &binding))
        {
            continue;
        }

        SDL_snprintf(name, sizeof(name), "%s.analog", kInputStickNames[i]);
        const char *analog = Lookup("input", name);
        if (analog != nullptr && analog[0] != '\0')
        {
            MioPan_InputSource src;
            std::memset(&src, 0, sizeof(src));
            if (ParseSourceList(name, analog, &src, 1) == 1)
            {
                binding.analog = src;
            }
        }

        SDL_snprintf(name, sizeof(name), "%s.keys", kInputStickNames[i]);
        const char *keys = Lookup("input", name);
        if (keys != nullptr && keys[0] != '\0')
        {
            MioPan_InputSource pair[2];
            std::memset(pair, 0, sizeof(pair));
            int got = ParseSourceList(name, keys, pair, 2);
            if (got >= 1)
            {
                binding.neg = pair[0];
            }
            if (got >= 2)
            {
                binding.pos = pair[1];
            }
        }

        SDL_snprintf(name, sizeof(name), "%s.ramp_frames", kInputStickNames[i]);
        ReadInt("input", name, &binding.ramp_frames);
        if (binding.ramp_frames < 1)
        {
            binding.ramp_frames = 1; /* 0 would freeze the digital contribution */
        }

        MioPan_InputSetStickBinding(i, &binding);
    }

    /* Finder mouse look.  Each starts from the live value, so a file written
     * before these existed -- or one missing a line -- keeps the default. */
    int look = MioPan_InputGetMouseLook();
    ReadInt("input", "mouse_look", &look);
    MioPan_InputSetMouseLook(look);

    /* A sensitivity that does not parse reads as 0 through atof; keeping the
     * default beats clamping a typo to the floor and leaving the mouse dead. */
    float sensitivity = 0.0f;
    ReadFloat("input", "mouse_sensitivity", &sensitivity);
    if (sensitivity > 0.0f)
    {
        MioPan_InputSetMouseSensitivity(sensitivity);
    }

    int invert = MioPan_InputGetMouseInvertX();
    ReadInt("input", "mouse_invert_x", &invert);
    MioPan_InputSetMouseInvertX(invert);

    invert = MioPan_InputGetMouseInvertY();
    ReadInt("input", "mouse_invert_y", &invert);
    MioPan_InputSetMouseInvertY(invert);

    /* Finder gyro look, on the same terms. */
    look = MioPan_InputGetGyroLook();
    ReadInt("input", "gyro_look", &look);
    MioPan_InputSetGyroLook(look);

    sensitivity = 0.0f;
    ReadFloat("input", "gyro_sensitivity", &sensitivity);
    if (sensitivity > 0.0f)
    {
        MioPan_InputSetGyroSensitivity(sensitivity);
    }

    invert = MioPan_InputGetGyroInvertX();
    ReadInt("input", "gyro_invert_x", &invert);
    MioPan_InputSetGyroInvertX(invert);

    invert = MioPan_InputGetGyroInvertY();
    ReadInt("input", "gyro_invert_y", &invert);
    MioPan_InputSetGyroInvertY(invert);

    int axis = MioPan_InputGetGyroTurnAxis();
    ReadEnum("input", "gyro_turn", kGyroTurnAxes, &axis);
    MioPan_InputSetGyroTurnAxis(axis);

    /* Movement schemes, on the same terms. */
    int scheme = MioPan_InputGetStickMoveScheme();
    ReadEnum("input", "stick_steering", kMoveSchemes, &scheme);
    MioPan_InputSetStickMoveScheme(scheme);

    scheme = MioPan_InputGetDpadMoveScheme();
    ReadEnum("input", "dpad_steering", kMoveSchemes, &scheme);
    MioPan_InputSetDpadMoveScheme(scheme);

    int stick = MioPan_InputGetFinderAimStick();
    ReadEnum("input", "finder_aim_stick", kFinderAimSticks, &stick);
    MioPan_InputSetFinderAimStick(stick);
}

/* One source as text, or "none" for an empty slot. */
std::string InputSourceText(const MioPan_InputSource &src)
{
    char buf[128];
    if (MioPan_InputSourceToString(&src, buf, sizeof(buf)) > 0)
    {
        return std::string(buf);
    }
    return std::string("none");
}

std::string BuildInputSection()
{
    std::string out =
        "[input]\n"
        "; Rebindable controls.  Each line lists every source that can press\n"
        ";   one PS2 button -- any of them will do, up to four, separated by\n"
        ";   commas.  These 16 buttons are what the whole game reads through,\n"
        ";   so rebinding one here moves it everywhere at once.\n"
        ";\n"
        ";   key:<name>     a keyboard key, by SDL's name: \"Space\",\n"
        ";                  \"Left Shift\", \"F1\", \",\"\n"
        ";   pad:<name>     a gamepad button: \"a\", \"leftshoulder\", \"start\"\n"
        ";   axis+:<name>   a gamepad axis, positive half: \"lefttrigger\"\n"
        ";   axis-:<name>   the negative half of one\n"
        ";   mouse:<name>   a mouse button: \"left\", \"right\", \"middle\",\n"
        ";                  \"x1\", \"x2\", or a number for one past those\n"
        ";   wheel:<dir>    the wheel: \"up\", \"down\", \"left\", \"right\";\n"
        ";                  each notch is one press\n"
        ";   none           an empty slot -- how you deliberately unbind\n"
        ";\n"
        ";   An axis counts as pressed past 8000 of its 0..32767 travel;\n"
        ";   append \"@<n>\" to change that, e.g. axis+:lefttrigger@20000.\n"
        ";\n"
        "; Delete a line to restore that control's default; delete the whole\n"
        ";   section to restore all of them.  A name this build does not know\n"
        ";   is skipped with a note in the log, and a line with nothing usable\n"
        ";   on it keeps its default rather than leaving the button dead.\n";

    {
        const char *pref = MioPan_InputGetPreferredGamepad();
        char        active[128];

        out += "\n; Which gamepad to use: \"auto\" for the first one SDL\n"
               ";   lists, or a device GUID to pin one -- useful when a wheel\n"
               ";   or a second pad also carries a mapping.  An unplugged\n"
               ";   preference is kept and reclaimed when it comes back.\n";
        if (MioPan_InputGetActiveGamepad(active, sizeof(active), nullptr, 0) &&
            active[0] != '\0')
        {
            out += "; in use now: ";
            out += active;
            out += "\n";
        }
        for (int i = 0, n = MioPan_InputGetGamepadCount(); i < n; i++)
        {
            char name[128];
            char guid[MIOPAN_GAMEPAD_GUID_LEN];
            if (MioPan_InputGetGamepadInfo(i, name, sizeof(name), guid,
                                           sizeof(guid)))
            {
                out += ";   ";
                out += guid;
                out += "  ";
                out += name;
                out += "\n";
            }
        }
        out += "gamepad = ";
        out += (pref != nullptr && pref[0] != '\0') ? pref : "auto";
        out += "\n\n";
    }

    for (int i = 0; i < MIOPAN_PAD_BUTTON_COUNT; i++)
    {
        MioPan_InputButtonBinding binding;
        if (!MioPan_InputGetButtonBinding(i, &binding))
        {
            continue;
        }

        std::string list;
        for (int s = 0; s < MIOPAN_INPUT_MAX_SOURCES; s++)
        {
            if (binding.src[s].kind == MIOPAN_SRC_NONE)
            {
                continue;
            }
            if (!list.empty())
            {
                list += ", ";
            }
            list += InputSourceText(binding.src[s]);
        }
        if (list.empty())
        {
            list = "none";
        }

        out += kInputButtonNames[i];
        out += " = ";
        out += list;
        out += "\n";
    }

    out +=
        "\n"
        "; Sticks.  `analog` is the gamepad axis; `keys` is the pair of keys\n"
        ";   pulling toward 0 and toward 255, in that order.  ramp_frames is\n"
        ";   how many updates a held key needs to reach full deflection: 1\n"
        ";   snaps, which is what movement wants, while the aim stick ramps so\n"
        ";   that keyboard aiming is steerable rather than all-or-nothing.\n";

    for (int i = 0; i < MIOPAN_PAD_STICK_COUNT; i++)
    {
        MioPan_InputStickBinding binding;
        char line[256];

        if (!MioPan_InputGetStickBinding(i, &binding))
        {
            continue;
        }

        SDL_snprintf(line, sizeof(line), "%s.analog = %s\n",
                     kInputStickNames[i],
                     InputSourceText(binding.analog).c_str());
        out += line;

        SDL_snprintf(line, sizeof(line), "%s.keys = %s, %s\n",
                     kInputStickNames[i],
                     InputSourceText(binding.neg).c_str(),
                     InputSourceText(binding.pos).c_str());
        out += line;

        SDL_snprintf(line, sizeof(line), "%s.ramp_frames = %d\n",
                     kInputStickNames[i], binding.ramp_frames);
        out += line;
    }

    {
        char line[256];

        out +=
            "\n"
            "; Mouse look.  While the Camera Obscura is raised the cursor is\n"
            ";   captured and the mouse aims it; it is let go when the camera\n"
            ";   comes down, and F1 while aiming hands it to the overlay.\n"
            ";   mouse_sensitivity scales the turn: 1.0 is 0.05 degrees per\n"
            ";   count of mouse travel, from 0.05 to 10.  The invert flags\n"
            ";   flip one axis each; the game's own vertical-camera option\n"
            ";   applies to the stick, not to the mouse.\n";

        SDL_snprintf(line, sizeof(line),
                     "mouse_look = %d\n"
                     "mouse_sensitivity = %.3f\n"
                     "mouse_invert_x = %d\n"
                     "mouse_invert_y = %d\n",
                     MioPan_InputGetMouseLook(),
                     (double)MioPan_InputGetMouseSensitivity(),
                     MioPan_InputGetMouseInvertX(),
                     MioPan_InputGetMouseInvertY());
        out += line;

        out +=
            "\n"
            "; Gyro look.  While the Camera Obscura is raised, a gamepad with a\n"
            ";   gyroscope aims it by being turned; the stick still works\n"
            ";   alongside it.  gyro_sensitivity multiplies the pad's own\n"
            ";   rotation: 1.0 turns the camera as far as the pad turned, from\n"
            ";   0.1 to 8.  gyro_turn picks what turns it left and right: yaw\n"
            ";   (point the pad, like a torch) or roll (tip it, like a steering\n"
            ";   wheel).  The gyro's drift is learned whenever the pad lies\n"
            ";   still with the camera down, and is not stored here.\n";

        SDL_snprintf(line, sizeof(line),
                     "gyro_look = %d\n"
                     "gyro_sensitivity = %.3f\n"
                     "gyro_invert_x = %d\n"
                     "gyro_invert_y = %d\n"
                     "gyro_turn = %s\n",
                     MioPan_InputGetGyroLook(),
                     (double)MioPan_InputGetGyroSensitivity(),
                     MioPan_InputGetGyroInvertX(),
                     MioPan_InputGetGyroInvertY(),
                     EnumToName(kGyroTurnAxes, MioPan_InputGetGyroTurnAxis()));
        out += line;

        out +=
            "\n"
            "; Movement.  The game's Options screen has one control type for\n"
            ";   the whole pad: normal, where only the stick walks and Mio\n"
            ";   heads where it points as the camera sees it, or vehicle, where\n"
            ";   the stick and the d-pad both turn her and drive her forward.\n"
            ";   stick_steering and dpad_steering override it per control:\n"
            ";   game (its setting), camera or tank.  finder_aim_stick picks\n"
            ";   the stick that aims the Camera Obscura -- game, left or right\n"
            ";   -- and the other one sidesteps.\n";

        SDL_snprintf(line, sizeof(line),
                     "stick_steering = %s\n"
                     "dpad_steering = %s\n"
                     "finder_aim_stick = %s\n",
                     EnumToName(kMoveSchemes, MioPan_InputGetStickMoveScheme()),
                     EnumToName(kMoveSchemes, MioPan_InputGetDpadMoveScheme()),
                     EnumToName(kFinderAimSticks,
                                MioPan_InputGetFinderAimStick()));
        out += line;
    }

    return out;
}

bool EnvOverrideActive()
{
    static const char *const kNames[] = {
        "MIOPAN_RENDER_RES",   "MIOPAN_RENDER_SCALE",
        "MIOPAN_RENDER_FILTER", "MIOPAN_ASPECT",
        "MIOPAN_WINDOW_MODE",  "MIOPAN_LIGHTING_MODE",
        "MIOPAN_VSYNC",        "MIOPAN_VOLUME",
        "MIOPAN_UI_SCALE",     "MIOPAN_SMOOTHING",
    };
    for (const char *name : kNames)
    {
        const char *value = std::getenv(name);
        if (value != nullptr && value[0] != '\0')
        {
            return true;
        }
    }
    return false;
}

const char *EnvValue(const char *name)
{
    const char *value = std::getenv(name);
    return (value != nullptr && value[0] != '\0') ? value : nullptr;
}

/*
 * The environment overrides for the settings that came after the renderer's.
 *
 * Applied here, over what the file said, rather than by each owner the way the
 * renderer reads its older MIOPAN_* variables: these three belong to three
 * different modules -- renderer, voice engine, overlay -- and this file is
 * where the precedence is written down.  Like the others, any one of them also
 * stops the session saving; see EnvOverrideActive().
 *
 *   MIOPAN_VSYNC     off | on | mailbox
 *   MIOPAN_VOLUME    0 .. 1
 *   MIOPAN_UI_SCALE  0 to follow the display, else a fixed factor
 *   MIOPAN_SMOOTHING field | display
 */
void ApplyEnvironmentOverrides(int *vsync, float *volume, float *ui_scale,
                               int *smoothing)
{
    if (const char *value = EnvValue("MIOPAN_SMOOTHING"))
    {
        if (!ParseEnum(kSmoothingModes, value, smoothing))
        {
            SDL_Log("MioPan: MIOPAN_SMOOTHING=\"%s\" not understood", value);
        }
    }
    if (const char *value = EnvValue("MIOPAN_VSYNC"))
    {
        if (!ParseEnum(kVsyncModes, value, vsync))
        {
            SDL_Log("MioPan: MIOPAN_VSYNC=\"%s\" not understood", value);
        }
    }
    if (const char *value = EnvValue("MIOPAN_VOLUME"))
    {
        *volume = (float)std::atof(value);
    }
    if (const char *value = EnvValue("MIOPAN_UI_SCALE"))
    {
        *ui_scale = (float)std::atof(value);
    }
}

/* ------------------------------------------------------------------------
 *  The title screen: [title], then one [title.sceneN] per scene.
 *
 *  The same text is the file's and the clipboard's, so a scene copied out of
 *  the overlay pastes back in, or into someone else's miopan.ini, unchanged.
 *  Sections are found by name rather than by number: [title.scene3] after
 *  [title.scene1] is the second scene, and a pasted block whose number is
 *  already taken is simply the next one.
 * --------------------------------------------------------------------- */

const MioPan_ConfigTitleScene kTitleSceneDefault = {
    /* .name           = */ "",
    /* .enabled        = */ 1,
    /* .room           = */ 40,
    /* 0: placed by itself when its room loads. */
    /* .camera_set     = */ 0,
    /* .a_eye          = */ { 0.0f, 0.0f, 0.0f },
    /* .a_target       = */ { 0.0f, 0.0f, 1.0f },
    /* .a_fov          = */ 50.0f,
    /* .b_eye          = */ { 0.0f, 0.0f, 0.0f },
    /* .b_target       = */ { 0.0f, 0.0f, 1.0f },
    /* .b_fov          = */ 50.0f,
    /* .travel_seconds = */ 30.0f,
    /* .hold_seconds   = */ 4.0f,
    /* .motion         = */ MIOPAN_TITLE_MOTION_PINGPONG,
    /* .easing         = */ MIOPAN_TITLE_EASING_SMOOTH,
};

/* The scenes this build ships with: the defaults' list, kept by
 * ConfigLoadOnce() before the file replaces it.  A miopan.ini that has scenes
 * of its own never sees the defaults otherwise, and the overlay offers these
 * back to it. */
MioPan_ConfigTitle g_title_builtin;

/* A scene counts as already in the list when one in the same room has its
 * name -- its camera may have been moved since. */
bool TitleHasScene(const MioPan_ConfigTitle &t, const MioPan_ConfigTitleScene &s)
{
    for (int i = 0; i < t.scene_count; i++)
    {
        if (t.scenes[i].room == s.room &&
            SDL_strcasecmp(t.scenes[i].name, s.name) == 0)
        {
            return true;
        }
    }
    return false;
}

/* Nothing between this file and outgame/title_room.c clamps these, and a room
 * number off the end of the table would load whatever file sits there.  The
 * room list itself is the game's, so only its length is checked here;
 * title_room.c refuses a room the disc does not carry. */
void ClampTitleScene(MioPan_ConfigTitleScene *s)
{
    if (s->room < 0 || s->room > 65)
    {
        s->room = 40;
        s->camera_set = 0;
    }
    s->enabled = s->enabled != 0 ? 1 : 0;
    s->camera_set = s->camera_set != 0 ? 1 : 0;
    s->a_fov = SDL_clamp(s->a_fov, 10.0f, 120.0f);
    s->b_fov = SDL_clamp(s->b_fov, 10.0f, 120.0f);
    s->travel_seconds = SDL_clamp(s->travel_seconds, 0.5f, 600.0f);
    s->hold_seconds = SDL_clamp(s->hold_seconds, 0.0f, 600.0f);
    if (s->motion < MIOPAN_TITLE_MOTION_PINGPONG || s->motion > MIOPAN_TITLE_MOTION_ONCE)
    {
        s->motion = MIOPAN_TITLE_MOTION_PINGPONG;
    }
    if (s->easing != MIOPAN_TITLE_EASING_LINEAR)
    {
        s->easing = MIOPAN_TITLE_EASING_SMOOTH;
    }
    s->name[MIOPAN_TITLE_NAME_MAX - 1] = '\0';
}

void ClampTitle(MioPan_ConfigTitle *t)
{
    if (t->backdrop != MIOPAN_TITLE_BACKDROP_ROOM)
    {
        t->backdrop = MIOPAN_TITLE_BACKDROP_CLASSIC;
    }
    if (t->change != MIOPAN_TITLE_CHANGE_VISIT)
    {
        t->change = MIOPAN_TITLE_CHANGE_PASS;
    }
    t->passes = SDL_clamp(t->passes, 1, 100);
    t->dim = SDL_clamp(t->dim, 0.0f, 1.0f);
    t->music_volume = SDL_clamp(t->music_volume, 0.0f, 1.0f);
    t->attract_movie = t->attract_movie != 0 ? 1 : 0;
    t->scene_count = SDL_clamp(t->scene_count, 1, MIOPAN_TITLE_SCENE_MAX);
}

/* The scene keys out of one section, over the defaults.  The section is the
 * scene's whole description, so nothing of whatever the slot held before
 * survives into it. */
void ReadTitleScene(const char *section, int number, MioPan_ConfigTitleScene *s)
{
    *s = kTitleSceneDefault;
    SDL_snprintf(s->name, sizeof(s->name), "Scene %d", number);

    ReadString(section, "name", s->name, sizeof(s->name));
    ReadInt(section, "enabled", &s->enabled);
    ReadInt(section, "room", &s->room);
    ReadInt(section, "camera_set", &s->camera_set);
    ReadVec3(section, "a_eye", s->a_eye);
    ReadVec3(section, "a_target", s->a_target);
    ReadFloat(section, "a_fov", &s->a_fov);
    ReadVec3(section, "b_eye", s->b_eye);
    ReadVec3(section, "b_target", s->b_target);
    ReadFloat(section, "b_fov", &s->b_fov);
    ReadFloat(section, "travel_seconds", &s->travel_seconds);
    ReadFloat(section, "hold_seconds", &s->hold_seconds);
    ReadEnum(section, "motion", kTitleMotions, &s->motion);
    ReadEnum(section, "easing", kTitleEasings, &s->easing);
    ClampTitleScene(s);
}

/* Every scene the parsed text holds, in the order it holds them.  A
 * [title.sceneN] section is one, whatever N is.  A camera written straight
 * into [title] -- how the first build of this kept its single scene -- is one
 * too, when there is no scene section at all; so is a block of scene keys
 * with no section header, which is what a hand-picked paste looks like. */
int CollectTitleScenes(MioPan_ConfigTitleScene *out, int max)
{
    std::vector<std::string> sections;
    for (const IniEntry &entry : g_entries)
    {
        const bool scene = entry.section.compare(0, 11, "title.scene") == 0 ||
                           entry.section.empty();
        bool seen = false;
        for (const std::string &s : sections)
        {
            seen = seen || s == entry.section;
        }
        if (scene && !seen)
        {
            sections.push_back(entry.section);
        }
    }

    int n = 0;
    for (const std::string &section : sections)
    {
        if (n >= max)
        {
            break;
        }
        /* A headerless block counts only if it says which room. */
        if (section.empty() && Lookup("", "room") == nullptr)
        {
            continue;
        }
        ReadTitleScene(section.c_str(), n + 1, &out[n]);
        n++;
    }

    if (n == 0 && max > 0 && Lookup("title", "room") != nullptr)
    {
        ReadTitleScene("title", 1, &out[0]);
        n = 1;
    }
    return n;
}

/* [title]'s own settings, then its scenes.  With `scenes` false the scene
 * list is left as it is -- the paste that adds scenes. */
void ReadTitle(MioPan_ConfigTitle *t, bool scenes)
{
    ReadEnum("title", "backdrop", kTitleBackdrops, &t->backdrop);
    ReadEnum("title", "change", kTitleChanges, &t->change);
    ReadInt("title", "passes", &t->passes);
    ReadFloat("title", "dim", &t->dim);
    ReadInt("title", "music", &t->music);
    ReadFloat("title", "music_volume", &t->music_volume);
    ReadInt("title", "attract_movie", &t->attract_movie);

    if (scenes)
    {
        MioPan_ConfigTitleScene found[MIOPAN_TITLE_SCENE_MAX];
        const int n = CollectTitleScenes(found, MIOPAN_TITLE_SCENE_MAX);

        if (n > 0)
        {
            for (int i = 0; i < n; i++)
            {
                t->scenes[i] = found[i];
            }
            t->scene_count = n;
        }
    }
    ClampTitle(t);
}

/* A name is written verbatim, so it must not be able to end its own line or
 * open a section. */
std::string TitleSceneName(const char *name)
{
    std::string out;
    for (const char *c = name; *c != '\0'; c++)
    {
        out += (*c == '\n' || *c == '\r' || *c == '[' || *c == ']') ? ' ' : *c;
    }
    return Trim(out);
}

std::string TitleSceneText(const MioPan_ConfigTitleScene &s, int number)
{
    char buf[1024];
    SDL_snprintf(
        buf, sizeof(buf),
        "[title.scene%d]\n"
        "name = %s\n"
        "enabled = %d\n"
        "room = %d\n"
        "camera_set = %d\n"
        "a_eye = %.2f %.2f %.2f\n"
        "a_target = %.2f %.2f %.2f\n"
        "a_fov = %.2f\n"
        "b_eye = %.2f %.2f %.2f\n"
        "b_target = %.2f %.2f %.2f\n"
        "b_fov = %.2f\n"
        "travel_seconds = %.2f\n"
        "hold_seconds = %.2f\n"
        "motion = %s\n"
        "easing = %s\n"
        "\n",
        number,
        TitleSceneName(s.name).c_str(),
        s.enabled,
        s.room,
        s.camera_set,
        (double)s.a_eye[0], (double)s.a_eye[1], (double)s.a_eye[2],
        (double)s.a_target[0], (double)s.a_target[1], (double)s.a_target[2],
        (double)s.a_fov,
        (double)s.b_eye[0], (double)s.b_eye[1], (double)s.b_eye[2],
        (double)s.b_target[0], (double)s.b_target[1], (double)s.b_target[2],
        (double)s.b_fov,
        (double)s.travel_seconds,
        (double)s.hold_seconds,
        EnumToName(kTitleMotions, s.motion),
        EnumToName(kTitleEasings, s.easing));
    return buf;
}

std::string TitleText()
{
    const MioPan_ConfigTitle &t = miopan_config.title;
    char buf[3072];
    SDL_snprintf(
        buf, sizeof(buf),
        "[title]\n"
        "; The title screen's backdrop: room | classic.  room draws one of the\n"
        ";   game's rooms, lit as the game lights it, behind the original menus\n"
        ";   while the camera travels between two points.  classic is the\n"
        ";   original title, picture and scrolling art and all.  The F1\n"
        ";   overlay's Settings > Title screen window edits everything here\n"
        ";   live, and copies it out and pastes it back in.\n"
        "backdrop = %s\n"
        "; When the title moves on to another scene, picked at random among\n"
        ";   the enabled ones: pass (after `passes` runs of the camera path)\n"
        ";   | visit (each time the title comes up).\n"
        "change = %s\n"
        "passes = %d\n"
        "; Black laid over the room under the menus, 0 none .. 1 black.\n"
        "dim = %.3f\n"
        "; 0 is the game's own title music, played as the original plays it.\n"
        ";   Anything else is a stream file number, looped -- the overlay\n"
        ";   lists them; 2270 is KAN007_KUROSAWA, 2238 the title theme.\n"
        "music = %d\n"
        "; Its volume, 0..1; 1 is the level the game plays its music at.\n"
        "music_volume = %.3f\n"
        "; 1 lets the attract movie start after fifteen idle seconds, as the\n"
        ";   original does.  Room backdrop only: classic always does.\n"
        "attract_movie = %d\n"
        "\n"
        "; The scenes, one section each.  room is 0..65 as the disc counts\n"
        ";   them: 0..12 RY00..RY12, 13 ROS00, 19 RTB00, 30 RRY00, 40 RKS00,\n"
        ";   58 RCH00, 65 RY13.  camera_set 0 has the camera placed by itself\n"
        ";   when the room loads.  The points are where the eye is and what\n"
        ";   it looks at, in the game's world units (Y grows downwards), and\n"
        ";   the field of view in degrees.  travel_seconds is one pass from A\n"
        ";   to B, hold_seconds the rest at each end.  motion: pingpong (there\n"
        ";   and back) | loop (cut back to A) | once.  easing: smooth | linear.\n"
        ";   enabled 0 keeps a scene out of the rotation.\n"
        "\n",
        EnumToName(kTitleBackdrops, t.backdrop),
        EnumToName(kTitleChanges, t.change),
        t.passes,
        (double)t.dim,
        t.music,
        (double)t.music_volume,
        t.attract_movie);

    std::string out = buf;
    for (int i = 0; i < t.scene_count; i++)
    {
        out += TitleSceneText(t.scenes[i], i + 1);
    }
    return out;
}

int CopyOut(const std::string &text, char *buf, int size)
{
    if (buf != nullptr && size > 0)
    {
        SDL_strlcpy(buf, text.c_str(), (size_t)size);
    }
    return (int)text.size();
}

} // namespace

extern "C" {

const char *MioPan_ConfigPath(void)
{
    if (!g_path_resolved)
    {
        g_path_resolved = true;
        if (MioPan_PathResolveUser("miopan.ini", g_path, sizeof(g_path)) == 0)
        {
            g_path[0] = '\0';
        }
    }
    return g_path[0] != '\0' ? g_path : nullptr;
}

static void ConfigLoadOnce(void)
{
    g_title_builtin = miopan_config.title;

    const char *path = MioPan_ConfigPath();
    if (path == nullptr)
    {
        return;
    }

    MioPan_File *file = MioPan_FileOpen(path, "rb");
    if (file == nullptr)
    {
        /*
         * First run.  Write the defaults out rather than waiting for a setting
         * to change.
         *
         * This used to be left alone on purpose, so that a file appeared only
         * once the player had chosen something.  [paths] data_folder ends that:
         * on a platform with no shell and no file manager pointed at the
         * install, an absent file is a setting that cannot be reached, and the
         * folder holding the game's data is exactly the setting most likely to
         * need reaching there.
         */
        MioPan_ConfigSave();
        return;
    }

    std::string text;
    int64_t size = MioPan_FileStreamSize(file);
    if (size > 0 && size < (1 << 20))
    {
        text.resize((size_t)size);
        size_t got = MioPan_FileRead(file, &text[0], (size_t)size);
        text.resize(got);
    }
    MioPan_FileCloseHandle(file);

    ParseIni(text);

    /* Before the renderer keys, because the paths module is what called us and
     * this is the value it is waiting on. */
    ReadString("paths", "data_folder", miopan_config.paths.data_folder,
               sizeof(miopan_config.paths.data_folder));

    MioPan_ConfigRenderer *r = &miopan_config.renderer;
    ReadEnum("window", "mode", kWindowModes, &r->window_mode);
    ReadInt("window", "width", &r->window_width);
    ReadInt("window", "height", &r->window_height);
    ReadEnum("window", "vsync", kVsyncModes, &r->vsync);
    ReadInt("window", "pause_in_background", &r->pause_in_background);
    ReadFloat("audio", "volume", &miopan_config.audio.volume);
    ReadInt("audio", "mute_in_background",
            &miopan_config.audio.mute_in_background);
    ReadInt("audio", "japanese_voices", &miopan_config.audio.japanese_voices);
    ReadFloat("ui", "scale", &miopan_config.ui.scale);
    ReadEnum("render", "mode", kRenderModes, &r->render_mode);
    ReadFloat("render", "scale", &r->render_scale);
    ReadEnum("render", "upscale_filter", kFilters, &r->upscale_filter);
    ReadInt("render", "smooth_2d", &r->smooth_2d);
    ReadInt("render", "msaa", &r->msaa_samples);
    ReadInt("render", "anisotropy", &r->anisotropy);
    ReadFloat("render", "aspect_ratio", &r->aspect_ratio);
    ReadFloat("render", "alpha_sharpen", &r->alpha_sharpen);
    ReadFloat("render", "alpha_cutoff", &r->alpha_cutoff);
    ReadInt("render", "resident_meshes", &r->resident_meshes);
    ReadInt("render", "resident_textures", &r->resident_textures);
    ReadInt("render", "gs_by_reference", &r->gs_by_reference);
    ReadInt("render", "indexed_streams", &r->indexed_streams);
    /* After aspect_ratio, so a "21:9" written in `aspect` wins over a stale
     * ratio left in the companion key. */
    ReadAspect("render", "aspect", &r->aspect_mode, &r->aspect_ratio);
    ReadEnum("lighting", "spotlights", kLightingModes, &r->lighting_mode);
    ReadEnum("shadow", "filter", kShadowFilters, &r->shadow_filter);
    ReadEnum("smoothing", "mode", kSmoothingModes, &r->smoothing_mode);
    ReadInt("smoothing", "extra_presents", &r->extra_presents);
    ReadInt("smoothing", "interpolate_camera", &r->interpolate_presents);
    ReadInt("smoothing", "interpolate_motion", &r->interpolate_geometry);
    ReadEnum("hdr", "mode", kHdrModes, &r->hdr_mode);
    ReadFloat("hdr", "paper_white", &r->hdr_paper_white);
    ReadFloat("hdr", "peak", &r->hdr_peak);
    ReadFloat("hdr", "highlight_expansion", &r->hdr_expansion);
    ReadFloat("hdr", "highlight_knee", &r->hdr_expansion_knee);
    ReadFloat("grade", "brightness", &r->grade_brightness);
    ReadFloat("grade", "contrast", &r->grade_contrast);
    ReadFloat("grade", "gamma", &r->grade_gamma);
    ReadFloat("grade", "saturation", &r->grade_saturation);
    ReadEnum("effects", "film_grain", kFilmGrain, &r->film_grain);
    ReadInt("finder", "surround", &r->finder_mask);
    ReadFloat("finder", "surround_blur", &r->finder_mask_blur);
    ReadFloat("finder", "surround_darken", &r->finder_mask_darken);
    ReadFloat("finder", "surround_tint", &r->finder_mask_tint);
    ReadFloat("finder", "surround_scale", &r->finder_mask_scale);
    ReadInt("developer", "debug_menu", &miopan_config.developer.debug_menu);
    miopan_config.developer.debug_menu =
        miopan_config.developer.debug_menu != 0 ? 1 : 0;

    ReadTitle(&miopan_config.title, true);

    /* The window size is the one value with no setter to clamp it, and a zero
     * or negative one would fail SDL_CreateWindow outright. */
    if (r->window_width < 320)
    {
        r->window_width = 320;
    }
    if (r->window_height < 240)
    {
        r->window_height = 240;
    }

    LoadInputBindings();

    g_entries.clear();
    SDL_Log("MioPan: loaded settings from %s", path);
}

void MioPan_ConfigLoad(void)
{
    /*
     * Exactly once, whichever thread arrives first.
     *
     * Two do: the paths module calls this the moment anything asks where the
     * game data is -- and the IOP emulation asks from its own reader thread --
     * while EnsureRenderer() calls it again on the main thread before applying
     * the settings.  A function-local static is initialized once and the
     * language serializes it, which is both halves of what is needed here; a
     * plain bool would be a data race, and a second parse would undo whatever
     * the settings UI had changed since the first.
     */
    static const bool once = (ConfigLoadOnce(), true);
    (void)once;
}

int MioPan_ConfigSave(void)
{
    /*
     * Saving is disabled outright for any session that took an environment
     * override.
     *
     * The alternative is worse than it looks: the capture reads the renderer's
     * live state, which under an override is the environment's value, so the
     * first unrelated settings change would quietly write the debug value into
     * the player's file.  An override is a one-off test run by intent -- the
     * honest behaviour is to leave the file exactly as it was.
     */
    if (EnvOverrideActive())
    {
        static bool warned;
        if (!warned)
        {
            warned = true;
            SDL_Log("MioPan: MIOPAN_* override in effect; settings will not "
                    "be saved this session");
        }
        return 0;
    }

    const char *path = MioPan_ConfigPath();
    if (path == nullptr)
    {
        return 0;
    }

    const MioPan_ConfigRenderer *r = &miopan_config.renderer;

    /* Formatted through a lambda so the format string and its arguments stay
     * in one place while the buffer is sized around them.
     *
     * This was a fixed char[4096], and a settings file that outgrew it simply
     * stopped being written -- the log line below was the only sign, and every
     * later change went on being lost.  Adding one commented setting was enough
     * to cross it.  Re-running the format at the length it asks for cannot go
     * stale the same way. */
    /* Built once, outside the lambda, because the lambda may run twice: the
     * section is variable-length text rather than a fixed format string, and
     * regenerating it on the resize pass would be pure waste. */
    const std::string input_section = BuildInputSection();
    const std::string title_section = TitleText();

    auto format = [r, &input_section, &title_section](char *buf, size_t cap) {
        return SDL_snprintf(
        buf, cap,
        "; MioPan settings.  Deleting this file restores the defaults.\n"
        "; MIOPAN_* environment variables override anything set here.\n"
        "\n"
        "[paths]\n"
        "; The folder holding the game's files -- IMG_BD.BIN, or an extracted\n"
        ";   bin/data tree.  It may be anywhere; nothing requires it to be\n"
        ";   near MioPan itself, which is the point of the setting.\n"
        ";   Leave it empty to have the folder looked for beside the\n"
        ";   executable and above it, and whatever is found written back here.\n"
        ";   MIOPAN_DATA_DIR overrides it for one run.\n"
        "data_folder = %s\n"
        "\n"
        "[window]\n"
        "; windowed | borderless   (borderless is desktop fullscreen)\n"
        "mode = %s\n"
        "; windowed size only -- going fullscreen does not overwrite these\n"
        "width = %d\n"
        "height = %d\n"
        "; Presentation sync: on | off | mailbox.  This decides only whether a\n"
        ";   frame may replace the one on screen mid-scan; the game keeps its\n"
        ";   own pace, which is a timer, whatever it is set to.\n"
        ";     on       no tearing.  Works everywhere.\n"
        ";     off      tears, but never waits for the display.\n"
        ";     mailbox  no tearing and no waiting, where the driver offers\n"
        ";              it; on where it does not.\n"
        ";   MIOPAN_VSYNC overrides it for one run.\n"
        "vsync = %s\n"
        "; Hold the game while the window is minimised or in the background\n"
        ";   -- audio, movies and all -- until it is brought back.  0 off, 1 on.\n"
        "pause_in_background = %d\n"
        "\n"
        "[audio]\n"
        "; Master volume, 0..1, applied to everything the game plays, movies\n"
        ";   included.  It is a slider position rather than a gain: the gain\n"
        ";   is its square, so 0.5 is about -12 dB.  MIOPAN_VOLUME overrides\n"
        ";   it for one run.\n"
        "volume = %.3f\n"
        "; Silence the game while the window is minimised or in the background.\n"
        ";   The game itself carries on -- see [window] pause_in_background\n"
        ";   for stopping it.  0 off, 1 on.\n"
        "mute_in_background = %d\n"
        "; The cast's language once Options > Special has imported the\n"
        ";   Japanese voices from a copy of the Japanese release: spoken\n"
        ";   lines and cutscene soundtracks, with the subtitles retimed to\n"
        ";   them.  0 English, 1 Japanese.  Nothing changes until they are\n"
        ";   imported.\n"
        "japanese_voices = %d\n"
        "\n"
        "[ui]\n"
        "; Size of the F1 overlay.  0 follows the display's own scale setting,\n"
        ";   including onto another monitor; anything else is a fixed factor,\n"
        ";   e.g. 1.5.  MIOPAN_UI_SCALE overrides it for one run.\n"
        "scale = %.2f\n"
        "\n"
        "[render]\n"
        "; match_window | native | window_scale\n"
        ";   native rasterises at 640x448 x scale, i.e. PS2 pixel density\n"
        "mode = %s\n"
        "scale = %.4f\n"
        "; nearest = sharp but uneven at non-integer scale; linear = soft\n"
        "upscale_filter = %s\n"
        "; Bilinear text and sprites; 0 preserves the game's texture filtering.\n"
        "smooth_2d = %d\n"
        "; Multisample anti-aliasing on the finished frame: 1 (off), 2, 4, 8.\n"
        ";   Only the image the game is rasterised into is multisampled; it is\n"
        ";   resolved before anything reads it back, so the effects, the\n"
        ";   photographs and the pause still are unchanged by it.\n"
        ";   This game samples the frame buffer for its effects and\n"
        ";   every such sample now forces a resolve of the whole target, so a\n"
        ";   frame full of effects is much more expensive than a plain one.\n"
        ";   Gameplay visibility uses its own single-sample depth pass.\n"
        ";   A level the GPU will not grant is clamped down.\n"
        "msaa = %d\n"
        "; Anisotropic filtering on the game's textures: 1 (off), 2, 4, 8, 16.\n"
        ";   No extra passes and no extra memory, and it is what stops tatami\n"
        ";   and corridor floors shimmering at grazing angles. Applies only\n"
        ";   where the PS2 asked for linear filtering in both directions, so\n"
        ";   a point-sampled texture stays point-sampled.\n"
        "anisotropy = %d\n"
        "; auto | original | 4:3 | 16:9 | 16:10 | custom\n"
        ";   a wider aspect shows more world, it does not stretch the image\n"
        ";   original is 640:448, the framing the game was composed for\n"
        "aspect = %s\n"
        "aspect_ratio = %.4f\n"
        "; Restores the one-texel alpha edge on cut-out textures --\n"
        ";   foliage, fences, hair -- that bilinear magnification softens.\n"
        ";   The game's own alpha test only ever discards fully transparent\n"
        ";   texels, which was enough at 640x448 where a texel was a pixel;\n"
        ";   above that the same step spreads over several pixels and drags\n"
        ";   the colour behind the cut-out into the silhouette.\n"
        ";   0 off (hardware behaviour) .. 1 full. Does nothing at or below\n"
        ";   native resolution whatever it is set to.\n"
        "alpha_sharpen = %.3f\n"
        "; A hard cut-out threshold: discard any texel whose alpha is below\n"
        ";   this. Blunter than alpha_sharpen and independent of it -- this\n"
        ";   aliases the edge, and can thin or erase fine geometry that is\n"
        ";   semi-transparent all the way across. Unlike alpha_sharpen it\n"
        ";   applies at every resolution. 0 off (hardware behaviour).\n"
        "alpha_cutoff = %.3f\n"
        "; Keep rooms, furniture and doors on the GPU: decoded once when first\n"
        ";   drawn, then drawn by reference with neighbouring parts merged.\n"
        ";   0 re-decodes and re-uploads every mesh every frame, as the PS2-\n"
        ";   shaped path always has. Same picture either way; 0 is the A/B.\n"
        "resident_meshes = %d\n"
        "; Resolve each room's, furniture's and door's textures once and stop\n"
        ";   uploading them to the emulated GS every frame. 0 re-sends and\n"
        ";   re-hashes them every draw, as the PS2 did -- the A/B.\n"
        "resident_textures = %d\n"
        "; Keep every texture as the bytes the game sent, and rearrange it\n"
        ";   into the emulated GS's memory layout only when something reads\n"
        ";   it there. Menus re-send their whole texture pack before nearly\n"
        ";   every sprite, which is what made them slow. 0 rearranges every\n"
        ";   send as it arrives, as the GS did -- the A/B.\n"
        "gs_by_reference = %d\n"
        "; Send characters, ghosts and items once per vertex, with their strips\n"
        ";   as indices, and merge neighbouring parts that share state into one\n"
        ";   draw. 0 sends every triangle's three corners -- the A/B.\n"
        "indexed_streams = %d\n"
        "\n"
        "[lighting]\n"
        "; vertex reproduces the PS2 VU1 path; fragment is smoother\n"
        "spotlights = %s\n"
        "\n"
        "[smoothing]\n"
        "; field | display\n"
        ";   field presents inside the game's own tick, at most 60 Hz, using\n"
        ";   extra_presents below.  display hands each tick to a presenter\n"
        ";   thread that shows it once per monitor refresh -- 120, 144, 240\n"
        ";   Hz -- blended to the moment each frame reaches the screen;\n"
        ";   extra_presents is then not used.\n"
        "mode = %s\n"
        "; Extra presents of each simulated frame, 0..3.  The game keeps its\n"
        ";   own 30 Hz tick either way -- these are the same frame shown\n"
        ";   again through a camera interpolated toward it, so 1 gives 60\n"
        ";   fps of camera motion and no change at all to game speed.\n"
        ";   Costs one more record and submit per extra frame.\n"
        "extra_presents = %d\n"
        "; 0 makes the extras identity copies -- a rendering self-test,\n"
        ";   not something to play with.\n"
        "interpolate_camera = %d\n"
        "; Move the world as well as the eye, so characters and moving\n"
        ";   objects stop stepping at 30 Hz.  Needs interpolate_camera.\n"
        ";   Costs one vertex-buffer upload per presented frame.\n"
        "interpolate_motion = %d\n"
        "\n[shadow]\n"
        "; How the projected shadow silhouette is sampled. soft averages a\n"
        ";   3x3 of it; none is the hard single tap the PS2's projected\n"
        ";   sprite gave.\n"
        "filter = %s\n"
        "\n"
        "[hdr]\n"
        "; off | auto | scrgb | hdr10\n"
        ";   auto uses HDR only when the display reports headroom, and\n"
        ";   follows it if you switch the OS setting or move the window.\n"
        ";   scrgb is preferred wherever both work: it is linear with sRGB\n"
        ";   primaries, so the conversion is one scale and nothing is\n"
        ";   requantised. hdr10 is for displays that only accept PQ.\n"
        ";   The game is SDR and stays SDR -- this is an output transform,\n"
        ";   not a rendering change. Set at launch it also buys a 10-bit\n"
        ";   scene target; turned on mid-session it works from 8.\n"
        "mode = %s\n"
        "; Nits an SDR white pixel is shown at. THIS is the brightness\n"
        ";   control that matters in HDR: it moves the whole picture without\n"
        ";   touching a ratio inside it, so nothing clips and nothing\n"
        ";   crushes. 0 follows the OS's own SDR white level; 200 is the\n"
        ";   reference both BT.2408 and Windows use.\n"
        "paper_white = %.1f\n"
        "; Brightest a lifted highlight may reach, in nits. 0 follows what\n"
        ";   the display reports it can do.\n"
        "peak = %.1f\n"
        "; How far into that headroom the top of the SDR range is stretched,\n"
        ";   0..1. 0 presents the image at paper_white and invents nothing,\n"
        ";   which is the faithful setting; higher makes the camera flash,\n"
        ";   lanterns and fire read as light rather than as paper.\n"
        "highlight_expansion = %.3f\n"
        "; Where that lift starts, as a fraction of paper white. Below it\n"
        ";   the picture is untouched, so a low knee reaches further down\n"
        ";   into the midtones.\n"
        "highlight_knee = %.3f\n"
        "\n"
        "[grade]\n"
        "; Display controls, applied in SDR as well as HDR and in the same\n"
        ";   order a monitor's own menu applies them. All four at 1.0 is\n"
        ";   identity, and identity costs nothing -- the output pass is\n"
        ";   skipped entirely unless HDR needs it.\n"
        "brightness = %.3f\n"
        "contrast = %.3f\n"
        "gamma = %.3f\n"
        "saturation = %.3f\n"
        "\n"
        "[effects]\n"
        "; The film grain scrolled over the picture, armed every frame the\n"
        ";   player is walking around.\n"
        ";     off     no grain at all.\n"
        ";     ps2     the ROM's own 128x128 sheet, one texel to a PS2\n"
        ";             pixel. Faithful, but a PS2 pixel is three or four of\n"
        ";             yours, so the grain arrives magnified to match.\n"
        ";     native  the same grain at one texel per screen pixel, off a\n"
        ";             larger sheet generated to match your resolution.\n"
        ";             Same density, same drift, same blend -- it is the\n"
        ";             sheet's size that changes, and that was a memory\n"
        ";             budget rather than a look.\n"
        "film_grain = %s\n"
        "\n"
        "[finder]\n"
        "; While the viewfinder is up, treat the world a wide window shows\n"
        ";   OUTSIDE the original 640x448 frame -- geometry the game was\n"
        ";   never composed to display. The 640x448 itself is left exactly\n"
        ";   as rendered. Does nothing on a 4:3 window. 0 off, 1 on.\n"
        "surround = %d\n"
        "; How far out of focus it goes, 0..1. 0 with a darken of 1 is a\n"
        ";   plain black surround instead.\n"
        "surround_blur = %.3f\n"
        "; How far it is darkened, 0 none .. 1 black.\n"
        "surround_darken = %.3f\n"
        "; How far it is cast toward crimson, 0 none .. 1 full.\n"
        "surround_tint = %.3f\n"
        "; The clear rectangle, against the original 640x448 frame. 1.0 is\n"
        ";   that frame exactly. Below 1.0 the surround climbs over the frame\n"
        ";   line and into the picture the game did compose, which is the one\n"
        ";   value here that can spoil the original look.\n"
        "surround_scale = %.3f\n"
        "\n"
        "[developer]\n"
        "; This prototype still opens its developers' debug menu when SELECT\n"
        ";   is pressed in play or on the pause screen -- and SELECT is Tab\n"
        ";   and the gamepad's Back button by default, so it is off here.\n"
        ";   1 restores the ROM's behaviour; SELECT again leaves the menu.\n"
        ";   The useful parts of it are on the F1 overlay's Assists menu\n"
        ";   either way.\n"
        "debug_menu = %d\n"
        "\n"
        "%s%s",
        miopan_config.paths.data_folder,
        EnumToName(kWindowModes, r->window_mode),
        r->window_width,
        r->window_height,
        EnumToName(kVsyncModes, r->vsync),
        r->pause_in_background,
        (double)miopan_config.audio.volume,
        miopan_config.audio.mute_in_background,
        miopan_config.audio.japanese_voices,
        (double)miopan_config.ui.scale,
        EnumToName(kRenderModes, r->render_mode),
        (double)r->render_scale,
        EnumToName(kFilters, r->upscale_filter),
        r->smooth_2d,
        r->msaa_samples,
        r->anisotropy,
        EnumToName(kAspectModes, r->aspect_mode),
        (double)r->aspect_ratio,
        (double)r->alpha_sharpen,
        (double)r->alpha_cutoff,
        r->resident_meshes,
        r->resident_textures,
        r->gs_by_reference,
        r->indexed_streams,
        EnumToName(kLightingModes, r->lighting_mode),
        EnumToName(kSmoothingModes, r->smoothing_mode),
        r->extra_presents,
        r->interpolate_presents,
        r->interpolate_geometry,
        EnumToName(kShadowFilters, r->shadow_filter),
        EnumToName(kHdrModes, r->hdr_mode),
        (double)r->hdr_paper_white,
        (double)r->hdr_peak,
        (double)r->hdr_expansion,
        (double)r->hdr_expansion_knee,
        (double)r->grade_brightness,
        (double)r->grade_contrast,
        (double)r->grade_gamma,
        (double)r->grade_saturation,
        EnumToName(kFilmGrain, r->film_grain),
        r->finder_mask,
        (double)r->finder_mask_blur,
        (double)r->finder_mask_darken,
        (double)r->finder_mask_tint,
        (double)r->finder_mask_scale,
        miopan_config.developer.debug_menu,
        title_section.c_str(),
        input_section.c_str());
    };

    std::vector<char> text(4096);
    int length = format(text.data(), text.size());
    if (length > 0 && length >= (int)text.size())
    {
        text.resize((size_t)length + 1);
        length = format(text.data(), text.size());
    }

    if (length <= 0 || length >= (int)text.size())
    {
        SDL_Log("MioPan: settings did not fit the write buffer; not saved");
        return 0;
    }

    MioPan_File *file = MioPan_FileOpen(path, "wb");
    if (file == nullptr)
    {
        SDL_Log("MioPan: could not open %s for writing", path);
        return 0;
    }
    size_t written = MioPan_FileWrite(file, text.data(), (size_t)length);
    MioPan_FileCloseHandle(file);

    if (written != (size_t)length)
    {
        SDL_Log("MioPan: short write saving %s", path);
        return 0;
    }
    return 1;
}

int MioPan_ConfigSetDataFolder(const char *folder)
{
    char *dst = miopan_config.paths.data_folder;

    if (folder == nullptr)
    {
        folder = "";
    }
    if (SDL_strcmp(dst, folder) == 0)
    {
        return 1;
    }

    SDL_strlcpy(dst, folder, sizeof(miopan_config.paths.data_folder));

    /* Persisted immediately.  Discovery's value is only useful to the player if
     * it survives to the next run -- that is how a folder guessed wrongly
     * becomes a line they can correct instead of a mystery -- and the settings
     * UI has nothing to show for a choice that was not written down. */
    return MioPan_ConfigSave();
}

int MioPan_ConfigDebugMenuEnabled(void)
{
    return miopan_config.developer.debug_menu;
}

int MioPan_ConfigSetDebugMenuEnabled(int enabled)
{
    enabled = enabled != 0 ? 1 : 0;
    if (miopan_config.developer.debug_menu == enabled)
    {
        return 1;
    }

    miopan_config.developer.debug_menu = enabled;

    /* Persisted immediately, like the data folder: it is one discrete choice,
     * not a slider the debounce in miopan_ui.cpp exists to coalesce, and that
     * debounce only watches the renderer, audio and ui structs anyway. */
    return MioPan_ConfigSave();
}

void MioPan_ConfigTitleSceneDefault(MioPan_ConfigTitleScene *scene)
{
    *scene = kTitleSceneDefault;
}

int MioPan_ConfigTitleBuiltinMissing(void)
{
    int missing = 0;
    for (int i = 0; i < g_title_builtin.scene_count; i++)
    {
        missing += TitleHasScene(miopan_config.title, g_title_builtin.scenes[i]) ? 0 : 1;
    }
    return missing;
}

int MioPan_ConfigTitleAddBuiltin(void)
{
    MioPan_ConfigTitle &t = miopan_config.title;
    int added = 0;

    for (int i = 0; i < g_title_builtin.scene_count && t.scene_count < MIOPAN_TITLE_SCENE_MAX; i++)
    {
        if (!TitleHasScene(t, g_title_builtin.scenes[i]))
        {
            t.scenes[t.scene_count++] = g_title_builtin.scenes[i];
            added++;
        }
    }
    return added;
}

int MioPan_ConfigTitleText(char *buf, int size)
{
    return CopyOut(TitleText(), buf, size);
}

int MioPan_ConfigTitleSceneText(int scene, char *buf, int size)
{
    if (scene < 0 || scene >= miopan_config.title.scene_count)
    {
        return CopyOut(std::string(), buf, size);
    }

    return CopyOut(TitleSceneText(miopan_config.title.scenes[scene], scene + 1),
                   buf, size);
}

int MioPan_ConfigTitlePaste(const char *text, int replace)
{
    if (text == nullptr)
    {
        return 0;
    }

    /* The reader's helpers all look in g_entries, which holds nothing once the
     * file has been loaded; the pasted text borrows it and gives it back. */
    std::vector<IniEntry> saved;
    saved.swap(g_entries);
    ParseIni(text);

    MioPan_ConfigTitle *t = &miopan_config.title;
    MioPan_ConfigTitleScene found[MIOPAN_TITLE_SCENE_MAX];
    const int n = CollectTitleScenes(found, MIOPAN_TITLE_SCENE_MAX);
    int taken = 0;

    if (n > 0)
    {
        if (replace != 0)
        {
            ReadTitle(t, false);
            t->scene_count = 0;
        }
        for (int i = 0; i < n && t->scene_count < MIOPAN_TITLE_SCENE_MAX; i++)
        {
            t->scenes[t->scene_count++] = found[i];
            taken++;
        }
        ClampTitle(t);
    }

    g_entries.swap(saved);
    return taken;
}

void MioPan_ConfigApply(void)
{
    const MioPan_ConfigRenderer *r = &miopan_config.renderer;

    /* Through the setters, so their clamps are the only place a range is
     * enforced and a hand-edited file cannot put the renderer in a state the
     * UI could not. */
    MioPan_RendererSetAspectMode(r->aspect_mode, r->aspect_ratio);
    MioPan_RendererSetRenderResolution(r->render_mode, r->render_scale);
    MioPan_RendererSetUpscaleFilter(r->upscale_filter);
    MioPan_RendererSetSmooth2d(r->smooth_2d);
    MioPan_RendererSetMsaaSamples(r->msaa_samples);
    MioPan_RendererSetAnisotropy(r->anisotropy);
    MioPan_RendererSetLightingMode(r->lighting_mode);
    MioPan_RendererSetShadowFilter(r->shadow_filter);
    MioPan_RendererSetExtraPresents(r->extra_presents);
    MioPan_RendererSetPresentInterpolation(r->interpolate_presents);
    MioPan_RendererSetGeometryInterpolation(r->interpolate_geometry);
    MioPan_RendererSetFilmGrain(r->film_grain);
    MioPan_RendererSetAlphaSharpen(r->alpha_sharpen);
    MioPan_RendererSetAlphaCutoff(r->alpha_cutoff);
    MioPan_RendererSetResidentMeshes(r->resident_meshes);
    MioPan_RendererSetResidentTextures(r->resident_textures);
    MioPan_GsSetByReference(r->gs_by_reference);
    MioPan_RendererSetIndexedStreams(r->indexed_streams);
    MioPan_RendererSetFinderMask(r->finder_mask, r->finder_mask_blur,
                                 r->finder_mask_darken, r->finder_mask_tint,
                                 r->finder_mask_scale);
    /* Before the window mode, and so before EnsureRenderer() creates the
     * device: the HDR mode is what decides whether the scene target gets its
     * ten bits, and that decision is taken once, when the pipelines are
     * built. */
    MioPan_RendererSetHdrMode(r->hdr_mode);
    MioPan_RendererSetHdrPaperWhite(r->hdr_paper_white);
    MioPan_RendererSetHdrPeak(r->hdr_peak);
    MioPan_RendererSetHdrExpansion(r->hdr_expansion, r->hdr_expansion_knee);
    MioPan_RendererSetGrade(r->grade_brightness, r->grade_contrast,
                            r->grade_gamma, r->grade_saturation);

    int vsync = r->vsync;
    float volume = miopan_config.audio.volume;
    float ui_scale = miopan_config.ui.scale;
    int smoothing = r->smoothing_mode;
    ApplyEnvironmentOverrides(&vsync, &volume, &ui_scale, &smoothing);

    MioPan_RendererSetSmoothingMode(smoothing);

    /* Before the device exists, like the HDR mode, so the swapchain is
     * created in the right present mode rather than switched into it. */
    MioPan_RendererSetVsync(vsync);
    MioPan_RendererSetPauseInBackground(r->pause_in_background);
    MioPan_RendererSetMuteInBackground(miopan_config.audio.mute_in_background);
    MioPan_UndubSetEnabled(miopan_config.audio.japanese_voices);
    /* The voice engine may or may not be up yet -- the IOP boots on its own
     * schedule -- and holds the value for its stream either way. */
    MioPan_VoiceSetOutputVolume(volume);
    MioPanUi::SetScaleOverride(ui_scale);
    MioPan_RendererSetWindowMode(r->window_mode);
}

void MioPan_ConfigCapture(void)
{
    MioPan_ConfigRenderer *r = &miopan_config.renderer;

    MioPan_RendererGetAspectMode(&r->aspect_mode, &r->aspect_ratio);
    MioPan_RendererGetRenderResolution(&r->render_mode, &r->render_scale);
    r->upscale_filter = MioPan_RendererGetUpscaleFilter();
    r->smooth_2d = MioPan_RendererGetSmooth2d();
    /* The requested levels, not the granted ones: a device that cannot do 8x
     * today must not rewrite the player's choice into the file. */
    r->msaa_samples = MioPan_RendererGetMsaaSamples();
    r->anisotropy = MioPan_RendererGetAnisotropy();
    r->lighting_mode = MioPan_RendererGetLightingMode();
    r->shadow_filter = MioPan_RendererGetShadowFilter();
    r->smoothing_mode = MioPan_RendererGetSmoothingMode();
    r->extra_presents = MioPan_RendererGetExtraPresents();
    r->interpolate_presents = MioPan_RendererGetPresentInterpolation();
    r->interpolate_geometry = MioPan_RendererGetGeometryInterpolation();
    r->hdr_mode = MioPan_RendererGetHdrMode();
    r->hdr_paper_white = MioPan_RendererGetHdrPaperWhite();
    r->hdr_peak = MioPan_RendererGetHdrPeak();
    MioPan_RendererGetHdrExpansion(&r->hdr_expansion, &r->hdr_expansion_knee);
    MioPan_RendererGetGrade(&r->grade_brightness, &r->grade_contrast,
                            &r->grade_gamma, &r->grade_saturation);
    MioPan_RendererGetFinderMask(&r->finder_mask, &r->finder_mask_blur,
                                 &r->finder_mask_darken, &r->finder_mask_tint,
                                 &r->finder_mask_scale);
    r->film_grain = MioPan_RendererGetFilmGrain();
    r->alpha_sharpen = MioPan_RendererGetAlphaSharpen();
    r->alpha_cutoff = MioPan_RendererGetAlphaCutoff();
    r->resident_meshes = MioPan_RendererGetResidentMeshes();
    r->resident_textures = MioPan_RendererGetResidentTextures();
    r->gs_by_reference = MioPan_GsGetByReference();
    r->indexed_streams = MioPan_RendererGetIndexedStreams();
    r->window_mode = MioPan_RendererGetWindowMode();
    /* The requested mode, not the granted one, for the same reason as MSAA:
     * a driver without MAILBOX must not rewrite the player's choice. */
    r->vsync = MioPan_RendererGetVsync();
    r->pause_in_background = MioPan_RendererGetPauseInBackground();

    miopan_config.audio.volume = MioPan_VoiceGetOutputVolume();
    miopan_config.audio.mute_in_background =
        MioPan_RendererGetMuteInBackground();
    miopan_config.audio.japanese_voices = MioPan_UndubGetEnabled();
    miopan_config.ui.scale = MioPanUi::GetScaleOverride();

    /* The windowed size, which the renderer latches while the window is not
     * fullscreen.  Reading the live window instead would store the desktop
     * size the moment anyone saved settings from borderless mode, and the
     * window would never return to its old shape. */
    int width = 0;
    int height = 0;
    MioPan_RendererGetWindowedSize(&width, &height);
    if (width >= 320 && height >= 240)
    {
        r->window_width = width;
        r->window_height = height;
    }
}

int MioPan_ConfigCaptureAndSave(void)
{
    MioPan_ConfigCapture();
    return MioPan_ConfigSave();
}

}
