#include "miopan_input.h"

#include "miopan_file.h"
#include "miopan_paths.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>

#include <string.h>

// Where an analog trigger starts counting as a button press.  Also the default
// threshold stamped into every axis source.
#define MIOPAN_TRIGGER_THRESHOLD 8000

// Full deflection, in the internal magnitude space every source is projected
// into.  Digital sources answer 0 or this; analog ones answer the axis value.
#define MIOPAN_SRC_FULL 32767

static SDL_Gamepad *s_gamepad;
static int          s_ready;
static int          s_gamepad_mappings_attempted;

// ---------------------------------------------------------------------------
//  Defaults
//
//  These reproduce the fixed tables this layer used to carry, so a fresh
//  install plays exactly as before.  The keyboard cluster rings WASD: Q/E/C and
//  Space are the four face buttons, R/F the shoulders, Z/X the triggers, V is
//  R3 and Left Shift is L3 (it clicks the stick WASD already drives).  IJKL is
//  the aim stick — the arrow keys are the D-pad, so they cannot also be aim.
// ---------------------------------------------------------------------------

#define KEY(sc)   { MIOPAN_SRC_KEY, (sc), 0, 0 }
#define PAD(b)    { MIOPAN_SRC_PAD_BUTTON, (b), 0, 0 }
#define AXIS(a,d) { MIOPAN_SRC_PAD_AXIS, (a), (d), MIOPAN_TRIGGER_THRESHOLD }
#define NOSRC     { MIOPAN_SRC_NONE, 0, 0, 0 }

static const MioPan_InputButtonBinding
    s_default_buttons[MIOPAN_PAD_BUTTON_COUNT] = {
        /*  0 UP       */ { { KEY(SDL_SCANCODE_UP),     PAD(SDL_GAMEPAD_BUTTON_DPAD_UP),         NOSRC, NOSRC } },
        /*  1 DOWN     */ { { KEY(SDL_SCANCODE_DOWN),   PAD(SDL_GAMEPAD_BUTTON_DPAD_DOWN),       NOSRC, NOSRC } },
        /*  2 LEFT     */ { { KEY(SDL_SCANCODE_LEFT),   PAD(SDL_GAMEPAD_BUTTON_DPAD_LEFT),       NOSRC, NOSRC } },
        /*  3 RIGHT    */ { { KEY(SDL_SCANCODE_RIGHT),  PAD(SDL_GAMEPAD_BUTTON_DPAD_RIGHT),      NOSRC, NOSRC } },
        /*  4 TRIANGLE */ { { KEY(SDL_SCANCODE_E),      PAD(SDL_GAMEPAD_BUTTON_NORTH),           NOSRC, NOSRC } },
        /*  5 CROSS    */ { { KEY(SDL_SCANCODE_SPACE),  PAD(SDL_GAMEPAD_BUTTON_SOUTH),           NOSRC, NOSRC } },
        /*  6 SQUARE   */ { { KEY(SDL_SCANCODE_Q),      PAD(SDL_GAMEPAD_BUTTON_WEST),            NOSRC, NOSRC } },
        /*  7 CIRCLE   */ { { KEY(SDL_SCANCODE_C),      PAD(SDL_GAMEPAD_BUTTON_EAST),            NOSRC, NOSRC } },
        /*  8 L1       */ { { KEY(SDL_SCANCODE_R),      PAD(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER),   NOSRC, NOSRC } },
        /*  9 L2       */ { { KEY(SDL_SCANCODE_Z),      AXIS(SDL_GAMEPAD_AXIS_LEFT_TRIGGER,  1), NOSRC, NOSRC } },
        /* 10 R1       */ { { KEY(SDL_SCANCODE_F),      PAD(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER),  NOSRC, NOSRC } },
        /* 11 R2       */ { { KEY(SDL_SCANCODE_X),      AXIS(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1), NOSRC, NOSRC } },
        /* 12 START    */ { { KEY(SDL_SCANCODE_ESCAPE), PAD(SDL_GAMEPAD_BUTTON_START),           NOSRC, NOSRC } },
        /* 13 SELECT   */ { { KEY(SDL_SCANCODE_TAB),    PAD(SDL_GAMEPAD_BUTTON_BACK),            NOSRC, NOSRC } },
        /* 14 L3       */ { { KEY(SDL_SCANCODE_LSHIFT), PAD(SDL_GAMEPAD_BUTTON_LEFT_STICK),      NOSRC, NOSRC } },
        /* 15 R3       */ { { KEY(SDL_SCANCODE_V),      PAD(SDL_GAMEPAD_BUTTON_RIGHT_STICK),     NOSRC, NOSRC } },
    };

