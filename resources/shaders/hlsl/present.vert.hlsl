/*
 * The present pass's vertex stage: one oversized triangle covering the whole
 * target, generated from SV_VertexID so the pass needs no vertex buffer at all.
 *
 * SDL_GPU's clip space has +Y up while texture UVs start top-left, the same
 * split ScreenYToClip() in miopan_renderer.cpp works in -- hence the -2 on the
 * Y scale, which flips the UV as it is turned into a position.
 */

struct VSOutput
{
    float4 position : SV_Position;
    float2 vUV : TEXCOORD0;
};

VSOutput main(uint id : SV_VertexID)
{
    VSOutput output;

    /* id 0 -> (0,0), 1 -> (2,0), 2 -> (0,2).  The half of the triangle that
     * falls outside the target is clipped away and never rasterised. */
    float2 uv = float2(float((id << 1) & 2u), float(id & 2u));

    output.vUV = uv;
    output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0),
                             0.0, 1.0);
    return output;
}
