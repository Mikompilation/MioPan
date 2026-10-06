/*
 * The present pass: the port's one and only output transform.
 *
 * Everything upstream of this shader -- the whole GS emulation, every blend
 * equation, the game's own brightness filters -- works in the sRGB-encoded
 * 0..1 space a PS2 framebuffer held, and it stays that way.  This is the last
 * stop, where that finished SDR picture is graded and then written in whatever
 * encoding the swapchain is actually running:
 *
 *   OUTPUT_SDR    unchanged 0..1 sRGB, what the port always did.
 *   OUTPUT_SCRGB  extended-linear sRGB (SDL_GPU_SWAPCHAINCOMPOSITION_
 *                 HDR_EXTENDED_LINEAR).  1.0 is 80 nits by definition, so the
 *                 whole picture is scaled by paperWhite/80.
 *   OUTPUT_PQ     BT.2020 ST.2084 (SDL_GPU_SWAPCHAINCOMPOSITION_HDR10_ST2084).
 *                 Primaries converted, then the PQ inverse-EOTF, whose 1.0 is
 *                 10000 nits.
 *
 * The source is SDR, so there is no real high dynamic range to recover -- what
 * HDR buys here is (a) the picture is displayed at a chosen absolute
 * brightness instead of whatever the panel's SDR mode does with it, and (b)
 * the top end can be lifted into the display's headroom so highlights -- the
 * camera flash, a lantern, fire -- read as light rather than as paper.  That
 * lift is ExpandHighlights() and it is a knob, not a policy: at strength 0 the
 * output is the SDR image shown at paperWhite nits and nothing else, which is
 * the faithful setting.
 */

cbuffer PresentUniforms : register(b0, space3)
{
    /* x = brightness, y = contrast, z = gamma, w = saturation.  All 1.0 is
     * identity, and the renderer skips this pass entirely when they are. */
    float4 uGrade;

    /* x = paper-white scale in the output encoding: paperNits/80 for scRGB,
     *     paperNits/10000 for PQ, 1 for SDR.
     * y = peak/paper ratio, i.e. how far above paper white the display can go.
     * z = highlight expansion strength, 0..1.
     * w = the luminance the expansion starts from, in paper-white units. */
    float4 uHdr;

    /* x = output mode, y = dither amplitude in output code values,
     * z = expansion shoulder exponent, w = spare. */
    float4 uOutput;

    /* xy = target size in pixels, zw = its reciprocal. */
    float4 uTarget;
};

Texture2D uTexture : register(t0, space2);
SamplerState uTextureSampler : register(s0, space2);

struct PSInput
{
    float4 position : SV_Position;
    float2 vUV : TEXCOORD0;
};

#define OUTPUT_SDR   0.0
#define OUTPUT_SCRGB 1.0
#define OUTPUT_PQ    2.0

/* Rec.709 luminance, the weighting every one of these transforms is defined
 * against.  Deliberately not the PS2 weighting used by ToBlackWhite(): that
 * one reproduces a specific in-game effect, this one is display maths. */
static const float3 kLumaBt709 = float3(0.2126, 0.7152, 0.0722);

/* BT.709 -> BT.2020, normalised so a Rec.709 white stays white. */
static const float3x3 kBt709ToBt2020 = float3x3(
    0.6274040, 0.3292820, 0.0433136,
    0.0690970, 0.9195400, 0.0113612,
    0.0163916, 0.0880132, 0.8955950);

/*
 * The sRGB transfer function, piecewise rather than a plain 2.2.  It matters
 * at the bottom: this game is almost entirely near-black, and the linear toe
 * is exactly where pow(2.2) and sRGB disagree most.
 *
 * step()/lerp() rather than a ternary because the condition is per-component,
 * which HLSL 2021 will not accept as a conditional operator.
 */
float3 SrgbToLinear(float3 c)
{
    float3 lo = c / 12.92;
    float3 hi = pow(max(c + 0.055, 0.0) / 1.055, 2.4);
    return lerp(lo, hi, step(float3(0.04045, 0.04045, 0.04045), c));
}

/* ST.2084 (PQ) inverse EOTF.  L is absolute luminance normalised so 1.0 is
 * 10000 cd/m2, which is what the encoding's own constants assume. */
