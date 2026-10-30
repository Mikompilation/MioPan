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
    int   smooth_2d;         /* bilinear text and sprite textures */
    int   msaa_samples;      /* 1 (off), 2, 4 or 8     */
    int   anisotropy;        /* 1 (off), 2, 4, 8 or 16 */
    int   aspect_mode;       /* MIOPAN_ASPECT_*        */
    float aspect_ratio;      /* used when aspect_mode is CUSTOM */
    int   lighting_mode;     /* MIOPAN_LIGHTING_*      */
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
    /* The cast's language, once Options > Special has imported the Japanese
     * voices (io/miopan_undub.h).  Nothing changes until they are there. */
    int   japanese_voices;       /* 0 English, 1 Japanese                    */
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
 * What the port adds to how the game plays.  ingame/camera/first_person_camera.h
 * and ingame/photo/finder_dpad.h are what read it, and their banners say how
 * each hangs off the ROM; Options > Special is what writes it.
 *
 * Every field is 4 bytes, so the overlay's settle-and-save compare can memcmp
 * it the way it does the structs above.
 */
#define MIOPAN_FIRST_PERSON_FOV_MIN     45.0f
#define MIOPAN_FIRST_PERSON_FOV_MAX     90.0f
#define MIOPAN_FIRST_PERSON_FOV_DEFAULT 60.0f

typedef struct MioPan_ConfigGame
{
    int   first_person;       /* 0 the game's own cameras, 1 Mio's eyes      */
    float first_person_fov;   /* vertical, degrees; the finder's is 44.4     */
    int   finder_dpad;        /* 0 the d-pad aims the finder, 1 it changes
                               * film and uses healing items (finder_dpad.h) */
} MioPan_ConfigGame;

/*
 * The title screen's backdrop and music.  outgame/title_room.h is what reads
 * it, and its banner says how it hangs off the ROM's title; the overlay's
 * Settings > Title screen window is what writes it.
 *
 * The backdrop is a list of scenes -- a room, two camera points and the path
 * between them -- and the title picks among the enabled ones at random, moving
 * on after `passes` runs of the path or each time the title comes up.  The
 * dim, the music and the attract movie belong to the title, not a scene.
 *
 * Camera points are world space as the game has it: Y grows downwards, and a
 * standing character is about a thousand units tall.  Fields of view are in
 * degrees here and radians everywhere else.
 *
 * Every field is 4 bytes or a char array of a multiple of 4, so the overlay's
 * settle-and-save compare can memcmp it the way it does the structs above.
 */
#define MIOPAN_TITLE_BACKDROP_CLASSIC 0     /* the original scrolling art      */
#define MIOPAN_TITLE_BACKDROP_ROOM    1     /* one of the game's rooms         */

#define MIOPAN_TITLE_MOTION_PINGPONG  0     /* A to B and back                 */
#define MIOPAN_TITLE_MOTION_LOOP      1     /* A to B, then cut back to A      */
#define MIOPAN_TITLE_MOTION_ONCE      2     /* A to B, then hold               */

#define MIOPAN_TITLE_EASING_SMOOTH    0     /* eases out of each end and in    */
#define MIOPAN_TITLE_EASING_LINEAR    1

#define MIOPAN_TITLE_CHANGE_PASS      0     /* another scene after `passes`    */
#define MIOPAN_TITLE_CHANGE_VISIT     1     /* another scene per title visit   */

#define MIOPAN_TITLE_SCENE_MAX        32
#define MIOPAN_TITLE_NAME_MAX         32
#define MIOPAN_TITLE_VIA_MAX          12    /* waypoints between A and B       */

typedef struct MioPan_ConfigTitleScene
{
    char  name[MIOPAN_TITLE_NAME_MAX];
    int   enabled;           /* 0 keeps it out of the rotation              */
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
    /* Points the path passes through on its way from A to B, in order.  With
     * none it is the straight line it always was; with some it is a smooth
     * curve through all of them, travelled at an even speed. */
    int   via_count;         /* 0..MIOPAN_TITLE_VIA_MAX                     */
    float via_eye[MIOPAN_TITLE_VIA_MAX][3];
    float via_target[MIOPAN_TITLE_VIA_MAX][3];
    float via_fov[MIOPAN_TITLE_VIA_MAX];
} MioPan_ConfigTitleScene;

typedef struct MioPan_ConfigTitle
{
    int   backdrop;          /* MIOPAN_TITLE_BACKDROP_*                     */
    int   change;            /* MIOPAN_TITLE_CHANGE_*                       */
    int   passes;            /* runs of the path before the next scene      */
    float dim;               /* black under the logo, 0 none .. 1 black     */
    int   music;             /* 0 the game's own; else a stream file, looped */
    float music_volume;      /* 0..1; 1 is the level the game plays music at */
    int   attract_movie;     /* room backdrop: let the idle movie start     */
    /* Which set of built-in scenes this list has been given.  A list from a
     * miopan.ini that predates the current set gets the ones it lacks added
     * once, at load; one deleted after that stays deleted. */
    int   builtins;
    int   scene_count;       /* 1..MIOPAN_TITLE_SCENE_MAX                   */
    MioPan_ConfigTitleScene scenes[MIOPAN_TITLE_SCENE_MAX];
} MioPan_ConfigTitle;

/* A scene as a fresh one starts: room 40, framed by itself on load. */
void MioPan_ConfigTitleSceneDefault(MioPan_ConfigTitleScene *scene);

/* The built-in scenes, for putting back the ones a list has had deleted.
 * Missing counts the ones it does not have (a scene in the same room under
 * the same name counts as had); Add appends them, up to
 * MIOPAN_TITLE_SCENE_MAX, and returns how many went in. */
int MioPan_ConfigTitleBuiltinMissing(void);
int MioPan_ConfigTitleAddBuiltin(void);

/*
 * The title settings as text, exactly as miopan.ini carries them, for the
 * overlay's copy and paste.  Both write NUL-terminated text and return the
 * length the whole of it needs, so a NULL or short buffer measures.
 *
 *   Title  [title] and every [title.sceneN] after it.
 *   Scene  one scene's section.
 */
int MioPan_ConfigTitleText(char *buf, int size);
int MioPan_ConfigTitleSceneText(int scene, char *buf, int size);

/* Append every scene section found in `text` -- [title.sceneN] blocks as the
 * two above write them, numbered or not -- after the scenes already there, up
 * to MIOPAN_TITLE_SCENE_MAX.  With `replace` the list is cleared first and a
 * [title] section in the text sets the title's own settings too.  Returns the
 * number of scenes taken; 0 changes nothing. */
int MioPan_ConfigTitlePaste(const char *text, int replace);

typedef struct MioPan_Config
{
    /* Order matters: miopan_config.cpp's defaults are a positional
     * initialiser in exactly this order. */
    MioPan_ConfigRenderer  renderer;
    MioPan_ConfigAudio     audio;
    MioPan_ConfigUi        ui;
    MioPan_ConfigPaths     paths;
    MioPan_ConfigDeveloper developer;
    MioPan_ConfigGame      game;
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
