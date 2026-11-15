#define MIKUPAN_VERTEX_STAGE
#include "mikupan_common.hlsli"

// Ray-traced shadow mask, G-buffer prepass: resident (cached-static) meshes.
// Same inputs and the same world-space arithmetic as mesh_spot.vert, so the
// position written here is bit-for-bit the one mesh_spot_rt.frag interpolates
// for the same surface -- which is how that shader tells whether the mask at
// its pixel belongs to it.  Its own outputs, so the prepass fragment stage's
// signature is not tied to mesh_spot.frag's packing.

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
    float4 uvAlpha : TEXCOORD0;
    float4 worldPosition : TEXCOORD1;
    float4 worldNormal : TEXCOORD2;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.position = mul(mvp, input.aPos);
    output.uvAlpha = float4(input.aUV.xy, input.inColor.a, 0.0);
    output.worldPosition = mul(model, input.aPos);
    output.worldNormal = float4(normalize(mul((float3x3)model, input.aNormal.xyz)), 0.0);
    return output;
}
