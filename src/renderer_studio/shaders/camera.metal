#include <metal_stdlib>

#include "../shader_defs.hpp"

using namespace metal;
using namespace pt::shaders_studio;

struct VertexOut {
  float4 position [[position]];
  uint16_t objectId;
};

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
  constant uint16_t& selectedNodeId [[buffer(0)]],
  constant EdgeConstants& c [[buffer(1)]]
) {
  float3 drawColor = selectedNodeId == in.objectId ? c.selectionColor : c.outlineColor;
  return float4(drawColor, 1.0);
}
