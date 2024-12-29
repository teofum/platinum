#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-attributes"

#ifndef PLATINUM_PT_SHADER_DEFS_HPP
#define PLATINUM_PT_SHADER_DEFS_HPP

#ifdef __METAL_VERSION__
#define metal_ptr(T, address_space) address_space T*
#else
#define metal_ptr(T, address_space) uint64_t
#endif

#include <simd/simd.h>

#include "../core/mesh.hpp"
#include "../core/material.hpp"

using namespace simd;

// Don't nest namespaces here, the MSL compiler complains it's a C++ 17 ext
namespace pt {
namespace shaders_pt {

/*
 * Argument buffer structs
 */
struct PrimitiveData {
  uint32_t indices[3];
};

struct CameraData {
  float3 position;
  float3 topLeft;
  float3 pixelDeltaU;
  float3 pixelDeltaV;
};

struct LightData {
  uint32_t instanceIdx;
  uint32_t indices[3];
  float area, power, cumulativePower;
  float3 emission;
  
#ifdef __METAL_VERSION__
  
  inline float pdf() const {
    return 1.0f / area;
  }
  
#endif
};

enum RendererFlags {
  RendererFlags_None = 0,
  RendererFlags_MultiscatterGGX = 1 << 0,
};

struct Constants {
  uint32_t frameIdx;
  uint32_t lightCount;
  uint32_t lutSizeE, lutSizeEavg;
  int flags;
  float totalLightPower;
  uint2 size;
  CameraData camera;
};

struct VertexResource {
  metal_ptr(float3, device) position;
  metal_ptr(VertexData, device) data;
};

struct PrimitiveResource {
  metal_ptr(uint32_t, device) materialSlot;
};

struct InstanceResource {
  metal_ptr(Material, device) materials;
};

#ifndef __METAL_VERSION__

struct Luts {
  MTL::ResourceID E;
  MTL::ResourceID Eavg;
  MTL::ResourceID EMs;
  MTL::ResourceID EavgMs;
  MTL::ResourceID ETransIn;
  MTL::ResourceID ETransOut;
  MTL::ResourceID EavgTransIn;
  MTL::ResourceID EavgTransOut;
};

struct Arguments {
  Constants constants;
  uint64_t vertexResources;
  uint64_t primitiveResources;
  uint64_t instanceResources;
  uint64_t instances;
  MTL::ResourceID accelStruct;
  uint64_t lights;
  uint64_t textures;
  
  Luts luts;
};

#endif

}
}

#endif //PLATINUM_PT_SHADER_DEFS_HPP

#pragma clang diagnostic pop
