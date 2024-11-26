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
  float4x4 viewModel;
  float3x3 normalViewModel;
  uint16_t nodeIdx = 0;
};

struct Constants {
  float4x4 projection;
};

#endif //PLATINUM_SHADER_DEFS_HPP

#pragma clang diagnostic pop