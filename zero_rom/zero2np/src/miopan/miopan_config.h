#ifndef MIOPAN_CONFIG_H
#define MIOPAN_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Persisted host settings.
 *
 * These are the port's own knobs -- window, resolution, aspect, shading -- and
 * they deliberately live OUTSIDE the game's save data.  system/mc/dat/save_data.c
 * is a manifest walk whose callback order is literally the byte layout of a save
 * file, so adding a field there would invalidate every existing save; and none
 * of this belongs to the ROM's option screen in any case.
 *
 * The file is `miopan.ini` in the port's user directory (see io/miopan_paths.h),
 * next to `memcard/`, and it is plain text meant to be hand-editable: enumerated
 * settings are written as names ("borderless", "16:9", "native") and read back
 * either way, so a numeric value from an older file still loads.
 *
 * Precedence is defaults < file < environment.  The MIOPAN_* environment
 * variables still win, so a one-off test run never has to disturb -- or
 * accidentally persist into -- the saved settings.
 */

typedef struct MioPan_ConfigRenderer
{
    int   window_mode;       /* MIOPAN_WINDOW_MODE_*   */
    int   window_width;      /* windowed size only; fullscreen is never stored */
    int   window_height;
    int   render_mode;       /* MIOPAN_RENDER_RES_*    */
    float render_scale;
    int   upscale_filter;    /* MIOPAN_RENDER_FILTER_* */
    int   msaa_samples;      /* 1 (off), 2, 4 or 8     */
    int   anisotropy;        /* 1 (off), 2, 4, 8 or 16 */
    int   aspect_mode;       /* MIOPAN_ASPECT_*        */
    float aspect_ratio;      /* used when aspect_mode is CUSTOM */
    int   lighting_mode;     /* MIOPAN_LIGHTING_*      */
    int   animated_lighting; /* MIOPAN_ANIMATED_LIGHTING_* */
    int   shadow_filter;     /* MIOPAN_SHADOW_FILTER_*  */
    int   extra_presents;    /* frame smoothing: 0..3 additional presents */
    int   interpolate_presents;
    int   interpolate_geometry; /* blend the world too, not just the camera */
    int   smoothing_mode;    /* MIOPAN_SMOOTHING_*: field-rate or display-rate */

    /* Output transform.  See the block comment above
     * MioPan_RendererSetHdrMode() in rendering/miopan_renderer.h. */
    int   hdr_mode;              /* MIOPAN_HDR_*                            */
    float hdr_paper_white;       /* nits; 0 follows the OS's SDR white level */
    float hdr_peak;              /* nits; 0 follows the display's headroom   */
    float hdr_expansion;         /* highlight lift, 0..1                     */
    float hdr_expansion_knee;    /* where the lift starts, 0.5..1            */

    /* The display grade.  Applies in SDR as well; all four at 1.0 is identity
     * and costs nothing, because the present pass is then skipped. */
    float grade_brightness;
    float grade_contrast;
    float grade_gamma;
    float grade_saturation;

    /* Screen effects the player may turn off.  See the block above
     * MioPan_RendererSetFilmGrain() in rendering/miopan_renderer.h. */
    int   film_grain;            /* MIOPAN_FILM_GRAIN_*                      */

    /* The viewfinder surround.  See the block above
     * MioPan_RendererDrawFinderMask() in rendering/miopan_renderer.h. */
    int   finder_mask;           /* 0 off, 1 on                              */
    float finder_mask_blur;      /* defocus outside the frame, 0..1          */
    float finder_mask_darken;    /* 0 none, 1 black                          */
    float finder_mask_tint;      /* cast toward crimson, 0..1                */
    float finder_mask_scale;     /* clear rect against the ROM's 640x448     */

    /* Cut-out edge sharpening.  See the block above
     * MioPan_RendererSetAlphaSharpen() in rendering/miopan_renderer.h. */
    float alpha_sharpen;         /* 0 off .. 1 full                          */
    float alpha_cutoff;          /* hard discard threshold, 0 off            */

    /* Static geometry kept on the GPU.  See the block above
     * MioPan_RendererCreateResidentMesh() in rendering/miopan_renderer.h. */
    int   resident_meshes;       /* 0 off, 1 on                              */
    /* Model textures resolved once, their GS uploads then skipped.  See
     * MioPan_RendererSetResidentTextures(). */
    int   resident_textures;     /* 0 off, 1 on                              */
    /* Textures kept as the bytes the game sent, GS memory written only when
     * read.  See MioPan_GsSetByReference() in gs/miopan_gs_c.h. */
    int   gs_by_reference;       /* 0 off, 1 on                              */
    /* Character, item and other runtime meshes sent as indexed streams.  See
     * MioPan_RendererBeginIndexedMeshStream(). */
    int   indexed_streams;       /* 0 off, 1 on                              */

    /* Presentation sync.  See the block above MioPan_RendererSetVsync() in
     * rendering/miopan_renderer.h. */
    int   vsync;                 /* MIOPAN_VSYNC_*                           */
    /* Hold the game while the window is in the background.  See the block
     * above MioPan_RendererSetMuteInBackground(). */
    int   pause_in_background;   /* 0 off, 1 on                              */
} MioPan_ConfigRenderer;

