#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-attributes"

#ifndef PLATINUM_PT_SHADER_DEFS_HPP
#define PLATINUM_PT_SHADER_DEFS_HPP

#include <simd/simd.h>

using namespace simd;

// Don't nest namespaces here, the MSL compiler complains it's a C++ 17 ext
namespace pt { // NOLINT(*-concat-nested-namespaces)
namespace shaders_pt {

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
  uint32_t frameIdx;
  uint2 size;
  CameraData camera;
};

}
}

#endif //PLATINUM_PT_SHADER_DEFS_HPP

#pragma clang diagnostic pop
