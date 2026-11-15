#define MIKUPAN_CUSTOM_LIGHT_BLOCK
// The flashlight's soft beam is a per-pixel feature; see MioPanVu1SoftCone().
#define MIOPAN_VU1_SOFT_FLASHLIGHT 1
#include "mikupan_common.hlsli"
#include "miopan_vu1_lighting.hlsli"

// The VU1's light image evaluated per FRAGMENT instead of per vertex.  Same
// law and the same data as mesh_animated_lit.vert.hlsl -- the kernel is shared
// through miopan_vu1_lighting.hlsli, so only the sampling rate differs.
//
// Which of the three kernels this stage runs is config.z, and the CPU has
// already folded the rest into the interpolated vertex colour.  The two live
// splits are:
//
//   MIOPAN_LIGHTING_FRAGMENT      SPOT here, ambient/directional/point on the
//                                 CPU -- the original per-pixel spot mode.
//   MIOPAN_LIGHTING_FRAGMENT_ALL  all three here, nothing on the CPU; the
//                                 vertex colour is then the raw prelight bake
//                                 (or white for a mesh that has none).
//
// Either way the seed arrives in GS MODULATE units and is taken back to the
// 0..255 the kernel works in, so the sum and its closing clamp happen exactly
// once, in the microcode's own scale.
cbuffer MioPanFragmentLightBlock : register(b1, MIKUPAN_UNIFORM_SPACE)
{
    MioPanVu1LightImage uVu1;
};

// Mio's flashlight bounce (port addition; MioPan_RendererSetFlashlightBounce).
// Where the beam lands, gathered on the CPU into a few small area lights --
// eight per bounce, up to four bounces.  Pushed once per pass, not per draw.
// Mirrors BounceUniforms in miopan_renderer.cpp.
#define MIOPAN_BOUNCE_LIGHTS 32
cbuffer MioPanBounceBlock : register(b3, MIKUPAN_UNIFORM_SPACE)
{
    float4 uBounceInfo;                          // x = live lights
    float4 uBouncePos[MIOPAN_BOUNCE_LIGHTS];     // xyz, w = patch area
    float4 uBounceNormal[MIOPAN_BOUNCE_LIGHTS];  // xyz = patch normal
    float4 uBounceColour[MIOPAN_BOUNCE_LIGHTS];  // rgb, LINEAR multiplier
};

// The bounce arriving at P, in linear light.  Each light is a disc facing
// along its normal; area * cos * cos / (pi d^2 + area) is the on-axis disc
// form factor, finite right up against the patch.
float3 MioPanBounceLight(float3 P, float3 N)
{
    float3 sum = 0.0.xxx;
    int count = min((int)uBounceInfo.x, MIOPAN_BOUNCE_LIGHTS);
    [loop]
    for (int i = 0; i < count; i++)
    {
        float3 L = uBouncePos[i].xyz - P;
        float d2 = max(dot(L, L), 1.0);
        float3 Ln = L * rsqrt(d2);
        float cosE = saturate(-dot(uBounceNormal[i].xyz, Ln));
        float cosR = saturate(dot(N, Ln));
        float area = uBouncePos[i].w;
        sum += uBounceColour[i].rgb *
               (area * cosE * cosR / (3.14159265 * d2 + area));
    }
    return sum;
}

struct PSInput
{
    float4 position : SV_Position;
    float2 vUV : TEXCOORD0;
    float2 vDstUV : TEXCOORD1;
    float4 uColor : TEXCOORD2;
    float4 worldPosition : TEXCOORD3;
    float3 worldNormal : TEXCOORD4;
    float4 fogW : TEXCOORD5;
};

