#include "miopan_input.h"

#include "miopan_file.h"
#include "miopan_paths.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_touch.h>

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

// Raised by the rebinding UI while it waits for a key, and by the RmlUi menus
// while one holds the controls; s_suppressed is either.  See the header.
static int s_rebind_suppressed;
static int s_menu_suppressed;
static int s_suppressed;

// Which device the player asked for, by GUID; empty means "first available".
// Set to 1 whenever the device list may have changed, so the next update
// re-runs the choice instead of keeping whatever it opened first.
static char s_preferred_guid[MIOPAN_GAMEPAD_GUID_LEN];
static int  s_devices_dirty = 1;

// ---------------------------------------------------------------------------
//  Mouse state.  See the header for the shape of it.
// ---------------------------------------------------------------------------

// SDL numbers mouse buttons from 1 and its button mask covers 32 of them.
#define MIOPAN_MOUSE_BUTTON_MAX 32

// Notches banked per wheel direction.  A notch costs two pad updates (press,
// release), so a fast flick of the wheel would otherwise queue presses that
// play out for a second after the hand has stopped.
#define MIOPAN_WHEEL_QUEUE_MAX 4

// How long the free cursor may sit still before it is hidden.
#define MIOPAN_CURSOR_IDLE_MS 3000

// Settings.  Defaults are mirrored in MioPan_InputResetMouseSettings().
static int   s_mouse_look = 1;
static float s_mouse_sensitivity = 1.0f;
static int   s_mouse_invert_x;
static int   s_mouse_invert_y;

// Movement schemes; all default to the game's own Options settings.
static int s_stick_move_scheme = MIOPAN_MOVE_SCHEME_GAME;
static int s_dpad_move_scheme = MIOPAN_MOVE_SCHEME_GAME;
static int s_finder_aim_stick = MIOPAN_FINDER_AIM_GAME;

// The game's requests, for the update now running and the one before.  Both
// count, so the capture neither drops in the gap between an update starting
// and the game asking again, nor outlives the finder by more than an update.
static int s_look_request;
static int s_look_request_prev;
static int s_captured;
static int s_capture_failure_logged;

// Relative motion since the last update, and the last update's share of it.
static float s_motion_x;
static float s_motion_y;
static float s_look_x;
static float s_look_y;

// What the overlay last reported.
static int s_ui_wants_mouse;
static int s_pointer_over_ui;

// Mouse buttons, one bit per SDL button (SDL_BUTTON_MASK).  `down` is what is
// physically held and was pressed where the game may have it; `latch` keeps a
// click whose press and release land between two updates from being missed;
// `held` is what the current update reads.
static Uint32 s_button_down;
static Uint32 s_button_latch;
static Uint32 s_button_held;

// Wheel: fractional travel (x, y) from high-resolution wheels and touchpads,
// whole notches waiting per direction, and which direction is pressed now.
static float s_wheel_travel[2];
static int   s_wheel_queue[4];
static int   s_wheel_active[4];

// SDL_GetTicks() of the last real mouse movement, button or wheel event.  0
// reads as long idle, which is what both a fresh start and leaving the finder
// want: the cursor stays out of sight until the mouse is actually moved.
static Uint64 s_last_mouse_activity;

// ---------------------------------------------------------------------------
//  Gyro state.  See the header for the shape of it.
// ---------------------------------------------------------------------------

#define MIOPAN_DEG_TO_RAD(d) ((d) * (SDL_PI_F / 180.0f))

// Rotation slower than this is eased toward zero (out = rate * rate / this):
// the fraction of a degree a second a gyro reads after calibration cannot
// creep the view, and a deliberate turn passes untouched.
#define MIOPAN_GYRO_TIGHTEN_RAD     MIOPAN_DEG_TO_RAD(2.0f)

// "Still", for calibration: every axis within NOISE of its own short average,
// and that average under CAP -- no gyro's bias is anywhere near ten degrees a
// second, so anything faster is the pad moving, however smoothly.  There is
// deliberately no test against the current bias estimate: a wrong estimate
// would then refuse the very samples that correct it.
#define MIOPAN_GYRO_STILL_NOISE_RAD MIOPAN_DEG_TO_RAD(2.0f)
#define MIOPAN_GYRO_STILL_CAP_RAD   MIOPAN_DEG_TO_RAD(10.0f)
#define MIOPAN_GYRO_FAST_TAU_S      0.2f   // the short average
#define MIOPAN_GYRO_SETTLE_S        0.5f   // still this long before learning
#define MIOPAN_GYRO_FIRST_TAU_S     0.25f  // learning rate until calibrated...
#define MIOPAN_GYRO_BIAS_TAU_S      1.0f   // ...and after
#define MIOPAN_GYRO_CALIBRATED_S    1.0f   // learning this long counts as done

