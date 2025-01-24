#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-attributes"

#ifndef PLATINUM_PT_SHADER_DEFS_HPP
#define PLATINUM_PT_SHADER_DEFS_HPP

#ifdef __METAL_VERSION__
#define metal_texture(n) texture ## n ## d<float>
#else
#define metal_texture(n) MTL::ResourceID
#endif

#ifdef __METAL_VERSION__
#define metal_ptr(T, address_space) address_space T*
#else
#define metal_ptr(T, address_space) uint64_t
#endif

#ifdef __METAL_VERSION__
#define metal_resource(T) T
#else
#define metal_resource(T) MTL::ResourceID
#endif

#include <simd/simd.h>

#include "../core/mesh.hpp"
#include "../core/material.hpp"
#include "../core/environment.hpp"

using namespace simd;

#ifdef __METAL_VERSION__
using namespace metal;
using namespace raytracing;

using IntersectionFunctionTable = intersection_function_table<triangle_data, instancing>;
#endif

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

struct AreaLight {
  uint32_t instanceIdx;
  uint32_t indices[3];
  float area, power, cumulativePower;
  float3 emission;
};

struct Distribution1D {
  metal_ptr(float, device) f;
  metal_ptr(float, device) cdf;
  float min, max, integral;
  uint32_t size;
};

struct Distribution2D {
  float2 min, max;
  Distribution1D marginal;
  metal_ptr(Distribution1D, device) conditional;
  uint32_t size;
};

struct EnvironmentLight {
  uint32_t textureId;
  metal_ptr(AliasEntry, device) alias;
};

enum RendererFlags {
  RendererFlags_None = 0,
  RendererFlags_MultiscatterGGX = 1 << 0,
};

struct Constants {
  uint32_t frameIdx;
  uint32_t lightCount;
  uint32_t envLightCount;
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

struct Luts {
  metal_texture(2) E;
  metal_texture(1) Eavg;
  metal_texture(3) EMs;
  metal_texture(2) EavgMs;
  metal_texture(3) ETransIn;
  metal_texture(3) ETransOut;
  metal_texture(2) EavgTransIn;
  metal_texture(2) EavgTransOut;
};

#ifdef __METAL_VERSION__

struct Texture {
  texture2d<float> tex;
};

#endif

struct Arguments {
  Constants constants;
  metal_ptr(VertexResource, device) vertexResources;
  metal_ptr(PrimitiveResource, device) primitiveResources;
  metal_ptr(InstanceResource, device) instanceResources;
  metal_ptr(MTLAccelerationStructureInstanceDescriptor, constant) instances;
  metal_resource(instance_acceleration_structure) accelStruct;
  metal_resource(IntersectionFunctionTable) intersectionFunctionTable;
  metal_ptr(AreaLight, constant) lights;
  metal_ptr(EnvironmentLight, constant) envLights;
  metal_ptr(Texture, constant) textures;
  
  Luts luts;
};

}
}

#endif //PLATINUM_PT_SHADER_DEFS_HPP

#pragma clang diagnostic pop
