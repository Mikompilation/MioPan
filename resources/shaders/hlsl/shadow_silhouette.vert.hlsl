#define MIKUPAN_VERTEX_STAGE
#include "mikupan_common.hlsli"

/*
 * Caster silhouette, written into the shadow map.
 *
 * The vertex layout is MioPan's streamed mesh layout, NOT the one this shader
 * carried over from MikuPan: position is TEXCOORD2 (attribute location 2), and
 * binding aPos to TEXCOORD0 read the UV stream as a position.  See
 * CreatePipelineVariant()'s MESH_PIPELINE_STREAMED branch -- 0 uv, 1 colour,
 * 2 position.
 *
 * `mvp` is the light camera's, because gra3dShadow.c's _RenderShadow() calls
 * _ApplyCamera(&s_Camera) before drawing the caster and that reaches the host
 * through MioPan_Graph3dApplyCamera().  Nothing here needs the light matrix
 * explicitly.
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
};

VSOutput main(VSInput input)
{
    VSOutput output;
    /* No MikuPanFixClipZ: MioPan renders reversed-Z and the projection already
     * arrives with near at w and far at 0. */
    output.position = mul(mvp, input.aPos);
    return output;
}