// A gap longer than this between two samples is a pad waking up or the window
// coming back, not a turn; the sample is weighted at the nominal rate instead.
#define MIOPAN_GYRO_MAX_DT_S        0.05f
#define MIOPAN_GYRO_FALLBACK_HZ     250.0f

// Settings.  Defaults are mirrored in MioPan_InputResetGyroSettings().
static int   s_gyro_look = 1;
static float s_gyro_sensitivity = MIOPAN_GYRO_SENSITIVITY_DEFAULT;
static int   s_gyro_invert_x;
static int   s_gyro_invert_y;
static int   s_gyro_turn_axis = MIOPAN_GYRO_TURN_YAW;

// The open pad: whether it has a gyro, whether it is reporting, its rate.
static int   s_gyro_has;
static int   s_gyro_active;
static float s_gyro_rate_hz;
static int   s_gyro_enable_failure_logged;

// Sensor timestamp of the last sample, 0 for none since the pad (re)started.
static Uint64 s_gyro_last_ns;

// Calibration, all in radians a second, SDL's axes (pitch, yaw, roll).
static float s_gyro_fast[3];
static int   s_gyro_fast_valid;
static float s_gyro_bias[3];
static float s_gyro_still_s;
static float s_gyro_calibrated_s;

// The last sample, bias removed, in the camera's signs -- for the readout.
static float s_gyro_rate_yaw;
static float s_gyro_rate_pitch;

// Turn gathered since the last update, and the last update's share of it, in
// radians in the camera's signs (yaw right, pitch up), before sensitivity.
static float s_gyro_motion_yaw;
static float s_gyro_motion_pitch;
static float s_gyro_look_yaw;
static float s_gyro_look_pitch;

static int GyroAiming(void)
{
    return (s_look_request != 0 || s_look_request_prev != 0) ? 1 : 0;
}

// Drop motion in flight and the sample clock, keeping the calibration.  For
// anything that breaks the run of samples: focus lost, the gyro switched.
static void ClearGyroMotion(void)
{
    s_gyro_last_ns = 0;
    s_gyro_motion_yaw = 0.0f;
    s_gyro_motion_pitch = 0.0f;
    s_gyro_look_yaw = 0.0f;
    s_gyro_look_pitch = 0.0f;
    s_gyro_rate_yaw = 0.0f;
    s_gyro_rate_pitch = 0.0f;
    s_gyro_still_s = 0.0f;
    s_gyro_fast_valid = 0;
}

// Forget the bias as well.  A different pad has a different one.
static void ResetGyroCalibration(void)
{
    ClearGyroMotion();
    memset(s_gyro_bias, 0, sizeof(s_gyro_bias));
    s_gyro_calibrated_s = 0.0f;
}

// Bring the open pad's gyro in line with the setting: on when there is a pad,
// it has a gyro and gyro aiming is on, off otherwise.  Called when a pad opens
// and when the setting changes; harmless to call again.
static void SyncGyroSensor(void)
{
    int want;

    if (s_gamepad == 0)
    {
        s_gyro_has = 0;
        s_gyro_active = 0;
        s_gyro_rate_hz = 0.0f;
        ClearGyroMotion();
        return;
    }

    s_gyro_has = SDL_GamepadHasSensor(s_gamepad, SDL_SENSOR_GYRO) ? 1 : 0;
    want = s_gyro_has != 0 && s_gyro_look != 0;

    if (s_gyro_has != 0 &&
        !SDL_SetGamepadSensorEnabled(s_gamepad, SDL_SENSOR_GYRO, want != 0))
    {
        if (s_gyro_enable_failure_logged == 0)
        {
            s_gyro_enable_failure_logged = 1;
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "MioPan: could not %s the gyro on \"%s\": %s",
                        want != 0 ? "enable" : "disable",
                        SDL_GetGamepadName(s_gamepad), SDL_GetError());
        }
        want = 0;
    }

    if (want != s_gyro_active)
    {
        ClearGyroMotion();
    }
    s_gyro_active = want;
    s_gyro_rate_hz = s_gyro_has != 0
                         ? SDL_GetGamepadSensorDataRate(s_gamepad, SDL_SENSOR_GYRO)
                         : 0.0f;
}

