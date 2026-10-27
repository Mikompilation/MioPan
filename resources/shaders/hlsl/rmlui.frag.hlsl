// RmlUi geometry -- see rmlui.vert.hlsl.  Texture and vertex colour are both
// premultiplied, so their product is too.  Untextured geometry is drawn with a
// 1x1 white texture bound rather than a second pipeline.

Texture2D uTexture : register(t0, space2);
SamplerState uTextureSampler : register(s0, space2);

struct PSInput
{
    float4 position : SV_Position;
    float4 vColour : TEXCOORD0;
    float2 vUV : TEXCOORD1;
};

float4 main(PSInput input) : SV_Target0
{
    return uTexture.Sample(uTextureSampler, input.vUV) * input.vColour;
}
