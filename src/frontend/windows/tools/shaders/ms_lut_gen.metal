#include <metal_stdlib>

// Header files use this guard to only include what the shader needs
#define METAL_SHADER

using namespace metal;

/*
 * Sampling
 */
namespace samplers {
  constant unsigned int primes[] = {
     2,    3,   5,   7,  11,  13,  17,  19,
     23,  29,  31,  37,  41,  43,  47,  53,
     59,  61,  67,  71,  73,  79,  83,  89,
     97, 101, 103, 107, 109, 113, 127, 131,
    137, 139, 149, 151, 157, 163, 167, 173,
     179, 181, 191, 193, 197, 199, 211, 223,
    227, 229, 233, 239, 241, 251, 257, 263,
    269, 271, 277, 281, 283, 293, 307, 311,
    313, 317, 331, 337, 347, 349, 353, 359,
    367, 373, 379, 383, 389, 397, 401, 409,
    419, 421, 431, 433, 439, 443, 449, 457,
    461, 463, 467, 479, 487, 491, 499, 503,
    509, 521, 523, 541, 547, 557, 563, 569,
    571, 577, 587, 593, 599, 601, 607, 613,
    617, 619, 631, 641, 643, 647, 653, 659,
  };
  
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

  inline float2 sampleDisk(float2 u) {
    const auto r = sqrt(u.x);
    const auto theta = 2.0f * M_PI_F * u.y;
    
    float cos;
    float sin = sincos(theta, cos);
    return float2(r * cos, r * sin);
  }
  
  inline float3 sampleCosineHemisphere(float2 u) {
    const auto phi = u.x * 2.0f * M_PI_F;
    const auto sinTheta = sqrt(u.y);
    const auto cosTheta = sqrt(1.0f - u.y);
    
    float cosPhi;
    float sinPhi = sincos(phi, cosPhi);
    
    return float3(cosPhi * sinTheta, sinPhi * sinTheta, cosTheta);
  }
  