/*
 * The output side of the sound.  Host gain only: see the block above
 * MioPan_VoiceSetOutputVolume() in sdk/iop_voice.h for why none of it reaches
 * the SPU2 model the ROM reads back.
 *
 * There is deliberately no music / effects / voice split.  The ROM's mixer
 * does have groups -- SndCalcValue() scales every voice by a per-group volume
 * -- but only two of its five slots are ever named (effects and BGM), there is
 * no group for speech at all, a sound's group is a 3-bit field read out of the
 * sound data rather than decided by the code, and the ROM's own option screen
 * writes the same value into both.  A bus slider built on that would have to
 * guess which sounds are which.
 */
typedef struct MioPan_ConfigAudio
{
    float volume;                /* master, 0..1 slider position; gain is its square */
    int   mute_in_background;    /* 0 off, 1 on                              */
} MioPan_ConfigAudio;

/* The F1 overlay. */
typedef struct MioPan_ConfigUi
{
    float scale;                 /* 0 follows the display; else a fixed factor */
} MioPan_ConfigUi;

/*
 * Where the game's own files are.
 *
 * This is the one setting that is not a preference: without it the port has
 * nothing to load.  It is here rather than compiled in because on a phone or a
 * console the data cannot be beside the executable and there is no shell to set
 * an environment variable in -- pointing this key at a folder is the whole
 * mechanism.  Empty means "discover it", and what discovery finds is written
 * back, so the file always names the folder actually in use.
 */
typedef struct MioPan_ConfigPaths
{
    char data_folder[1024];
} MioPan_ConfigPaths;

/*
 * The prototype's own developer tooling.
 *
 * This Feb 2004 build still answers SELECT with the developers' debug menu
 * (GID_STORY_DEBUG), from normal play and from both pause screens.  SELECT is
 * Tab and the gamepad's Back button on the port's default bindings, so a player
 * reaching for either lands in a menu that says nothing about how to leave it.
 * Off by default; on restores the ROM's behaviour exactly.  The useful parts of
 * that menu are on the overlay's Assists menu either way.
 */
typedef struct MioPan_ConfigDeveloper
{
    int debug_menu;      /* 0 off, 1 SELECT opens the ROM's debug menu */
} MioPan_ConfigDeveloper;

/*
 * The title screen's backdrop and music.  outgame/title_room.h is what reads
 * it, and its banner says how it hangs off the ROM's title; the overlay's
 * Settings > Title screen window is what writes it.
 *
 * Camera points are world space as the game has it: Y grows downwards, and a
 * standing character is about a thousand units tall.  Fields of view are in
 * degrees here and radians everywhere else.
 *
 * Every field is 4 bytes, so the overlay's settle-and-save compare can memcmp
 * it the way it does the structs above.
 */
