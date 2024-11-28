#include <metal_stdlib>

#include "../shader_defs.hpp"

using namespace metal;

struct VertexOut {
    float4 position [[position]];
    uint16_t objectId;
};

constant float3 objectColor(0.1, 0.1, 0.1);
constant float3 selectionColor(0.05, 0.25, 0.65);

vertex VertexOut cameraVertex(
    Vertex in [[stage_in]],
    constant NodeData &data [[buffer(1)]],
    constant Constants &c [[buffer(2)]]
) {
    VertexOut out;
    out.position = c.projection * c.view * data.model * float4(in.position, 1.0);
    out.objectId = data.nodeIdx;

    return out;
}

fragment float4 cameraFragment(
    VertexOut in [[stage_in]],
    constant uint16_t& selectedNodeId [[buffer(0)]]
) {
    float3 drawColor = selectedNodeId == in.objectId ? selectionColor : objectColor;
    return float4(drawColor, 1.0);
}
