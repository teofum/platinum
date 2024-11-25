#include <metal_stdlib>

#include "../shader_defs.hpp"

using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float4 vsPosition;
    float3 vsNormal;
    uint16_t objectId;
};

struct FragmentOut {
    float4 color [[color(0)]];
    uint16_t objectId [[color(1)]];
};

constant float3 objectColor(0.5, 0.5, 0.5);

vertex VertexOut vertexShader(
    Vertex in [[stage_in]],
    constant NodeData &data [[buffer(2)]],
    constant Constants &c [[buffer(3)]]
) {
    VertexOut out;
    out.vsPosition = data.viewModel * float4(in.position, 1.0);
    out.position = c.projection * out.vsPosition;
    out.vsNormal = normalize(data.normalViewModel * in.normal);
    out.objectId = data.nodeIdx;

    return out;
}

fragment FragmentOut fragmentShader(VertexOut in [[stage_in]]) {
    FragmentOut out;
    out.color = float4(in.vsNormal * 0.5 + 0.5, 1.0);
    out.objectId = in.objectId;

    return out;
}
