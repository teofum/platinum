#ifndef PLATINUM_MATRICES_HPP
#define PLATINUM_MATRICES_HPP

#include <simd/simd.h>

using namespace simd;

namespace pt::mat {

float4x4 identity();

float4x4 translation(float3 t);

float4x4 rotation(float angle, float3 axis);

float4x4 rotation_x(float angle);

float4x4 rotation_y(float angle);

float4x4 rotation_z(float angle);

float3x3 rotation3_x(float angle);

float3x3 rotation3_y(float angle);

float3x3 rotation3_z(float angle);

float4x4 scaling(float3 s);

float4x4 scaling(float s);

float4x4 projection(float fov, float aspect, float near, float far);

}

#endif //PLATINUM_MATRICES_HPP
