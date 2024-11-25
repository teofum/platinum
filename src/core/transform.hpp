#ifndef PLATINUM_TRANSFORM_HPP
#define PLATINUM_TRANSFORM_HPP

#include <simd/simd.h>

#include <utils/matrices.hpp>

using namespace simd;

namespace pt {

enum class TransformType : uint8_t {
  Vector = 0,
  Point = 1,
  Normal = 2,
};

struct Transform {
  float3 translation;
  float3 rotation;
  float3 scale;

  constexpr explicit Transform() noexcept {
    translation = {0, 0, 0};
    rotation = {0, 0, 0};
    scale = {1, 1, 1};
  }

  [[nodiscard]] constexpr float4x4 matrix() const {
    const auto T = mat::translation(translation);
    const auto S = mat::scaling(scale);
    const auto Rx = mat::rotation_x(rotation.x);
    const auto Ry = mat::rotation_y(rotation.y);
    const auto Rz = mat::rotation_z(rotation.z);

    return T * Rz * Rx * Ry * S;
  }

  [[nodiscard]] constexpr float3x3 normalMatrix() const {
    const auto S = simd_diagonal_matrix(scale);
    const auto Rx = mat::rotation3_x(rotation.x);
    const auto Ry = mat::rotation3_y(rotation.y);
    const auto Rz = mat::rotation3_z(rotation.z);

    return transpose(Rz * Rx * Ry * S);
  }

  [[nodiscard]] constexpr float4 operator()(const float4& vec) const {
    return matrix() * vec;
  }

  [[nodiscard]] constexpr float3 operator()(
    const float3& vec,
    TransformType type = TransformType::Vector
  ) const {
    if (type == TransformType::Normal) return normalMatrix() * vec;
    return (matrix() * make_float4(vec, static_cast<float>(type))).xyz;
  }
};

}

#endif //PLATINUM_TRANSFORM_HPP