float4 main(PSInput input) : SV_Target0
{
#ifdef MIOPAN_VU1_SHADOW_RAYS
    gMioPanRtPixel = input.position.xy;
    MioPanRtLoadMask(input.position.xy, input.worldPosition.xyz,
                     normalize(input.worldNormal));
#endif
    float4 texel = uTexture.Sample(uTextureSampler, input.vUV);

    if (uPadFlags.z != 0)
    {
        float2 texelSize = 1.0 / max(uTextureSize.xy, 1.0.xx);
        float4 soft = texel * 0.40;
        soft += uTexture.Sample(uTextureSampler, input.vUV + float2( texelSize.x, 0.0)) * 0.15;
        soft += uTexture.Sample(uTextureSampler, input.vUV + float2(-texelSize.x, 0.0)) * 0.15;
        soft += uTexture.Sample(uTextureSampler, input.vUV + float2(0.0,  texelSize.y)) * 0.15;
        soft += uTexture.Sample(uTextureSampler, input.vUV + float2(0.0, -texelSize.y)) * 0.15;
        texel = soft;
    }

    // Interpolating a unit normal shortens it; renormalise before the dots.
    // The positions are interpolated in world space, which is what the kernel
    // wants -- LWLOOP2 pre-transforms every vertex before the vertex loop.
    float3 N = normalize(input.worldNormal);
    float3 lit = MioPanVu1ToModulate(
        MioPanVu1Evaluate(uVu1, input.worldPosition.xyz, N,
                          MioPanVu1FromModulate(input.uColor.rgb),
                          uVu1.config.z));

    // The flashlight's bounce, added in LINEAR light: the GS units are gamma
    // encoded, and a dim bounce summed there would lift every black into a
    // grey wash instead of reading as light.  The kernel's own ceiling (255,
    // twice unity) still holds afterwards.
    float3 bounce = MioPanBounceLight(input.worldPosition.xyz, N);
#ifdef MIOPAN_VU1_SHADOW_RAYS
    // The bounce is light from all round too; occlusion scales it in linear
    // light, so undo the gamma shaping gMioPanRtAo carries.
    bounce *= pow(gMioPanRtAo, 2.2);
#endif
    if (any(bounce > 0.0))
    {
        float3 linearLit = pow(max(lit, 0.0.xxx), 2.2) + bounce;
        lit = min(pow(linearLit, 1.0 / 2.2),
                  uVu1.ambient.www * kMioPanGsModulateUnity);
    }

#ifdef MIOPAN_VU1_SHADOW_RAYS
    // Debug view: the light image alone, untextured and unfogged, so shadow
    // edges are not lost in the room's own texture detail.
    if ((uRtFlags.x & MIOPAN_RT_DEBUG_LIGHT) != 0)
    {
        return float4(saturate(lit * 0.5), 1.0);
    }
    // Debug view: the ambient occlusion factor, as applied.
    if ((uRtFlags.x & MIOPAN_RT_DEBUG_AO) != 0)
    {
        return float4(gMioPanRtAo.xxx, 1.0);
    }
    // Debug view: the flashlight's bounce alone, on the same scale.
    if ((uRtFlags.x & MIOPAN_RT_DEBUG_BOUNCE) != 0)
    {
        return float4(saturate(pow(bounce, 1.0 / 2.2) * 0.5), 1.0);
    }
    // Debug view: the rays themselves.  Red where a traced lane was blocked,
    // green where it got through, black where no lane was bright enough to
    // trace; the grey underneath is the light image for orientation.  Blue
    // marks lanes this fragment had to trace itself because the shadow mask
    // could not answer for them -- the mask's misses.
    if ((uRtFlags.x & MIOPAN_RT_DEBUG_RAYS) != 0)
    {
        float grey = dot(saturate(lit * 0.25), float3(0.3, 0.4, 0.3));
        float shadowed = gMioPanRtTraced > 0.0
            ? gMioPanRtShadowed / gMioPanRtTraced : 0.0;
        float traced = gMioPanRtTraced > 0.0 ? 1.0 : 0.0;
        float missed = gMioPanRtTraced > 0.0
            ? (gMioPanRtTraced - gMioPanRtFromMask) / gMioPanRtTraced : 0.0;
        return float4(grey + shadowed * 0.8,
                      grey + traced * (1.0 - shadowed) * 0.5,
                      grey + missed * 0.7, 1.0);
    }
#endif

    // Port deviation: restore the one-texel alpha edge that magnification
    // softened.  Applied to the texel, ahead of the vertex-colour modulate
    // below, so a surface being faded out by vertex alpha still fades rather
    // than being contrast-stretched out of existence.
    texel.a = MikuPanSharpenAlpha(texel.a, input.vUV);
    if (MikuPanAlphaCutoffFails(texel.a))
    {
        discard;
    }

    float4 col = texel * float4(lit, input.uColor.a);

    if (MikuPanAlphaTestFails(col.a))
    {
        discard;
    }

    // See sprite.frag: fogging leaves alpha alone, so running it behind the
    // alpha test rather than ahead of it costs nothing and matches the GS.
    col.rgb = MikuPanApplyGsFog(col.rgb, input.fogW.x);

    if (uParams1.y > 0.001)
    {
        float strength = clamp(uParams1.y, 0.0, 1.0);
        float3 negativeColor = clamp((96.0 / 255.0).xxx - col.rgb,
                                     0.0.xxx, 1.0.xxx);
        col.rgb = lerp(col.rgb, negativeColor, strength);
    }

    if (col.a <= 0.0)
    {
        discard;
    }
    return col;
}
