#include <metal_stdlib>

#include "../shader_defs.hpp"

using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float4 color;
    uint16_t objectId;
};

struct FragmentOut {
    float4 color [[color(0)]];
    uint16_t objectId [[color(1)]];
};

vertex VertexOut vertexShader(
    Vertex in [[stage_in]],
    constant NodeData &data [[buffer(1)]],
    constant float4x4 &viewProjection [[buffer(2)]]
) {
    VertexOut out;
    out.position = viewProjection * data.model * float4(in.position, 1.0);
    out.color = float4(in.position * 0.5 + 0.5, 1.0);
    out.objectId = data.nodeIdx;

    return out;
}

fragment FragmentOut fragmentShader(VertexOut in [[stage_in]]) {
    FragmentOut out;
    out.color = in.color;
    out.objectId = in.objectId;

    return out;
}
