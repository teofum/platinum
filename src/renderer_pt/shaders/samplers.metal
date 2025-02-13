#include <metal_stdlib>

// Header files use this guard to only include what the shader needs
#define METAL_SHADER

#include "defs.metal"

using namespace metal;

/*
 * Sampling
 */
namespace samplers {
  float halton(unsigned int i, unsigned int d) {
    unsigned int b = primes[d];
  
    float f = 1.0f;
    float invB = 1.0f / b;
  
    float r = 0;
  
    while (i > 0) {
      f = f * invB;
      r = r + f * (i % b);
      i = i / b;
    }
  
    return r;
  }

  float2 sampleDisk(float2 u) {
    const auto r = sqrt(u.x);
    const auto theta = 2.0f * M_PI_F * u.y;
    
    float cos;
    float sin = sincos(theta, cos);
    return float2(r * cos, r * sin);
  }
  
  float3 sampleCosineHemisphere(float2 u) {
    const auto phi = u.x * 2.0f * M_PI_F;
    const auto sinTheta = sqrt(u.y);
    const auto cosTheta = sqrt(1.0f - u.y);
    
    float cosPhi;
    float sinPhi = sincos(phi, cosPhi);
    
    return float3(cosPhi * sinTheta, sinPhi * sinTheta, cosTheta);
  }
  
  float2 sampleTriUniform(float2 u) {
    float b0, b1;
    if (u.x < u.y) {
      b0 = u.x * 0.5f;
      b1 = u.y - b0;
    } else {
      b1 = u.y * 0.5f;
      b0 = u.x - b1;
    }
    
    return float2(b0, b1);
  }
}
