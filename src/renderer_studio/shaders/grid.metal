#include <metal_stdlib>

#include "../shader_defs.hpp"

using namespace metal;
using namespace pt::shaders_studio;

// Screen filling quad in normalized device coordinates
constant float2 quadVertices[] = {
  float2(-1, -1),
  float2( 1,  1),
  float2(-1,  1),
  float2(-1, -1),
  float2( 1, -1),
  float2( 1,  1),
};

struct VertexOut {
  float4 position [[position]];
  float3 wsPosition;
  float2 uv;
  float depth;
};

vertex VertexOut gridVertex(
    unsigned short vid [[vertex_id]],
    constant GridProperties& grid [[buffer(0)]],
    constant Constants& c [[buffer(1)]]
) {
  float2 position = quadVertices[vid];

  VertexOut out;
  out.wsPosition = float3(0.0);
  out.wsPosition.xz = position * grid.size;
  out.position = float4(out.wsPosition, 1.0);
  out.position = c.projection * c.view * out.position;
  out.uv = position * grid.size / grid.spacing;
  out.depth = out.position.z;

  return out;
}

fragment float4 gridFragment(
  VertexOut in [[stage_in]],
  constant GridProperties& grid [[buffer(0)]],
  constant float3& cameraPosition [[buffer(1)]]
) {
  // Fancy grid shader based on https://bgolus.medium.com/the-best-darn-grid-shader-yet-727f9278b9d8
  float2 uvDeriv = fwidth(in.uv);
  float2 lineAA = 1.5 * uvDeriv;
  float2 drawWidth = grid.lineWidth * uvDeriv;

  float2 uv = 1.0 - abs(fract(in.uv) * 2.0f - 1.0);
  float2 lines = smoothstep(drawWidth + lineAA, drawWidth - lineAA, uv);
  lines = mix(lines, float2(0.0), saturate(uvDeriv * 2.0 - 1.0));
  float line = max(lines.x, lines.y);

  // Fade with distance from camera
  float cameraDistance = distance(cameraPosition, in.wsPosition);
  float fadeDistance = exp10((float)grid.level + 1.0) * grid.fadeDistance;
  line *= saturate(1.0 - cameraDistance / fadeDistance);
  line *= 1.0 - saturate(in.position.z - 0.99999) * 100000.0;

  // Color the x/z axes
  float2 axes = smoothstep(drawWidth + lineAA, drawWidth - lineAA, abs(in.uv));
  float3 lineColor = mix(mix(grid.lineColor, grid.zAxisColor, axes.x), grid.xAxisColor, axes.y);

  return float4(lineColor, line);
}