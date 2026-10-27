// RmlUi geometry, as miopan_rmlui.cpp records it: context pixels in, the
// output's clip space out.  Deliberately apart from mikupan_common.hlsli -- the
// UI draws over the finished composite and none of the GS emulation applies.

cbuffer RmlUiUniforms : register(b0, space1)
{
    // The output projection times RmlUi's own transform (identity when the
    // element has none), so a transformed element is projected as RmlUi's GL
    // backends do it.
    column_major float4x4 uTransform;
    // xy: the translation RmlUi passes with each draw, applied first.
    float4 uTranslation;
};

struct VSInput
{
    float2 aPos : TEXCOORD0;
    float4 aColour : TEXCOORD1;
    float2 aUV : TEXCOORD2;
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 vColour : TEXCOORD0;
    float2 vUV : TEXCOORD1;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.position = mul(uTransform,
                          float4(input.aPos + uTranslation.xy, 0.0, 1.0));
    // Premultiplied, as RmlUi hands it over; the pipeline blends to match.
    output.vColour = input.aColour;
    output.vUV = input.aUV;
    return output;
}
