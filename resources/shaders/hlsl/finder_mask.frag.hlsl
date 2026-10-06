#include "mikupan_common.hlsli"

/*
 * The viewfinder surround: everything outside the camera's framing rectangle,
 * defocused and darkened.
 *
 * A port addition, not something the ROM did.  The PS2 draws six opaque
 * "kakiwari" plates around the aperture (CFinderBase::DrawKakiwari) and leaves
 * the world behind them exactly as it was rendered; there was no spare fill
 * rate on that hardware to defocus a full screen.  This pass sits underneath
 * those plates -- MioPan_RendererDrawFinderMask() is queued before the finder
 * HUD -- so the frame art, the gauges and the reticle all stay sharp on top of
 * a soft surround.
 *
 * MikuPan does the same thing for Fatal Frame 1, in
 * resources/shaders/hlsl/finder_viewport_blur.frag.hlsl, and the kernel here is
 * its 13x13 Gaussian.  Two things are deliberately not carried over: that
 * shader's violet lens tint and its radial vignette, both of which would fight
 * the aperture this one is given.  MikuPan masks with four hard-edged bands and
 * lets the vignette soften them; here the aperture is a real rectangle measured
 * off the kakiwari plates, so the softening belongs on that rectangle's own
 * edge instead.
 *
 * Two coordinate spaces meet here and keeping them apart is the whole trick:
 *
 *   vUV     the capture texture, 0..1 across the whole render target.  This is
 *           what we sample; the target may be wider than the game's own frame
 *           when the window is, and this spans all of it.
 *   vDstUV  the ROM's own 640x448 frame coordinate at this pixel, carried per
 *           vertex by MioPan_RendererDrawFinderMask().  The aperture is
 *           expressed in that space because that is where the game's numbers
 *           live -- the kakiwari plates and plyr_wrk.fp -- so the mask lands on
 *           the same pixels the HUD art does, at any window aspect.
 *
 * Blending is fixed SRC_ALPHA / ONE_MINUS_SRC_ALPHA in the pipeline, so alpha
 * is the mask: 0 inside the aperture leaves the frame untouched, and there is
 * no need to discard or scissor.
 */

struct PSInput
{
    float4 position : SV_Position;
    float2 vUV : TEXCOORD0;
    float2 vDstUV : TEXCOORD1;
    float4 uColor : TEXCOORD2;
    // Unread.  Declared so this stage's inputs match sprite.vert's outputs
    // element for element on every backend.
    float4 fogW : TEXCOORD3;
};

/*
 * The Gaussian's half-window, in taps.  6 gives MikuPan's 13x13 grid.
 *
 * Sigma is half the window, so the kernel is truncated at 2 sigma and the
 * outermost tap still carries exp(-2) ~ 0.135 of the centre's weight -- enough
 * that the truncation does not show as a ring, little enough that the shape is
 * still recognisably a Gaussian rather than the near-box a sigma equal to the
 * whole window would give.
 */
#define GAUSS_TAPS 6
static const float kGaussSigma = 0.5;   /* in normalised window units */

