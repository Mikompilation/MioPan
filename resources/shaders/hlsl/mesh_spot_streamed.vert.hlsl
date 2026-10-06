#define MIKUPAN_VERTEX_STAGE
#include "mikupan_common.hlsli"

struct VSInput
{
    // zw carry an octahedrally encoded normal on fragment-lit mesh streams.
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
    float4 worldPosition : TEXCOORD3;
    float3 worldNormal : TEXCOORD4;
    // View depth for the fog ramp, in .x -- see MikuPanApplyGsFog().  A whole
    // float4 for the signature-packing reason sprite.vert.hlsl gives.
    float4 fogW : TEXCOORD5;
};

float3 DecodeOctahedralNormal(float2 encoded)
{
    float2 p = encoded * 2.0 - 1.0;
    float3 normal = float3(p, 1.0 - abs(p.x) - abs(p.y));
    if (normal.z < 0.0)
    {
        float2 signs = float2(normal.x >= 0.0 ? 1.0 : -1.0,
                              normal.y >= 0.0 ? 1.0 : -1.0);
        normal.xy = (1.0 - abs(normal.yx)) * signs;
    }
    return normalize(normal);
}

VSOutput main(VSInput input)
{
    VSOutput output;
    float3 localNormal = DecodeOctahedralNormal(input.aUV.zw);
    output.position = mul(mvp, input.aPos);
    output.vUV = input.aUV.xy;
    output.vDstUV = 0.0.xx;
    output.uColor = input.inColor;
    output.worldPosition = mul(model, input.aPos);
    output.worldNormal = normalize(mul((float3x3)model, localNormal));
    output.fogW = output.position.wwww;
    return output;
}
