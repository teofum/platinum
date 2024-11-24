#include <metal_stdlib>

#include "../shader_defs.hpp"

using namespace metal;

struct RasterVertex {
    float4 position [[position]];
    float4 color;
};

vertex RasterVertex vertexShader(
    Vertex in [[stage_in]],
    constant Transforms &t [[buffer(1)]]
) {
    RasterVertex out;
    out.position = t.projection * t.view * t.model * float4(in.position, 1.0);
    out.color = in.color;

    return out;
}

fragment float4 fragmentShader(RasterVertex in [[stage_in]]) {
    return in.color;
}
