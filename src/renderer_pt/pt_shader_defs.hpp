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
#include "../core/postprocessing.hpp"

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
  float apertureRadius;
  uint32_t apertureBlades;
  float apertureRoundness;
};

struct AreaLight {
  uint32_t instanceIdx;
  uint32_t indices[3];
  float area, power, cumulativePower;
  float3 emission;
};

struct EnvironmentLight {
  uint32_t textureIdx;
  metal_ptr(AliasEntry, device) alias;
};

enum RendererFlags {
  RendererFlags_None = 0,
  RendererFlags_MultiscatterGGX = 1 << 0,
  RendererFlags_GMoN = 1 << 1,
};

/*
 * Material struct used GPU side for rendering
 */
struct MaterialGPU {
  enum MaterialFlags {
    Material_ThinDielectric = 1 << 0,
    Material_UseAlpha = 1 << 1,
    Material_Emissive = 1 << 2,
    Material_Anisotropic = 1 << 3,
  };

  float4 baseColor = {0.8, 0.8, 0.8, 1.0};
  float3 emission = {0.0, 0.0, 0.0};
  float emissionStrength = 0.0f;
  float roughness = 1.0f, metallic = 0.0f, transmission = 0.0f;
  float ior = 1.5f;
  float anisotropy = 0.0f, anisotropyRotation = 0.0f;
  float clearcoat = 0.0f, clearcoatRoughness = 0.05f;

  int flags = 0;

  int32_t baseTextureId = -1, rmTextureId = -1, transmissionTextureId = -1, clearcoatTextureId = -1, emissionTextureId = -1, normalTextureId = -1;
};

struct Constants {
  uint32_t frameIdx, spp, gmonBuckets;
  uint32_t lightCount;
  uint32_t envLightCount;
  uint32_t lutSizeE, lutSizeEavg;
  int flags;
  float totalLightPower;
  float apertureRadius;
  uint32_t apertureBlades;
  float apertureRoundness;
  uint2 size;
  float3 position;
  float3 topLeft;
  float3 pixelDeltaU;
  float3 pixelDeltaV;
};

struct VertexResource {
  metal_ptr(float3, device) position;
  metal_ptr(VertexData, device) data;
};

struct PrimitiveResource {
  metal_ptr(uint32_t, device) materialSlot;
};

struct InstanceResource {
  metal_ptr(MaterialGPU, device) materials;
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
  metal_ptr(MTLAccelerationStructureInstanceDescriptor, device) instances;
  metal_resource(instance_acceleration_structure) accelStruct;
  metal_resource(IntersectionFunctionTable) intersectionFunctionTable;
  metal_ptr(AreaLight, device) lights;
  metal_ptr(EnvironmentLight, device) envLights;
  metal_ptr(Texture, device) textures;

  Luts luts;
};

struct GmonOptions {
  float cap = 1.0f;
};

}
}

#endif //PLATINUM_PT_SHADER_DEFS_HPP

#pragma clang diagnostic pop