// ramp_frames 1 on the left stick keeps the movement keys snapping to full
// deflection, which is what they have always done; the right stick ramps over
// six updates so keyboard aim is steerable.
static const MioPan_InputStickBinding
    s_default_sticks[MIOPAN_PAD_STICK_COUNT] = {
        /* 0 left X  */ { AXIS(SDL_GAMEPAD_AXIS_LEFTX,  1), KEY(SDL_SCANCODE_A), KEY(SDL_SCANCODE_D), 1 },
        /* 1 left Y  */ { AXIS(SDL_GAMEPAD_AXIS_LEFTY,  1), KEY(SDL_SCANCODE_W), KEY(SDL_SCANCODE_S), 1 },
        /* 2 right X */ { AXIS(SDL_GAMEPAD_AXIS_RIGHTX, 1), KEY(SDL_SCANCODE_J), KEY(SDL_SCANCODE_L), 6 },
        /* 3 right Y */ { AXIS(SDL_GAMEPAD_AXIS_RIGHTY, 1), KEY(SDL_SCANCODE_I), KEY(SDL_SCANCODE_K), 6 },
    };

#undef KEY
#undef PAD
#undef AXIS
#undef NOSRC

// Live bindings.  Seeded from the defaults on first use; a settings file or the
// rebinding UI writes over them through the accessors at the bottom.
static MioPan_InputButtonBinding s_buttons[MIOPAN_PAD_BUTTON_COUNT];
static MioPan_InputStickBinding  s_sticks[MIOPAN_PAD_STICK_COUNT];
static int                       s_bindings_ready;

// Digital contribution per stick, -1.0 .. +1.0, advanced once per update.  This
// is the only piece of per-frame state the layer keeps; the analog sources are
// read straight from SDL each time.
static float s_stick_ramp[MIOPAN_PAD_STICK_COUNT];

// Raised by the rebinding UI while it waits for a key.  See the header.
static int s_suppressed;

// Which device the player asked for, by GUID; empty means "first available".
// Set to 1 whenever the device list may have changed, so the next update
// re-runs the choice instead of keeping whatever it opened first.
static char s_preferred_guid[MIOPAN_GAMEPAD_GUID_LEN];
static int  s_devices_dirty = 1;

static void EnsureBindings(void)
{
    if (s_bindings_ready == 0)
    {
        memcpy(s_buttons, s_default_buttons, sizeof(s_buttons));
        memcpy(s_sticks, s_default_sticks, sizeof(s_sticks));
        s_bindings_ready = 1;
    }
}

// ---------------------------------------------------------------------------
//  Source evaluation
// ---------------------------------------------------------------------------

static int KeyDown(const bool *keys, int scancode)
{
    return keys != 0 &&
           scancode > 0 &&
           scancode < SDL_SCANCODE_COUNT &&
           keys[scancode] != 0;
}

