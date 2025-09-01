#ifndef PLATINUM_MATERIAL_HPP
#define PLATINUM_MATERIAL_HPP

#include <simd/simd.h>
#include <unordered_dense.h>

using namespace simd;

namespace pt {

#ifndef __METAL_VERSION__

/*
 * Material struct used for the scene representation
 */
struct Material {
  enum class TextureSlot {
    BaseColor,
    RoughnessMetallic,
    Transmission,
    Clearcoat,
    Emission,
    Normal
  };

  std::string name;

  float4 baseColor = {0.8, 0.8, 0.8, 1.0};
  float3 emission;
  float emissionStrength = 0.0f;
  float roughness = 1.0f, metallic = 0.0f, transmission = 0.0f;
  float ior = 1.5f;
  float anisotropy = 0.0f, anisotropyRotation = 0.0f;
  float clearcoat = 0.0f, clearcoatRoughness = 0.05f;

  bool thinTransmission = false;

  ankerl::unordered_dense::map<TextureSlot, uint64_t> textures;

  constexpr std::optional<uint64_t> getTexture(TextureSlot slot) const {
    if (textures.contains(slot))
      return textures.at(slot);
    return std::nullopt;
  }

  constexpr bool isEmissive() const {
    return length_squared(emission * emissionStrength) > 0.0 ||
           textures.contains(TextureSlot::Emission);
  }
};

#endif

} // namespace pt

#endif // PLATINUM_MATERIAL_HPP
