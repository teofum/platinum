#ifndef PLATINUM_METAL_DEFS_H
#define PLATINUM_METAL_DEFS_H

#include <metal_stdlib>

// Header files use this guard to only include what the shader needs
#define METAL_SHADER

#include "../../core/material.hpp"
#include "../pt_shader_defs.hpp"

using namespace metal;
using namespace pt::shaders_pt;

/*
 * Sampling
 */
namespace samplers {
	constant constexpr unsigned int primes[] = {
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
	  661, 673, 677, 683, 691, 701, 709, 719,
	  727, 733, 739, 743, 751, 757, 761, 769,
	  773, 787, 797, 809, 811, 821, 823, 827,
	  829, 839, 853, 857, 859, 863, 877, 881,
	  883, 887, 907, 911, 919, 929, 937, 941,
	  947, 953, 967, 971, 977, 983, 991, 997,
	  1009, 1013, 1019, 1021, 1031, 1033, 1039, 1049,
	  1051, 1061, 1063, 1069, 1087, 1091, 1093, 1097,
	  1103, 1109, 1117, 1123, 1129, 1151, 1153, 1163,
	  1171, 1181, 1187, 1193, 1201, 1213, 1217, 1223,
	  1229, 1231, 1237, 1249, 1259, 1277, 1279, 1283,
	  1289, 1291, 1297, 1301, 1303, 1307, 1319, 1321,
	  1327, 1361, 1367, 1373, 1381, 1399, 1409, 1423,
	  1427, 1429, 1433, 1439, 1447, 1451, 1453, 1459,
	  1471, 1481, 1483, 1487, 1489, 1493, 1499, 1511,
	  1523, 1531, 1543, 1549, 1553, 1559, 1567, 1571,
	  1579, 1583, 1597, 1601, 1607, 1609, 1613, 1619,
	  1621, 1627, 1637, 1657, 1663, 1667, 1669, 1693,
	  1697, 1699, 1709, 1721, 1723, 1733, 1741, 1747,
	  1753, 1759, 1777, 1783, 1787, 1789, 1801, 1811,
	  1823, 1831, 1847, 1861, 1867, 1871, 1873, 1877,
	  1879, 1889, 1901, 1907, 1913, 1931, 1933, 1949,
	  1951, 1973, 1979, 1987, 1993, 1997, 1999, 2003,
	  2011, 2017, 2027, 2029, 2039, 2053, 2063, 2069,
	  2081, 2083, 2087, 2089, 2099, 2111, 2113, 2129,
	  2131, 2137, 2141, 2143, 2153, 2161, 2179, 2203,
	  2207, 2213, 2221, 2237, 2239, 2243, 2251, 2267,
	  2269, 2273, 2281, 2287, 2293, 2297, 2309, 2311,
	  2333, 2339, 2341, 2347, 2351, 2357, 2371, 2377,
	  2381, 2383, 2389, 2393, 2399, 2411, 2417, 2423,
	  2437, 2441, 2447, 2459, 2467, 2473, 2477, 2503,
	  2521, 2531, 2539, 2543, 2549, 2551, 2557, 2579,
	  2591, 2593, 2609, 2617, 2621, 2633, 2647, 2657,
	  2659, 2663, 2671, 2677, 2683, 2687, 2689, 2693,
	  2699, 2707, 2711, 2713, 2719, 2729, 2731, 2741,
	  2749, 2753, 2767, 2777, 2789, 2791, 2797, 2801,
	  2803, 2819, 2833, 2837, 2843, 2851, 2857, 2861,
	  2879, 2887, 2897, 2903, 2909, 2917, 2927, 2939,
	  2953, 2957, 2963, 2969, 2971, 2999, 3001, 3011,
	  3019, 3023, 3037, 3041, 3049, 3061, 3067, 3079,
	  3083, 3089, 3109, 3119, 3121, 3137, 3163, 3167,
	  3169, 3181, 3187, 3191, 3203, 3209, 3217, 3221,
	  3229, 3251, 3253, 3257, 3259, 3271, 3299, 3301,
	  3307, 3313, 3319, 3323, 3329, 3331, 3343, 3347,
	  3359, 3361, 3371, 3373, 3389, 3391, 3407, 3413,
	  3433, 3449, 3457, 3461, 3463, 3467, 3469, 3491,
	  3499, 3511, 3517, 3527, 3529, 3533, 3539, 3541,
	  3547, 3557, 3559, 3571, 3581, 3583, 3593, 3607,
	  3613, 3617, 3623, 3631, 3637, 3643, 3659, 3671,
	  3673, 3677, 3691, 3697, 3701, 3709, 3719, 3727,
	  3733, 3739, 3761, 3767, 3769, 3779, 3793, 3797,
	  3803, 3821, 3823, 3833, 3847, 3851, 3853, 3863,
	  3877, 3881, 3889, 3907, 3911, 3917, 3919, 3923,
	  3929, 3931, 3943, 3947, 3967, 3989, 4001, 4003,
	  4007, 4013, 4019, 4021, 4027, 4049, 4051, 4057,
	  4073, 4079, 4091, 4093, 4099, 4111, 4127, 4129,
	  4133, 4139, 4153, 4157, 4159, 4177, 4201, 4211,
	  4217, 4219, 4229, 4231, 4241, 4243, 4253, 4259,
	  4261, 4271, 4273, 4283, 4289, 4297, 4327, 4337,
	  4339, 4349, 4357, 4363, 4373, 4391, 4397, 4409,
	  4421, 4423, 4441, 4447, 4451, 4457, 4463, 4481,
	  4483, 4493, 4507, 4513, 4517, 4519, 4523, 4547,
	  4549, 4561, 4567, 4583,
	};
  
