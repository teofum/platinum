#include <metal_stdlib>

#include "../shader_defs.hpp"

using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float4 wsPosition;
    float3 vsNormal;
    uint16_t objectId;
};

struct FragmentOut {
    float4 color [[color(0)]];
    uint16_t objectId [[color(1)]];
    uint32_t stencil [[stencil]];
};

constant float3 objectColor(0.5, 0.5, 0.5);
constant float3 vsLightDirection(0.2, -0.2, -1.0);
constant float ambientLight = 0.5;

vertex VertexOut vertexShader(
    Vertex in [[stage_in]],
    constant NodeData &data [[buffer(2)]],
    constant Constants &c [[buffer(3)]]
) {
    VertexOut out;
    out.wsPosition = data.model * float4(in.position, 1.0);
    out.position = c.projection * c.view * out.wsPosition;
    out.vsNormal = normalize(data.normalViewModel * in.normal);
    out.objectId = data.nodeIdx;

    return out;
}

fragment FragmentOut fragmentShader(
    VertexOut in [[stage_in]],
    constant float3& cameraPosition [[buffer(0)]]
) {
    float shading = mix(abs(dot(vsLightDirection, in.vsNormal)), 1.0, ambientLight);

    FragmentOut out;
    out.color = float4(objectColor * shading, 1.0);
    out.objectId = in.objectId;
    out.stencil = 2 * saturate(sign(cameraPosition.y * (in.wsPosition.y + sign(cameraPosition.y) * 0.001)));

    return out;
}
