#include <metal_stdlib>

#include "../viewport.hpp"

using namespace metal;

// Quad in y-flipped UV coordinates
constant float2 quadVertices[] = {
  float2(0, 0),
  float2(0, 1),
  float2(1, 1),
  float2(0, 0),
  float2(1, 1),
  float2(1, 0)
};

struct VertexOut {
  float4 position [[position]];
  float2 uv;
};

// Simple vertex shader that passes through NDC quad positions
vertex VertexOut viewportVertex(
  unsigned short          vid         [[vertex_id]],
  constant pt::Viewport&  viewport    [[buffer(0)]],
  constant float2&        screenSize  [[buffer(1)]]
) {
  // Get vertex position on screen
  float2 position = (quadVertices[vid] * viewport.displaySize + viewport.displayOffset + viewport.viewportOffset) / screenSize;

  // Clip
  float2 minPos = viewport.viewportOffset / screenSize;
  float2 maxPos = (viewport.viewportOffset + viewport.viewportSize) / screenSize;
  position = clamp(position, minPos, maxPos);

  // Calculate UVs
  float2 p0 = (float2(0, 0) * viewport.displaySize + viewport.displayOffset + viewport.viewportOffset) / screenSize;
  float2 p1 = (float2(1, 1) * viewport.displaySize + viewport.displayOffset + viewport.viewportOffset) / screenSize;
  float2 uvMin = saturate((minPos - p0) / (p1 - p0));
  float2 uvMax = saturate((maxPos - p0) / (p1 - p0));
  float2 uv = mix(uvMin, uvMax, quadVertices[vid]);

  // Convert to NDC
  position = (position * 2.0 - 1.0) * float2(1, -1);

  VertexOut out;
  out.position = float4(position, 0, 1);
  out.uv = uv;

  return out;
}

// Trivial fragment shader that displays a texture
fragment float4 viewportFragment(
  VertexOut         in    [[stage_in]],
  texture2d<float>  src   [[texture(0)]]
) {
  constexpr sampler sampler(min_filter::linear, mag_filter::nearest, mip_filter::none);
  return src.sample(sampler, in.uv);
}