float3 PqEncode(float3 L)
{
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;

    float3 Lm = pow(max(L, 0.0), m1);
    return pow((c1 + c2 * Lm) / (1.0 + c3 * Lm), m2);
}

/*
 * The grade, applied in the display-encoded domain rather than in linear.
 *
 * That is on purpose: these are the knobs a monitor's own OSD offers, and a
 * player reaching for "brightness" expects the OSD's behaviour -- a lift of
 * the whole curve -- not a linear exposure change that leaves the shadows
 * looking untouched.  The order is the OSD's too: brightness, contrast,
 * saturation, gamma.
 */
float3 Grade(float3 c)
{
    c *= uGrade.x;
    c = (c - 0.5.xxx) * uGrade.y + 0.5.xxx;

    float luma = dot(max(c, 0.0.xxx), kLumaBt709);
    c = lerp(luma.xxx, c, uGrade.w);

    c = max(c, 0.0.xxx);
    return pow(c, 1.0 / max(uGrade.z, 0.01));
}

/*
 * Lift the top of the SDR range into the display's headroom.
 *
 * The gain is computed from the channel maximum and applied to all three, so
 * the hue and saturation of a highlight are untouched and only its brightness
 * moves -- the alternative, expanding per channel, walks bright colours toward
 * white.  Below the knee the gain is exactly 1, so midtones and the darks that
 * make up most of this game are the SDR image unchanged.
 */
float3 ExpandHighlights(float3 lin)
{
    float strength = saturate(uHdr.z);
    float peak = max(uHdr.y, 1.0);
    if (strength <= 0.001 || peak <= 1.001)
    {
        return lin;
    }

    float knee = clamp(uHdr.w, 0.0, 0.99);
    float m = max(lin.r, max(lin.g, lin.b));
    if (m <= knee)
    {
        return lin;
    }

    float t = saturate((m - knee) / max(1.0 - knee, 0.01));
    float top = 1.0 + (peak - 1.0) * strength;
    return lin * lerp(1.0, top, pow(t, max(uOutput.z, 0.01)));
}

/*
 * Triangular-PDF dither, one code value peak to peak.
 *
 * Cheap insurance rather than decoration: the grade above is evaluated in
 * float and then quantised by the swapchain, so a gamma or brightness change
 * that stretches the darks turns a smooth wall into contour rings.  Two
 * independent hashes make the noise triangular, which decorrelates it from the
 * signal in a way a single uniform hash does not.
 */
float3 Dither(float2 pixel)
{
    float2 a = pixel + float2(0.06711056, 0.00583715);
    float2 b = pixel + float2(0.51264503, 0.31415926);
    float n0 = frac(sin(dot(a, float2(127.1, 311.7))) * 43758.5453123);
    float n1 = frac(sin(dot(b, float2(269.5, 183.3))) * 43758.5453123);
    return ((n0 + n1) - 1.0).xxx * uOutput.y;
}

float4 main(PSInput input) : SV_Target0
{
    float3 graded = Grade(uTexture.Sample(uTextureSampler, input.vUV).rgb);
    float mode = uOutput.x;

    if (mode < OUTPUT_SCRGB)
    {
        /* SDR: the picture is already in the encoding the swapchain wants, so
         * the grade is the whole pass and the dither goes on directly. */
        return float4(saturate(graded + Dither(input.position.xy)), 1.0);
    }

    /* Clamped before linearising: anything the grade pushed past white is a
     * clipped SDR value, not headroom the source ever had.  Letting it through
     * would hand ExpandHighlights() a number it would then multiply again. */
    float3 lin = SrgbToLinear(saturate(graded));
    lin = ExpandHighlights(lin);
    lin = min(lin, max(uHdr.y, 1.0).xxx);

    if (mode < OUTPUT_PQ)
    {
        /* scRGB is linear and shares sRGB's primaries, so paper white is the
         * only conversion left.  No dither: the target is 16-bit float, whose
         * quantisation near black is far finer than any noise worth adding. */
        return float4(lin * uHdr.x, 1.0);
    }

    float3 rec2020 = max(mul(kBt709ToBt2020, lin), 0.0.xxx);
    float3 pq = PqEncode(rec2020 * uHdr.x);
    return float4(saturate(pq + Dither(input.position.xy)), 1.0);
}
