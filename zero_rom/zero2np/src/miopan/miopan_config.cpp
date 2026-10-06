#include "miopan_config.h"

#include "io/miopan_file.h"
#include "io/miopan_input.h"
#include "io/miopan_paths.h"
#include "rendering/miopan_renderer.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

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
        /* .animated_lighting = */ MIOPAN_ANIMATED_LIGHTING_GPU,
        /* .shadow_filter     = */ MIOPAN_SHADOW_FILTER_SOFT,
        /* .extra_presents    = */ 0,
        /* .interpolate_presents = */ 1,
        /* .interpolate_geometry = */ 1,
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
    },
};

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

const EnumName kAnimatedLighting[] = {
    {"gpu", MIOPAN_ANIMATED_LIGHTING_GPU},
    {"cpu", MIOPAN_ANIMATED_LIGHTING_CPU},
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

void ReadEnum(const char *section, const char *name, const EnumName *table,
              int *dst)
{
    const char *value = Lookup(section, name);
    if (value == nullptr || value[0] == '\0')
    {
        return;
    }
    for (const EnumName *e = table; e->label != nullptr; e++)
    {
        if (SDL_strcasecmp(value, e->label) == 0)
        {
            *dst = e->value;
            return;
        }
    }
    /* Not a name we know: a number from an older file, or a typo.  A typo
     * lands on 0, which every one of these enums defines as its default. */
    *dst = (int)std::strtol(value, nullptr, 10);
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
        "key:", "pad:", "axis+:", "axis-:", "none"
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

    return out;
}

bool EnvOverrideActive()
{
    static const char *const kNames[] = {
        "MIOPAN_RENDER_RES",   "MIOPAN_RENDER_SCALE",
        "MIOPAN_RENDER_FILTER", "MIOPAN_ASPECT",
        "MIOPAN_WINDOW_MODE",  "MIOPAN_LIGHTING_MODE",
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
    ReadEnum("render", "mode", kRenderModes, &r->render_mode);
    ReadFloat("render", "scale", &r->render_scale);
    ReadEnum("render", "upscale_filter", kFilters, &r->upscale_filter);
    ReadInt("render", "msaa", &r->msaa_samples);
    ReadInt("render", "anisotropy", &r->anisotropy);
    ReadFloat("render", "aspect_ratio", &r->aspect_ratio);
    ReadFloat("render", "alpha_sharpen", &r->alpha_sharpen);
    ReadFloat("render", "alpha_cutoff", &r->alpha_cutoff);
    ReadInt("render", "resident_meshes", &r->resident_meshes);
    ReadInt("render", "resident_textures", &r->resident_textures);
    /* After aspect_ratio, so a "21:9" written in `aspect` wins over a stale
     * ratio left in the companion key. */
    ReadAspect("render", "aspect", &r->aspect_mode, &r->aspect_ratio);
    ReadEnum("lighting", "spotlights", kLightingModes, &r->lighting_mode);
    ReadEnum("lighting", "animated_characters", kAnimatedLighting,
             &r->animated_lighting);
    ReadEnum("shadow", "filter", kShadowFilters, &r->shadow_filter);
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

    auto format = [r, &input_section](char *buf, size_t cap) {
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
        "\n"
        "[render]\n"
        "; match_window | native | window_scale\n"
        ";   native rasterises at 640x448 x scale, i.e. PS2 pixel density\n"
        "mode = %s\n"
        "scale = %.4f\n"
        "; nearest = sharp but uneven at non-integer scale; linear = soft\n"
        "upscale_filter = %s\n"
        "; Multisample anti-aliasing on the finished frame: 1 (off), 2, 4, 8.\n"
        ";   Only the image the game is rasterised into is multisampled; it is\n"
        ";   resolved before anything reads it back, so the effects, the\n"
        ";   photographs and the pause still are unchanged by it.\n"
        ";   Two costs. This game samples the frame buffer for its effects and\n"
        ";   every such sample now forces a resolve of the whole target, so a\n"
        ";   frame full of effects is much more expensive than a plain one.\n"
        ";   And the depth read-back stops while it is on -- a ghost can stay\n"
        ";   a camera target through a wall it should be hidden behind.\n"
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
        "\n"
        "[lighting]\n"
        "; vertex reproduces the PS2 VU1 path; fragment is smoother\n"
        "spotlights = %s\n"
        "; gpu | cpu   (cpu is the slow reference path, for A/B only)\n"
        "animated_characters = %s\n"
        "\n"
        "[smoothing]\n"
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
        "\n%s",
        miopan_config.paths.data_folder,
        EnumToName(kWindowModes, r->window_mode),
        r->window_width,
        r->window_height,
        EnumToName(kRenderModes, r->render_mode),
        (double)r->render_scale,
        EnumToName(kFilters, r->upscale_filter),
        r->msaa_samples,
        r->anisotropy,
        EnumToName(kAspectModes, r->aspect_mode),
        (double)r->aspect_ratio,
        (double)r->alpha_sharpen,
        (double)r->alpha_cutoff,
        r->resident_meshes,
        r->resident_textures,
        EnumToName(kLightingModes, r->lighting_mode),
        EnumToName(kAnimatedLighting, r->animated_lighting),
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

void MioPan_ConfigApply(void)
{
    const MioPan_ConfigRenderer *r = &miopan_config.renderer;

    /* Through the setters, so their clamps are the only place a range is
     * enforced and a hand-edited file cannot put the renderer in a state the
     * UI could not. */
    MioPan_RendererSetAspectMode(r->aspect_mode, r->aspect_ratio);
    MioPan_RendererSetRenderResolution(r->render_mode, r->render_scale);
    MioPan_RendererSetUpscaleFilter(r->upscale_filter);
    MioPan_RendererSetMsaaSamples(r->msaa_samples);
    MioPan_RendererSetAnisotropy(r->anisotropy);
    MioPan_RendererSetLightingMode(r->lighting_mode);
    MioPan_RendererSetAnimatedLightingBackend(r->animated_lighting);
    MioPan_RendererSetShadowFilter(r->shadow_filter);
    MioPan_RendererSetExtraPresents(r->extra_presents);
    MioPan_RendererSetPresentInterpolation(r->interpolate_presents);
    MioPan_RendererSetGeometryInterpolation(r->interpolate_geometry);
    MioPan_RendererSetFilmGrain(r->film_grain);
    MioPan_RendererSetAlphaSharpen(r->alpha_sharpen);
    MioPan_RendererSetAlphaCutoff(r->alpha_cutoff);
    MioPan_RendererSetResidentMeshes(r->resident_meshes);
    MioPan_RendererSetResidentTextures(r->resident_textures);
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
    MioPan_RendererSetWindowMode(r->window_mode);
}

void MioPan_ConfigCapture(void)
{
    MioPan_ConfigRenderer *r = &miopan_config.renderer;

    MioPan_RendererGetAspectMode(&r->aspect_mode, &r->aspect_ratio);
    MioPan_RendererGetRenderResolution(&r->render_mode, &r->render_scale);
    r->upscale_filter = MioPan_RendererGetUpscaleFilter();
    /* The requested levels, not the granted ones: a device that cannot do 8x
     * today must not rewrite the player's choice into the file. */
    r->msaa_samples = MioPan_RendererGetMsaaSamples();
    r->anisotropy = MioPan_RendererGetAnisotropy();
    r->lighting_mode = MioPan_RendererGetLightingMode();
    r->animated_lighting = MioPan_RendererGetAnimatedLightingBackend();
    r->shadow_filter = MioPan_RendererGetShadowFilter();
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
    r->window_mode = MioPan_RendererGetWindowMode();

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
