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
} MioPan_ConfigRenderer;

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

typedef struct MioPan_Config
{
    MioPan_ConfigRenderer renderer;
    MioPan_ConfigPaths    paths;
} MioPan_Config;

extern MioPan_Config miopan_config;

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