// One source projected into 0..MIOPAN_SRC_FULL.  Digital sources answer either
// end; an axis answers its travel in the bound direction, which is what will
// let the pressure bytes carry something real once libpad stops filling them
// with 0xff.  A gamepad source with no gamepad open answers 0.
static int SourceMagnitude(const MioPan_InputSource *src, const bool *keys)
{
    int value;

    switch (src->kind)
    {
    case MIOPAN_SRC_KEY:
        return KeyDown(keys, src->code) ? MIOPAN_SRC_FULL : 0;

    case MIOPAN_SRC_PAD_BUTTON:
        if (s_gamepad == 0)
        {
            return 0;
        }
        return SDL_GetGamepadButton(s_gamepad, (SDL_GamepadButton)src->code)
                   ? MIOPAN_SRC_FULL
                   : 0;

    case MIOPAN_SRC_PAD_AXIS:
        if (s_gamepad == 0)
        {
            return 0;
        }
        value = SDL_GetGamepadAxis(s_gamepad, (SDL_GamepadAxis)src->code);
        value = src->dir < 0 ? -value : value;
        if (value < 0)
        {
            return 0;
        }
        return value > MIOPAN_SRC_FULL ? MIOPAN_SRC_FULL : value;

    default:
        return 0;
    }
}

// Held is "past the threshold".  Digital sources leave threshold at 0 and so
// trip on any press; axis sources carry MIOPAN_TRIGGER_THRESHOLD by default.
static int SourceHeld(const MioPan_InputSource *src, const bool *keys)
{
    int mag;

    if (src->kind == MIOPAN_SRC_NONE || s_suppressed != 0)
    {
        return 0;
    }

    mag = SourceMagnitude(src, keys);
    return mag > src->threshold ? 1 : 0;
}

static unsigned char AxisToPs2(int axis)
{
    int ps2;

    if (axis < -32768)
    {
        axis = -32768;
    }
    else if (axis > 32767)
    {
        axis = 32767;
    }

    ps2 = (axis + 32768) * 255 / 65535;
    return (unsigned char)ps2;
}

static int AbsInt(int x)
{
    return x < 0 ? -x : x;
}

// ---------------------------------------------------------------------------
//  Device lifecycle
// ---------------------------------------------------------------------------

static void CloseGamepad(void)
{
    if (s_gamepad != 0)
    {
        SDL_RumbleGamepad(s_gamepad, 0, 0, 0);
        SDL_CloseGamepad(s_gamepad);
        s_gamepad = 0;
    }
}

static void LoadGamepadMappings(void)
{
    char path[1024];

    if (s_gamepad_mappings_attempted != 0)
    {
        return;
    }
    s_gamepad_mappings_attempted = 1;

    if (MioPan_PathResolveAsset("resources/gamecontrollerdb.txt",
                               path,
                               sizeof(path)) == 0)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "MioPan: resources/gamecontrollerdb.txt was not found; "
                    "SDL built-in controller mappings will be used");
        return;
    }

    if (!SDL_AddGamepadMappingsFromFile(path))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "MioPan: failed to load controller mappings from %s: %s",
                    path,
                    SDL_GetError());
    }
}

// The GUID of a device id, as 32 hex characters.
static void GamepadGuidForId(SDL_JoystickID id, char *out, int len)
{
    if (out == 0 || len <= 0)
    {
        return;
    }
    SDL_GUIDToString(SDL_GetGamepadGUIDForID(id), out, len);
}

static void CopyStr(char *dst, int len, const char *src)
{
    if (dst == 0 || len <= 0)
    {
        return;
    }
    if (src == 0)
    {
        dst[0] = 0;
        return;
    }
    SDL_strlcpy(dst, src, (size_t)len);
}

/*
 * Pick the device to drive the pad with.
 *
 * The preference is by GUID, not by position: SDL's list is in whatever order
 * the OS enumerated this boot, so "the first one" is a different device from
 * one session to the next as soon as more than one is plugged in.  A wheel or a
 * second pad carrying a mapping used to be able to claim the slot permanently.
 *
 * With no preference set this still takes the first device, which is the
 * previous behaviour and the right default for the overwhelmingly common case
 * of exactly one controller.
 */
