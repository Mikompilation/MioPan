#ifndef MIOPAN_RENDERER_H
#define MIOPAN_RENDERER_H

#include "../gs/miopan_gs_c.h"

#ifdef __cplusplus
extern "C" {
#endif

void MioPan_RendererBeginFrame(void);
void MioPan_RendererEndFrame(void);

/* How much wider/taller the output window is than the original 640x448 frame,
 * in original-frame units: 1.0 on a 4:3 output, about 1.21 on 16:9.  Only one
 * of the two is ever above 1.  The 3D projection widens by these so the extra
 * area shows more world rather than black bars; 2D overlays are shrunk by the
 * reciprocal and stay centred at their original proportions.  Sampled once per
 * BeginFrame, so they are stable for the whole frame. */
void MioPan_RendererGetViewExtend(float *ext_x, float *ext_y);

/* The same thing as a screen rect in 640x448 units: exactly (0,0)-(640,448) on
 * a 4:3 output, about (-66,0)-(706,448) on 16:9.  Full-screen backdrops that
 * must reach the window edge (MapSky's fog band and horizon strip) build their
 * geometry against this instead of the fixed frame. */
void MioPan_RendererGetViewBounds(float *x0, float *y0, float *x1, float *y1);
void MioPan_RendererSetPresentInterval(int interval);

/* Internal render resolution, decoupled from the window.
 *
 * The scene is drawn into an off-screen target and blitted to the swapchain at
 * present, so how many pixels the game is rasterised into is independent of how
 * large the window is.  The aspect ratio is NOT a setting here: it always comes
 * from the window, through g_view_extend_*, so the target and the swapchain are
 * always the same shape and the present blit is a plain stretch with no
 * letterboxing.  What this chooses is pixel density, not framing.
 *
 *   MATCH_WINDOW  render at the output's pixel size.  The scene target is
 *                 skipped entirely and drawing goes straight into the
 *                 swapchain, exactly as it did before this existed.
 *   NATIVE_PS2    render at the ROM's own 640x448 times `scale`, widened by
 *                 the view extend.  At scale 1.0 that is PS2 pixel density
 *                 whatever the window is -- about 796x448 on 16:9 -- which is
 *                 the authentic look, and the only mode in which the CRT
 *                 filter's scanline pitch means anything.
 *   WINDOW_SCALE  render at the output's size times `scale`.  A performance
 *                 dial for high-DPI displays; 1.0 is the same as MATCH_WINDOW.
 *
 * `scale` is clamped to 0.25 .. 8.0 and the resolved size to the device's
 * maximum texture dimension.  A target that cannot be allocated falls back to
 * MATCH_WINDOW for that frame rather than failing the present. */
enum
{
    MIOPAN_RENDER_RES_MATCH_WINDOW = 0,
    MIOPAN_RENDER_RES_NATIVE_PS2 = 1,
    MIOPAN_RENDER_RES_WINDOW_SCALE = 2
};
void MioPan_RendererSetRenderResolution(int mode, float scale);
void MioPan_RendererGetRenderResolution(int *mode, float *scale);
/* The size the last presented frame was actually rasterised at, for the UI. */
void MioPan_RendererGetRenderSize(int *width, int *height);

/* How the scene target is resampled onto the swapchain.  NEAREST keeps the
 * chunky original pixels but duplicates unevenly at non-integer scale, which
 * reads as shimmer on a camera pan; LINEAR is even but soft.  Ignored in
 * MATCH_WINDOW, where nothing is resampled. */
enum
{
    MIOPAN_RENDER_FILTER_NEAREST = 0,
    MIOPAN_RENDER_FILTER_LINEAR = 1
};
void MioPan_RendererSetUpscaleFilter(int filter);
int  MioPan_RendererGetUpscaleFilter(void);

/* Multisample anti-aliasing, on the final image and nothing else.
 *
 * 1, 2, 4 or 8 samples; 1 is off and is the default.  The frame the game is
 * rasterised into becomes the renderer's only multisampled surface, and it is
 * resolved into an ordinary single-sampled copy at the end of every pass
 * segment.  Everything downstream of that resolve is untouched: the GS capture
 * slots the effects sample, the pause still, the 640x448 screen mirror a
 * photograph is read back out of, the shadow atlas, the present blit and the
 * host UI are all single-sampled exactly as before.
 *
 * Two costs are worth knowing before turning it up.
 *
 *   1. Resolves.  An effect that samples the GS frame buffer already breaks
 *      the replay into segments so the frame so far can be copied; with MSAA
 *      on, every one of those breaks resolves the whole target as well.  A
 *      frame carrying several refraction or smear effects therefore pays
 *      several full-resolution resolves on top of the extra rasterisation.
 *      This game uses those effects heavily, so the cost is not the "free at
 *      4x" of a forward renderer with a single pass.
 *   2. The depth read-back stops.  A multisampled depth texture cannot be
 *      downloaded and SDL_GPU resolves colour only, so while MSAA is on
 *      MioPan_RendererQueryPointOccluded() answers UNAVAILABLE and
 *      effect_sub.o's CheckPointDepth() degrades to "on screen means
 *      visible" -- a ghost can stay a finder target through a wall.  That is
 *      the behaviour the port had before the probe existed, and it comes back
 *      the frame MSAA is turned off.
 *
 * Live.  Pipelines are cached per sample count and never released, so a change
 * costs one warm-up of the new set and leaves any command buffer still holding
 * the old set's pointers valid.  A level the device will not grant is clamped;
 * Get returns what was asked for, GetActive what is running, and GetMax what
 * this device supports. */
void MioPan_RendererSetMsaaSamples(int samples);
int  MioPan_RendererGetMsaaSamples(void);
int  MioPan_RendererGetMsaaActiveSamples(void);
int  MioPan_RendererGetMsaaMaxSamples(void);

/* Anisotropic filtering on material textures -- 1, 2, 4, 8 or 16 taps; 1 is
 * off and is the default.
 *
 * Applies wherever the game samples a texture, the GS capture slots an effect
 * reads included, and only where the PS2's TEX1 asked for linear filtering in
 * both directions.  That is partly taste -- a NEAREST minification is the
 * hardware asking for point sampling, and averaging taps under it would change
 * what it did -- and partly a hard D3D12 rule, which has exactly one
 * anisotropic filter value and it is the all-linear one.  The fixed internal
 * bindings -- the 1x1 white texture, the shadow atlas, the present pass's 1:1
 * read of the finished frame -- stay plain, because none of them is a minified
 * surface.
 *
 * Essentially free where it does anything: no extra passes, no extra memory,
 * and the taps are only taken on pixels whose texel footprint is actually
 * anisotropic, which is the grazing-angle floor and little else.
 *
 * The honest limit: the port decodes each GS page as a single-level texture,
 * so there are no mip chains for this to select between.  What it still does
 * is clamp the LOD to 0 and average up to N taps along the major axis of the
 * footprint, which is texture-space supersampling and is what kills the
 * shimmer on tatami and corridor floors.  What it cannot fix without mip
 * levels is the far end of such a floor, where the minor axis has minified
 * past level 0 too.  Generating mips for GS pages is a separate change and a
 * larger one -- they are packed sheets, and a level 1 would bleed one
 * sub-image into its neighbour.
 *
 * Live, and for the same reason MSAA is: sampler sets are built per level and
 * never released. */
void MioPan_RendererSetAnisotropy(int max_anisotropy);
int  MioPan_RendererGetAnisotropy(void);
int  MioPan_RendererGetAnisotropyActive(void);
int  MioPan_RendererGetAnisotropyMax(void);

/* Window mode.
 *
 * BORDERLESS is desktop fullscreen: the window is given no display mode of its
 * own (SDL_SetWindowFullscreenMode(NULL)) and simply covers the desktop at
 * whatever resolution the desktop is already running.  Because no video mode
 * switch happens, alt-tab is instant, the desktop does not re-lay out, and a
 * second monitor is unaffected -- which is the whole reason to prefer it to
 * exclusive fullscreen.  Exclusive is deliberately not offered.
 *
 * Changing resolution is the render-resolution setting's job, not this one:
 * the scene target is already decoupled from the window, so a borderless window
 * on a 4K desktop can still rasterise at 640x448.  That is what makes exclusive
 * mode unnecessary here rather than merely unfashionable.
 *
 * Toggled at runtime with Alt+Enter, or from the host UI's Renderer menu. */
enum
{
    MIOPAN_WINDOW_MODE_WINDOWED = 0,
    MIOPAN_WINDOW_MODE_BORDERLESS = 1
};
void MioPan_RendererSetWindowMode(int mode);
int  MioPan_RendererGetWindowMode(void);
void MioPan_RendererToggleFullscreen(void);

/* The size the window has -- or would return to -- when NOT fullscreen, latched
 * from resize events while windowed.  Querying the live window instead would
 * report the desktop size whenever settings are saved from borderless mode, and
 * the window would never come back to its old shape. */
void MioPan_RendererGetWindowedSize(int *width, int *height);

/* Display aspect ratio, decoupled from the window.
 *
 * This is what g_view_extend_* is built from, so it decides two things at once
 * and they cannot be separated: how much world the 3D camera shows, and what
 * shape the picture is.  A wider aspect widens the camera rather than stretching
 * the image -- that is the port's established widescreen behaviour and it is
 * unchanged; all this adds is the ability to choose the number instead of
 * inheriting it from the window.
 *
 * When the chosen aspect does not match the window, the picture is centred in
 * the window at its own shape and the remainder is cleared to black -- pillars
 * on a window wider than the aspect, bars on one taller.
 *
 *   AUTO      the window's own shape.  No bars, ever; the default, and exactly
 *             what the port did before this existed.
 *   ORIGINAL  640:448, the PS2 frame itself.  Both extends are 1.0, so this is
 *             the only setting that shows precisely the field of view the game
 *             was composed for, with no widescreen extension at all.
 *   CUSTOM    the ratio passed to Set, clamped to 0.5 .. 5.0.
 *
 * Note ORIGINAL is 640:448 (~1.43), not 4:3.  The port rasterises square
 * pixels -- see the fAspectY deviation in gra3d.c -- where the PS2 produced
 * 640x448 for a CRT to stretch to 4:3.  Selecting 4_3 keeps the same square
 * pixels and simply composes for a 4:3 frame; it does not reproduce the CRT's
 * horizontal squash, which would be a pixel-aspect setting rather than this. */
enum
{
    MIOPAN_ASPECT_AUTO = 0,
    MIOPAN_ASPECT_ORIGINAL = 1,
    MIOPAN_ASPECT_4_3 = 2,
    MIOPAN_ASPECT_16_9 = 3,
    MIOPAN_ASPECT_16_10 = 4,
    MIOPAN_ASPECT_CUSTOM = 5
};
void MioPan_RendererSetAspectMode(int mode, float custom_ratio);
void MioPan_RendererGetAspectMode(int *mode, float *custom_ratio);
/* Where the picture landed in the window last frame, in output pixels; equal to
 * the whole swapchain whenever there are no bars. */
void MioPan_RendererGetPresentRect(int *x, int *y, int *width, int *height);

/* ------------------------------------------------------------------------
 *  Output transform: display grade and HDR.
 *
 *  The game is SDR and stays SDR.  Every blend the GS emulation reproduces --
 *  the alpha rules, the brightness filter's Cd*(1+As/128), the screen
 *  negatives -- is defined on 0..1 framebuffer values, and moving any of it
 *  into a high dynamic range would change the picture rather than present it.
 *  So the whole pipeline is untouched and one pass at the very end converts
 *  the finished frame into whatever encoding the swapchain is running.  That
 *  pass is resources/shaders/hlsl/present.frag.hlsl.
 *
 *  What that buys, in order of how much it actually matters:
 *
 *    1. Absolute brightness.  On an HDR display, SDR content is composited at
 *       whatever level the OS was told to use.  Paper white makes it the
 *       player's choice instead, which for a game this dark is the whole
 *       point -- Fatal Frame's darks are meant to be near the display's floor,
 *       and a panel driving them at 400 nits of "SDR" washes the game out.
 *    2. Highlight headroom.  The source tops out at white, so anything above
 *       paper white has to be invented.  The expansion knob does that on a
 *       knee, and at strength 0 it does nothing at all -- which is the honest
 *       setting and stays available on purpose.
 *    3. Precision.  With HDR active the scene target and the composited output
 *       are 10-bit rather than 8, which is what stops the expansion turning
 *       this game's many full-screen blend passes into contour rings.
 *
 *  The pass runs whenever it has something to do: an HDR swapchain, or a grade
 *  that is not identity.  A plain SDR session at default settings never
 *  allocates the output target and never takes the pass, so the default path
 *  is exactly what it was before any of this existed.
 * --------------------------------------------------------------------- */

/*
 * HDR output mode.
 *
 *   OFF     SDR swapchain.  Always available; the default.
 *   AUTO    HDR when the window reports headroom over SDR white, preferring
 *           scRGB.  Falls back to SDR silently on an SDR display, and follows
 *           the display if it is switched at runtime.
 *   SCRGB   force SDL_GPU_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR.
 *   HDR10   force SDL_GPU_SWAPCHAINCOMPOSITION_HDR10_ST2084.
 *
 * scRGB is preferred over HDR10 wherever both exist: it is linear with sRGB
 * primaries, so the conversion out of the game's own space is a single scale
 * and there is no PQ quantisation to dither around.  HDR10 is offered because
 * some displays and some capture paths only accept it.
 *
 * Set is live -- the swapchain composition is re-negotiated on the spot.  The
 * one thing that cannot follow at runtime is the 10-bit scene target, which is
 * chosen when the renderer starts because every pipeline is built against it;
 * a session that started in SDR therefore runs HDR from an 8-bit scene until
 * the next launch.  MioPan_RendererHdrIsFullPrecision() reports which of the
 * two a live session is on so the UI can say so.
 */
enum
{
    MIOPAN_HDR_OFF = 0,
    MIOPAN_HDR_AUTO = 1,
    MIOPAN_HDR_SCRGB = 2,
    MIOPAN_HDR_HDR10 = 3
};
void MioPan_RendererSetHdrMode(int mode);
int  MioPan_RendererGetHdrMode(void);

/* Whether the swapchain is actually running an HDR composition right now.  A
 * mode of AUTO on an SDR monitor reports 0 here, which is what the UI should
 * show rather than repeating the requested mode back at the player. */
int  MioPan_RendererGetHdrActive(void);

/* Whether the display currently reports headroom above SDR white, i.e. whether
 * asking for HDR would get anything.  Follows the display at runtime. */
int  MioPan_RendererHdrIsAvailable(void);

/* 1 when the scene target is the 10-bit one HDR wants.  0 while HDR is running
 * on a session that started in SDR -- correct, but 8-bit. */
int  MioPan_RendererHdrIsFullPrecision(void);

/* What the display says about itself, for the UI to show beside the sliders.
 * `sdr_white` is the OS's SDR white level in nits, `peak` the brightest it
 * claims to reach.  Both are 0 when the display reports nothing useful, which
 * is the normal answer on an SDR monitor. */
void MioPan_RendererGetHdrDisplayInfo(float *sdr_white_nits, float *peak_nits);

/* What the present pass will actually use, in nits, with every "follow the
 * display" already resolved.  This is what a UI should show beside a slider
 * left on auto -- the alternative is re-deriving the fallbacks there, which is
 * how the number on screen and the number in the shader drift apart. */
void MioPan_RendererGetHdrEffective(float *paper_nits, float *peak_nits);

/*
 * Paper white: the luminance an SDR white pixel is displayed at, in nits.
 *
 * This is the brightness control that matters in HDR, and it is not the same
 * knob as the grade's brightness below -- it moves where the whole picture
 * sits on the display without touching a single ratio inside it, so nothing
 * clips and nothing crushes.  Reach for this first.
 *
 * 0 means follow the OS's own SDR white level.  Otherwise clamped to
 * 50 .. 1000 nits; 200 is the default and is what BT.2408 and Windows both
 * treat as reference SDR white in an HDR container.
 */
void  MioPan_RendererSetHdrPaperWhite(float nits);
float MioPan_RendererGetHdrPaperWhite(void);

/* Peak: the brightest the expansion is allowed to drive a highlight, in nits.
 * 0 follows the display's reported headroom.  Clamped to 100 .. 10000, and
 * never below paper white -- a peak under paper white would darken highlights
 * rather than lift them. */
void  MioPan_RendererSetHdrPeak(float nits);
float MioPan_RendererGetHdrPeak(void);

/* Highlight expansion, 0 .. 1.  0 presents the SDR image at paper white and
 * invents nothing, which is the faithful setting.  1 drives white all the way
 * to peak.  `knee` is where the lift starts, as a fraction of paper white
 * (0.5 .. 1.0); below it the image is untouched. */
void  MioPan_RendererSetHdrExpansion(float strength, float knee);
void  MioPan_RendererGetHdrExpansion(float *strength, float *knee);

/*
 * The display grade.  Available in SDR as well as HDR, and applied in the
 * display-encoded domain -- these behave like a monitor's own OSD controls
 * rather than like a linear exposure, which is what a player expects of
 * something labelled "brightness".
 *
 * All four at 1.0 is identity and costs nothing: the present pass is skipped
 * entirely unless HDR needs it.  Ranges are brightness 0.25..2, contrast
 * 0.5..2, gamma 0.5..2.5, saturation 0..2.
 */
void  MioPan_RendererSetGrade(float brightness, float contrast, float gamma,
                              float saturation);
void  MioPan_RendererGetGrade(float *brightness, float *contrast, float *gamma,
                              float *saturation);

/* ------------------------------------------------------------------------
 *  The viewfinder surround.
 *
 *  A port addition: in finder mode, the world a wide window shows outside the
 *  ROM's own 640x448 frame is defocused, darkened and cast toward crimson.
 *
 *  That region is the port's, not the game's -- the PS2 rendered 640x448 and
 *  nothing else, so every pixel beyond it is world the game was never composed
 *  to show.  Treating it keeps a widescreen window from re-framing shots the
 *  original set up, while leaving the 640x448 the artists did compose exactly
 *  as it was rendered.
 *
 *  The pass is queued from the top of CNPlyrCamera::Draw(), which is after
 *  gra3dDraw() and every effect and before the first HUD sprite.  That is the
 *  whole reason it can work: it samples the finished world and the viewfinder
 *  chrome is then drawn sharp on top of it.
 * --------------------------------------------------------------------- */

/*
 * Queue the surround for this frame.  `alpha` is the viewfinder's own 0..128
 * master alpha, so it fades in and out with the HUD.
 *
 * The rectangle left clear is the ROM's 640x448 frame itself, which the
 * renderer already knows, so the caller supplies nothing but the fade: what
 * this treats is the world a wide window shows *beyond* the original frame,
 * and the composition the game was authored for is left untouched.
 *
 * Costs a render-pass break and one full-frame copy on the frames it runs, and
 * returns without doing either when the effect is off, the alpha is zero, every
 * strength is zero, or the window is 4:3 and there is nothing outside the frame
 * to treat.
 */
void MioPan_RendererDrawFinderMask(int alpha);

/*
 * `blur`, `darken` and `tint` are 0..1 and independent, which is what makes
 * each of the obvious looks reachable on its own: darken 1 with blur 0 is a
 * plain black surround, blur 1 with a little darkening is a defocused one, and
 * tint casts whatever is left toward the crimson the game is named for.
 *
 * `scale` moves the clear rectangle against the original 640x448 frame.  1.0 is
 * that frame exactly; below it the surround climbs into the picture the game
 * composed, which is the one value here that can spoil the original look.
 */
void MioPan_RendererSetFinderMask(int enable, float blur, float darken,
                                  float tint, float scale);
void MioPan_RendererGetFinderMask(int *enable, float *blur, float *darken,
                                  float *tint, float *scale);

/*
 * The film grain.
 *
 * effect_scr.c's SubDither3()/SubDither4() scroll three copies of a 128x128
 * noise sheet over the finished picture, and ingame_effect.c arms one every
 * frame the player is walking around (ScreenEffectParam00's Dither 3), so it
 * is the one screen effect that is essentially always on.  It is also the one
 * that survives least well off a CRT: the sheet was authored against an
 * interlaced 640x448 and at a modern render resolution each grain reads as a
 * crisp dot rather than as part of the picture's own texture.
 *
 * OFF makes both draws return before they build a packet -- the same place and
 * the same semantics as the game's own EffWrkDithOffSet() switch, which nothing
 * in the ROM ever raises -- so the effect's in/keep/out flow still advances and
 * its slot is still released on time.
 */
enum
{
    MIOPAN_FILM_GRAIN_OFF = 0,
    /* The ROM's own sheet: 128x128, sampled one texel per *PS2* pixel.  At the
     * PS2's 640x448 that is film grain; at three or four times that it is the
     * same grain magnified three or four times, which is what makes it read as
     * dots.  This is the faithful setting. */
    MIOPAN_FILM_GRAIN_PS2 = 1,
    /* One texel per *output* pixel, off a host-generated sheet with the same
     * distribution.  See MioPan_RendererFilmGrainBegin(). */
    MIOPAN_FILM_GRAIN_NATIVE = 2
};
void MioPan_RendererSetFilmGrain(int mode);
int  MioPan_RendererGetFilmGrain(void);

/*
 * Cut-out edge sharpening, 0..1.  A port setting rather than GS state.
 *
 * The GS alpha test this game writes is "discard fully transparent" and
 * nothing stronger -- measured over a full session, the highest AREF anywhere
 * is 0x01 -- which was sufficient at 640x448 where one texel covered one
 * pixel.  Magnified to a modern window, the bilinear filter spreads that same
 * one-texel alpha step over several pixels and pulls the RGB stored behind the
 * cut-out into the silhouette, which is the fog-coloured rim on foliage,
 * fences and hair.
 *
 * 0 is off and reproduces the previous behaviour exactly; the effect is also
 * an identity at or below 1:1 texel:pixel, so a native-resolution frame is
 * untouched whatever this is set to.
 */
void  MioPan_RendererSetAlphaSharpen(float strength);
float MioPan_RendererGetAlphaSharpen(void);

/*
 * A hard cut-out threshold, 0..1.  Discards any texel below it outright.
 *
 * Blunter than the sharpener above and independent of it: this aliases the
 * edge and can thin or erase fine geometry that is semi-transparent all the
 * way across, where the sharpener only restores the ramp magnification
 * softened.  Unlike the sharpener it applies at every resolution, native
 * included.  0 disables it, which is the hardware's behaviour.
 */
void  MioPan_RendererSetAlphaCutoff(float cutoff);
float MioPan_RendererGetAlphaCutoff(void);
/*
 * Bracket the grain's three quads so they come off a native-resolution sheet
 * instead of the ROM's 128x128 one.  Returns 1 if that substitution is in
 * force, 0 if the draws should go through the GS texture as usual (grain set
 * to PS2, or the sheet could not be created).
 *
 * Between Begin and End every textured quad is drawn from the host sheet, with
 * its UVs -- still written by the game in the 128x128 sheet's texel units --
 * rescaled so the span the ROM chose covers the same part of the screen at one
 * texel per output pixel.  The caller changes nothing else: the packet, the
 * blend, the depth and the three phased alphas are the ROM's throughout, which
 * is what keeps the two modes the same effect at two densities.
 *
 * `alpmx` and `colmx` are the maxima the ROM hands MakeRDither3(), and the
 * sheet is rebuilt only when they change -- as the ROM's is.  Its noise comes
 * from a private generator rather than MioPan_Rand(), deliberately: drawing a
 * million samples out of the game's own stream would move every later draw
 * from it and change enemy and effect behaviour.
 */
int  MioPan_RendererFilmGrainBegin(int alpmx, int colmx);
void MioPan_RendererFilmGrainEnd(void);

/* Where the VU1 light image is evaluated.  All three modes run the SAME law --
 * the kernel is one body, in resources/shaders/hlsl/miopan_vu1_lighting.hlsli,
 * mirrored on the CPU by gra3dCalcVu1VertexColor() -- so these choose a
 * sampling rate, not a lighting model.
 *
 *   VERTEX        every term on the CPU, once per vertex.  The reference; this
 *                 is what the PS2 did, facets and all.
 *   FRAGMENT      spotlights per pixel, the rest on the CPU per vertex.  Cheap
 *                 and targeted: it is the cones on big room triangles that
 *                 facet most visibly.
 *   FRAGMENT_ALL  the whole image per pixel, nothing on the CPU.  Removes the
 *                 per-vertex lighting cost entirely and moves the eighth-power
 *                 speculars, the per-light min(coef,1) saturation and the
 *                 closing clamp from "interpolated across a triangle" to
 *                 "evaluated where it actually happens".  That is more faithful
 *                 to the microcode and visibly unlike the PS2, which is the
 *                 whole point of keeping VERTEX selectable beside it.
 *
 * The split is expressed to the shader as a MIOPAN_VU1_TERM_* mask; whatever
 * the fragment stage is not given, the CPU has already folded into the vertex
 * colour it seeds from, so the terms are evaluated exactly once either way. */
enum
{
    MIOPAN_LIGHTING_VERTEX = 0,
    MIOPAN_LIGHTING_FRAGMENT = 1,
    MIOPAN_LIGHTING_FRAGMENT_ALL = 2
};
int  MioPan_RendererGetLightingMode(void);
void MioPan_RendererSetLightingMode(int mode);

/* Point-visibility probes -- the host half of effect_sub.c's CheckPointDepth().
 *
 * The ROM asked the GS for an 8x1 strip of the Z buffer and compared it against
 * the point's own Z.  There is no GS bus here and the depth lives in an SDL_GPU
 * texture, so the comparison is made on this side instead, entirely in the
 * renderer's own reversed-Z space -- which avoids having to map host depth back
 * into the PS2's 16-bit Z at all.
 *
 * `slot` identifies one persistent probe.  A query projects the point with the
 * frame's camera, registers that pixel for the NEXT frame's read-back, and
 * answers from the PREVIOUS frame's.  One frame of latency is deliberate: a
 * synchronous read would stall the pipeline, and nothing this feeds moves fast
 * enough to notice.
 *
 * Returns 1 occluded, 0 clear, or MIOPAN_DEPTH_PROBE_UNAVAILABLE when there is
 * no answer yet -- no frame read back, the point off screen, its pixel outside
 * the region that was downloaded, or the depth format not one this path
 * decodes.  Callers must treat that as "cannot tell" and fall back; it is the
 * normal answer for the first frames after a room load. */
enum
{
    MIOPAN_DEPTH_PROBE_UNAVAILABLE = -1,
    MIOPAN_DEPTH_PROBE_SLOTS = 128
};
int MioPan_RendererQueryPointOccluded(int slot, const float *world_pos);

/* Projected-shadow edge quality.
 *
 * The shadow map is a projected SILHOUETTE (coverage), not a depth map -- the
 * ROM's SRT_MAPSHADOW pass did the same -- so filtering it is a plain average
 * of coverage with no depth comparison to get right.
 *
 *   NONE  one bilinear tap, which is what the hardware's projected sprite gave
 *         and the port has always done.
 *   SOFT  a 3x3 box at one-texel spacing.  A deliberate deviation: the PS2's
 *         edge was hard.  It costs eight extra taps of a 1-channel map on the
 *         receiver pass only, which is the cheapest visible improvement in the
 *         whole light path.
 */
enum
{
    MIOPAN_SHADOW_FILTER_NONE = 0,
    MIOPAN_SHADOW_FILTER_SOFT = 1
};
int  MioPan_RendererGetShadowFilter(void);
void MioPan_RendererSetShadowFilter(int filter);

/* Which kernels the FRAGMENT stage is responsible for, carried in
 * MioPanLightState::config[2].  Mirrors MIOPAN_VU1_TERM_* in
 * miopan_vu1_lighting.hlsli.  DIRECTIONAL covers the ambient term as well --
 * they are one block in the microcode and there is no way to want one without
 * the other. */
enum
{
    MIOPAN_VU1_TERM_DIRECTIONAL = 1 << 0,
    MIOPAN_VU1_TERM_SPOT = 1 << 1,
    MIOPAN_VU1_TERM_POINT = 1 << 2,
    MIOPAN_VU1_TERM_ALL = MIOPAN_VU1_TERM_DIRECTIONAL |
                          MIOPAN_VU1_TERM_SPOT |
                          MIOPAN_VU1_TERM_POINT
};

/* Animated-character lighting backend.  GPU is the normal fast path; CPU is
 * the established gra3dCalcVertexColor reference retained for visual A/B
 * checks and unsupported/malformed mesh fallback. */
enum
{
    MIOPAN_ANIMATED_LIGHTING_GPU = 0,
    MIOPAN_ANIMATED_LIGHTING_CPU = 1
};
int  MioPan_RendererGetAnimatedLightingBackend(void);
void MioPan_RendererSetAnimatedLightingBackend(int backend);

/* Diagnostic render overrides exposed through the host UI. */
enum
{
    MIOPAN_RENDERER_DEBUG_WIREFRAME = 1 << 0,
    MIOPAN_RENDERER_DEBUG_DISABLE_DEPTH = 1 << 1,
    MIOPAN_RENDERER_DEBUG_FLAT_COLOUR = 1 << 2,
    MIOPAN_RENDERER_DEBUG_DISABLE_LIGHTING = 1 << 3,
    MIOPAN_RENDERER_DEBUG_DISABLE_BILLBOARD_HOST = 1 << 4,
    MIOPAN_RENDERER_DEBUG_SKIP_BILLBOARD_LEGACY_PACKETS = 1 << 5,
    MIOPAN_RENDERER_DEBUG_DISABLE_SKY_DOME = 1 << 6,
    MIOPAN_RENDERER_DEBUG_DISABLE_SKY_HORIZON = 1 << 7,
    /* Take 2D primitives back out of the depth unit -- what the port did
     * before it modelled ZTST/ZMSK for them.  The depth-buffer-as-stencil
     * passes (photo_make, n_equip_tray, effect_scr's stacked copies) stop
     * being masked, which is the A/B for "is this draw's problem the 2D depth
     * test". */
    MIOPAN_RENDERER_DEBUG_DISABLE_2D_DEPTH = 1 << 8,
    /* Paint the projected-shadow receivers by what the projector says
     * instead of blending them: green where the map is clear, red where
     * it is occluded, blue where the projection misses entirely.  Tells
     * "no shadow map" apart from "map is empty" apart from "receivers
     * are not being submitted". */
    MIOPAN_RENDERER_DEBUG_SHADOW_VIEW = 1 << 9
};
unsigned int MioPan_RendererGetDebugViewFlags(void);
void MioPan_RendererSetDebugViewFlags(unsigned int flags);

/* GS local-image copy, host side (g2d_draw.c's LocalCopy* family).  Captures
 * the frame as drawn so far -- or another already-captured block -- into the GS
 * block `dst_addr`, at the logical size the PS2 copy produces.  Effects that
 * later sample `dst_addr` as a texture are served from it.  Pass
 * MIOPAN_GS_CAPTURE_LIVE as `src_addr` to copy out of the frame buffer. */
#define MIOPAN_GS_CAPTURE_LIVE 0xFFFFFFFFu
void MioPan_RendererCaptureGsBlock(unsigned int dst_addr,
                                   unsigned int src_addr,
                                   int logical_w, int logical_h);

/* Screen-space textured triangles with per-vertex UV and colour -- the shape
 * the effect packet builders emit, which no other bridge carries.  `xy` is in
 * 640x448 frame coordinates, `rgba` is one colour per vertex, and `uv` is
 * texel space unless `uv_normalised` is non-zero (the GS ST registers hold
 * 0..1, the UV registers hold texels).  Resolves `tex0` through the capture
 * slots, so a mesh sampling a frame-buffer copy is drawn from the capture.
 *
 * `projected` says which space the coordinates are in, and it matters on any
 * output that is not 4:3.  0 is a whole-screen filter authored in 640x448,
 * grown to cover the window.  1 is geometry that came through gra3d's
 * matWorldScreen -- which is built WITHOUT the widescreen extend, only the
 * clip matrices get it -- so it is placed and sampled through the same
 * contraction the renderer applies to the 3D scene.
 *
 * ndc_z is one depth per vertex in the engine's symmetric clip convention
 * (near -1, far +1, i.e. clip.z / clip.w), or NULL for a draw that carries no
 * depth.  The sprite vertex shader's MikuPanFixClipZ() turns it into the
 * renderer's reversed-Z, the same route the particle billboard bridge takes.
 * depth_test is the draw env's ZTST: non-zero for GEQUAL, zero for ALWAYS.  A
 * NULL ndc_z with depth_test set would put the whole mesh at mid-depth, so the
 * pair travels together. */
void MioPan_RendererDrawTexturedTriangles2D(const sceGsTex0 *tex0,
                                            const float *xy,
                                            const float *uv,
                                            const unsigned char *rgba,
                                            const float *ndc_z,
                                            int vertex_count,
                                            int uv_normalised,
                                            int projected,
                                            int depth_test);

/* The Z a 2D primitive carries, straight out of DISP_SPRT / DISP_SQAR.
 *
 * The GS tested and wrote depth for 2D primitives exactly as it did for 3D
 * ones, and the engine uses that: a quad with ZMSK clear stamps a rectangle
 * into the depth buffer and a later full-screen pass with ZTST GEQUAL is
 * masked to everything outside it.  photo_make.c's picture, n_equip_tray.c's
 * accumulator dial and effect_scr.c's stacked screen copies are all built that
 * way, and none of them worked while 2D draws carried no depth at all.
 *
 * Set by g2d_draw.c's three primitive builders right before the draw, next to
 * the SetDrawEnv() that supplies the matching TEST and ZBUF. */
void MioPan_RendererSetGs2dDepth(unsigned int gs_z);

void MioPan_RendererCaptureScreen(unsigned int addr);

/* Read the last presented frame back into EE memory as PS2 PSMCT32 words
 * (bytes R, G, B, A, with alpha on the PS2's 0..128 scale).
 *
 * The host half of a GS LOCAL->HOST store: nothing draws into emulated GS
 * memory, so g2d_draw.c's LocalCopy*toB family has no pixels to hand back
 * without this.  `src_*` is a rectangle in the ROM's 640x448 frame
 * coordinates; it is box-filtered into a tightly packed `dst_w` x `dst_h`
 * image `dst_pitch` pixels to the row (0 for `dst_w`), which is how the 2:1
 * vertical squash a photograph is stored at and the 45x15 album thumbnail
 * dropped into a wider page both come out of the same primitive.
 *
 * Synchronous -- the caller compresses the result in the same call -- and
 * returns 0 if no frame has been presented yet. */
int MioPan_RendererReadbackScreen(unsigned char *dst, int dst_pitch,
                                  int src_x, int src_y, int src_w, int src_h,
                                  int dst_w, int dst_h);
void MioPan_RendererDrawCapturedScreen(unsigned int addr,
                                       unsigned char r,
                                       unsigned char g,
                                       unsigned char b,
                                       unsigned char a);

void MioPan_RendererDrawTexturedQuad(const sceGsTex0 *tex0,
                                     const sceGsTex1 *tex1,
                                     const float *xy,
                                     const float *uv,
                                     unsigned char r,
                                     unsigned char g,
                                     unsigned char b,
                                     unsigned char a);
/* The same quad, honouring the GS CLAMP register the sprite carries.
 *
 * WMS/WMT 0 is REPEAT, and effect_scr.c's SubDither3/SubDither4 are the only
 * sprite draws in the tree that ask for it -- CopySprDToSpr2() defaults every
 * other one to CLAMP/CLAMP.  They tile the 128x128 noise sheet five across and
 * four down, so clamping returns the sheet's last column and row stretched over
 * four fifths of the screen instead of grain.
 *
 * The host sampler table has one address mode for both axes, so REPEAT is
 * selected only when WMS and WMT both ask for it.  Nothing in the tree mixes
 * them; a mixed pair falls back to clamping rather than guessing an axis.
 */
void MioPan_RendererDrawTexturedQuadClamp(const sceGsTex0 *tex0,
                                          const sceGsTex1 *tex1,
                                          const float *xy,
                                          const float *uv,
                                          uint64_t clamp,
                                          unsigned char r,
                                          unsigned char g,
                                          unsigned char b,
                                          unsigned char a);

/* The same quad with a depth.  For an effect particle billboard --
 * screen-space geometry standing in for a 3D primitive the GS depth-tested
 * through its sprite Z.  `view_depth` is the particle's clip w (its view-space
 * depth) and is what the depth is built from, with the projection the meshes
 * use; `ndc_z` is the engine's symmetric z/w (-1 near .. 1 far), the fallback
 * for a frame with no perspective camera installed. */
void MioPan_RendererDrawTexturedQuadDepth(const sceGsTex0 *tex0,
                                          const sceGsTex1 *tex1,
                                          const float *xy,
                                          const float *uv,
                                          float ndc_z,
                                          float view_depth,
                                          unsigned char r,
                                          unsigned char g,
                                          unsigned char b,
                                          unsigned char a);
void MioPan_RendererDrawSkyQuad(const sceGsTex0 *tex0,
                                const float *xy,
                                const float *uv,
                                unsigned char r,
                                unsigned char g,
                                unsigned char b,
                                unsigned char a);
void MioPan_RendererSetFontTexture(int bank, const sceGsTex0 *tex0);
void MioPan_RendererDrawFontQuad(const float *xy,
                                 const float *uv,
                                 unsigned char r,
                                 unsigned char g,
                                 unsigned char b,
                                 unsigned char a);
void MioPan_RendererDrawSolidQuad(const float *xy, const unsigned char *rgba);
void MioPan_RendererDrawSolidTriangles2D(const float *xy,
                                         const unsigned char *rgba,
                                         int vertex_count);
void MioPan_RendererDrawLine2D(const float *xy,
                               const unsigned char *rgba,
                               float width);
void MioPan_RendererDrawWorldLine(const float *positions,
                                  const unsigned char *rgba,
                                  float width,
                                  int depth_test);
void MioPan_RendererDrawWorldPoint(const float *position,
                                   const unsigned char *rgba,
                                   float size,
                                   int depth_test);
void MioPan_RendererDrawClipLine(const float *positions,
                                 const unsigned char *rgba,
                                 float width,
                                 int depth_test);
void MioPan_RendererDrawClipPoint(const float *position,
                                  const unsigned char *rgba,
                                  float size,
                                  int depth_test);
/* The four clip-space bridges above and below take clip coordinates whose w
 * is the view depth, and rebuild z from that w with the projection the meshes
 * use, so a billboard's depth test agrees with the surface behind it.  The z a
 * caller passes is only used before a perspective camera is installed, and is
 * then read as the engine's symmetric convention (near -w, far +w). */
void MioPan_RendererDrawClipTriangles(const sceGsTex0 *tex0,
                                      const float *positions,
                                      const float *uv,
                                      const float *rgba,
                                      int vertex_count,
                                      int depth_test);
/* Dedicated source tag for camera-facing/world-space effect quads.  Geometry
 * handling is identical to DrawClipTriangles; the separate entry point keeps
 * profiler attribution away from debug and utility triangle callers. */
/* `ndc_z_bias` is the GS-Z offset the ROM's DIRECT packet applies, converted
 * by the caller to the engine's symmetric NDC -- so NEGATIVE is nearer, which
 * is why RendererPacket3DUV() negates the ROM's offset. */
void MioPan_RendererDrawBillboardTriangles(const sceGsTex0 *tex0,
                                           const float *positions,
                                           const float *uv,
                                           const float *rgba,
                                           int vertex_count,
                                           int depth_test,
                                           float ndc_z_bias = 0.0f);
/* Host shadow of the GS TEST register's alpha-test fields (ATE / ATST / AREF /
 * AFAIL).  The GS held these as global state until something wrote the register
 * again, so this is a plain "last value wins" store; mesh draws snapshot it as
 * they are queued.  `test` is the raw 64-bit register value -- everything the
 * alpha test needs sits in the low 14 bits, which is why the ROM shim's 32-bit
 * `long` on this host still carries it. */
void MioPan_RendererSetGsTestRegister(unsigned long long test);

/* The rest of the GS draw environment, on the same "last value wins" model as
 * TEST above -- draw_env.c writes them, draws snapshot them as they are queued.
 *
 *   ALPHA   the blend equation (A-B)*C>>7 + D.  Classified into the handful of
 *           shapes SDL_GPU can express; anything else falls back to the
 *           ordinary source-alpha blend rather than dropping the draw.
 *   ZBUF    only ZMSK is used, to stop a transparent pass writing depth.
 *   SCISSOR the clip box, in the PS2's 640x448 framebuffer pixels. */
void MioPan_RendererSetGsAlphaRegister(unsigned long long alpha);
void MioPan_RendererSetGsZbufRegister(unsigned long long zbuf);
void MioPan_RendererSetGsScissorRegister(unsigned long long scissor);

void MioPan_RendererSet3DViewProjection(const float *view,
                                        const float *projection);

/* ---- Camera reprojection (frame-rate decoupling) -------------------------
 *
 * The game simulates at a fixed 30 Hz and has no delta time anywhere, so extra
 * presented frames cannot come from ticking it faster.  They come from
 * re-aiming the frame that has already been queued: every 3D draw stores its
 * own model matrix beside the MVP it was given, and vertices are model-space
 * with the transform in a uniform, so a second camera costs one matrix product
 * per draw and no vertex work.
 *
 * All three take/return row-major 4x4s in the renderer's row-vector convention
 * (p' = p * M), the same shape MioPan_RendererSet3DViewProjection() wants. */

/* Non-zero once two consecutive logical frames have set a 3D camera, i.e. once
 * there is something to interpolate between. */
int MioPan_RendererHavePreviousCamera(void);

/* Blend the previous logical frame's camera toward the current one, t in
 * [0,1] (0 = previous, 1 = current).  The orientation is slerped and the eye
 * position lerped, so a turning camera does not shear.  Returns non-zero and
 * fills `view` / `projection` on success. */
int MioPan_RendererBlendCameraFromPrevious(float t, float *view,
                                           float *projection);

/* Re-aim every 3D draw already queued this frame at a different camera, and
 * install it as the live one.  Screen-space draws and shadow casters are left
 * alone -- the first have no model matrix, the second belong to the shadow map.
 * Returns the number of draws rewritten. */
int MioPan_RendererReprojectDraws(const float *view, const float *projection);

/* Present the current frame's draw list again, re-recorded through whatever
 * camera is installed now.  MioPan_RendererEndFrame() has to have presented the
 * frame first -- this replays the buffers that frame's upload stage filled and
 * refills nothing -- so the sequence for an in-between frame is EndFrame(),
 * then blend, reproject and repeat.  The shadow map, the GS block captures and
 * the pause/photo stills are the real frame's and are reused, not retaken.
 * Returns non-zero if a frame reached the swapchain. */
int MioPan_RendererRepeatPresent(void);

/* ---- Frame smoothing ------------------------------------------------------
 *
 * Extra presents of every logical frame, 0..3.  The game keeps simulating at
 * its own rate -- it has no delta time and cannot be ticked faster -- so these
 * are the same frame put on screen again, each through a camera interpolated
 * from the previous logical frame's toward this one's.  The in-betweens are
 * presented first and the frame's own camera last, one CRTC field apart; the
 * fields come out of the wait vfunc() was going to do anyway, so the tick
 * still takes exactly as long as the game asked for.
 *
 * With interpolation off the extras are identity copies of the real present
 * instead.  That is the check that the renderer survives a second record pass
 * at all -- a separate question from what camera an in-between should use --
 * and every copy must come out pixel-identical.
 *
 * The presents per logical frame actually achieved show up in the profiler
 * summary beside the two FPS figures. */
void MioPan_RendererSetExtraPresents(int extra);
int MioPan_RendererGetExtraPresents(void);

/* The most extra presents that actually fit, 0..3.  One less than the fields
 * the game is waiting per tick, because vfunc() always waits for the last one
 * unconditionally -- so 1 while the game paces itself normally, and 0 if the
 * pacing harness has taken it down to a single field.  A larger setting is
 * clamped to this: presenting past the budget lengthens the tick instead of
 * raising the frame rate, which is the simulation slowing down. */
int MioPan_RendererGetMaxExtraPresents(void);
void MioPan_RendererSetPresentInterpolation(int enable);
int MioPan_RendererGetPresentInterpolation(void);

/* World units the eye moved between the last two logical frames' cameras, or
 * negative when smoothing did not have a camera pair to interpolate.  Zero is
 * the expected reading in a fixed-angle room: a camera that did not move makes
 * every in-between identical to the real frame, so smoothing is working and
 * has nothing to show.  Reported beside the FPS figures. */
float MioPan_RendererGetCameraMotion(void);

/* The second half of an in-between: move the world as well as the eye.
 *
 * A reprojected camera does not move anything the camera is looking at, so
 * with it smoothed the remaining 30 Hz cue is character and object motion.
 * This blends that too, in the two places the engine keeps it -- the per-draw
 * transform of a rigidly bound block, and the streamed vertices of a block the
 * CPU has already skinned into world space.
 *
 * It costs a vertex-buffer upload per present, where camera-only smoothing
 * costs none, so `Renderer upload` in the profiler follows `present/frame`
 * with this on.  It rides on Interpolate camera and needs a previous frame
 * whose draw list still corresponds to this one's; where it does not, those
 * draws fall back to camera-only smoothing rather than to anything wrong. */
void MioPan_RendererSetGeometryInterpolation(int enable);
int MioPan_RendererGetGeometryInterpolation(void);

/* Share of this frame's 3D draws whose geometry is being blended, 0..1, or
 * negative when geometry smoothing had nothing to work with.  The companion to
 * MioPan_RendererGetCameraMotion(): "--" means off or no corresponding
 * previous frame, a low figure means the draw list keeps changing shape, and a
 * high one means the world is interpolating.  Reported beside the FPS
 * figures. */
float MioPan_RendererGetGeometryCoverage(void);

/* GS fogging, standing in for the VU1 fog block gra3dApplyFog() uploads and
 * the FOGCOL register gra3dSetFogColor() writes.  Also "last value wins":
 * MapFog.c and scene.c push a new setting whenever the ramp moves, and every
 * transformed 3D draw queued afterwards is fogged with it.
 *
 *   enable   PRIM's FOGE bit, i.e. gra3dIsFogEnable().
 *   f_min    density floor, reached at f_far          } raw GS fog units,
 *   f_max    density ceiling, reached at f_near       } 0..255, not 0..1
 *   fa, fb   the ramp coefficients, F = FA + FB / viewZ, in those same units
 *            (g3dCalcFA / g3dCalcFB)
 *   r, g, b  FOGCOL, 0..255 -- a direct framebuffer value, so it is scaled by
 *            255 here and not by the 128 a modulate factor would use. */
void MioPan_RendererSetFog(int enable,
                           float f_min,
                           float f_max,
                           float fa,
                           float fb,
                           int r,
                           int g,
                           int b);

typedef struct MioPanLightState MioPanLightState;

/* Direct object-space mesh stream.  The SGD decoder opens one transaction,
 * appends complete strip triangles, then commits a single draw command.  This
 * avoids the old position/UV/colour vectors and the second SpriteVertex copy.
 * Only one stream may be open at a time; a zero token means Begin failed. */
typedef struct MioPanMeshVertexInput
{
    const float *position;
    const float *normal;
    const float *rgb;
    float s;
    float t;
    float alpha;
} MioPanMeshVertexInput;

unsigned int MioPan_RendererBeginMeshStream(const sceGsTex0 *tex0,
                                             int expected_vertex_count,
                                             const float *local_world,
                                             /* Non-NULL selects the per-pixel
                                              * light path for this stream; its
                                              * config[2] says which terms. */
                                             const MioPanLightState *fragment_lights,
                                             /* A handle from
                                              * MioPan_RendererResolveTexture();
                                              * when non-NULL it is drawn
                                              * instead of resolving tex0. */
                                             const void *texture = nullptr);

/* ---- Resolved textures -----------------------------------------------------
 *
 * Resolve a TEX0 through the GS texture caches now, and return a handle to the
 * host texture it names that stays valid for the rest of the session.  NULL
 * when it cannot be resolved, or when it names something that is not a stable
 * texture (a movie picture is replaced every frame).
 *
 * This is what lets a model keep drawing its textures after it has stopped
 * sending them to the GS: resolve once while its upload is still in GS memory,
 * then hand the handle to the draw calls.  The texture itself is the one the
 * ordinary lookup returns -- the same decode, the same cache entry. */
const void *MioPan_RendererResolveTexture(const sceGsTex0 *tex0);

/* Textures resolved once per model, and the model's GS upload skipped after
 * that.  Off sends every model's textures to the GS every frame the way the
 * port always has -- the A/B.  Live. */
void MioPan_RendererSetResidentTextures(int enable);
int  MioPan_RendererGetResidentTextures(void);

/* For the profiler summary: the SGD bridge reports each model whose textures
 * it resolved, each TRI2 it then did not send to the GS, and each skipped run
 * it had to send after all because a draw sampled GS memory. */
void MioPan_RendererCountTextureCapture(void);
void MioPan_RendererCountSkippedTextureUpload(void);
void MioPan_RendererCountTextureReplay(void);
void MioPan_RendererAppendMeshTriangle(
    unsigned int stream,
    const MioPanMeshVertexInput vertices[3]);
void MioPan_RendererCommitMeshStream(unsigned int stream);
void MioPan_RendererAbortMeshStream(unsigned int stream);

/* Compatibility path for callers that already own an expanded triangle list.
 * It is implemented through the same direct stream and no longer constructs a
 * temporary SpriteVertex vector. */
void MioPan_RendererDrawMeshTriangles(const sceGsTex0 *tex0,
                                      const float *positions,
                                      const float *uv,
                                      const float *rgba,
                                      int vertex_count,
                                      const float *local_world);

/* ---- Resident meshes -------------------------------------------------------
 *
 * Object-space geometry uploaded to the GPU once and then drawn by reference.
 * Everything a draw decides is still decided per draw, by the same caller and
 * in the same order as a streamed mesh -- culling, TEX0, the GS draw
 * environment, the light image, the local->world matrix.  What stops happening
 * every frame is decoding the packet, expanding its strips into triangles and
 * uploading them again.
 *
 * Vertices keep the order the caller gives them and indices are relative to
 * the start of the mesh, so one mesh can hold a whole model and each draw names
 * the slice it wants.  Only colour still travels per draw, because it carries
 * the realtime lighting and that moves every frame.
 *
 * A mesh is immutable.  Release drops the caller's reference; draws already
 * queued hold their own, and SDL defers the buffers themselves past any
 * submission still reading them, so a mesh may be released at any point in a
 * frame -- including between two draws of it.
 */
typedef struct MioPanResidentVertex
{
    float uv[4];        /* s, t, 0, 0                                     */
    float position[4];  /* object space, w = 1                            */
    float normal[4];    /* object space, w = 0; read only when fragment-lit */
} MioPanResidentVertex;

/* Upload a mesh.  Every index must be below vertex_count and index_count a
 * multiple of three; both are checked here, once, rather than per draw.
 * Returns a non-zero handle, or 0 if the GPU buffers could not be made -- the
 * caller then keeps streaming, which is what it did before. */
unsigned int MioPan_RendererCreateResidentMesh(
    const MioPanResidentVertex *vertices,
    unsigned int vertex_count,
    const unsigned int *indices,
    unsigned int index_count);
void MioPan_RendererReleaseResidentMesh(unsigned int mesh);

/* Queue indices [first_index, first_index + index_count) of `mesh`.
 * [first_vertex, first_vertex + vertex_count) must be the span those indices
 * reference, and `rgba` holds one colour for each vertex of it, on the same
 * scale MioPanMeshVertexInput takes (RGB 0..2 overbright, alpha 0..1).  The
 * rest is MioPan_RendererBeginMeshStream()'s contract.
 *
 * A draw that carries on from the one queued immediately before it -- the same
 * mesh, the next span, identical state -- is folded into that draw instead of
 * becoming another.  That is what brings a room's few hundred walker units down
 * to one draw per run of shared state.
 *
 * Returns non-zero when handled, 0 when the caller should stream instead. */
int MioPan_RendererDrawResidentMesh(unsigned int mesh,
                                    unsigned int first_vertex,
                                    unsigned int vertex_count,
                                    unsigned int first_index,
                                    unsigned int index_count,
                                    const float *rgba,
                                    const sceGsTex0 *tex0,
                                    const float *local_world,
                                    const MioPanLightState *fragment_lights,
                                    /* As for MioPan_RendererBeginMeshStream(). */
                                    const void *texture = nullptr);

/* Resident meshes on or off.  Off streams every mesh the way the port always
 * has, which is the A/B for anything that looks different.  Live: a mesh
 * already resident simply goes unused until it is turned back on. */
void MioPan_RendererSetResidentMeshes(int enable);
int  MioPan_RendererGetResidentMeshes(void);

/* Logical frames begun since startup, for callers that age their own caches.
 * Advances once per MioPan_RendererBeginFrame(). */
unsigned long long MioPan_RendererGetFrameIndex(void);

/* Projected shadows -- the host side of gra3dShadow.c, whose projection pass
 * is VU1 microcode (SgSuShadow_dma_main) with no host interpreter.  The shadow
 * code keeps drawing through _gra3dDrawSGD(); these brackets say which of its
 * two passes the resulting geometry belongs to, so the caster can be captured
 * into a shadow map and the receivers replayed through a projective shader.
 *
 * BeginCaster/EndCaster wrap _RenderShadow()'s draw; EndCaster snapshots the
 * light camera, so it must be called while that camera is still applied.
 * BeginReceiver/EndReceiver wrap each SRT_MAPSHADOW block.  Reset() matches
 * gra3dshadowClearProjectModel() at the top of the frame. */
void MioPan_RendererShadowBeginCaster(void);
void MioPan_RendererShadowEndCaster(void);
void MioPan_RendererShadowBeginReceiver(void);
void MioPan_RendererShadowEndReceiver(void);
void MioPan_RendererShadowSetStrength(float strength);
void MioPan_RendererShadowReset(void);
/* The host half of gra3dshadowDrawSGD()'s camOrigin save and _DrawShadow()'s
 * _gra3dSetCameraForce() restore, which only assigns the ROM's camera struct
 * and so never reaches the renderer. */
void MioPan_RendererGetShadowStats(int *episodes, int *casters,
                                   int *receivers, int *valid);
void MioPan_RendererShadowSaveCamera(void);
void MioPan_RendererShadowRestoreCamera(void);

/* Persistent object-space geometry cache.  Preset SGD meshes keep immutable
 * position/UV data and strip indices on the GPU.  Animated meshes keep only
 * immutable UVs and indices; one unique position/normal pair is streamed per
 * strip vertex on each draw, and its PS2 colour is evaluated in the vertex
 * shader.
 *
 * DORMANT: nothing has called HasIndexedMesh/DrawIndexedMesh since d4bafb4.
 * Preset meshes are kept on the GPU by the resident mesh API above instead. */
enum
{
    MIOPAN_MESH_CACHE_PRESET = 1,
    MIOPAN_MESH_CACHE_ANIMATED = 2,
    MIOPAN_MESH_CACHE_DEFER = -2,
    MIOPAN_MESH_DRAW_REBUILD = -1
};

/* Snapshot of the PS2 VU1 light image.  One block serves both stages: the
 * vertex shader runs it once per streamed animated vertex, the fragment shader
 * once per pixel, and both go through the same kernel body in
 * miopan_vu1_lighting.hlsli.  Field-for-field mirror of GRA3DVU1LIGHTSNAPSHOT
 * (gra3dTypes.h), which is where the derivation and the VU memory addresses are
 * documented; every array is a float4/int4 so the C layout is identical to the
 * HLSL constant buffer.
 *
 * Colours are GS 0..255 units with gra3dCalcVu1MaterialData*()'s scales
 * already folded in, and monotone mode is baked in there too -- the shader
 * must not re-apply either. */
/* How many positional lanes of each type the block carries.
 *
 * The VU1 had three per type, so gra3d ranks its bank by power at the bounding
 * box and drops the rest (_SelectLightByType, gra3dSGD.c).  That ranking runs
 * per bounding box, so which three survive changes as objects move and lights
 * visibly pop in and out.  The host has no register file to respect, so it
 * carries the room's whole selected set and the popping goes away.
 *
 * 16 is chosen against MapLightSelect()'s own MAP_LIGHT_SELECT_MAX of 14 (the
 * earlier, per-room thinning that DOES survive here), not against gra3d's raw
 * bank of 19 point / 17 spot -- so it covers every case a room can present with
 * headroom to spare.  Keep in step with MIOPAN_VU1_MAX_LANES in
 * resources/shaders/hlsl/miopan_vu1_lighting.hlsli: the two structs must agree
 * exactly or the cbuffer is misread. */
enum
{
    MIOPAN_VU1_MAX_LANES = 16
};

struct MioPanLightState
{
    /* x = light-type enables, bit 0 spot / bit 1 point.  The microcode's own:
     *     a disabled TYPE means the kernel never ran and VU1 memory kept the
     *     previous draw's contents, so a lane must not be evaluated "anyway".
     * y = flags, bit 0 "lighting enabled".
     * z = MIOPAN_VU1_TERM_* mask the fragment stage is responsible for.  The
     *     renderer owns this one; gra3dSnapshotVu1Lighting() leaves it zero,
     *     and the vertex stage ignores it. */
    int config[4];
    /* x = live spot lanes, y = live point lanes; both <= MIOPAN_VU1_MAX_LANES.
     * The shader loops to these rather than to the array bound, so a room with
     * two spotlights costs two lanes and not sixteen. */
    int counts[4];
    /* GLOBALAMBIENT: xyz ambient term, w = 255 (the microcode's final clamp). */
    float ambient[4];

    /* Not widened: gra3d's bank has exactly three directional
     * (GRA3D_NUM_LIGHT_DIRECTIONAL) and SelectLight() does not rank them. */
    float directional_diffuse_dir[3][4];
    float directional_specular_dir[3][4];   /* per-frame half-vector */
    float directional_diffuse[3][4];
    float directional_specular[3][4];

    float spot_position[MIOPAN_VU1_MAX_LANES][4];
    float spot_direction[MIOPAN_VU1_MAX_LANES][4];
    float spot_diffuse[MIOPAN_VU1_MAX_LANES][4];
    float spot_specular[MIOPAN_VU1_MAX_LANES][4];
    /* x = bTimes (fMaxRange * SetMaxColor255's divisor),
     * y = cos^2(cone half-angle), z = 1 / sin^2, w unused. */
    float spot_params[MIOPAN_VU1_MAX_LANES][4];

    float point_position[MIOPAN_VU1_MAX_LANES][4];
    float point_diffuse[MIOPAN_VU1_MAX_LANES][4];
    float point_specular[MIOPAN_VU1_MAX_LANES][4];
    /* x = bTimes, yzw unused. */
    float point_params[MIOPAN_VU1_MAX_LANES][4];
};

/* Returns 1 for a ready or CPU-pending entry, 0 when this frame may build a
 * cold entry, or MIOPAN_MESH_CACHE_DEFER when the cold-build budget is spent
 * and the caller should use its streamed path without decoding cache data. */
int MioPan_RendererHasIndexedMesh(const void *owner,
                                  const void *vuvn,
                                  const void *mesh,
                                  unsigned int layout_kind,
                                  int estimated_vertex_count);
/* Returns 1 when queued/handled, 0 when the caller should use its streamed
 * fallback, or MIOPAN_MESH_DRAW_REBUILD when a reported hit became stale and
 * the caller must supply fresh static geometry. */
int MioPan_RendererDrawIndexedMesh(const void *owner,
                                   const void *vuvn,
                                   const void *mesh,
                                   unsigned int layout_kind,
                                   const sceGsTex0 *tex0,
                                   const float *positions,
                                   const float *uv,
                                   /* XYZ3; required every ANIMATED draw and
                                    * for fragment-lit PRESET cache creation. */
                                   const float *normals,
                                   /* RGBA4 for PRESET; unused for ANIMATED. */
                                   const float *colors,
                                   int vertex_count,
                                   const unsigned int *indices,
                                   int index_count,
                                   const float *local_world,
                                   const MioPanLightState *vertex_lights,
                                   const MioPanLightState *fragment_lights);
void MioPan_RendererInvalidateMeshCache(const void *owner);

#ifdef __cplusplus
}
#endif

#endif
