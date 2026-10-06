#define MIKUPAN_VERTEX_STAGE
#define MIKUPAN_CUSTOM_LIGHT_BLOCK
#include "mikupan_common.hlsli"
#include "miopan_vu1_lighting.hlsli"

// Per-vertex evaluation of the VU1 light image, for animated meshes whose
// live position and normal are streamed once per unique strip vertex.  The
// kernel itself lives in miopan_vu1_lighting.hlsli and is shared with
// mesh_spot.frag; this file only decides WHERE it is evaluated.
cbuffer MioPanVertexLightBlock : register(b1, MIKUPAN_UNIFORM_SPACE)
{
    MioPanVu1LightImage uVu1;
};

struct VSInput
{
    float4 aUV : TEXCOORD0;
    float4 aNormal : TEXCOORD1;
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

float3 CalcVu1VertexLighting(float4 localPosition, float3 localNormal)
{
    if ((uVu1.config.y & 1) == 0)
    {
        return 1.0.xxx;
    }

    // The upper 3x3 is applied directly and the result normalised rather than
    // using an inverse transpose -- the VU1 does the same thing
    // (g3dSetVu1LightData normalises the rows of the transposed matrix).
    float3 P = mul(model, localPosition).xyz;
    float3 N = MioPanVu1SafeNormalize(mul((float3x3)model, localNormal));

    // A runtime VUVN packet carries no colour of its own, so the accumulator
    // starts from black, and this path owns the whole image.
    return MioPanVu1ToModulate(
        MioPanVu1Evaluate(uVu1, P, N, 0.0.xxx,
                          MIOPAN_VU1_TERM_DIRECTIONAL |
                          MIOPAN_VU1_TERM_SPOT |
                          MIOPAN_VU1_TERM_POINT));
}

VSOutput main(VSInput input)
{
    VSOutput output;
    output.position = mul(mvp, input.aPos);
    output.vUV = input.aUV.xy;
    output.vDstUV = input.aUV.zw;
    // Alpha is not lit: every kernel writes .xyz only, and the vertex alpha the
    // VU1 emits is DIRCOLDIF[0].w -- the material's own vDiffuse[3], which
    // gra3dCalcVu1MaterialDataDirectional() stores unconditionally.  That is
    // the term ManmdlSetAlpha() drives, so it is what fades a ghost out.  GS
    // units, hence the 128 (not 255) divisor: the blend is
    // (((A - B) * C) >> 7) + D, so As == 0x80 is 1.0.
    output.uColor = float4(
        CalcVu1VertexLighting(input.aPos, input.aNormal.xyz),
        saturate(uVu1.dirColDif[0].w * kMioPanGsModulateUnity));
    output.fogW = output.position.wwww;
    return output;
}
