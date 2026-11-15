#define MIKUPAN_VERTEX_STAGE
#include "mikupan_common.hlsli"

// Ray-traced shadow mask, G-buffer prepass: streamed meshes.  The twin of
// mesh_spot_streamed.vert -- the normal arrives octahedrally packed in the
// spare UV pair -- for the reason rt_gbuffer.vert gives.

struct VSInput
{
    float4 aUV : TEXCOORD0;
    float4 inColor : TEXCOORD1;
    float4 aPos : TEXCOORD2;
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 uvAlpha : TEXCOORD0;
    float4 worldPosition : TEXCOORD1;
    float4 worldNormal : TEXCOORD2;
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
    output.uvAlpha = float4(input.aUV.xy, input.inColor.a, 0.0);
    output.worldPosition = mul(model, input.aPos);
    output.worldNormal = float4(normalize(mul((float3x3)model, localNormal)), 0.0);
    return output;
}
