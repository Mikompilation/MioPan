#ifndef MIOPAN_INPUT_H
#define MIOPAN_INPUT_H

#ifdef __cplusplus
extern "C" {
#endif

// Host input wrapper.  Confines SDL gamepad/keyboard/event handling to the
// miopan layer.  The SDK pad shim (libpad) keeps the PS2 DualShock2 report
// format (bit layout, rdata byte packing); this layer answers device-neutral
// questions: "is logical button i held?", "what is stick axis i?" — polling a
// gamepad with a keyboard fallback, exactly as the pad shim did inline before.
//
// Button indices match the PS2/FF2 sce_pad order used by the pad shim:
//   buttons 0..15: up, down, left, right, triangle, cross, square, circle,
//                   L1, L2, R1, R2, start, select, L3, R3
//   sticks  0..3 : left X, left Y, right X, right Y
//
// Those 16 indices are the whole vocabulary of the layers above.  Everything
// the game reads — paddat[], key_now[], the raw pad[0].now/one/rpt bitmasks and
// GetPadAnalogRpt() — resolves through them, so rebinding what a physical
// control *means* is done here and nothing upstream has to change.  This header
// deliberately names no SDL type: the codes below are SDL scancode / gamepad
// button / gamepad axis / mouse button values, but keeping them plain ints is
// what lets sdk/libpad.cpp include this without pulling SDL in.  (The two
// event-loop hooks at the bottom take SDL pointers, forward-declared only.)

#define MIOPAN_PAD_BUTTON_COUNT 16
#define MIOPAN_PAD_STICK_COUNT  4

// Sources bound to one button.  Four is enough for a keyboard key, a pad
// button, and an alternate of each; a source list is how a button ends up held
// by either device, which is what the fixed gamepad-or-keyboard test did before
// the list was made data.
#define MIOPAN_INPUT_MAX_SOURCES 4

// What kind of physical control a binding source names.  New kinds go on the
// end: the values are not written to the settings file, but the rebinding UI
// and the defaults compare them.
enum
{
    MIOPAN_SRC_NONE = 0,
    MIOPAN_SRC_KEY,          // `code` is an SDL scancode
    MIOPAN_SRC_PAD_BUTTON,   // `code` is an SDL_GamepadButton
    MIOPAN_SRC_PAD_AXIS,     // `code` is an SDL_GamepadAxis; see dir/threshold
    MIOPAN_SRC_MOUSE_BUTTON, // `code` is an SDL mouse button, 1 (left) .. 32
    MIOPAN_SRC_MOUSE_WHEEL   // `code` is one of MIOPAN_WHEEL_*
};

// Wheel directions, as MIOPAN_SRC_MOUSE_WHEEL codes.  A wheel has no "held":
// each notch reads as one press -- held for one pad update, then released for
// one -- so the game sees every notch as its own edge.
enum
{
    MIOPAN_WHEEL_UP = 1,
    MIOPAN_WHEEL_DOWN,
    MIOPAN_WHEEL_LEFT,
    MIOPAN_WHEEL_RIGHT
};

typedef struct MioPan_InputSource
{
    int kind;      // MIOPAN_SRC_*; MIOPAN_SRC_NONE means "empty slot"
    int code;      // scancode / gamepad button / gamepad axis, per kind
    int dir;       // axis only: +1 or -1, which half of the travel counts
    int threshold; // axis only: projected magnitude at or past this reads held
} MioPan_InputSource;

typedef struct MioPan_InputButtonBinding
{
    MioPan_InputSource src[MIOPAN_INPUT_MAX_SOURCES];
} MioPan_InputButtonBinding;

// A stick axis takes one full-range analog source plus an optional digital pair
// pulling either way — the generalisation of the gamepad-axis-with-keyboard-
// fallback arrangement this layer already had.
//
// `ramp_frames` is how many updates a held digital source takes to reach full
// deflection.  1 snaps, which is what movement wants and what the left stick
// has always done; the aim stick ramps instead, because a camera that jumps to
// the rail on the first frame of a key press is unusable.
typedef struct MioPan_InputStickBinding
{
    MioPan_InputSource analog;
    MioPan_InputSource neg; // pulls toward 0
    MioPan_InputSource pos; // pulls toward 255
    int                ramp_frames;
} MioPan_InputStickBinding;

// Initialise the input subsystem (events + gamepad).  Returns non-zero on
// success.  Idempotent; also called lazily by the query functions.
int  MioPan_InputInit(void);
void MioPan_InputShutdown(void);

// Pump host events, (re)open the first connected gamepad if needed, and advance
// the digital stick ramps by one step.  Call once per poll before reading
// button/stick state — the ramp advances per call, not per wall-clock tick.
// The same goes for the mouse: this is where the motion accumulated since the
// last call becomes this update's look delta, and where a wheel notch is
// turned into its one-update press.
void MioPan_InputUpdate(void);

// 1 if logical button `index` (0..15) is currently held by any of its sources.
int MioPan_InputButtonHeld(int index);

// Stick axis `index` (0..3) as a PS2 0..255 value (127 = centred), taking
// whichever of the analog and digital sources shows the larger deflection.
unsigned char MioPan_InputStickValue(int index);

// Rumble the active gamepad; `low`/`high` are the two PS2 actuator bytes
// (data[1] small motor, data[0] large motor).  Returns non-zero on success.
int MioPan_InputRumble(unsigned char large_on, unsigned char small_strength);

// ---------------------------------------------------------------------------
//  Gamepad selection.
//
//  SDL enumerates every device that carries a gamepad mapping, which on a desk
//  with a wheel, a second pad, or anything the controller database happens to
//  cover is not necessarily the one the player means.  A device is remembered
//  by its GUID rather than its position in that list, because the list order is
//  whatever the OS enumerated this boot.
//
//  Note the bindings themselves stay device-neutral: a source names an SDL
//  gamepad button ("south", "leftshoulder"), and SDL's mapping layer already
//  makes that mean the same thing on an Xbox pad, a DualShock and a Switch Pro.
//  So there are deliberately no per-device binding sets -- one layout is
//  correct across all of them.
// ---------------------------------------------------------------------------

// A GUID renders as 32 hex characters; 33 covers the terminator.
#define MIOPAN_GAMEPAD_GUID_LEN 33

// How many gamepads SDL currently sees.
int MioPan_InputGetGamepadCount(void);

// Identity of the `index`-th connected gamepad.  Either buffer may be NULL.
// Returns non-zero if `index` named a device.
int MioPan_InputGetGamepadInfo(int index, char *name, int name_len,
                               char *guid, int guid_len);

// Identity of the gamepad actually in use, or 0 if none is open.
int MioPan_InputGetActiveGamepad(char *name, int name_len,
                                 char *guid, int guid_len);

// Which device to prefer.  NULL or "" means "whichever is first", the previous
// behaviour.  A GUID that is not currently plugged in is remembered and honoured
// the moment it appears, so unplugging a pad mid-session does not lose the
// choice.  Takes effect on the next update.
void        MioPan_InputSetPreferredGamepad(const char *guid);
const char *MioPan_InputGetPreferredGamepad(void);

// Tell the input layer the device list changed.  The event loop calls this on
// SDL_EVENT_GAMEPAD_ADDED / _REMOVED so a hotplug is acted on that frame rather
// than waiting for the next poll to notice.
void MioPan_InputRefreshDevices(void);

// ---------------------------------------------------------------------------
//  Binding access.  All of these copy, so a caller may hold the struct while it
//  edits it and commit once.  Button/stick indices out of range are rejected
//  with 0 rather than clamped.
// ---------------------------------------------------------------------------

int  MioPan_InputGetButtonBinding(int button, MioPan_InputButtonBinding *out);
int  MioPan_InputSetButtonBinding(int button, const MioPan_InputButtonBinding *in);
int  MioPan_InputGetStickBinding(int stick, MioPan_InputStickBinding *out);
int  MioPan_InputSetStickBinding(int stick, const MioPan_InputStickBinding *in);

// Restore every binding to the built-in defaults.
void MioPan_InputResetBindings(void);

// While suppressed, every button reads as released and every stick as centred.
// The rebinding UI raises this for exactly as long as it is waiting for a key,
// because the game polls SDL directly rather than through the event queue — so
// without it, pressing Space to bind CROSS also presses CROSS.  It is not a
// pause: the game keeps running, it just stops being driven.
void MioPan_InputSetSuppressed(int suppressed);
int  MioPan_InputIsSuppressed(void);

// 1 if every binding still matches the built-in defaults.  The settings file
// writes the section either way — it is meant to be read and hand-edited, so
// showing the defaults is the point — and this exists for the rebinding UI,
// which wants to say whether anything has been changed and whether its reset
// affordance would do anything.
int MioPan_InputBindingsAreDefault(void);

// Text form, for the settings file and the rebinding UI.  The grammar is
// "key:<scancode name>", "pad:<button name>" and "axis+:<axis name>" /
// "axis-:<axis name>", using SDL's own names so the file stays hand-editable;
// a non-default threshold is appended as "@<n>".  The mouse adds
// "mouse:left|middle|right|x1|x2" (or "mouse:<n>" for a button past the fifth)
// and "wheel:up|down|left|right".  ToString returns the number of characters
// written (0 on failure); FromString returns non-zero on a recognised source
// and leaves `out` untouched otherwise, so an unknown name in a settings file
// is skipped rather than clearing the binding.
int MioPan_InputSourceToString(const MioPan_InputSource *src, char *buf, int len);
int MioPan_InputSourceFromString(const char *text, MioPan_InputSource *out);

// ---------------------------------------------------------------------------
//  Mouse.
//
//  Two jobs.  Mouse buttons and the wheel are binding sources like any key, and
//  while the Camera Obscura's viewfinder is up the mouse aims it.
//
//  Aiming does not go through the stick.  The finder turns at one of three
//  fixed rates the moment the stick passes a third of its travel
//  (finder_camera.c, SetFinderRot), so a mouse folded into pad[0].analog could
//  only ever pick a speed, never a distance.  Instead the game asks for mouse
//  look each tick it is aiming and adds the motion to its own yaw and pitch.
//
//  The motion is accumulated from SDL's events between pad updates and handed
//  over once per update, so how many frames are presented in between -- one or
//  four -- does not change how far a given hand movement turns the camera.
//
//  The cursor is captured (SDL relative mode) only while the game is asking
//  and the overlay does not want the mouse, and released on the first update
//  that does not ask.  The overlay always wins: while it is using the mouse,
//  mouse look stops and mouse buttons and the wheel stop reaching the game.
// ---------------------------------------------------------------------------

union SDL_Event;
struct SDL_Window;

// Feed every polled event through.  Only mouse, gamepad-sensor and focus events
// are looked at.
void MioPan_InputProcessEvent(const union SDL_Event *event);

// Settle whether the cursor should be captured.  Call after each batch of
// events has been polled; `window` is the window to capture it to.
void MioPan_InputUpdateMouse(struct SDL_Window *window);

// The overlay's say, reported once per UI frame.  `ui_wants_mouse` is the
// overlay holding the mouse (a menu or tool window open, F1 pressed while
// aiming); `pointer_over_ui` is the free cursor resting on an overlay item.
void MioPan_InputSetUiMouse(int ui_wants_mouse, int pointer_over_ui);

// 1 while the cursor is captured for mouse look.
int MioPan_InputMouseCaptured(void);

// 1 while the game is asking for mouse look, captured or not.
int MioPan_InputMouseLookRequested(void);

// 1 when the free cursor has not moved for a while and may be hidden.  Never
// while captured -- relative mode hides it already.
int MioPan_InputCursorIdle(void);

// Game side.  RequestMouseLook says "the player is aiming this tick" and must
// be repeated every tick the finder is up; the capture lapses one update after
// the requests stop.  MouseLook answers this tick's motion in radians -- yaw
// positive to the right, pitch positive upward, sensitivity and invert already
// applied -- and returns non-zero when there is any.
void MioPan_InputRequestMouseLook(void);
int  MioPan_InputMouseLook(float *yaw, float *pitch);

// Settings, persisted in [input].  Sensitivity is a multiplier on
// MIOPAN_MOUSE_LOOK_DEG_PER_COUNT and is clamped to the range below.
#define MIOPAN_MOUSE_LOOK_DEG_PER_COUNT 0.05f
#define MIOPAN_MOUSE_SENSITIVITY_MIN    0.05f
#define MIOPAN_MOUSE_SENSITIVITY_MAX    10.0f

int   MioPan_InputGetMouseLook(void);
void  MioPan_InputSetMouseLook(int enabled);
float MioPan_InputGetMouseSensitivity(void);
void  MioPan_InputSetMouseSensitivity(float sensitivity);
int   MioPan_InputGetMouseInvertX(void);
void  MioPan_InputSetMouseInvertX(int invert);
int   MioPan_InputGetMouseInvertY(void);
void  MioPan_InputSetMouseInvertY(int invert);

// 1 if the mouse settings are all at their defaults.
int MioPan_InputMouseSettingsAreDefault(void);
void MioPan_InputResetMouseSettings(void);

// ---------------------------------------------------------------------------
//  Gyro.
//
//  While the viewfinder is up, a gamepad with a gyroscope aims it by being
//  turned, the same way the mouse does: the game asks for look input with
//  MioPan_InputRequestMouseLook() and the motion goes straight onto its yaw and
//  pitch, not through the stick.  Nothing else uses the gyro -- walking around
//  with the finder down ignores it entirely.
//
//  Every sample SDL reports is integrated over its own sensor timestamp, so the
//  turn is an angle, not a rate: tilting the pad 10 degrees turns the camera
//  10 degrees times the sensitivity however the samples fell between ticks.
//
//  A gyro reads a little above zero when still, and that bias walks with
//  temperature.  It is learned continuously whenever the pad lies still with
//  the finder down -- never while aiming, where a slow, steady turn would look
//  the same -- and MioPan_InputGyroRecalibrate() starts it over.  What bias is
//  left is damped by easing rotation slower than a couple of degrees a second
//  toward zero, which is what stops the view creeping.
// ---------------------------------------------------------------------------

// Mouse and gyro together, for the finder: this tick's turn in radians, with
// the same signs and return as MioPan_InputMouseLook().
int MioPan_InputAimLook(float *yaw, float *pitch);

// The gyro's share alone.
int MioPan_InputGyroLook(float *yaw, float *pitch);

// Settings, persisted in [input].  Sensitivity multiplies the controller's own
// rotation: 1.0 turns the camera as far as the pad turned.
#define MIOPAN_GYRO_SENSITIVITY_DEFAULT 1.5f
#define MIOPAN_GYRO_SENSITIVITY_MIN     0.1f
#define MIOPAN_GYRO_SENSITIVITY_MAX     8.0f

// Which rotation turns the camera left and right.  Pitch -- tipping the pad's
// far edge up or down -- always looks up and down.
#define MIOPAN_GYRO_TURN_YAW  0   // point the pad left and right, like a torch
#define MIOPAN_GYRO_TURN_ROLL 1   // tip it sideways, like a steering wheel

int   MioPan_InputGetGyroLook(void);
void  MioPan_InputSetGyroLook(int enabled);
float MioPan_InputGetGyroSensitivity(void);
void  MioPan_InputSetGyroSensitivity(float sensitivity);
int   MioPan_InputGetGyroInvertX(void);
void  MioPan_InputSetGyroInvertX(int invert);
int   MioPan_InputGetGyroInvertY(void);
void  MioPan_InputSetGyroInvertY(int invert);
int   MioPan_InputGetGyroTurnAxis(void);
void  MioPan_InputSetGyroTurnAxis(int axis);

int  MioPan_InputGyroSettingsAreDefault(void);
void MioPan_InputResetGyroSettings(void);

// Forget the learned bias; it is learned again the next time the pad lies
// still with the finder down.
void MioPan_InputGyroRecalibrate(void);

// What the Controls window shows.  `rate_*` is the pad's motion now, bias
// removed, in degrees a second and in the camera's own signs (yaw positive to
// the right, pitch up), before sensitivity -- so it reads the same whatever the
// setting.  `calibration` runs 0..1 while the first bias is being learned.
typedef struct MioPan_InputGyroStatus
{
    int   has_gamepad;
    int   has_gyro;     // the open pad reports one
    int   active;       // reporting now: enabled in settings and on the pad
    int   aiming;       // the game is asking for look input
    float rate_hz;      // the pad's own sample rate, 0 if it does not say
    float calibration;
    float bias_deg[3];  // pitch, yaw, roll, degrees a second
    float rate_yaw_deg;
    float rate_pitch_deg;
} MioPan_InputGyroStatus;

void MioPan_InputGetGyroStatus(MioPan_InputGyroStatus *out);

// ---------------------------------------------------------------------------
//  Movement schemes.
//
//  The game's own Options screen has two settings that decide how the pad
//  steers Mio: the control type (opt_wrk.move_operate -- normal, where only the
//  stick walks and she heads where it points as seen from the camera, or
//  "vehicle", where the d-pad and the stick both turn her and drive her
//  forward) and which stick aims the Camera Obscura (opt_wrk.ana_replace).
//  Each setting below overrides one of those for one part of the pad, so the
//  d-pad can steer one way and the stick the other.  GAME, the default, leaves
//  the game's own setting in charge, which is the ROM's behaviour exactly.
//  player.c and finder_camera.c read them.  Persisted in [input].
// ---------------------------------------------------------------------------

#define MIOPAN_MOVE_SCHEME_GAME   0   // the game's control type
#define MIOPAN_MOVE_SCHEME_CAMERA 1   // head where it points, seen from the camera
#define MIOPAN_MOVE_SCHEME_TANK   2   // left/right turn, up walks, down about-faces

int  MioPan_InputGetStickMoveScheme(void);
void MioPan_InputSetStickMoveScheme(int scheme);
int  MioPan_InputGetDpadMoveScheme(void);
void MioPan_InputSetDpadMoveScheme(int scheme);

// Which stick aims the viewfinder; the other one sidesteps.  The d-pad aims
// whatever this says.
#define MIOPAN_FINDER_AIM_GAME  0   // the game's own setting
#define MIOPAN_FINDER_AIM_LEFT  1   // left aims, right sidesteps (the game's default)
#define MIOPAN_FINDER_AIM_RIGHT 2   // right aims, left sidesteps

int  MioPan_InputGetFinderAimStick(void);
void MioPan_InputSetFinderAimStick(int stick);

int  MioPan_InputMoveSettingsAreDefault(void);
void MioPan_InputResetMoveSettings(void);

#ifdef __cplusplus
}
#endif

#endif /* MIOPAN_INPUT_H */
