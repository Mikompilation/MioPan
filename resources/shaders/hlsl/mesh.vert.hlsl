#define MIKUPAN_VERTEX_STAGE
#include "mikupan_common.hlsli"

struct VSInput
{
    float4 aUV : TEXCOORD0;
    float4 inColor : TEXCOORD1;
    float4 aPos : TEXCOORD2;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 vUV : TEXCOORD0;
    float2 vDstUV : TEXCOORD1;
    float4 uColor : TEXCOORD2;
    // View depth for the fog ramp, in .x -- see MikuPanApplyGsFog().  A whole
    // float4 for the signature-packing reason sprite.vert.hlsl gives.
    float4 fogW : TEXCOORD3;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.position = mul(mvp, input.aPos);
    output.vUV = input.aUV.xy;
    output.vDstUV = input.aUV.zw;
    output.uColor = input.inColor;
    output.fogW = output.position.wwww;
    return output;
}