static int SelectGamepad(void)
{
    int             count = 0;
    SDL_JoystickID *ids;
    SDL_JoystickID  chosen = 0;
    int             i;

    if (MioPan_InputInit() == 0)
    {
        return 0;
    }

    /* A pad pulled out from under us. */
    if (s_gamepad != 0 && !SDL_GamepadConnected(s_gamepad))
    {
        CloseGamepad();
        s_devices_dirty = 1;
    }

    /* Nothing to reconsider: a pad is open and no device has come or gone. */
    if (s_gamepad != 0 && s_devices_dirty == 0)
    {
        return 1;
    }
    s_devices_dirty = 0;

    ids = SDL_GetGamepads(&count);
    if (ids == 0 || count <= 0)
    {
        if (ids != 0)
        {
            SDL_free(ids);
        }
        return s_gamepad != 0 ? 1 : 0;
    }

    if (s_preferred_guid[0] != 0)
    {
        char guid[MIOPAN_GAMEPAD_GUID_LEN];
        for (i = 0; i < count; i++)
        {
            GamepadGuidForId(ids[i], guid, sizeof(guid));
            if (SDL_strcasecmp(guid, s_preferred_guid) == 0)
            {
                chosen = ids[i];
                break;
            }
        }
    }

    /* No preference, or the preferred device is not plugged in right now.  Fall
     * back rather than leaving the player with nothing -- the preference is
     * kept, so plugging it back in reclaims the slot. */
    if (chosen == 0)
    {
        chosen = ids[0];
    }

    SDL_free(ids);

    if (s_gamepad != 0)
    {
        if (SDL_GetGamepadID(s_gamepad) == chosen)
        {
            return 1; /* already the right one */
        }
        CloseGamepad();
    }

    s_gamepad = SDL_OpenGamepad(chosen);
    if (s_gamepad != 0)
    {
        SDL_Log("MioPan: using gamepad \"%s\"", SDL_GetGamepadName(s_gamepad));
    }

    return s_gamepad != 0 ? 1 : 0;
}

/* The old name, kept because the rumble path reads as "open one if needed". */
static int OpenGamepad(void)
{
    return SelectGamepad();
}

// Move each stick's digital contribution one step toward what its keys are
// asking for.  Both keys down, or neither, decays to centre — the same "no
// clear direction" answer the old either/or test gave.
static void AdvanceStickRamps(void)
{
    const bool *keys = SDL_GetKeyboardState(0);
    int         i;

    for (i = 0; i < MIOPAN_PAD_STICK_COUNT; i++)
    {
        const MioPan_InputStickBinding *b = &s_sticks[i];
        int   neg = SourceHeld(&b->neg, keys);
        int   pos = SourceHeld(&b->pos, keys);
        float target = (float)(pos - neg);
        float step;

        if (b->ramp_frames > 1)
        {
            step = 1.0f / (float)b->ramp_frames;
        }
        else
        {
            step = 1.0f;
        }

        if (s_stick_ramp[i] < target)
        {
            s_stick_ramp[i] += step;
            if (s_stick_ramp[i] > target)
            {
                s_stick_ramp[i] = target;
            }
        }
        else if (s_stick_ramp[i] > target)
        {
            s_stick_ramp[i] -= step;
            if (s_stick_ramp[i] < target)
            {
                s_stick_ramp[i] = target;
            }
        }
    }
}

