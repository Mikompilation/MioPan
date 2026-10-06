#define MIKUPAN_VERTEX_STAGE
#include "mikupan_common.hlsli"

/*
 * Receiver pass.  This is the host stand-in for the ROM's SRT_MAPSHADOW draw:
 * gra3dShadow.c walks every registered receiver block a second time and the
 * VU1 program SgSuShadow_dma_main (vu1/ff2_03.vsm) generates projective
 * texture coordinates from matLIP = s_matIP * matLocalWorld.  Here matLocalWorld
 * is the per-draw `model` and s_matIP's role is played by uShadowMatrix, the
 * light camera's world->clip that _RenderShadow() rendered the map with.
 *
 * Vertex layout is MioPan's streamed mesh layout -- position at TEXCOORD2.  The
 * MikuPan original bound aPos to TEXCOORD0, which is the UV stream here.
 */
struct VSInput
{
    float4 aUV : TEXCOORD0;
    float4 inColor : TEXCOORD1;
    float4 aPos : TEXCOORD2;
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 vShadowClip : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    float4 localPos = float4(input.aPos.xyz, 1.0);

    VSOutput output;
    output.position = mul(mvp, localPos);
    output.vShadowClip = mul(uShadowMatrix, mul(model, localPos));
    return output;
}
