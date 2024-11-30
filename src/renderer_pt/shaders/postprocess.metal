#include <metal_stdlib>

#include "../pt_shader_defs.hpp"

using namespace metal;
using namespace pt::shaders_pt;

// Screen filling quad in normalized device coordinates
constant float2 quadVertices[] = {
    float2(-1, -1),
    float2(-1,  1),
    float2( 1,  1),
    float2(-1, -1),
    float2( 1,  1),
    float2( 1, -1)
};

struct VertexOut {
    float4 position [[position]];
    float2 uv;
};

// Simple vertex shader that passes through NDC quad positions
vertex VertexOut postprocessVertex(unsigned short vid [[vertex_id]]) {
    float2 position = quadVertices[vid];

    VertexOut out;
    out.position = float4(position, 0, 1);
    out.uv = position * 0.5f + 0.5f;

    return out;
}

// Simple fragment shader that copies a texture
fragment float4 postprocessFragment(
    VertexOut in [[stage_in]],
    texture2d<float> src
) {
    constexpr sampler sampler(min_filter::nearest, mag_filter::nearest, mip_filter::none);

    float3 color = src.sample(sampler, in.uv).xyz;
    return float4(color, 1.0f);
}