// One gyro sample: learn the bias when the pad is still and nobody is aiming,
// and while the game is aiming add the turn to this update's motion.
static void GyroSample(const float *w, Uint64 sample_ns)
{
    float dt = 0.0f;
    float r[3];
    float turn;
    float yaw;
    float pitch;
    float speed;
    int   still;
    int   i;

    if (s_gyro_active == 0)
    {
        return;
    }

    if (s_gyro_last_ns != 0 && sample_ns > s_gyro_last_ns)
    {
        dt = (float)((double)(sample_ns - s_gyro_last_ns) * 1.0e-9);
    }
    s_gyro_last_ns = sample_ns;
    if (!(dt > 0.0f) || dt > MIOPAN_GYRO_MAX_DT_S)
    {
        dt = 1.0f / (s_gyro_rate_hz > 0.0f ? s_gyro_rate_hz
                                           : MIOPAN_GYRO_FALLBACK_HZ);
    }

    // Calibration.
    if (s_gyro_fast_valid == 0)
    {
        memcpy(s_gyro_fast, w, sizeof(s_gyro_fast));
        s_gyro_fast_valid = 1;
    }
    else
    {
        const float k = dt / (MIOPAN_GYRO_FAST_TAU_S + dt);
        for (i = 0; i < 3; i++)
        {
            s_gyro_fast[i] += (w[i] - s_gyro_fast[i]) * k;
        }
    }

    still = 1;
    for (i = 0; i < 3; i++)
    {
        if (SDL_fabsf(w[i] - s_gyro_fast[i]) > MIOPAN_GYRO_STILL_NOISE_RAD ||
            SDL_fabsf(s_gyro_fast[i]) > MIOPAN_GYRO_STILL_CAP_RAD)
        {
            still = 0;
        }
    }

    // Never while aiming: a slow, steady turn through the finder is exactly
    // what a still pad with a bias looks like.
    if (still != 0 && GyroAiming() == 0)
    {
        s_gyro_still_s += dt;
        if (s_gyro_still_s >= MIOPAN_GYRO_SETTLE_S)
        {
            const float tau = s_gyro_calibrated_s < MIOPAN_GYRO_CALIBRATED_S
                                  ? MIOPAN_GYRO_FIRST_TAU_S
                                  : MIOPAN_GYRO_BIAS_TAU_S;
            const float k = dt / (tau + dt);
            for (i = 0; i < 3; i++)
            {
                s_gyro_bias[i] += (s_gyro_fast[i] - s_gyro_bias[i]) * k;
            }
            if (s_gyro_calibrated_s < MIOPAN_GYRO_CALIBRATED_S)
            {
                s_gyro_calibrated_s += dt;
            }
        }
    }
    else
    {
        s_gyro_still_s = 0.0f;
    }

    for (i = 0; i < 3; i++)
    {
        r[i] = w[i] - s_gyro_bias[i];
    }

    // SDL's rotations are counter-clockwise positive seen from the axis's own
    // end: positive yaw points the pad left, positive roll leans its top left,
    // positive pitch lifts its far edge.  The camera turns right on positive
    // yaw and looks up on positive pitch, so the turn axis is negated.
    turn = s_gyro_turn_axis == MIOPAN_GYRO_TURN_ROLL ? r[2] : r[1];
    yaw = -turn;
    pitch = r[0];
    s_gyro_rate_yaw = yaw;
    s_gyro_rate_pitch = pitch;

    speed = SDL_sqrtf(yaw * yaw + pitch * pitch);
    if (speed < MIOPAN_GYRO_TIGHTEN_RAD)
    {
        const float k = speed / MIOPAN_GYRO_TIGHTEN_RAD;
        yaw *= k;
        pitch *= k;
    }

    if (GyroAiming() != 0 && s_suppressed == 0)
    {
        s_gyro_motion_yaw += yaw * dt;
        s_gyro_motion_pitch += pitch * dt;
    }
}

// Once per pad update, after AdvanceMouse() has rolled the request window:
// hand over the turn gathered while the previous update was aiming.
static void AdvanceGyro(void)
{
    if (s_look_request_prev != 0)
    {
        s_gyro_look_yaw = s_gyro_motion_yaw;
        s_gyro_look_pitch = s_gyro_motion_pitch;
    }
    else
    {
        s_gyro_look_yaw = 0.0f;
        s_gyro_look_pitch = 0.0f;
    }
    s_gyro_motion_yaw = 0.0f;
    s_gyro_motion_pitch = 0.0f;
}

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

    // Both answer the state latched at the last update rather than SDL's live
    // state, so a click shorter than one update still reads as a press and a
    // button pressed on the overlay never reaches the game at all.
    case MIOPAN_SRC_MOUSE_BUTTON:
        if (src->code < 1 || src->code > MIOPAN_MOUSE_BUTTON_MAX)
        {
            return 0;
        }
        return (s_button_held & SDL_BUTTON_MASK(src->code)) != 0
                   ? MIOPAN_SRC_FULL
                   : 0;

    case MIOPAN_SRC_MOUSE_WHEEL:
        if (src->code < MIOPAN_WHEEL_UP || src->code > MIOPAN_WHEEL_RIGHT)
        {
            return 0;
        }
        return s_wheel_active[src->code - MIOPAN_WHEEL_UP] != 0
                   ? MIOPAN_SRC_FULL
                   : 0;

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
    SyncGyroSensor();
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

        ResetGyroCalibration();
        SyncGyroSensor();
        if (s_gyro_has != 0)
        {
            SDL_Log("MioPan: it has a gyro (%.0f Hz); gyro aiming is %s",
                    (double)s_gyro_rate_hz,
                    s_gyro_active != 0 ? "on" : "off");
        }
        else
        {
            SDL_Log("MioPan: it reports no gyro");
        }
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

