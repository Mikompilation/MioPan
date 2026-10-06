#define MIKUPAN_CUSTOM_LIGHT_BLOCK
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
