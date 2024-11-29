#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-attributes"

#ifndef PLATINUM_PT_SHADER_DEFS_HPP
#define PLATINUM_PT_SHADER_DEFS_HPP

#include <simd/simd.h>

using namespace simd;

struct PrimitiveData {
  uint32_t indices[3];
};

struct CameraData {
  float3 position;
  float3 topLeft;
  float3 pixelDeltaU;
  float3 pixelDeltaV;
};

struct Constants {
  uint2 size;
  CameraData camera;
};

#endif //PLATINUM_PT_SHADER_DEFS_HPP

#pragma clang diagnostic pop