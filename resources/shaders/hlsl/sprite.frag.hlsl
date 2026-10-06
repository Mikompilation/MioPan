#include "mikupan_common.hlsli"

struct PSInput
{
    float4 position : SV_Position;
    float2 vUV : TEXCOORD0;
    float2 vDstUV : TEXCOORD1;
    float4 uColor : TEXCOORD2;
    float4 fogW : TEXCOORD3;
};

float4 main(PSInput input) : SV_Target0
{
    float4 texel = uTexture.Sample(uTextureSampler, input.vUV);

    if (uPadFlags.z != 0)
    {
        float2 texel_size = 1.0 / max(uTextureSize.xy, 1.0.xx);
        float4 soft = texel * 0.40;
        soft += uTexture.Sample(uTextureSampler, input.vUV + float2( texel_size.x, 0.0)) * 0.15;
        soft += uTexture.Sample(uTextureSampler, input.vUV + float2(-texel_size.x, 0.0)) * 0.15;
        soft += uTexture.Sample(uTextureSampler, input.vUV + float2(0.0,  texel_size.y)) * 0.15;
        soft += uTexture.Sample(uTextureSampler, input.vUV + float2(0.0, -texel_size.y)) * 0.15;
        texel = soft;
    }


    // Port deviation: restore the one-texel alpha edge that magnification
    // softened.  Applied to the texel, ahead of the vertex-colour modulate
    // below, so a surface being faded out by vertex alpha still fades rather
    // than being contrast-stretched out of existence.
    texel.a = MikuPanSharpenAlpha(texel.a, input.vUV);
    if (MikuPanAlphaCutoffFails(texel.a))
    {
        discard;
    }

    float4 col = texel * input.uColor;

    // The GS ran its alpha test on the fragment straight out of the texture
    // function, before the colour was touched by anything else -- so this sits
    // ahead of the negative pass, not with the `col.a <= 0` check below.
    if (MikuPanAlphaTestFails(col.a))
    {
        discard;
    }

    // GS order is texture function -> fogging -> alpha test, but fogging never
    // touches alpha, so sitting behind the test here changes nothing except
    // that killed fragments do not pay for it.
    col.rgb = MikuPanApplyGsFog(col.rgb, input.fogW.x);

    if (uParams1.y > 0.001)
    {
        float strength = clamp(uParams1.y, 0.0, 1.0);
        float3 negative_color = clamp((96.0 / 255.0).xxx - col.rgb, 0.0.xxx, 1.0.xxx);
        col.rgb = lerp(col.rgb, negative_color, strength);
    }

    if (col.a <= 0.0)
    {
        discard;
    }

    return col;
}