#define MIOPAN_TITLE_BACKDROP_CLASSIC 0     /* the original scrolling art      */
#define MIOPAN_TITLE_BACKDROP_ROOM    1     /* one of the game's rooms         */

#define MIOPAN_TITLE_MOTION_PINGPONG  0     /* A to B and back                 */
#define MIOPAN_TITLE_MOTION_LOOP      1     /* A to B, then cut back to A      */
#define MIOPAN_TITLE_MOTION_ONCE      2     /* A to B, then hold               */

#define MIOPAN_TITLE_EASING_SMOOTH    0     /* eases out of each end and in    */
#define MIOPAN_TITLE_EASING_LINEAR    1

typedef struct MioPan_ConfigTitle
{
    int   backdrop;          /* MIOPAN_TITLE_BACKDROP_*                     */
    int   room;              /* 0..65, as RY00_PK2 counts them              */
    int   camera_set;        /* 0: frame the room by itself when it loads   */
    float a_eye[3];
    float a_target[3];
    float a_fov;             /* degrees                                     */
    float b_eye[3];
    float b_target[3];
    float b_fov;             /* degrees                                     */
    float travel_seconds;    /* one pass, A to B                            */
    float hold_seconds;      /* at each end                                 */
    int   motion;            /* MIOPAN_TITLE_MOTION_*                       */
    int   easing;            /* MIOPAN_TITLE_EASING_*                       */
    float dim;               /* black under the logo, 0 none .. 1 black     */
    int   music;             /* 0 the game's own; else a stream file, looped */
    float music_volume;      /* 0..1; 1 is the level the game plays music at */
    int   attract_movie;     /* room backdrop: let the idle movie start     */
} MioPan_ConfigTitle;

typedef struct MioPan_Config
{
    /* Order matters: miopan_config.cpp's defaults are a positional
     * initialiser in exactly this order. */
    MioPan_ConfigRenderer  renderer;
    MioPan_ConfigAudio     audio;
    MioPan_ConfigUi        ui;
    MioPan_ConfigPaths     paths;
    MioPan_ConfigDeveloper developer;
    MioPan_ConfigTitle     title;
} MioPan_Config;

extern MioPan_Config miopan_config;

/* [developer] debug_menu.  The game's SELECT tests (ingame.c, pause.c,
 * mission_pause.c) ask through the getter.  The setter persists at once, as
 * MioPan_ConfigSetDataFolder() does, and returns non-zero once it is on disk. */
int MioPan_ConfigDebugMenuEnabled(void);
int MioPan_ConfigSetDebugMenuEnabled(int enabled);

/* Absolute path of the settings file, or NULL if the user directory could not be
 * created. */
const char *MioPan_ConfigPath(void);

/* Read the file over the defaults.  Idempotent -- the paths module calls it as
 * soon as anything asks where the data is, which is well before the renderer
 * exists, and the second call is a no-op.  Safe to call when no file exists:
 * that is the first run, and it leaves the defaults in place and writes the file
 * out so there is something to edit.  Does not touch the renderer; call
 * MioPan_ConfigApply() for that. */
void MioPan_ConfigLoad(void);

/* Record the data folder and persist it.  Called by the paths module with what
 * discovery found, so an unconfigured first run still ends up with the key
 * filled in, and by the settings UI when the player chooses one.  Returns
 * non-zero when the value is stored and on disk -- an unchanged value counts,
 * a refused write does not, which is what lets the UI say which happened. */
int MioPan_ConfigSetDataFolder(const char *folder);

/* Write the current struct out.  Returns non-zero on success. */
int MioPan_ConfigSave(void);

/* Push the struct into the renderer, and pull the renderer's live state back
 * into the struct, respectively.  Capture reads the *windowed* size even while
 * the window is fullscreen, so toggling fullscreen never overwrites the size the
 * window should return to. */
void MioPan_ConfigApply(void);
void MioPan_ConfigCapture(void);

/* Capture then save -- what the host UI calls after a settings change. */
int MioPan_ConfigCaptureAndSave(void);

#ifdef __cplusplus
}
#endif

#endif /* MIOPAN_CONFIG_H */
