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
    // View depth for the fog ramp, in .x -- see MikuPanApplyGsFog().  Last, so
    // finder_mask.frag's inputs keep their places; it declares it too.
    //
    // A whole float4 on purpose.  DXIL links stages by signature REGISTER, and
    // DXC packs scalars into holes: sprite.frag never reads vDstUV, so its
    // signature leaves r0.zw free and a scalar fogW lands there, while this
    // stage writes it to r2.x -- D3D12 then hands the fragment garbage.  A
    // float4 cannot fit a hole and takes the same fresh row in both stages.
    // SPIR-V links by location and does not care either way.
    float4 fogW : TEXCOORD3;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.position = MikuPanFixClipZ(input.aPos);
    output.vUV = input.aUV.xy;
    output.vDstUV = input.aUV.zw;
    output.uColor = input.inColor;
    output.fogW = output.position.wwww;
    return output;
}