  inline float2 sampleTriUniform(float2 u) {
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

/*
 * Simplified single scatter GGX BSDF implementation
 */
namespace bsdf {
  /*
   * Schlick fresnel approximation for conductors
   */
  inline float schlick(float f0, float cosTheta) {
    const auto k = 1.0f - cosTheta;
    const auto k2 = k * k;
    return f0 + (1.0f - f0) * (k2 * k2 * k);
  }
  
  /*
   * Real fresnel equations for dielectrics
   * We don't use the complex equations for conductors, as they're meaningless in RGB rendering
   */
  inline float fresnel(float cosTheta, float ior) {
    cosTheta = saturate(cosTheta);
    
    const auto sin2Theta_t = (1.0f - cosTheta * cosTheta) / (ior * ior);
    if (sin2Theta_t >= 1.0f) return 1.0f;
    
    const auto cosTheta_t = sqrt(1.0f - sin2Theta_t);
    const auto parallel = (ior * cosTheta - cosTheta_t) / (ior * cosTheta + cosTheta_t);
    const auto perpendicular = (cosTheta - ior * cosTheta_t) / (cosTheta + ior * cosTheta_t);
    return (parallel * parallel + perpendicular * perpendicular) * 0.5f;
  }
  
  /*
   * Trowbridge-Reitz GGX microfacet distribution implementation
   * Simple wrapper around all the GGX sampling functions, supports anisotropy. Works with all
   * directions in tangent space (Z-up, aligned to surface normal)
   */
  class GGX {
  public:
    explicit GGX(float roughness) noexcept
    : m_alpha(roughness * roughness) {}
    
    GGX(float roughness, float anisotropic) noexcept {
      const auto alpha = roughness * roughness;
      const auto aspect = sqrt(1.0f - 0.9f * anisotropic);
      m_alpha = float2(alpha / aspect, alpha * aspect);
    }
    
    // Microfacet distribution function
    inline float mdf(float3 w) const {
      const auto cos2Theta = w.z * w.z;
      const auto sin2Theta = max(0.0f, 1.0f - cos2Theta);
      const auto tan2Theta = sin2Theta / cos2Theta;
      
      const auto cos4Theta = cos2Theta * cos2Theta;
      auto k = tan2Theta;
      
      if (m_alpha.x != m_alpha.y) {
        const auto cos2Phi = sin2Theta == 0.0f ? 1.0f : w.x * w.x / sin2Theta;
        const auto sin2Phi = sin2Theta == 0.0f ? 1.0f : w.y * w.y / sin2Theta;
        k *= (cos2Phi / (m_alpha.x * m_alpha.x) + sin2Phi / (m_alpha.y * m_alpha.y));
      } else {
        k /= (m_alpha.x * m_alpha.x);
      }
      
      k = (1.0f + k) * (1.0f + k);
      return 1.0f / (M_PI_F * m_alpha.x * m_alpha.y * cos4Theta * k);
    }
    
    // Smith approximation for G1 masking function
    inline float g1(float3 w) const {
      return 1.0f / (1.0f + lambda(w));
    }
    
    // Smith approximation for masking+shadowing function
    inline float g(float3 wo, float3 wi) const {
      return 1.0f / (1.0f + lambda(wo) + lambda(wi));
    }
    
    // Visible microfacet distribution function
    inline float vmdf(float3 w, float3 wm) const {
      return g1(w) / abs(w.z) * mdf(wm) * abs(dot(w, wm));
    }
    
    // Visible microfacet distribution function, shortcut version
    // This overload lets us avoid evaluating the MDF twice if we already have its value from earlier
    inline float vmdf(float3 w, float3 wm, float mdf) const {
      return g1(w) / abs(w.z) * mdf * abs(dot(w, wm));
    }
    
    // Sample a microfacet from the visible distribution
    inline float3 sampleVmdf(float3 w, float2 u) const {
      auto wh = normalize(w * float3(m_alpha, 1.0));
      if (wh.z < 0) wh *= -1.0;
      
      const auto b = (wh.z < 0.9999f) ? normalize(cross(float3(0.0, 0.0, 1.0), wh)) : float3(1.0, 0.0, 0.0);
      const auto t = cross(wh, b);
      
      auto p = samplers::sampleDisk(u);
      const auto h = sqrt(1.0f - p.x * p.x);
      p.y = mix(h, p.y, 0.5f * wh.z + 0.5f);
      
      const auto pz = sqrt(max(0.0f, 1.0f - length_squared(p)));
      const auto nh = p.x * b + p.y * t + pz * wh;
      
      return normalize(float3(m_alpha.x * nh.x, m_alpha.y * nh.y, max(1e-6f, nh.z)));
    }
    
    // Surfaces with a very small roughness value are considered perfect specular, this helps deal
    // with the numerical instability at such values and makes almost no visible difference
    inline bool isSmooth() const {
      return m_alpha.x < 1e-3f && m_alpha.y < 1e-3f;
    }
    
  private:
    float2 m_alpha;
    
    // Implementation detail for the masking and shadowing functions
    inline float lambda(float3 w) const {
      const auto cos2Theta = w.z * w.z;
      const auto sin2Theta = 1.0f - cos2Theta;
      const auto tan2Theta = sin2Theta / cos2Theta;
      
      auto alpha2 = m_alpha.x * m_alpha.x;
      if (m_alpha.x != m_alpha.y) {
        const auto cos2Phi = sin2Theta == 0.0f ? 1.0f : w.x * w.x / sin2Theta;
        const auto sin2Phi = sin2Theta == 0.0f ? 1.0f : w.y * w.y / sin2Theta;
        alpha2 = alpha2 * cos2Phi + m_alpha.y * m_alpha.y * sin2Phi;
      }
      
      return (sqrt(1.0f + alpha2 * tan2Theta) - 1.0f) * 0.5f;
    }
  };
  
  /*
   * Holds all relevant BSDF sample information
   */
  struct Sample {
    float3 wi;
    float f;
    float pdf;
  };
  
  struct Eval {
    float f;
    float pdf = 0.0f;
  };
  
  /*
   * Evaluate GGX conductor BRDF and PDF
   * Internal version, lets us skip the microfacet normal calculation if we already have it (i.e.
   * if we just sampled the VMDF)
   */
  __attribute__((always_inline))
  Eval evalMetallic(float3 wo, float3 wi, float3 wm, thread const GGX& ggx) {
    const auto fresnel_ss = schlick(1.0f, abs(dot(wo, wm)));
    
    const auto cosTheta_o = abs(wo.z), cosTheta_i = abs(wi.z);
    const auto brdf_ss = ggx.mdf(wm) * ggx.g(wo, wi) / (4 * cosTheta_o * cosTheta_i);
    
    return {
      .f = fresnel_ss * brdf_ss,
      .pdf = ggx.vmdf(wo, wm) / (4.0f * abs(dot(wo, wm))),
    };
  }
  
  /*
   * Evaluate GGX conductor BRDF and PDF
   */
  Eval evalMetallic(float3 wo, float3 wi, thread const GGX& ggx) {
    if (ggx.isSmooth()) return {};
    
    auto wm = wo + wi;
    if (length_squared(wm) == 0.0f) return {};
    wm = normalize(wm * sign(wm.z));
    
    return evalMetallic(wo, wi, wm, ggx);
  }
  
  /*
   * Get an incident light direction by importance sampling the BRDF, and evaluate it
   */
  Sample sampleMetallic(float3 wo, thread const GGX& ggx, float2 r) {
    // Sample the microfacet normal, get incident light direction and evaluate the BRDF
    auto wm = ggx.sampleVmdf(wo, r);
    auto wi = reflect(-wo, wm);
    if (wo.z * wi.z < 0.0f) return {
      .wi     = wi,
      .f      = 0.0f,
      .pdf    = 1.0f,
    };
    
    const auto eval = evalMetallic(wo, wi, wm, ggx);
    
    return {
      .wi     = wi,
      .f      = eval.f,
      .pdf    = eval.pdf,
    };
  }
}

/**
 * Generate a directional albedo LUT
 * Tabulates directional albedo E (integral over the hemisphere of light from all directions reflected towards one direction)
 */
kernel void generateDirectionalAlbedoLookup(
  uint2                                                 tid                 [[thread_position_in_grid]],
  constant uint32_t&                                    size                [[buffer(0)]],
  constant uint32_t&                                   	frameIdx         		[[buffer(1)]],
  texture2d<float>                       								src                 [[texture(0)]],
  texture2d<float, access::write>                       dst                 [[texture(1)]],
  texture2d<uint32_t>                                   randomTex           [[texture(2)]]
) {
  /*
   * Calculate parameters
   */
  float roughness = ((float) tid.y + 0.5f) / (float) size;
  float cosTheta = ((float) tid.x + 0.5f) / (float) size;
  
  /*
   * Create GGX distribution
   */
  bsdf::GGX ggx(roughness);
  
  /*
   * Get outgoing light dir
   */
  float sinTheta = sqrt(1.0f - cosTheta * cosTheta);
  float3 wo(sinTheta, 0.0, cosTheta);

  /*
   * Sample a random value
   */
  uint32_t offset = randomTex.read(tid).x;
  float2 r(samplers::halton(offset + frameIdx, 0),
           samplers::halton(offset + frameIdx, 1));
  
  /*
   * Sample the distribution for directional albedo
   */
  auto sample = bsdf::sampleMetallic(wo, ggx, r);
  auto v = sample.f * abs(sample.wi.z) / sample.pdf;
  
  /*
   * Accumulate samples
   */
  if (frameIdx > 0) {
    float prev = src.read(tid).r;
    
    v += prev * frameIdx;
    v /= (frameIdx + 1);
  }
  
  dst.write(float4(v, v, v, 1.0), tid);
}

/**
 * Generate a hemispherical albedo LUT
 * Tabulates hemispherical albedo E_avg (integral over the hemisphere of directional albedo E)
 */
kernel void generateHemisphericalAlbedoLookup(
  uint2                                              		tid                 [[thread_position_in_grid]],
  constant uint32_t&                                    size                [[buffer(0)]],
  constant uint32_t&                                    frameIdx            [[buffer(1)]],
  texture2d<float>                                      src                 [[texture(0)]],
  texture2d<float, access::write>                       dst                 [[texture(1)]],
  texture2d<uint32_t>                                   randomTex           [[texture(2)]]
) {
  /*
   * Calculate parameters
   */
  float roughness = ((float) tid.x + 0.5f) / (float) size;
  
  /*
   * Create GGX distribution
   */
  bsdf::GGX ggx(roughness);

  /*
   * Sample a random value
   */
  uint32_t offset = randomTex.read(tid).x;
  float3 r(samplers::halton(offset + frameIdx, 0),
           samplers::halton(offset + frameIdx, 1),
           samplers::halton(offset + frameIdx, 2));
  
  /*
   * Get outgoing light dir
   */
  float cosTheta = r.z;
  float sinTheta = sqrt(1.0f - cosTheta * cosTheta);
  float3 wo(sinTheta, 0.0, cosTheta);
  
  /*
   * Sample the distribution for directional albedo
   */
  auto sample = bsdf::sampleMetallic(wo, ggx, r.xy);
  auto v = 2.0f * sample.f * abs(sample.wi.z) * wo.z / sample.pdf;
  
  /*
   * Accumulate samples
   */
  if (frameIdx > 0) {
    float prev = src.read(tid).r;
    
    v += prev * frameIdx;
    v /= (frameIdx + 1);
  }
  
  dst.write(float4(v, v, v, 1.0), tid);
}
