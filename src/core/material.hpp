#ifndef PLATINUM_MATERIAL_HPP
#define PLATINUM_MATERIAL_HPP

#include <simd/simd.h>

using namespace simd;

namespace pt {

struct Material {
  enum MaterialFlags {
    Material_ThinDielectric = 1 << 0,
    Material_UseAlpha = 1 << 1,
    Material_Emissive = 1 << 2,
    Material_Anisotropic = 1 << 3,
  };
  
  float4 baseColor = {0.8, 0.8, 0.8, 1.0};
  float3 emission;
  float emissionStrength = 0.0f;
  float roughness = 1.0f, metallic = 0.0f, transmission = 0.0f;
  float ior = 1.5f;
  float anisotropy = 0.0f, anisotropyRotation = 0.0f;
  float clearcoat = 0.0f, clearcoatRoughness = 0.05f;
  
  int flags = 0;
  
  int32_t baseTextureId = -1, rmTextureId = -1, transmissionTextureId = -1, clearcoatTextureId = -1, emissionTextureId = -1;
};

}

#endif // PLATINUM_MATERIAL_HPP
