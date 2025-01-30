#include <metal_stdlib>

#include "../shader_defs.hpp"

using namespace metal;
using namespace pt::shaders_studio;

struct VertexOut {
  float4 position [[position]];
  float2 texCoords;
};

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
  constant float2& viewportSize [[buffer(0)]],
  constant uint16_t& selectedNodeId [[buffer(1)]],
  constant EdgeConstants& c [[buffer(2)]]
) {
  float2 offset = 1.0 / viewportSize;
  float edgeKernel[9] = {
    1.0, 1.0, 1.0,
    1.0, -8.0, 1.0,
    1.0, 1.0, 1.0,
  };

  float3 drawColor = c.outlineColor;
  float edge = 0.0;
  for (int8_t i = 0; i < 9; i++) {
    float2 localOffset = offset * float2(i % 3 - 1, i / 3 - 1);
    uint16_t sample = objectTexture.sample(sampler, in.texCoords + localOffset).x;
    if (sample != 0 && sample == selectedNodeId) drawColor = c.selectionColor;

    edge += edgeKernel[i] * sample;
  }
  edge = smoothstep(0.0, 1.0, abs(edge));

  float3 color = colorTexture.sample(sampler, in.texCoords).xyz;
  color = mix(color, drawColor, edge);

  return float4(color, 1.0);
}
