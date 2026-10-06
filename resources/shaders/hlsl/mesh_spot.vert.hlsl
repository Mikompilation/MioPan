#define MIKUPAN_VERTEX_STAGE
#include "mikupan_common.hlsli"

struct VSInput
{
    float4 aUV : TEXCOORD0;
    float4 inColor : TEXCOORD1;
    float4 aPos : TEXCOORD2;
    float4 aNormal : TEXCOORD3;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 vUV : TEXCOORD0;
    float2 vDstUV : TEXCOORD1;
    float4 uColor : TEXCOORD2;
    float4 worldPosition : TEXCOORD3;
    float3 worldNormal : TEXCOORD4;
    // View depth for the fog ramp, in .x -- see MikuPanApplyGsFog().  A whole
    // float4 so DXC cannot pack it into the r0.zw hole mesh_spot.frag leaves by
    // not reading vDstUV; sprite.vert.hlsl has the full story.
    float4 fogW : TEXCOORD5;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.position = mul(mvp, input.aPos);
    output.vUV = input.aUV.xy;
    output.vDstUV = input.aUV.zw;
    output.uColor = input.inColor;
    output.worldPosition = mul(model, input.aPos);
    output.worldNormal = normalize(mul((float3x3)model, input.aNormal.xyz));
    output.fogW = output.position.wwww;
    return output;
}
