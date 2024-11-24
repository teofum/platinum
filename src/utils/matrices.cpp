#include "matrices.hpp"

namespace pt::mat {

float4x4 identity() {
  return {1.0f};
}

float4x4 translation(float3 t) {
  return {
    float4{1.0f, 0.0f, 0.0f, 0.0f},
    float4{0.0f, 1.0f, 0.0f, 0.0f},
    float4{0.0f, 0.0f, 1.0f, 0.0f},
    make_float4(t, 1.0f)
  };
}

float4x4 rotation(float angle, float3 rotationAxis) {
  const float a = angle;
  const float c = cos(a);
  const float s = sin(a);

  const float3 axis = normalize(rotationAxis);
  const float3 temp = (1.0f - c) * axis;

  return {
    float4{
      c + temp.x * axis.x,
      temp.x * axis.y + s * axis.z,
      temp.x * axis.z - s * axis.y,
      0.0f
    },
    float4{
      temp.y * axis.x - s * axis.z,
      c + temp.y * axis.y,
      temp.y * axis.z + s * axis.x,
      0.0f
    },
    float4{
      temp.z * axis.x + s * axis.y,
      temp.z * axis.y - s * axis.x,
      c + temp.z * axis.z,
      0.0f
    },
    float4{0.0f, 0.0f, 0.0f, 1.0f},
  };
}

float4x4 rotation_x(float angle) {
  const float a = angle;
  const float c = cos(a);
  const float s = sin(a);

  return {
    float4{1.0f, 0.0f, 0.0f, 0.0f},
    float4{0.0f, c, s, 0.0f},
    float4{0.0f, -s, c, 0.0f},
    float4{0.0f, 0.0f, 0.0f, 1.0f},
  };
}

float4x4 rotation_y(float angle) {
  const float a = angle;
  const float c = cos(a);
  const float s = sin(a);

  return {
    float4{c, 0.0f, -s, 0.0f},
    float4{0.0f, 1.0f, 0.0f, 0.0f},
    float4{s, 0.0f, c, 0.0f},
    float4{0.0f, 0.0f, 0.0f, 1.0f},
  };
}

float4x4 rotation_z(float angle) {
  const float a = angle;
  const float c = cos(a);
  const float s = sin(a);

  return {
    float4{c, s, 0.0f, 0.0f},
    float4{-s, c, 0.0f, 0.0f},
    float4{0.0f, 0.0f, 1.0f, 0.0f},
    float4{0.0f, 0.0f, 0.0f, 1.0f},
  };
}

float3x3 rotation3_x(float angle) {
  const float a = angle;
  const float c = cos(a);
  const float s = sin(a);

  return {
    float3{1.0f, 0.0f, 0.0f},
    float3{0.0f, c, s},
    float3{0.0f, -s, c},
  };
}

float3x3 rotation3_y(float angle) {
  const float a = angle;
  const float c = cos(a);
  const float s = sin(a);

  return {
    float3{c, 0.0f, -s},
    float3{0.0f, 1.0f, 0.0f},
    float3{s, 0.0f, c},
  };
}

float3x3 rotation3_z(float angle) {
  const float a = angle;
  const float c = cos(a);
  const float s = sin(a);

  return {
    float3{c, s, 0.0f},
    float3{-s, c, 0.0f},
    float3{0.0f, 0.0f, 1.0f},
  };
}

float4x4 scaling(float3 s) {
  return simd_diagonal_matrix(make_float4(s, 1.0f));
}

float4x4 scaling(float s) {
  return simd_diagonal_matrix(float4{s, s, s, 1.0f});
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

