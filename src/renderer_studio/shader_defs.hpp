#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-attributes"

#ifndef PLATINUM_SHADER_DEFS_HPP
#define PLATINUM_SHADER_DEFS_HPP

#include <simd/simd.h>

using namespace simd;

struct Vertex {
  float3 position [[attribute(0)]];
  float4 color [[attribute(1)]];
};

struct Transforms {
  float4x4 model;
  float4x4 view;
  float4x4 projection;
};

#endif //PLATINUM_SHADER_DEFS_HPP

#pragma clang diagnostic pop