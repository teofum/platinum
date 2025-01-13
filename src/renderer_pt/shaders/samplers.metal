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
  
  /*
   * Sampling function for a 1D piecewise constant distribution. Code based on PBRT.
   * https://pbr-book.org/4ed/Sampling_Algorithms/Sampling_1D_Functions#SamplingPiecewise-Constant1DFunctions
   */
  float sampleDistribution1D(device Distribution1D& dist, float r, thread float* pdf, thread uint32_t* offset) {
    // Find CDF segment to sample
    int64_t size = int64_t(dist.size - 2), first = 1, h, middle;

    while (size > 0) {
      h = size >> 1;
      middle = first + h;
      if (dist.cdf[middle] < r) {
        first = middle + 1;
        size -= h + 1;
      } else {
        size = h;
      }
    }
    int64_t o = clamp(first - 1, 0l, int64_t(dist.size) - 2);
    if (offset) *offset = uint32_t(o);
    
    // Calculate offset along segment
    float dr = r - dist.cdf[o];
    if (dist.cdf[0 + 1] - dist.cdf[o] > 0) dr /= dist.cdf[0 + 1] - dist.cdf[o];
    if (pdf) *pdf = (dist.integral > 0) ? dist.f[o] / dist.integral : 0.0f;

    // Remap sampled x value
    return mix(dist.min, dist.max, (float(o) + dr) / float(dist.size));
  }
  
  /*
   * Sampling function for a 2D piecewise constant distribution. Code based on PBRT.
   * https://pbr-book.org/4ed/Sampling_Algorithms/Sampling_Multidimensional_Functions#Piecewise-Constant2DDistributions
   */
  float2 sampleDistribution2D(device Distribution2D& dist, float2 r, thread float* pdf) {
    float pdfs[2];
    
    uint32_t offset;
    float d1 = sampleDistribution1D(dist.marginal, r.y, &pdfs[1], &offset);
    float d0 = sampleDistribution1D(dist.conditional[offset], r.x, &pdfs[0]);

    if (pdf) *pdf = pdfs[0] * pdfs[1];
    return {d0, d1};
  }
  
  float pdfDistribution2D(device Distribution2D& dist, float2 uv) {
    float2 p = (uv - dist.min) / (dist.max - dist.min);
    uint32_t iu = clamp(
      uint32_t(p.x * float(dist.conditional[0].size)),
      0u,
      dist.conditional[0].size - 1
    );
    uint32_t iv = clamp(
      uint32_t(p.y * float(dist.marginal.size)),
      0u,
      dist.marginal.size - 1
    );

    return dist.conditional[iv].f[iu] / dist.marginal.integral;
  }
}