// ---------------------------------------------------------------------------
//  Mouse
// ---------------------------------------------------------------------------

// Whether a press or a notch arriving now belongs to the game.  While captured
// it always does -- the overlay has been told to ignore the mouse -- and while
// free only if the overlay is neither holding the mouse nor under the cursor.
static int GameOwnsMouse(void)
{
    return s_captured != 0 ||
           (s_ui_wants_mouse == 0 && s_pointer_over_ui == 0);
}

static void NoteMouseActivity(void)
{
    s_last_mouse_activity = SDL_GetTicks();
    if (s_last_mouse_activity == 0)
    {
        s_last_mouse_activity = 1;
    }
}

// Drop everything a press or a notch has queued up.  Physically held buttons
// are let go of too: their release may never be seen (focus lost mid-press),
// and a button the game still thinks is down is worse than one press lost.
static void ClearMouseButtons(void)
{
    s_button_down = 0;
    s_button_latch = 0;
    s_button_held = 0;
    memset(s_wheel_travel, 0, sizeof(s_wheel_travel));
    memset(s_wheel_queue, 0, sizeof(s_wheel_queue));
    memset(s_wheel_active, 0, sizeof(s_wheel_active));
}

static void BankWheelNotch(int dir)
{
    int *queue = &s_wheel_queue[dir - MIOPAN_WHEEL_UP];
    if (*queue < MIOPAN_WHEEL_QUEUE_MAX)
    {
        (*queue)++;
    }
}

// Whole notches out of a wheel event.  High-resolution wheels and touchpads
// report fractions, so travel is carried over until it adds up to one.
static void QueueWheel(const SDL_MouseWheelEvent *wheel)
{
    float x = wheel->x;
    float y = wheel->y;

    // A flipped ("natural") wheel reports the content's direction; the
    // binding names the finger's.
    if (wheel->direction == SDL_MOUSEWHEEL_FLIPPED)
    {
        x = -x;
        y = -y;
    }

    s_wheel_travel[0] += x;
    s_wheel_travel[1] += y;

    while (s_wheel_travel[1] >= 1.0f)
    {
        BankWheelNotch(MIOPAN_WHEEL_UP);
        s_wheel_travel[1] -= 1.0f;
    }
    while (s_wheel_travel[1] <= -1.0f)
    {
        BankWheelNotch(MIOPAN_WHEEL_DOWN);
        s_wheel_travel[1] += 1.0f;
    }
    while (s_wheel_travel[0] >= 1.0f)
    {
        BankWheelNotch(MIOPAN_WHEEL_RIGHT);
        s_wheel_travel[0] -= 1.0f;
    }
    while (s_wheel_travel[0] <= -1.0f)
    {
        BankWheelNotch(MIOPAN_WHEEL_LEFT);
        s_wheel_travel[0] += 1.0f;
    }
}

