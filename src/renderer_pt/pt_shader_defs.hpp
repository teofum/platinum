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

#ifdef __METAL_VERSION__
#define address_space(space) space
#else
#define address_space(space)
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

struct EnvironmentLight {
  uint32_t textureIdx;
  metal_ptr(AliasEntry, device) alias;
};

enum RendererFlags {
  RendererFlags_None = 0,
  RendererFlags_MultiscatterGGX = 1 << 0,
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

}

namespace postprocess {

namespace agx {

struct Look {
  float3 offset, slope, power;
  float saturation;
};

namespace looks {

address_space(constant) constexpr const Look none = {
  .offset = float3(0.0),
  .slope = float3(1.0),
  .power = float3(1.0),
  .saturation = 1.0,
};

address_space(constant) constexpr const Look golden = {
  .offset = float3(0.0),
  .slope = float3{1.0, 0.9, 0.5},
  .power = float3(0.8),
  .saturation = 0.8,
};

address_space(constant) constexpr const Look punchy = {
  .offset = float3(0.0),
  .slope = float3(1.0),
  .power = float3(1.35),
  .saturation = 1.4,
};

}

struct Options {
  Look look = looks::none;
};

}

namespace khronos_pbr {

struct Options {
  float compressionStart = 0.8;
  float desaturation = 0.15;
};

}

namespace flim {

struct Options {
  float preExposure;
  float3 preFormationFilter;
  float preFormationFilterStrength;

  float3 extendedGamutScale;
  float3 extendedGamutRotation;
  float3 extendedGamutMul;

  float sigmoidLog2Min;
  float sigmoidLog2Max;
  float2 sigmoidToe;
  float2 sigmoidShoulder;

  float negativeExposure;
  float negativeDensity;

  float3 printBacklight;
  float printExposure;
  float printDensity;

  float blackPoint; // -1 = auto
  float3 postFormationFilter;
  float postFormationFilterStrength;

  float midtoneSaturation;
};

namespace presets {

address_space(constant) constexpr const Options flim{
  .preExposure = 4.3,
  .preFormationFilter = {1.0, 1.0, 1.0},
  .preFormationFilterStrength = 0.0,

  .extendedGamutScale = {1.05, 1.12, 1.045},
  .extendedGamutRotation = {0.5, 2.0, 0.1},
  .extendedGamutMul = {1.0, 1.0, 1.0},

  .sigmoidLog2Min = -10.0,
  .sigmoidLog2Max = 22.0,
  .sigmoidToe = {0.440, 0.280},
  .sigmoidShoulder = {0.591, 0.779},

  .negativeExposure = 6.0,
  .negativeDensity = 5.0,

  .printBacklight = {1.0, 1.0, 1.0},
  .printExposure = 6.0,
  .printDensity = 27.5,

  .blackPoint = -1.0,
  .postFormationFilter = {1.0, 1.0, 1.0},
  .postFormationFilterStrength = 0.0,

  .midtoneSaturation = 1.02,
};

address_space(constant) constexpr const Options silver{
  .preExposure = 3.9,
  .preFormationFilter = {0.0, 0.5, 1.0},
  .preFormationFilterStrength = 0.05,

  .extendedGamutScale = {1.05, 1.12, 1.045},
  .extendedGamutRotation = {0.5, 2.0, 0.1},
  .extendedGamutMul = {1.0, 1.0, 1.06},

  .sigmoidLog2Min = -10.0,
  .sigmoidLog2Max = 22.0,
  .sigmoidToe = {0.440, 0.280},
  .sigmoidShoulder = {0.591, 0.779},

  .negativeExposure = 4.7,
  .negativeDensity = 7.0,

  .printBacklight = {0.9992, 0.99, 1.0},
  .printExposure = 4.7,
  .printDensity = 30.0,

  .blackPoint = 0.5,
  .postFormationFilter = {1.0, 1.0, 0.0},
  .postFormationFilterStrength = 0.04,

  .midtoneSaturation = 1.0,
};

}

}

enum class Tonemap {
  None,
  AgX,
  KhronosPBR,
  flim,
};

struct TonemapOptions {
  Tonemap tonemapper = Tonemap::AgX;
  agx::Options agxOptions;
  khronos_pbr::Options khrOptions;
  flim::Options flimOptions = flim::presets::flim;
};

struct PostProcessOptions {
  float exposure = 0.0f;
  TonemapOptions tonemap;
};

}
}

#endif //PLATINUM_PT_SHADER_DEFS_HPP

#pragma clang diagnostic pop
