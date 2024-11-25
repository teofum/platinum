#ifndef PLATINUM_CAMERA_HPP
#define PLATINUM_CAMERA_HPP

#include <simd/simd.h>

using namespace simd;

namespace pt::renderer_studio {

struct Camera {
  float3 position;
  float3 target;
  float fov;
  float near = 0.1f;
  float far = 100.0f;

  explicit Camera(
    const float3& position,
    const float3& target = {0, 0, 0},
    float fov = 45.0f
  ) noexcept;

  [[nodiscard]] float4x4 view() const;

  [[nodiscard]] float4x4 projection(float aspect) const;

  void orbit(float2 angles);
};

}

#endif //PLATINUM_CAMERA_HPP
