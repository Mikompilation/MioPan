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
// button / gamepad axis values, but keeping them plain ints is what lets
// sdk/libpad.cpp include this without pulling SDL in.

#define MIOPAN_PAD_BUTTON_COUNT 16
#define MIOPAN_PAD_STICK_COUNT  4

// Sources bound to one button.  Four is enough for a keyboard key, a pad
// button, and an alternate of each; a source list is how a button ends up held
// by either device, which is what the fixed gamepad-or-keyboard test did before
// the list was made data.
#define MIOPAN_INPUT_MAX_SOURCES 4

// What kind of physical control a binding source names.
enum
{
    MIOPAN_SRC_NONE = 0,
    MIOPAN_SRC_KEY,        // `code` is an SDL scancode
    MIOPAN_SRC_PAD_BUTTON, // `code` is an SDL_GamepadButton
    MIOPAN_SRC_PAD_AXIS    // `code` is an SDL_GamepadAxis; see dir/threshold
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
// a non-default threshold is appended as "@<n>".  ToString returns the number
// of characters written (0 on failure); FromString returns non-zero on a
// recognised source and leaves `out` untouched otherwise, so an unknown name in
// a settings file is skipped rather than clearing the binding.
int MioPan_InputSourceToString(const MioPan_InputSource *src, char *buf, int len);
int MioPan_InputSourceFromString(const char *text, MioPan_InputSource *out);

#ifdef __cplusplus
}
#endif

#endif /* MIOPAN_INPUT_H */
