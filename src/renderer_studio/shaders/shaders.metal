#include <metal_stdlib>

#include "../shader_defs.hpp"

using namespace metal;

struct RasterVertex {
    float4 position [[position]];
    float4 color;
};

vertex RasterVertex vertexShader(
    Vertex in [[stage_in]],
    constant float4x4 &model [[buffer(1)]],
    constant float4x4 &viewProjection [[buffer(2)]]
) {
    RasterVertex out;
    out.position = viewProjection * model * float4(in.position, 1.0);
    out.color = float4(in.position * 0.5 + 0.5, 1.0);

    return out;
}

fragment float4 fragmentShader(RasterVertex in [[stage_in]]) {
    return in.color;
}
