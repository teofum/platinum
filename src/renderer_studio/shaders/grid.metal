#include <metal_stdlib>

#include "../shader_defs.hpp"

using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float3 wsPosition;
    float2 uv;
};

vertex VertexOut gridVertex(
    PostPassVertex in [[stage_in]],
    constant Constants& c [[buffer(1)]],
    constant GridProperties& grid [[buffer(2)]]
) {
    VertexOut out;
    out.wsPosition = float3(0.0);
    out.wsPosition.xz = in.position * grid.size;
    out.position = float4(out.wsPosition, 1.0);
    out.position = c.projection * c.view * out.position;
    out.uv = in.position * grid.size / grid.spacing;

    return out;
}

fragment float4 gridFragment(
    VertexOut in [[stage_in]],
    constant GridProperties& grid [[buffer(0)]],
    constant float3& cameraPosition [[buffer(1)]]
) {
    float2 uvDeriv = fwidth(in.uv);
    float2 lineAA = 1.5 * uvDeriv;
    float2 drawWidth = grid.lineWidth * uvDeriv;

    float2 uv = 1.0 - abs(fract(in.uv) * 2.0f - 1.0);
    float2 lines = smoothstep(drawWidth + lineAA, drawWidth - lineAA, uv);
    lines = mix(lines, float2(0.0), saturate(uvDeriv * 2.0 - 1.0));
    float line = max(lines.x, lines.y);

    float cameraDistance = distance(cameraPosition, in.wsPosition);
    float fadeDistance = exp10((float)grid.level + 1.0) * grid.fadeDistance;
    line *= saturate(1.0 - cameraDistance / fadeDistance);

    float3 color = mix(float3(0.0), grid.lineColor, line);
    return float4(color, line);
}