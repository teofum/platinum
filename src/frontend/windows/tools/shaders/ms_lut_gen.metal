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
  
  inline float avgDielectricFresnelFit(float ior) {
    return ior >= 1.0f
           ? (ior - 1.0f) / (4.08567f + 1.00071f * ior)
           : 0.997118f + 0.1014f * ior - 0.965241 * ior * ior - 0.130607 * ior * ior * ior;
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
    
    __attribute__((always_inline))
    float singleScatterBRDF(float3 wo, float3 wi, float3 wm) const {
      return mdf(wm) * g(wo, wi) / (4 * abs(wo.z) * abs(wi.z));
    }
    
    __attribute__((always_inline))
    float pdf(float3 wo, float3 wm) const {
      return vmdf(wo, wm) / (4.0f * abs(dot(wo, wm)));
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
  
  /*
   * Get an incident light direction by importance sampling the BRDF, and evaluate it
   */
  Sample sampleSingleScatterGGX(float3 wo, thread const GGX& ggx, float2 r) {
    // Sample the microfacet normal, get incident light direction and evaluate the BRDF
    auto wm = ggx.sampleVmdf(wo, r);
    auto wi = reflect(-wo, wm);
    if (wo.z * wi.z < 0.0f) return {
      .wi     = wi,
      .f      = 0.0f,
      .pdf    = 1.0f,
    };
    
    const auto cosTheta_o = abs(wo.z), cosTheta_i = abs(wi.z);
    const auto brdf_ss = ggx.mdf(wm) * ggx.g(wo, wi) / (4 * cosTheta_o * cosTheta_i);
    
    return {
      .wi     = wi,
      .f 			= brdf_ss,
      .pdf 		= ggx.vmdf(wo, wm) / (4.0f * abs(dot(wo, wm))),
    };
  }
  
  /*
   * Get an incident light direction by importance sampling the BRDF, and evaluate it
   */
  Sample sampleMultiscatterDielectricGGX(float3 wo, float ior, float roughness, thread const GGX& ggx, float2 r, thread const texture2d<float>& lutE, thread const texture1d<float>& lutEavg) {
    // Sample the microfacet normal, get incident light direction and evaluate the BRDF
    auto wm = ggx.sampleVmdf(wo, r);
    auto wi = reflect(-wo, wm);
    if (wo.z * wi.z < 0.0f) return {
      .wi     = wi,
      .f      = 0.0f,
      .pdf    = 1.0f,
    };
    
    const auto cosTheta_o = abs(wo.z), cosTheta_i = abs(wi.z);
    const auto brdf_ss = ggx.mdf(wm) * ggx.g(wo, wi) / (4 * cosTheta_o * cosTheta_i);
    const auto fresnel_ss = fresnel(abs(dot(wo, wm)), ior);
    
    // Multiple scattering
    constexpr sampler s(address::clamp_to_edge, filter::linear);
    const auto E_wo = lutE.sample(s, float2(wo.z, roughness)).r;
    const auto E_wi = lutE.sample(s, float2(wi.z, roughness)).r;
    const auto E_avg = lutEavg.sample(s, roughness).r;
    const auto F_avg = avgDielectricFresnelFit(ior);
    
    const auto brdf_ms = (1.0f - E_wo) * (1.0f - E_wi) / (M_PI_F * (1.0f - E_avg));
    const auto fresnel_ms = F_avg * F_avg * E_avg / (1.0f - F_avg * (1.0f - E_avg));
    
    return {
      .wi     = wi,
      .f      = fresnel_ss * brdf_ss + fresnel_ms * brdf_ms,
      .pdf    = ggx.vmdf(wo, wm) / (4.0f * abs(dot(wo, wm))),
    };
  }
  
  /*
   * Get an incident light direction by importance sampling the BSDF, and evaluate it
   */
  Sample sampleTransparentDielectricGGX(float3 wo, thread const GGX& ggx, float ior, float3 r, bool thin = false) {
    // Sample the microfacet normal and evaluate single-scattering fresnel
    const auto wm = ggx.sampleVmdf(wo, r.xy);
    const auto fresnel_ss = fresnel(abs(dot(wo, wm)), ior);
    
    float3 wi;
    if (r.z < fresnel_ss) {
      wi = reflect(-wo, wm);
      if (wo.z * wi.z < 0.0f) return {
        .wi     = wi,
        .f      = 0.0f,
        .pdf    = 1.0f,
      };
    } else if (thin) {
      wi = reflect(-wo, wm) * float3(1.0, 1.0, -1.0);
    } else {
      wi = refract(-wo, wm * sign(dot(wo, wm)), 1.0f / ior);
      if (wo.z * wi.z >= 0.0f) return {
        .wi     = wi,
        .f      = 0.0f,
        .pdf    = 1.0f,
      };
    }
    
    const auto isReflection = wo.z * wi.z > 0.0f;
    
    float bsdf, pdf;
    if (isReflection || thin) {
      bsdf = ggx.singleScatterBRDF(wo, wi, wm);
      pdf = ggx.pdf(wo, wm);
    } else {
      auto denom = dot(wi, wm) * ior + dot(wo, wm);
      denom *= denom;
      
      const auto dwm_dwi = abs(dot(wi, wm)) / denom;
      bsdf = ggx.mdf(wm) * ggx.g(wo, wi) * abs(dot(wi, wm) * dot(wo, wm) / (wi.z * wo.z * denom));
      pdf = ggx.vmdf(wo, wm) * dwm_dwi;
    }
    
    float k = isReflection ? fresnel_ss : 1.0f - fresnel_ss;
    return {
      .wi     = wi,
      .f      = k * bsdf,
      .pdf    = k * pdf,
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
  auto sample = bsdf::sampleSingleScatterGGX(wo, ggx, r);
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
  auto sample = bsdf::sampleSingleScatterGGX(wo, ggx, r.xy);
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

/**
 * Generate a multiscatter directional albedo LUT
 * Tabulates directional albedo E for a multiple scattering dielectric GGX BRDF
 */
kernel void generateMultiscatterDirectionalAlbedoLookup(
  uint3                                                 tid                 [[thread_position_in_grid]],
  constant uint32_t&                                    size                [[buffer(0)]],
  constant uint32_t&                                    frameIdx            [[buffer(1)]],
  texture3d<float>                                      src                 [[texture(0)]],
  texture3d<float, access::write>                       dst                 [[texture(1)]],
  texture3d<uint32_t>                                   randomTex           [[texture(2)]],
  texture2d<float>                                   		lutE           			[[texture(3)]],
  texture1d<float>                                   		lutEavg           	[[texture(4)]]
) {
  /*
   * Calculate parameters
   */
  float iorParam = ((float) tid.z + 0.5f) / (float) size;
  float roughness = ((float) tid.y + 0.5f) / (float) size;
  float cosTheta = ((float) tid.x + 0.5f) / (float) size;
  
  // Inverse of the IOR parametrization. We use (eta - 1) / eta, which has no physical meaning but
  // gives a more useful curve for the common IOR range 1 < eta < 2 than F0.
  // See https://www.desmos.com/calculator/1pkvgrisbx for details.
  float ior = 1.0f / (1.0f - iorParam);
  
  /*
   * Create GGX distribution
   */
  bsdf::GGX ggx(roughness);

  /*
   * Sample a random value
   */
  uint32_t offset = randomTex.read(tid).x;
  float2 r(samplers::halton(offset + frameIdx, 0),
           samplers::halton(offset + frameIdx, 1));
  
  /*
   * Get outgoing light dir
   */
  float sinTheta = sqrt(1.0f - cosTheta * cosTheta);
  float3 wo(sinTheta, 0.0, cosTheta);
  
  /*
   * Sample the distribution for directional albedo
   */
  auto sample = bsdf::sampleMultiscatterDielectricGGX(wo, ior, roughness, ggx, r, lutE, lutEavg);
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
 * Generate a multiscatter hemispherical albedo LUT
 * Tabulates hemispherical albedo E_avg for a multiple scattering dielectric GGX BRDF
 */
kernel void generateMultiscatterHemisphericalAlbedoLookup(
  uint2                                                 tid                 [[thread_position_in_grid]],
  constant uint32_t&                                    size                [[buffer(0)]],
  constant uint32_t&                                    frameIdx            [[buffer(1)]],
  texture2d<float>                                      src                 [[texture(0)]],
  texture2d<float, access::write>                       dst                 [[texture(1)]],
  texture2d<uint32_t>                                   randomTex           [[texture(2)]],
  texture2d<float>                                      lutE                [[texture(3)]],
  texture1d<float>                                      lutEavg             [[texture(4)]]
) {
  /*
   * Calculate parameters
   */
  float roughness = ((float) tid.y + 0.5f) / (float) size;
  float iorParam = ((float) tid.x + 0.5f) / (float) size;
  
  // Inverse of the IOR parametrization. We use (eta - 1) / eta, which has no physical meaning but
  // gives a more useful curve for the common IOR range 1 < eta < 2 than F0.
  // See https://www.desmos.com/calculator/1pkvgrisbx for details.
  float ior = 1.0f / (1.0f - iorParam);
  
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
  auto sample = bsdf::sampleMultiscatterDielectricGGX(wo, ior, roughness, ggx, r.xy, lutE, lutEavg);
  auto v = 2.0f * sample.f * abs(sample.wi.z) * abs(wo.z) / sample.pdf;
  
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
 * Tabulates directional albedo E for a transparent dielectric GGX BSDF
 */
void generateTransparentDirectionalAlbedoLookup(
  uint3                            	tid,
  constant uint32_t&               	size,
  constant uint32_t&               	frameIdx,
  texture3d<float>                 	src,
  texture3d<float, access::write>  	dst,
  texture3d<uint32_t>              	randomTex,
  bool 															out
) {
  /*
   * Calculate parameters
   */
  float iorParam = ((float) tid.z + 0.5f) / (float) size;
  float roughness = ((float) tid.y + 0.5f) / (float) size;
  float cosTheta = ((float) tid.x + 0.5f) / (float) size;
  
  // Inverse of the IOR parametrization. We use (eta - 1) / eta, which has no physical meaning but
  // gives a more useful curve for the common IOR range 1 < eta < 2 than F0.
  // For eta < 1, we simply use 1 - eta as this fits the entire 0 < eta < 1 range nicely.
  float ior = out ? 1.0f - iorParam : 1.0f / (1.0f - iorParam);
  
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
  float sinTheta = sqrt(1.0f - cosTheta * cosTheta);
  float3 wo(sinTheta, 0.0, cosTheta * (out ? -1 : 1));
  
  /*
   * Sample the distribution for directional albedo
   */
  auto sample = bsdf::sampleTransparentDielectricGGX(wo, ggx, ior, r);
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

kernel void generateTransparentDirectionalAlbedoInLookup(
  uint3                                                 tid                 [[thread_position_in_grid]],
  constant uint32_t&                                    size                [[buffer(0)]],
  constant uint32_t&                                    frameIdx            [[buffer(1)]],
  texture3d<float>                                      src                 [[texture(0)]],
  texture3d<float, access::write>                       dst                 [[texture(1)]],
  texture3d<uint32_t>                                   randomTex           [[texture(2)]]
) {
  generateTransparentDirectionalAlbedoLookup(tid, size, frameIdx, src, dst, randomTex, false);
}

kernel void generateTransparentDirectionalAlbedoOutLookup(
  uint3                                                 tid                 [[thread_position_in_grid]],
  constant uint32_t&                                    size                [[buffer(0)]],
  constant uint32_t&                                    frameIdx            [[buffer(1)]],
  texture3d<float>                                      src                 [[texture(0)]],
  texture3d<float, access::write>                       dst                 [[texture(1)]],
  texture3d<uint32_t>                                   randomTex           [[texture(2)]]
) {
  generateTransparentDirectionalAlbedoLookup(tid, size, frameIdx, src, dst, randomTex, true);
}

/**
 * Tabulates directional albedo E for a transparent dielectric GGX BSDF
 */
void generateTransparentHemisphericalAlbedoLookup(
  uint2                              tid,
  constant uint32_t&                 size,
  constant uint32_t&                 frameIdx,
  texture2d<float>                   src,
  texture2d<float, access::write>    dst,
  texture2d<uint32_t>                randomTex,
  bool                               out
) {
  /*
   * Calculate parameters
   */
  float roughness = ((float) tid.y + 0.5f) / (float) size;
  float iorParam = ((float) tid.x + 0.5f) / (float) size;
  
  // Inverse of the IOR parametrization. We use (eta - 1) / eta, which has no physical meaning but
  // gives a more useful curve for the common IOR range 1 < eta < 2 than F0.
  // For eta < 1, we simply use 1 - eta as this fits the entire 0 < eta < 1 range nicely.
  float ior = out ? 1.0f - iorParam : 1.0f / (1.0f - iorParam);
  
  /*
   * Create GGX distribution
   */
  bsdf::GGX ggx(roughness);

  /*
   * Sample a random value
   */
  uint32_t offset = randomTex.read(tid).x;
  float4 r(samplers::halton(offset + frameIdx, 0),
           samplers::halton(offset + frameIdx, 1),
           samplers::halton(offset + frameIdx, 2),
           samplers::halton(offset + frameIdx, 2));
  
  /*
   * Get outgoing light dir
   */
  float cosTheta = r.w * 2.0f - 1.0f;
  float sinTheta = sqrt(1.0f - cosTheta * cosTheta);
  float3 wo(sinTheta, 0.0, cosTheta);
  
  /*
   * Sample the distribution for directional albedo
   */
  auto sample = bsdf::sampleTransparentDielectricGGX(wo, ggx, ior, r.xyz);
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

kernel void generateTransparentHemisphericalAlbedoInLookup(
  uint2                                                 tid                 [[thread_position_in_grid]],
  constant uint32_t&                                    size                [[buffer(0)]],
  constant uint32_t&                                    frameIdx            [[buffer(1)]],
  texture2d<float>                                      src                 [[texture(0)]],
  texture2d<float, access::write>                       dst                 [[texture(1)]],
  texture2d<uint32_t>                                   randomTex           [[texture(2)]]
) {
  generateTransparentHemisphericalAlbedoLookup(tid, size, frameIdx, src, dst, randomTex, false);
}

kernel void generateTransparentHemisphericalAlbedoOutLookup(
  uint2                                                 tid                 [[thread_position_in_grid]],
  constant uint32_t&                                    size                [[buffer(0)]],
  constant uint32_t&                                    frameIdx            [[buffer(1)]],
  texture2d<float>                                      src                 [[texture(0)]],
  texture2d<float, access::write>                       dst                 [[texture(1)]],
  texture2d<uint32_t>                                   randomTex           [[texture(2)]]
) {
  generateTransparentHemisphericalAlbedoLookup(tid, size, frameIdx, src, dst, randomTex, true);
}