float4 main(PSInput input) : SV_Target0
{
    float fade = saturate(input.uColor.a);

    /* uFinderMask   xy = aperture centre, zw = half size, in frame units.
     * uFinderMask2  x  = feather width in frame units, y = darkening,
     *               zw = the Gaussian's half-window as a fraction of the
     *                    target's width and height (the renderer does that
     *                    conversion, because only it knows how wide the view
     *                    was extended). */
    float2 centre = uFinderMask.xy;
    float2 half_size = uFinderMask.zw;
    float feather = max(uFinderMask2.x, 1.0);
    float darken = saturate(uFinderMask2.y);
    float2 blur_radius = uFinderMask2.zw;

    /*
     * Signed distance to the aperture rectangle, positive outside.  The usual
     * box SDF: the max() term measures how far past a face we are, and the
     * min() term is negative inside and gives the corners their curvature for
     * free, which is what stops the feather showing a crease along the
     * diagonals.
     */
    float2 d = abs(input.vDstUV - centre) - half_size;
    float outside = length(max(d, 0.0.xx)) + min(max(d.x, d.y), 0.0);

    /* One knob drives both ramps, so the softness of the edge and the onset of
     * the defocus always agree.  Squared on the blur so the picture stays
     * readable right at the frame line and falls away faster past it -- a
     * linear ramp reads as a smear rather than as depth of field.  This is the
     * one place the pass departs from MikuPan's, which switches to full blur
     * at a hard band edge. */
    float ramp = saturate(outside / feather);
    float mask = ramp;
    float focus = ramp * ramp;

    float alpha = mask * fade;
    if (alpha <= (1.0 / 255.0))
    {
        discard;
    }

    float2 radius = blur_radius * focus;
    float3 colour = uTexture.Sample(uTextureSampler, input.vUV).rgb;

    /* Below about a texel there is nothing to gather, and skipping the loop
     * there keeps the whole kernel off the band nearest the aperture -- which
     * is the widest part of the ramp on screen. */
    if (radius.x * uRenderSize.x > 0.75 || radius.y * uRenderSize.y > 0.75)
    {
        float3 gathered = 0.0.xxx;
        float total_weight = 0.0;

        for (int y = -GAUSS_TAPS; y <= GAUSS_TAPS; y++)
        {
            for (int x = -GAUSS_TAPS; x <= GAUSS_TAPS; x++)
            {
                /* -1..1 across the window, so the kernel is defined
                 * independently of how wide the window turned out to be. */
                float2 offset = float2((float)x, (float)y) /
                                (float)GAUSS_TAPS;
                float dist2 = dot(offset, offset);

                /*
                 * Truncate to a disc rather than to the square the grid
                 * describes.  This is not free -- the corners hold 8.3% of the
                 * square window's weight, so the normalised kernel differs
                 * from a square-truncated one by about 16% -- but it is the
                 * truncation an isotropic kernel wants: a square window
                 * reaches 2.83 sigma along the diagonals and 2.0 along the
                 * axes, which is a blur that is quietly wider corner to
                 * corner than it is edge to edge.
                 *
                 * 113 taps instead of 169, for a rounder kernel.  MikuPan
                 * keeps all 169 and the square.
                 *
                 * The test survives into the output as a real branch -- the
                 * loop is left rolled deliberately, so it is not folded away
                 * at compile time -- but it depends only on the loop counters,
                 * so it is uniform across the wavefront and skips 56 texture
                 * fetches for one predictable branch.  MikuPan's [unroll] on
                 * the same loop is what makes its bytecode 68 KB against this
                 * one's 8; rolled is the friendlier shape for a pass that
                 * covers most of the screen.
                 */
                if (dist2 > 1.0)
                {
                    continue;
                }

                /* exp(-d^2 / 2*sigma^2).  Isotropic, so this is the separable
                 * kernel's product form written as one exponential. */
                float weight = exp(-dist2 /
                                   (2.0 * kGaussSigma * kGaussSigma));

                /* Clamped rather than wrapped: a tap that walks off the top of
                 * the frame must not fetch the bottom of it. */
                float2 uv = clamp(input.vUV + offset * radius,
                                  0.0.xx, 1.0.xx);
                gathered += uTexture.Sample(uTextureSampler, uv).rgb * weight;
                total_weight += weight;
            }
        }

        colour = gathered / max(total_weight, 0.000001);
    }

    /* Darkening and the crimson cast are applied after the gather, so a bright
     * highlight still spreads its light into the surround before either takes
     * any of it away -- dimming first would flatten the blur into a uniform
     * grey, and tinting first would let the blur average the cast back out.
     *
     * uColor.rgb is the tint, already faded in by strength on the CPU: white
     * at strength 0, so this multiply is then the identity.  It only ever
     * scales channels down, which is why no clamp is needed here. */
    colour *= 1.0 - darken;
    colour *= input.uColor.rgb;

    return float4(colour, alpha);
}