extern "C" {

int MioPan_InputInit(void)
{
    if (s_ready == 0)
    {
        if (!SDL_InitSubSystem(SDL_INIT_EVENTS | SDL_INIT_GAMEPAD))
        {
            return 0;
        }
        LoadGamepadMappings();
        s_ready = 1;
    }
    EnsureBindings();
    return 1;
}

void MioPan_InputShutdown(void)
{
    CloseGamepad();
}

void MioPan_InputUpdate(void)
{
    if (MioPan_InputInit() == 0)
    {
        return;
    }
    SDL_PumpEvents();
    SelectGamepad();
    AdvanceStickRamps();
}

int MioPan_InputGetGamepadCount(void)
{
    int             count = 0;
    SDL_JoystickID *ids;

    if (MioPan_InputInit() == 0)
    {
        return 0;
    }

    ids = SDL_GetGamepads(&count);
    if (ids != 0)
    {
        SDL_free(ids);
    }
    return count > 0 ? count : 0;
}

int MioPan_InputGetGamepadInfo(int index, char *name, int name_len,
                               char *guid, int guid_len)
{
    int             count = 0;
    SDL_JoystickID *ids;
    int             ok = 0;

    CopyStr(name, name_len, "");
    CopyStr(guid, guid_len, "");

    if (index < 0 || MioPan_InputInit() == 0)
    {
        return 0;
    }

    ids = SDL_GetGamepads(&count);
    if (ids == 0)
    {
        return 0;
    }
    if (index < count)
    {
        CopyStr(name, name_len, SDL_GetGamepadNameForID(ids[index]));
        GamepadGuidForId(ids[index], guid, guid_len);
        ok = 1;
    }
    SDL_free(ids);
    return ok;
}

int MioPan_InputGetActiveGamepad(char *name, int name_len,
                                 char *guid, int guid_len)
{
    CopyStr(name, name_len, "");
    CopyStr(guid, guid_len, "");

    if (s_gamepad == 0)
    {
        return 0;
    }

    CopyStr(name, name_len, SDL_GetGamepadName(s_gamepad));
    GamepadGuidForId(SDL_GetGamepadID(s_gamepad), guid, guid_len);
    return 1;
}

void MioPan_InputSetPreferredGamepad(const char *guid)
{
    if (guid == 0)
    {
        guid = "";
    }
    SDL_strlcpy(s_preferred_guid, guid, sizeof(s_preferred_guid));

    /* Re-run the choice on the next update rather than switching here: this is
     * called from the settings UI and from the config load, neither of which
     * should be opening devices. */
    s_devices_dirty = 1;
}

const char *MioPan_InputGetPreferredGamepad(void)
{
    return s_preferred_guid;
}

void MioPan_InputRefreshDevices(void)
{
    s_devices_dirty = 1;
}

int MioPan_InputButtonHeld(int index)
{
    const bool *keys;
    int         i;

    if (index < 0 || index >= MIOPAN_PAD_BUTTON_COUNT)
    {
        return 0;
    }
    EnsureBindings();

    keys = SDL_GetKeyboardState(0);

    for (i = 0; i < MIOPAN_INPUT_MAX_SOURCES; i++)
    {
        if (SourceHeld(&s_buttons[index].src[i], keys))
        {
            return 1;
        }
    }

    return 0;
}

unsigned char MioPan_InputStickValue(int index)
{
    int analog_value = 127;
    int digital_value = 127;

    if (index < 0 || index >= MIOPAN_PAD_STICK_COUNT || s_suppressed != 0)
    {
        return 127;
    }
    EnsureBindings();

    if (s_sticks[index].analog.kind == MIOPAN_SRC_PAD_AXIS && s_gamepad != 0)
    {
        int raw = SDL_GetGamepadAxis(
            s_gamepad, (SDL_GamepadAxis)s_sticks[index].analog.code);
        analog_value = AxisToPs2(s_sticks[index].analog.dir < 0 ? -raw : raw);
    }

    // The ramp is signed -1..+1; 32767 rather than 32768 on the negative side
    // keeps AxisToPs2 symmetric about 127.
    if (s_stick_ramp[index] != 0.0f)
    {
        digital_value = AxisToPs2((int)(s_stick_ramp[index] * 32767.0f));
    }

    return (unsigned char)((AbsInt(analog_value - 127) >= AbsInt(digital_value - 127))
                               ? analog_value
                               : digital_value);
}

int MioPan_InputRumble(unsigned char large_on, unsigned char small_strength)
{
    Uint16 low;
    Uint16 high;

    if (OpenGamepad() == 0)
    {
        return 0;
    }

    low = (Uint16)((unsigned int)small_strength * 257u);
    high = large_on != 0 ? 0xffffu : 0u;
    return SDL_RumbleGamepad(s_gamepad, low, high, 120) ? 1 : 0;
}

// ---------------------------------------------------------------------------
//  Binding access
// ---------------------------------------------------------------------------

int MioPan_InputGetButtonBinding(int button, MioPan_InputButtonBinding *out)
{
    if (button < 0 || button >= MIOPAN_PAD_BUTTON_COUNT || out == 0)
    {
        return 0;
    }
    EnsureBindings();
    *out = s_buttons[button];
    return 1;
}

int MioPan_InputSetButtonBinding(int button,
                                 const MioPan_InputButtonBinding *in)
{
    if (button < 0 || button >= MIOPAN_PAD_BUTTON_COUNT || in == 0)
    {
        return 0;
    }
    EnsureBindings();
    s_buttons[button] = *in;
    return 1;
}

int MioPan_InputGetStickBinding(int stick, MioPan_InputStickBinding *out)
{
    if (stick < 0 || stick >= MIOPAN_PAD_STICK_COUNT || out == 0)
    {
        return 0;
    }
    EnsureBindings();
    *out = s_sticks[stick];
    return 1;
}

int MioPan_InputSetStickBinding(int stick, const MioPan_InputStickBinding *in)
{
    if (stick < 0 || stick >= MIOPAN_PAD_STICK_COUNT || in == 0)
    {
        return 0;
    }
    EnsureBindings();
    s_sticks[stick] = *in;

    // A stick whose keys just moved should not keep the deflection the old
    // pair had built up.
    s_stick_ramp[stick] = 0.0f;
    return 1;
}

void MioPan_InputSetSuppressed(int suppressed)
{
    s_suppressed = suppressed != 0 ? 1 : 0;

    // Drop whatever deflection the digital keys had built up, so releasing
    // suppression does not hand the game a stick that is still pushed over.
    if (s_suppressed != 0)
    {
        memset(s_stick_ramp, 0, sizeof(s_stick_ramp));
    }
}

int MioPan_InputIsSuppressed(void)
{
    return s_suppressed;
}

void MioPan_InputResetBindings(void)
{
    memcpy(s_buttons, s_default_buttons, sizeof(s_buttons));
    memcpy(s_sticks, s_default_sticks, sizeof(s_sticks));
    memset(s_stick_ramp, 0, sizeof(s_stick_ramp));
    s_bindings_ready = 1;
}

int MioPan_InputBindingsAreDefault(void)
{
    EnsureBindings();
    return memcmp(s_buttons, s_default_buttons, sizeof(s_buttons)) == 0 &&
           memcmp(s_sticks, s_default_sticks, sizeof(s_sticks)) == 0;
}

// ---------------------------------------------------------------------------
//  Text form
// ---------------------------------------------------------------------------

int MioPan_InputSourceToString(const MioPan_InputSource *src, char *buf, int len)
{
    const char *name;
    const char *prefix;
    int         written;

    if (src == 0 || buf == 0 || len <= 0)
    {
        return 0;
    }

    switch (src->kind)
    {
    case MIOPAN_SRC_KEY:
        name = SDL_GetScancodeName((SDL_Scancode)src->code);
        prefix = "key:";
        break;

    case MIOPAN_SRC_PAD_BUTTON:
        name = SDL_GetGamepadStringForButton((SDL_GamepadButton)src->code);
        prefix = "pad:";
        break;

    case MIOPAN_SRC_PAD_AXIS:
        name = SDL_GetGamepadStringForAxis((SDL_GamepadAxis)src->code);
        prefix = src->dir < 0 ? "axis-:" : "axis+:";
        break;

    default:
        buf[0] = 0;
        return 0;
    }

    if (name == 0 || name[0] == 0)
    {
        buf[0] = 0;
        return 0;
    }

    if (src->kind == MIOPAN_SRC_PAD_AXIS &&
        src->threshold != MIOPAN_TRIGGER_THRESHOLD)
    {
        written = SDL_snprintf(buf, (size_t)len, "%s%s@%d",
                               prefix, name, src->threshold);
    }
    else
    {
        written = SDL_snprintf(buf, (size_t)len, "%s%s", prefix, name);
    }

    return (written > 0 && written < len) ? written : 0;
}

int MioPan_InputSourceFromString(const char *text, MioPan_InputSource *out)
{
    MioPan_InputSource parsed;
    char               name[128];
    const char         *body;
    const char         *at;
    size_t              n;

    if (text == 0 || out == 0)
    {
        return 0;
    }

    memset(&parsed, 0, sizeof(parsed));

    if (SDL_strncmp(text, "key:", 4) == 0)
    {
        parsed.kind = MIOPAN_SRC_KEY;
        body = text + 4;
    }
    else if (SDL_strncmp(text, "pad:", 4) == 0)
    {
        parsed.kind = MIOPAN_SRC_PAD_BUTTON;
        body = text + 4;
    }
    else if (SDL_strncmp(text, "axis+:", 6) == 0)
    {
        parsed.kind = MIOPAN_SRC_PAD_AXIS;
        parsed.dir = 1;
        body = text + 6;
    }
    else if (SDL_strncmp(text, "axis-:", 6) == 0)
    {
        parsed.kind = MIOPAN_SRC_PAD_AXIS;
        parsed.dir = -1;
        body = text + 6;
    }
    else if (SDL_strcmp(text, "none") == 0)
    {
        out->kind = MIOPAN_SRC_NONE;
        out->code = 0;
        out->dir = 0;
        out->threshold = 0;
        return 1;
    }
    else
    {
        return 0;
    }

    // An "@<n>" suffix overrides the axis threshold.
    parsed.threshold = parsed.kind == MIOPAN_SRC_PAD_AXIS
                           ? MIOPAN_TRIGGER_THRESHOLD
                           : 0;
    at = SDL_strchr(body, '@');
    if (at != 0)
    {
        n = (size_t)(at - body);

        // A bare or non-numeric "@" keeps the default threshold instead of
        // parsing as 0.  Zero on an axis is a hair trigger that fires on
        // stick drift, which is the worst thing a typo in a hand-edited
        // settings file could quietly produce.
        if (at[1] >= '0' && at[1] <= '9')
        {
            parsed.threshold = SDL_atoi(at + 1);
        }
    }
    else
    {
        n = SDL_strlen(body);
    }

    if (n == 0 || n >= sizeof(name))
    {
        return 0;
    }
    memcpy(name, body, n);
    name[n] = 0;

    switch (parsed.kind)
    {
    case MIOPAN_SRC_KEY:
    {
        SDL_Scancode sc = SDL_GetScancodeFromName(name);
        if (sc == SDL_SCANCODE_UNKNOWN)
        {
            return 0;
        }
        parsed.code = (int)sc;
        break;
    }

    case MIOPAN_SRC_PAD_BUTTON:
    {
        SDL_GamepadButton b = SDL_GetGamepadButtonFromString(name);
        if (b == SDL_GAMEPAD_BUTTON_INVALID)
        {
            return 0;
        }
        parsed.code = (int)b;
        break;
    }

    default:
    {
        SDL_GamepadAxis a = SDL_GetGamepadAxisFromString(name);
        if (a == SDL_GAMEPAD_AXIS_INVALID)
        {
            return 0;
        }
        parsed.code = (int)a;
        break;
    }
    }

    *out = parsed;
    return 1;
}

}
