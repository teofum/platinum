#include <metal_stdlib>

#include "../shader_defs.hpp"

using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float2 texCoords;
};

constant float3 edgeColor(0.15, 0.15, 0.15);

vertex VertexOut edgePassVertex(PostPassVertex in [[stage_in]]) {
    VertexOut out;
    out.position = float4(in.position, 0.0, 1.0);
    out.texCoords = in.position * float2(0.5, -0.5) + 0.5;

    return out;
}

fragment float4 edgePassFragment(
    VertexOut in [[stage_in]],
    texture2d<float> colorTexture [[texture(0)]],
    texture2d<uint16_t> objectTexture [[texture(1)]],
    sampler sampler [[sampler(0)]],
    constant float2& viewportSize [[buffer(0)]]
) {
    float2 offset = 1.0 / viewportSize;
    float edgeKernel[9] = {
        1.0, 1.0, 1.0,
        1.0, -8.0, 1.0,
        1.0, 1.0, 1.0,
    };

    float edge = 0.0;
    for (int8_t i = 0; i < 9; i++) {
        float2 localOffset = offset * float2(i % 3 - 1, i / 3 - 1);
        edge += edgeKernel[i] * objectTexture.sample(sampler, in.texCoords + localOffset).x;
    }
    edge = smoothstep(0.0, 1.0, abs(edge));

    float3 color = colorTexture.sample(sampler, in.texCoords).xyz;
    color = mix(color, edgeColor, edge);

    return float4(color, 1.0);
}