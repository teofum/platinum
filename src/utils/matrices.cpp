#include "matrices.hpp"

namespace mat {
float4x4 identity() {
  return {1.0f};
}

float4x4 translation(float3 t) {
  return {
    float4{1.0f, 0.0f, 0.0f, 0.0f},
    float4{0.0f, 1.0f, 0.0f, 0.0f},
    float4{0.0f, 0.0f, 1.0f, 0.0f},
    float4{t.x, t.y, t.z, 1.0f}
  };
}

float4x4 rotation(float angle, float3 rotationAxis) {
  const float a = angle;
  const float c = cos(a);
  const float s = sin(a);

  const float3 axis = normalize(rotationAxis);
  const float3 temp = (1.0f - c) * axis;

  return {
    float4{c + temp.x * axis.x, temp.x * axis.y + s * axis.z, temp.x * axis.z - s * axis.y, 0.0f},
    float4{temp.y * axis.x - s * axis.z, c + temp.y * axis.y, temp.y * axis.z + s * axis.x, 0.0f},
    float4{temp.z * axis.x + s * axis.y, temp.z * axis.y - s * axis.x, c + temp.z * axis.z, 0.0f},
    float4{0.0f, 0.0f, 0.0f, 1.0f},
  };
}

float4x4 scaling(float3 s) {
  return {
    float4{s.x, 0.0f, 0.0f, 0.0f},
    float4{0.0f, s.y, 0.0f, 0.0f},
    float4{0.0f, 0.0f, s.z, 0.0f},
    float4{0.0f, 0.0f, 0.0f, 1.0f},
  };
}

float4x4 scaling(float s) {
  return {
    float4{s, 0.0f, 0.0f, 0.0f},
    float4{0.0f, s, 0.0f, 0.0f},
    float4{0.0f, 0.0f, s, 0.0f},
    float4{0.0f, 0.0f, 0.0f, 1.0f},
  };
}

float4x4 projection(float fov, float aspect, float near, float far) {
  const float sy = 1.0f / std::tan(fov * 0.5f);
  const float sx = sy / aspect;
  const float zRange = near - far;
  const float sz = (far + near) / zRange;
  const float tz = 2 * far * near / zRange;

  return {
    float4{sx, 0.0f, 0.0f, 0.0f},
    float4{0.0f, sy, 0.0f, 0.0f},
    float4{0.0f, 0.0f, sz, -1.0f},
    float4{0.0f, 0.0f, tz, 0.0f},
  };
}
}

