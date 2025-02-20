#include <metal_stdlib>

// Header files use this guard to only include what the shader needs
#define METAL_SHADER

#include "defs.metal"

using namespace metal;

/*
 * Sampling
 */
namespace samplers {

__attribute__((always_inline))
uint4 pcg4d(uint4 v) {
  v = v * 1664525u + 1013904223u;
  v.x += v.y * v.w; v.y += v.z * v.x; v.z += v.x * v.y; v.w += v.y * v.z;
  v = v ^ (v >> 16u);
  v.x += v.y * v.w; v.y += v.z * v.x; v.z += v.x * v.y; v.w += v.y * v.z;

  return v;
}

__attribute__((always_inline))
float fixedPt2Float(uint32_t v) {
    float f = float(v) * float(2.3283064365386963e-10);
    return min(f, oneMinusEpsilon);
}

__attribute__((always_inline))
uint32_t nextPowerOfTwo(uint32_t v) {
  v--;
  v |= v >> 1;
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  v++;

  return v;
}

__attribute__((always_inline))
uint32_t reverseBits32(uint32_t v) {
  v = (v << 16) | (v >> 16);
  v = ((v & 0x00ff00ff) << 8) | ((v & 0xff00ff00) >> 8);
  v = ((v & 0x0f0f0f0f) << 4) | ((v & 0xf0f0f0f0) >> 4);
  v = ((v & 0x33333333) << 2) | ((v & 0xcccccccc) >> 2);
  v = ((v & 0x55555555) << 1) | ((v & 0xaaaaaaaa) >> 1);
  return v;
}

__attribute__((always_inline))
uint32_t scrambleHash(uint32_t x) {
  x = ((x >> 16) ^ x) * 0x45d9f3b;
  x = ((x >> 16) ^ x) * 0x45d9f3b;
  x = (x >> 16) ^ x;
  return x;
}

ZSampler::ZSampler(uint2 tid, uint2 size, uint32_t spp, uint32_t sample) {
  uint32_t resolution = nextPowerOfTwo(max(size.x, size.y));
  m_log2Resolution = uint32_t(ceil(log2(float(resolution))));
  m_log2spp = uint32_t(ceil(log2(float(spp))));

  m_base4Digits = m_log2Resolution + (m_log2spp + 1) / 2;

  m_z = 0;

  // Calculate the "canonical" Morton curve index for the pixel
  for (uint32_t i = 0; i < m_log2Resolution; i++) {
    m_z |= ((tid.x >> i) & 1) << (2 * i);
    m_z |= ((tid.y >> i) & 1) << (2 * i + 1);
  }

  // Shift back and calculate the full "canonical" index for the sample
  // by appending a gray code for the sample index (it's like a 1D morton curve!)
  uint32_t gray = sample ^ (sample >> 1);
  m_z <<= m_log2spp;
  m_z |= sample;

  if (m_log2spp & 1) {
    m_z <<= 1;
    m_z |= (sample & 1);
  }
}

float ZSampler::sample1d() {
  uint32_t idx = index(m_dim++);
  return fixedPt2Float(sobol(idx, zMatrix1stD));
}

float2 ZSampler::sample2d() {
  uint32_t idx = index(m_dim++);
  return float2(fixedPt2Float(sobol(idx, zMatrix1stD)), fixedPt2Float(sobol(idx, zMatrix2ndD)));
}

__attribute__((always_inline))
uint32_t ZSampler::hash(uint32_t i, uint32_t d) {
  constexpr uint32_t mask = (1 << 24) - 1;
  constexpr uint32_t alpha = 0x9E377A; // Approximates 1 - golden ratio

  i ^= (0x55555555 * d);
  uint32_t x = (i * alpha) & mask; // Fractional part
  return (x * 24) >> 24; // Map to 0-23 range
}

__attribute__((always_inline))
uint32_t ZSampler::index(uint32_t dim) {
  uint32_t z_pi = 0; // Permuted index

  uint32_t lastDigit = m_log2spp & 1;
  for (uint32_t j = lastDigit; j < m_base4Digits; j++) {
    z_pi <<= 2;

    uint32_t shift = m_base4Digits - j - 1;
    uint32_t x = (m_z >> (2 * shift));
    uint32_t digit = x & 3;
    uint32_t prefix = x >> 2;
    digit = c_permutations[hash(prefix, dim)][digit];
    z_pi |= digit;
  }

  if (m_log2spp & 1) {
    uint32_t digit = m_z & 1;
    z_pi <<= 1;
    z_pi |= digit ^ (hash(m_z >> 1, dim) & 1);
  }

  return z_pi;
}

__attribute__((always_inline))
uint32_t ZSampler::sobol(uint32_t index, const constant uint32_t* matrix) {
  uint32_t v = 0;
  for (int i = 0; index; i++, index >>= 1) {
    v ^= matrix[i] * (index & 1);
  }
  return scramble(v, scrambleHash(m_dim));
}

__attribute__((always_inline))
uint32_t ZSampler::scramble(uint32_t v, uint32_t seed) {
  v = reverseBits32(v);
  v ^= v * 0x3d20adea;
  v += seed;
  v *= (seed >> 16) | 1;
  v ^= v * 0x05526c56;
  v ^= v * 0x53a22864;
  return reverseBits32(v);
}

HaltonSampler::HaltonSampler(uint2 tid, uint2 size, uint32_t spp, uint32_t sample) {
  m_offset = pcg4d(uint4(tid, sample, tid.x + tid.y)).x;
}

float HaltonSampler::sample1d() {
  return halton(m_offset, m_dim++);
}

float2 HaltonSampler::sample2d() {
  return float2(halton(m_offset, m_dim++), halton(m_offset, m_dim++));
}

__attribute__((always_inline))
float HaltonSampler::halton(uint32_t i, uint32_t d) {
  uint32_t b = c_primes[d];

  float f = 1.0f;
  float invB = 1.0f / b;

  float r = 0;

  while (i > 0) {
    f = f * invB;
    r = r + f * (i % b);
    i = i / b;
  }

  return min(r, oneMinusEpsilon);
}

PCG4DSampler::PCG4DSampler(uint2 tid, uint2 size, uint32_t spp, uint32_t sample) {
  m_v = pcg4d(uint4(tid, sample, tid.x + tid.y)).x;
}

float PCG4DSampler::sample1d() {
  m_v = pcg4d(m_v);
  return fixedPt2Float(m_v.x);
}

float2 PCG4DSampler::sample2d() {
  m_v = pcg4d(m_v);
  return float2(fixedPt2Float(m_v.x), fixedPt2Float(m_v.y));
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