  float halton(unsigned int i, unsigned int d);
  float2 sampleDisk(float2 u);
  float3 sampleCosineHemisphere(float2 u);
  float2 sampleTriUniform(float2 u);
}

/*
 * Parametric GGX BSDF implementation
 * Loosely based on Enterprise PBR and Blender's Principled BSDF
 */
namespace bsdf {
  float3 schlick(float3 f0, float cosTheta);
  float3 gulbrandsenFresnelFit(float3 f0, float3 g = float3(1.0));
  
  float fresnel(float cosTheta, float ior);
  float avgDielectricFresnelFit(float ior);

  class GGX {
  public:
    explicit GGX(float roughness);
    GGX(float roughness, float anisotropic);
    
    float mdf(float3 w) const;
    float g1(float3 w) const;
    float g(float3 wo, float3 wi) const;
    float vmdf(float3 w, float3 wm) const;
    
    inline float3 sampleVmdf(float3 w, float2 u) const;
    
    __attribute__((always_inline))
    float singleScatterBRDF(float3 wo, float3 wi, float3 wm) const;
    
    __attribute__((always_inline))
    float pdf(float3 wo, float3 wm) const;
    
    inline bool isSmooth() const;
    
  private:
    float2 m_alpha;
    
    inline float lambda(float3 w) const;
  };
  
  enum Sample_Flags {
    Sample_Absorbed      = 0,
    Sample_Emitted       = 1 << 0,
    Sample_Reflected     = 1 << 1,
    Sample_Transmitted   = 1 << 2,
    Sample_Diffuse       = 1 << 3,
    Sample_Glossy        = 1 << 4,
    Sample_Specular      = 1 << 5,
  };
      
  struct Sample {
    float3 wi;
    float3 f;
    float3 Le;
    float pdf;
    int flags = 0;
  };
  
  struct Eval {
    float3 f;
    float3 Le;
    float pdf = 1.0f;
  };
  
  class BSDF {
  public:
    BSDF(
      device const pt::Material& material,
      thread const texture2d<float>& lutE,
      thread const texture1d<float>& lutEavg,
      thread const texture3d<float>& lutMsE,
      thread const texture2d<float>& lutMsEavg,
      thread const texture3d<float>& lutETransIn,
      thread const texture3d<float>& lutETransOut,
      thread const texture2d<float>& lutEavgTransIn,
      thread const texture2d<float>& lutEavgTransOut,
      constant const Constants& constants
    );
    
    Eval eval(float3 wo, float3 wi, float2 uv);
    Sample sample(float3 wo, float2 uv, float4 r);
    
  private:
    device const pt::Material& m_material;
    GGX m_ggx, m_ggxCoat;
    thread const texture2d<float>& m_lutE;
    thread const texture1d<float>& m_lutEavg;
    thread const texture3d<float>& m_lutMsE;
    thread const texture2d<float>& m_lutMsEavg;
    thread const texture3d<float>& m_lutETransIn;
    thread const texture3d<float>& m_lutETransOut;
    thread const texture2d<float>& m_lutEavgTransIn;
    thread const texture2d<float>& m_lutEavgTransOut;
    constant const Constants& m_constants;
    constant constexpr static float m_clearcoatIor = 1.5f;
    
    /*
     * Multiple scattering term for GGX BRDF
     * Implementation of Kulla & Conty, https://blog.selfshadow.com/publications/s2017-shading-course/imageworks/s2017_pbs_imageworks_slides_v2.pdf
     */
    template<typename T>
    __attribute__((always_inline))
    T multiscatter(float3 wo, float3 wi, T F_avg) {
      constexpr sampler s(address::clamp_to_edge, filter::linear);
      const auto E_wo = m_lutE.sample(s, float2(wo.z, m_material.roughness)).r;
      const auto E_wi = m_lutE.sample(s, float2(wi.z, m_material.roughness)).r;
      const auto E_avg = m_lutEavg.sample(s, m_material.roughness).r;
      
      const auto brdf_ms = (1.0f - E_wo) * (1.0f - E_wi) / (M_PI_F * (1.0f - E_avg));
      const auto fresnel_ms = F_avg * F_avg * E_avg / (1.0f - F_avg * (1.0f - E_avg));
      
      return fresnel_ms * brdf_ms;
    }
    
    __attribute__((always_inline))
    float transparentMultiscatter(float3 wo, float3 wi, float ior);
    
    __attribute__((always_inline))
    float diffuseFactor(float3 wo, float3 wi, float ior, float r);
    
    __attribute__((always_inline))
    Eval evalMetallic(float3 wo, float3 wi, float3 wm);
    
    Eval evalMetallic(float3 wo, float3 wi);
    Sample sampleMetallic(float3 wo, float3 r);
    
    __attribute__((always_inline))
    Eval evalTransparentDielectric(float3 wo, float3 wi, float3 wm, float fresnel_ss, float ior);
    
    Eval evalTransparentDielectric(float3 wo, float3 wi);
    Sample sampleTransparentDielectric(float3 wo, float3 r);
    
    Eval evalOpaqueDielectric(float3 wo, float3 wi);
    Sample sampleOpaqueDielectric(float3 wo, float3 r);
    
    Eval evalClearcoat(float3 wo, float3 wi);
    Sample sampleClearcoat(float3 wo, float3 r);
  };
}

#endif // PLATINUM_METAL_DEFS_H
