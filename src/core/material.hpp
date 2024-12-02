#ifndef PLATINUM_MATERIAL_HPP
#define PLATINUM_MATERIAL_HPP

#include <simd/simd.h>

using namespace simd;

namespace pt {

struct Material {
  enum MaterialFlags {
    Material_ThinDelectric = 1 << 0,
    Material_UseAlpha = 1 << 1,
    Material_Emissive = 1 << 2,
    Material_Anisotropic = 1 << 3,
  };
  
  float4 baseColor;
  float3 emission;
  float roughness = 1.0f, metallic = 0.0f, transmission = 0.0f;
  float ior = 1.5f;
  float anisotropy = 0.0f, anisotropyRotation = 0.0f;
  float clearcoat = 0.0f, clearcoatRoughness = 0.03f;
  
  int flags = 0;
  
  uint16_t baseTextureId, rmtcTextureId, emissionTextureId;
};

}

#endif // PLATINUM_MATERIAL_HPP
