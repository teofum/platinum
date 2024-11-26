#include "camera.hpp"

#include <utils/matrices.hpp>

namespace pt::renderer_studio {

Camera::Camera(
  const float3& position,
  const float3& target,
  float fov
) noexcept: position(position), target(target), fov(fov) {
}

float4x4 Camera::view() const {
  return mat::lookAt(position, target, {0, 1, 0});
}

float4x4 Camera::projection(float aspect) const {
  float far = max(distance(position, target) * 2, 1000.0f);
  return mat::perspective(fov, aspect, near, far);
}

void Camera::orbit(float2 angles) {
  float3 viewDirection = normalize(target - position);
  if (viewDirection.y > 0.99f && angles.y > 0) { angles.y = 0; }
  if (viewDirection.y < -0.99f && angles.y < 0) { angles.y = 0; }

  if (length_squared(angles) < 0.00001f) return;

  auto right = cross(viewDirection, float3{0, 1, 0});
  auto up = cross(right, viewDirection);

  auto axis = normalize(up * angles.x + right * angles.y);
  float sinTheta = sqrt(1.0f - viewDirection.y * viewDirection.y);
  auto rotation = mat::rotation(length(angles) * sinTheta, axis);

  float4 posFromTarget = rotation * make_float4(position - target, 1.0f);
  position = posFromTarget.xyz + target;
}

void Camera::moveTowardTarget(float amt) {
  position = target + (position - target) * (1.0f - amt);
}

}
