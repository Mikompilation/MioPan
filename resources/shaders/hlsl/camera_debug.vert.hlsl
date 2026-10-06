#define MIKUPAN_VERTEX_STAGE
#include "mikupan_common.hlsli"

struct VSInput
{
    float4 aPos : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 outColor : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    /* aPos arrives in host clip space already, so z passes straight through.
     * This used to read `clip.z = clip.z * 2.0 - clip.w` feeding
     * MikuPanFixClipZ, whose two halves cancelled to exactly that passthrough;
     * with the reversed-Z conversion now living in MikuPanFixClipZ, dropping
     * both keeps the net behaviour identical. */
    float4 clip = mul(uWorldClipView, input.aPos);

    VSOutput output;
    output.position = clip;
    output.outColor = uColor;
    return output;
}