// Once per pad update: hand over the motion gathered since the last one, roll
// the request window, latch the buttons, and step the wheel.
static void AdvanceMouse(void)
{
    int d;

    if (s_captured != 0)
    {
        s_look_x = s_motion_x;
        s_look_y = s_motion_y;
    }
    else
    {
        s_look_x = 0.0f;
        s_look_y = 0.0f;
    }
    s_motion_x = 0.0f;
    s_motion_y = 0.0f;

    s_look_request_prev = s_look_request;
    s_look_request = 0;

    s_button_held = s_button_down | s_button_latch;
    s_button_latch = 0;

    // Press for one update, release for the next, so two notches in a row are
    // two edges rather than one long hold.
    for (d = 0; d < 4; d++)
    {
        if (s_wheel_active[d] != 0)
        {
            s_wheel_active[d] = 0;
        }
        else if (s_wheel_queue[d] > 0)
        {
            s_wheel_queue[d]--;
            s_wheel_active[d] = 1;
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
    /* No SDL_PumpEvents() here.  SDL pumps -- and with it refreshes the pads
     * and the keyboard -- before every tick it runs (SDL_AppIterate, main.c),
     * and while Windows holds the thread in a window drag it ticks the game
     * from inside that loop, where a nested pump would take the drag's own
     * mouse messages. */
    SelectGamepad();
    AdvanceMouse();
    AdvanceGyro();
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

static void ApplySuppression(void)
{
    s_suppressed = (s_rebind_suppressed != 0 || s_menu_suppressed != 0) ? 1 : 0;

    // Drop whatever deflection the digital keys had built up, so releasing
    // suppression does not hand the game a stick that is still pushed over.
    // The same for a click or a notch banked while the player was choosing.
    if (s_suppressed != 0)
    {
        memset(s_stick_ramp, 0, sizeof(s_stick_ramp));
        ClearMouseButtons();
    }
}

void MioPan_InputSetSuppressed(int suppressed)
{
    s_rebind_suppressed = suppressed != 0 ? 1 : 0;
    ApplySuppression();
}

void MioPan_InputSetMenuSuppressed(int suppressed)
{
    s_menu_suppressed = suppressed != 0 ? 1 : 0;
    ApplySuppression();
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

// SDL has no names for these, so the file uses its own.  Indexed by SDL button
// number and by MIOPAN_WHEEL_*; slot 0 is unused in both.
static const char *const s_mouse_button_names[] = {
    0, "left", "middle", "right", "x1", "x2"
};
static const char *const s_wheel_names[] = {
    0, "up", "down", "left", "right"
};

#define MIOPAN_NAMED_MOUSE_BUTTONS \
    ((int)(sizeof(s_mouse_button_names) / sizeof(s_mouse_button_names[0])))

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
    case MIOPAN_SRC_MOUSE_BUTTON:
        if (src->code >= 1 && src->code < MIOPAN_NAMED_MOUSE_BUTTONS)
        {
            written = SDL_snprintf(buf, (size_t)len, "mouse:%s",
                                   s_mouse_button_names[src->code]);
        }
        else if (src->code >= 1 && src->code <= MIOPAN_MOUSE_BUTTON_MAX)
        {
            written = SDL_snprintf(buf, (size_t)len, "mouse:%d", src->code);
        }
        else
        {
            buf[0] = 0;
            return 0;
        }
        return (written > 0 && written < len) ? written : 0;

    case MIOPAN_SRC_MOUSE_WHEEL:
        if (src->code < MIOPAN_WHEEL_UP || src->code > MIOPAN_WHEEL_RIGHT)
        {
            buf[0] = 0;
            return 0;
        }
        written = SDL_snprintf(buf, (size_t)len, "wheel:%s",
                               s_wheel_names[src->code]);
        return (written > 0 && written < len) ? written : 0;

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

    // The mouse has no threshold and no SDL name lookup, so it is settled
    // here rather than in the shared path below.
    if (SDL_strncmp(text, "mouse:", 6) == 0)
    {
        int b;

        body = text + 6;
        for (b = 1; b < MIOPAN_NAMED_MOUSE_BUTTONS; b++)
        {
            if (SDL_strcasecmp(body, s_mouse_button_names[b]) == 0)
            {
                break;
            }
        }
        if (b == MIOPAN_NAMED_MOUSE_BUTTONS)
        {
            // "mouse:<n>", for a button past the five SDL names.
            char *end = 0;
            long  number = SDL_strtol(body, &end, 10);
            if (body[0] < '0' || body[0] > '9' || end == 0 || *end != 0 ||
                number < 1 || number > MIOPAN_MOUSE_BUTTON_MAX)
            {
                return 0;
            }
            b = (int)number;
        }
        out->kind = MIOPAN_SRC_MOUSE_BUTTON;
        out->code = b;
        out->dir = 0;
        out->threshold = 0;
        return 1;
    }

    if (SDL_strncmp(text, "wheel:", 6) == 0)
    {
        int d;

        body = text + 6;
        for (d = MIOPAN_WHEEL_UP; d <= MIOPAN_WHEEL_RIGHT; d++)
        {
            if (SDL_strcasecmp(body, s_wheel_names[d]) == 0)
            {
                out->kind = MIOPAN_SRC_MOUSE_WHEEL;
                out->code = d;
                out->dir = 0;
                out->threshold = 0;
                return 1;
            }
        }
        return 0;
    }

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

// ---------------------------------------------------------------------------
//  Mouse
// ---------------------------------------------------------------------------

void MioPan_InputProcessEvent(const SDL_Event *event)
{
    Uint32 mask;

    if (event == 0)
    {
        return;
    }

    switch (event->type)
    {
    case SDL_EVENT_MOUSE_MOTION:
        // A touchscreen's synthesized mouse is not a mouse: it would aim the
        // finder with a fingertip and fire bindings on every tap.
        if (event->motion.which == SDL_TOUCH_MOUSEID)
        {
            break;
        }
        // Zero-length motion is SDL restoring the cursor on leaving relative
        // mode, not the player; counting it would show the cursor again.
        if (event->motion.xrel == 0.0f && event->motion.yrel == 0.0f)
        {
            break;
        }
        NoteMouseActivity();
        if (s_captured != 0)
        {
            s_motion_x += event->motion.xrel;
            s_motion_y += event->motion.yrel;
        }
        break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event->button.which == SDL_TOUCH_MOUSEID ||
            event->button.button < 1 ||
            event->button.button > MIOPAN_MOUSE_BUTTON_MAX)
        {
            break;
        }
        NoteMouseActivity();
        if (GameOwnsMouse() && s_suppressed == 0)
        {
            mask = SDL_BUTTON_MASK(event->button.button);
            s_button_down |= mask;
            s_button_latch |= mask;
        }
        break;

    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event->button.which == SDL_TOUCH_MOUSEID ||
            event->button.button < 1 ||
            event->button.button > MIOPAN_MOUSE_BUTTON_MAX)
        {
            break;
        }
        NoteMouseActivity();
        s_button_down &= ~SDL_BUTTON_MASK(event->button.button);
        break;

    case SDL_EVENT_MOUSE_WHEEL:
        if (event->wheel.which == SDL_TOUCH_MOUSEID)
        {
            break;
        }
        NoteMouseActivity();
        if (GameOwnsMouse() && s_suppressed == 0)
        {
            QueueWheel(&event->wheel);
        }
        break;

    case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
        if (event->gsensor.sensor == SDL_SENSOR_GYRO && s_gamepad != 0 &&
            event->gsensor.which == SDL_GetGamepadID(s_gamepad))
        {
            // The pad's own clock when it has one; SDL's arrival time is
            // jittered by USB and by how events were batched.
            GyroSample(event->gsensor.data,
                       event->gsensor.sensor_timestamp != 0
                           ? event->gsensor.sensor_timestamp
                           : event->gsensor.timestamp);
        }
        break;

    case SDL_EVENT_WINDOW_FOCUS_LOST:
        ClearMouseButtons();
        // SDL stops delivering pad events to a window in the background, so
        // the next sample after it comes back is no continuation of this one.
        ClearGyroMotion();
        break;

    default:
        break;
    }
}

void MioPan_InputUpdateMouse(SDL_Window *window)
{
    int want;

    if (window == 0)
    {
        return;
    }

    // Capture is never *started* under the cursor's own overlay item -- the
    // player is pointing at it -- but once captured the cursor is not really
    // anywhere, and its virtual position wandering under the menu bar must not
    // let go of it.
    want = s_mouse_look != 0 &&
           (s_look_request != 0 || s_look_request_prev != 0) &&
           s_ui_wants_mouse == 0 &&
           (s_captured != 0 || s_pointer_over_ui == 0) &&
           s_suppressed == 0;

    if (want == s_captured)
    {
        return;
    }

    if (!SDL_SetWindowRelativeMouseMode(window, want != 0))
    {
        if (s_capture_failure_logged == 0)
        {
            s_capture_failure_logged = 1;
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "MioPan: could not %s the mouse: %s",
                        want != 0 ? "capture" : "release", SDL_GetError());
        }
        if (want != 0)
        {
            return; // stay free and try again next time
        }
    }

    s_captured = want;

    // Motion from before the switch belongs to a free cursor, and spending it
    // on the first captured update would jerk the camera.
    s_motion_x = 0.0f;
    s_motion_y = 0.0f;
    s_look_x = 0.0f;
    s_look_y = 0.0f;

    if (want == 0)
    {
        s_last_mouse_activity = 0;
    }
}

void MioPan_InputSetUiMouse(int ui_wants_mouse, int pointer_over_ui)
{
    s_ui_wants_mouse = ui_wants_mouse != 0 ? 1 : 0;
    s_pointer_over_ui = pointer_over_ui != 0 ? 1 : 0;
}

int MioPan_InputMouseCaptured(void)
{
    return s_captured;
}

int MioPan_InputMouseLookRequested(void)
{
    return (s_look_request != 0 || s_look_request_prev != 0) ? 1 : 0;
}

int MioPan_InputCursorIdle(void)
{
    if (s_captured != 0)
    {
        return 0;
    }
    if (s_last_mouse_activity == 0)
    {
        return 1;
    }
    return SDL_GetTicks() - s_last_mouse_activity >= MIOPAN_CURSOR_IDLE_MS
               ? 1
               : 0;
}

void MioPan_InputRequestMouseLook(void)
{
    s_look_request = 1;
}

int MioPan_InputMouseLook(float *yaw, float *pitch)
{
    float x = 0.0f;
    float y = 0.0f;

    if (s_captured != 0 && s_suppressed == 0)
    {
        const float scale = s_mouse_sensitivity *
                            (MIOPAN_MOUSE_LOOK_DEG_PER_COUNT * SDL_PI_F / 180.0f);

        // Screen y grows downward, so moving the mouse away from you is a
        // negative yrel -- and should look up.
        x = s_look_x * scale;
        y = -s_look_y * scale;

        if (s_mouse_invert_x != 0)
        {
            x = -x;
        }
        if (s_mouse_invert_y != 0)
        {
            y = -y;
        }
    }

    if (yaw != 0)
    {
        *yaw = x;
    }
    if (pitch != 0)
    {
        *pitch = y;
    }
    return (x != 0.0f || y != 0.0f) ? 1 : 0;
}

int MioPan_InputGetMouseLook(void)
{
    return s_mouse_look;
}

void MioPan_InputSetMouseLook(int enabled)
{
    s_mouse_look = enabled != 0 ? 1 : 0;
}

float MioPan_InputGetMouseSensitivity(void)
{
    return s_mouse_sensitivity;
}

void MioPan_InputSetMouseSensitivity(float sensitivity)
{
    // Written so a NaN from a hand-edited file lands on the floor too.
    if (!(sensitivity >= MIOPAN_MOUSE_SENSITIVITY_MIN))
    {
        sensitivity = MIOPAN_MOUSE_SENSITIVITY_MIN;
    }
    else if (sensitivity > MIOPAN_MOUSE_SENSITIVITY_MAX)
    {
        sensitivity = MIOPAN_MOUSE_SENSITIVITY_MAX;
    }
    s_mouse_sensitivity = sensitivity;
}

int MioPan_InputGetMouseInvertX(void)
{
    return s_mouse_invert_x;
}

void MioPan_InputSetMouseInvertX(int invert)
{
    s_mouse_invert_x = invert != 0 ? 1 : 0;
}

int MioPan_InputGetMouseInvertY(void)
{
    return s_mouse_invert_y;
}

void MioPan_InputSetMouseInvertY(int invert)
{
    s_mouse_invert_y = invert != 0 ? 1 : 0;
}

int MioPan_InputMouseSettingsAreDefault(void)
{
    return s_mouse_look == 1 && s_mouse_sensitivity == 1.0f &&
           s_mouse_invert_x == 0 && s_mouse_invert_y == 0;
}

void MioPan_InputResetMouseSettings(void)
{
    s_mouse_look = 1;
    s_mouse_sensitivity = 1.0f;
    s_mouse_invert_x = 0;
    s_mouse_invert_y = 0;
}

// ---------------------------------------------------------------------------
//  Gyro
// ---------------------------------------------------------------------------

int MioPan_InputGyroLook(float *yaw, float *pitch)
{
    float x = 0.0f;
    float y = 0.0f;

    if (s_gyro_active != 0 && s_suppressed == 0)
    {
        x = s_gyro_look_yaw * s_gyro_sensitivity;
        y = s_gyro_look_pitch * s_gyro_sensitivity;

        if (s_gyro_invert_x != 0)
        {
            x = -x;
        }
        if (s_gyro_invert_y != 0)
        {
            y = -y;
        }
    }

    if (yaw != 0)
    {
        *yaw = x;
    }
    if (pitch != 0)
    {
        *pitch = y;
    }
    return (x != 0.0f || y != 0.0f) ? 1 : 0;
}

int MioPan_InputAimLook(float *yaw, float *pitch)
{
    float mouse_yaw;
    float mouse_pitch;
    float gyro_yaw;
    float gyro_pitch;
    int   any;

    any = MioPan_InputMouseLook(&mouse_yaw, &mouse_pitch);
    any |= MioPan_InputGyroLook(&gyro_yaw, &gyro_pitch);

    if (yaw != 0)
    {
        *yaw = mouse_yaw + gyro_yaw;
    }
    if (pitch != 0)
    {
        *pitch = mouse_pitch + gyro_pitch;
    }
    return any;
}

int MioPan_InputGetGyroLook(void)
{
    return s_gyro_look;
}

void MioPan_InputSetGyroLook(int enabled)
{
    enabled = enabled != 0 ? 1 : 0;
    if (enabled == s_gyro_look)
    {
        return;
    }
    s_gyro_look = enabled;

    // Switch the pad's sensor with it, so a pad whose gyro is not wanted is
    // not streaming 250 reports a second for nothing.
    SyncGyroSensor();
}

float MioPan_InputGetGyroSensitivity(void)
{
    return s_gyro_sensitivity;
}

void MioPan_InputSetGyroSensitivity(float sensitivity)
{
    // Written so a NaN from a hand-edited file lands on the floor too.
    if (!(sensitivity >= MIOPAN_GYRO_SENSITIVITY_MIN))
    {
        sensitivity = MIOPAN_GYRO_SENSITIVITY_MIN;
    }
    else if (sensitivity > MIOPAN_GYRO_SENSITIVITY_MAX)
    {
        sensitivity = MIOPAN_GYRO_SENSITIVITY_MAX;
    }
    s_gyro_sensitivity = sensitivity;
}

int MioPan_InputGetGyroInvertX(void)
{
    return s_gyro_invert_x;
}

void MioPan_InputSetGyroInvertX(int invert)
{
    s_gyro_invert_x = invert != 0 ? 1 : 0;
}

int MioPan_InputGetGyroInvertY(void)
{
    return s_gyro_invert_y;
}

void MioPan_InputSetGyroInvertY(int invert)
{
    s_gyro_invert_y = invert != 0 ? 1 : 0;
}

int MioPan_InputGetGyroTurnAxis(void)
{
    return s_gyro_turn_axis;
}

void MioPan_InputSetGyroTurnAxis(int axis)
{
    s_gyro_turn_axis = axis == MIOPAN_GYRO_TURN_ROLL ? MIOPAN_GYRO_TURN_ROLL
                                                     : MIOPAN_GYRO_TURN_YAW;
}

int MioPan_InputGyroSettingsAreDefault(void)
{
    return s_gyro_look == 1 &&
           s_gyro_sensitivity == MIOPAN_GYRO_SENSITIVITY_DEFAULT &&
           s_gyro_invert_x == 0 && s_gyro_invert_y == 0 &&
           s_gyro_turn_axis == MIOPAN_GYRO_TURN_YAW;
}

void MioPan_InputResetGyroSettings(void)
{
    s_gyro_sensitivity = MIOPAN_GYRO_SENSITIVITY_DEFAULT;
    s_gyro_invert_x = 0;
    s_gyro_invert_y = 0;
    s_gyro_turn_axis = MIOPAN_GYRO_TURN_YAW;
    MioPan_InputSetGyroLook(1);
}

void MioPan_InputGyroRecalibrate(void)
{
    ResetGyroCalibration();
    SDL_Log("MioPan: gyro calibration restarted");
}

void MioPan_InputGetGyroStatus(MioPan_InputGyroStatus *out)
{
    const float to_deg = 180.0f / SDL_PI_F;
    int         i;

    if (out == 0)
    {
        return;
    }

    out->has_gamepad = s_gamepad != 0 ? 1 : 0;
    out->has_gyro = s_gyro_has;
    out->active = s_gyro_active;
    out->aiming = GyroAiming();
    out->rate_hz = s_gyro_rate_hz;
    out->calibration = s_gyro_calibrated_s >= MIOPAN_GYRO_CALIBRATED_S
                           ? 1.0f
                           : s_gyro_calibrated_s / MIOPAN_GYRO_CALIBRATED_S;
    for (i = 0; i < 3; i++)
    {
        out->bias_deg[i] = s_gyro_bias[i] * to_deg;
    }
    out->rate_yaw_deg = s_gyro_rate_yaw * to_deg;
    out->rate_pitch_deg = s_gyro_rate_pitch * to_deg;
}

// Anything unknown -- a hand-edited file, say -- falls back on the game's own
// setting rather than on a scheme the player did not ask for.
static int ClampMoveScheme(int scheme)
{
    return (scheme == MIOPAN_MOVE_SCHEME_CAMERA ||
            scheme == MIOPAN_MOVE_SCHEME_TANK)
               ? scheme
               : MIOPAN_MOVE_SCHEME_GAME;
}

int MioPan_InputGetStickMoveScheme(void)
{
    return s_stick_move_scheme;
}

void MioPan_InputSetStickMoveScheme(int scheme)
{
    s_stick_move_scheme = ClampMoveScheme(scheme);
}

int MioPan_InputGetDpadMoveScheme(void)
{
    return s_dpad_move_scheme;
}

void MioPan_InputSetDpadMoveScheme(int scheme)
{
    s_dpad_move_scheme = ClampMoveScheme(scheme);
}

int MioPan_InputGetFinderAimStick(void)
{
    return s_finder_aim_stick;
}

void MioPan_InputSetFinderAimStick(int stick)
{
    s_finder_aim_stick = (stick == MIOPAN_FINDER_AIM_LEFT ||
                          stick == MIOPAN_FINDER_AIM_RIGHT)
                             ? stick
                             : MIOPAN_FINDER_AIM_GAME;
}

int MioPan_InputMoveSettingsAreDefault(void)
{
    return s_stick_move_scheme == MIOPAN_MOVE_SCHEME_GAME &&
           s_dpad_move_scheme == MIOPAN_MOVE_SCHEME_GAME &&
           s_finder_aim_stick == MIOPAN_FINDER_AIM_GAME;
}

void MioPan_InputResetMoveSettings(void)
{
    s_stick_move_scheme = MIOPAN_MOVE_SCHEME_GAME;
    s_dpad_move_scheme = MIOPAN_MOVE_SCHEME_GAME;
    s_finder_aim_stick = MIOPAN_FINDER_AIM_GAME;
}

}
