#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-attributes"

#ifndef PLATINUM_SHADER_DEFS_HPP
#define PLATINUM_SHADER_DEFS_HPP

#include <simd/simd.h>

using namespace simd;

struct PostPassVertex {
  float2 position [[attribute(0)]];
};

struct Vertex {
  float3 position [[attribute(0)]];
  float3 normal [[attribute(1)]];
};

struct NodeData {
  float4x4 model;
  float3x3 normalViewModel;
  uint16_t nodeIdx = 0;
};

struct Constants {
  float4x4 projection;
  float4x4 view;
};

struct GridProperties {
  float size = 1000.0;
  float spacing = 0.1;
  float lineWidth = 1.0;
  float fadeDistance = 1.0;
  float3 lineColor = {0, 0, 0};
  float3 xAxisColor = {0.4, 0, 0};
  float3 zAxisColor = {0, 0, 0.4};
  uint32_t level = 0;
};

#endif //PLATINUM_SHADER_DEFS_HPP

#pragma clang diagnostic pop