#include <metal_stdlib>

#include "defs.metal"

using namespace metal;

/*
 * Parametric GGX BSDF implementation
 * Loosely based on Enterprise PBR and Blender's Principled BSDF
 */
namespace bsdf {
  ShadingContext::ShadingContext(device const MaterialGPU& mat, float2 uv, device Texture* textures) {
    albedo = mat.baseColor.rgb;
    emission = mat.emission * mat.emissionStrength;
    roughness = mat.roughness;
    metallic = mat.metallic;
    transmission = mat.transmission;
    clearcoat = mat.clearcoat;
    clearcoatRoughness = mat.clearcoatRoughness;
    anisotropy = mat.anisotropy;
    ior = mat.ior;
    flags = mat.flags;
    
    constexpr sampler s(address::repeat, filter::linear);
    if (mat.baseTextureId >= 0) albedo = textures[mat.baseTextureId].tex.sample(s, uv).rgb;
    if (mat.emissionTextureId >= 0) emission *= textures[mat.emissionTextureId].tex.sample(s, uv).rgb;
    if (mat.transmissionTextureId >= 0) transmission = textures[mat.transmissionTextureId].tex.sample(s, uv).r;
    if (mat.clearcoatTextureId >= 0) clearcoat = textures[mat.clearcoatTextureId].tex.sample(s, uv).r;
    if (mat.rmTextureId >= 0) {
      float2 rm = textures[mat.rmTextureId].tex.sample(s, uv).rg;
      roughness *= rm.x;
      metallic *= rm.y;
    }
  }
  
  /*
   * Schlick fresnel approximation for conductors
   * Not physically correct, but close enough for RGB rendering
   */
  float3 schlick(float3 f0, float cosTheta) {
    const auto k = 1.0f - cosTheta;
    const auto k2 = k * k;
    return f0 + (float3(1.0) - f0) * (k2 * k2 * k);
  }
  
  /*
   * Numerical fit for average conductor fresnel supporting an edge tint parameter.
   * TODO: Currently unused.
   */
  float3 gulbrandsenFresnelFit(float3 f0, float3 g) {
    return 0.087237 + 0.0230685 * g    - 0.0864902 * g * g       + 0.0774594 * g * g * g
                     + 0.782654 * f0    - 0.136432 * f0 * f0     + 0.278708 * f0 * f0 * f0
                     + 0.19744 * f0 * g + 0.0360605 * f0 * g * g - 0.2586 * f0 * f0 * g;
  }
  
  /*
   * Real fresnel equations for dielectrics
   * We don't use the complex equations for conductors, as they're meaningless in RGB rendering
   */
  float fresnel(float cosTheta, float ior) {
    cosTheta = saturate(cosTheta);
    
    const auto sin2Theta_t = (1.0f - cosTheta * cosTheta) / (ior * ior);
    if (sin2Theta_t >= 1.0f) return 1.0f;
    
    const auto cosTheta_t = sqrt(1.0f - sin2Theta_t);
    const auto parallel = (ior * cosTheta - cosTheta_t) / (ior * cosTheta + cosTheta_t);
    const auto perpendicular = (cosTheta - ior * cosTheta_t) / (cosTheta + ior * cosTheta_t);
    return (parallel * parallel + perpendicular * perpendicular) * 0.5f;
  }
  
  /*
   * Numerical fit for average dielectric fresnel.
   * Reference: Revisiting Physically Based Shading at Imageworks, C. Kulla & A. Conty 2017
   */
  float avgDielectricFresnelFit(float ior) {
    return ior >= 1.0f
           ? (ior - 1.0f) / (4.08567f + 1.00071f * ior)
           : 0.997118f + 0.1014f * ior - 0.965241 * ior * ior - 0.130607 * ior * ior * ior;
  }
  
  /*
   * Trowbridge-Reitz GGX microfacet distribution implementation
   * Simple wrapper around all the GGX sampling functions, supports anisotropy. Works with all
   * directions in tangent space (Z-up, aligned to surface normal)
   */
  GGX::GGX(float roughness) : m_alpha(roughness * roughness) {}
    
  GGX::GGX(float roughness, float anisotropic) {
    const auto alpha = roughness * roughness;
    const auto aspect = sqrt(1.0f - 0.9f * anisotropic);
    m_alpha = float2(alpha / aspect, alpha * aspect);
  }
    
  // Microfacet distribution function
  inline float GGX::mdf(float3 w) const {
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
  inline float GGX::g1(float3 w) const {
    return 1.0f / (1.0f + lambda(w));
  }
    
  // Smith approximation for masking+shadowing function
  inline float GGX::g(float3 wo, float3 wi) const {
    return 1.0f / (1.0f + lambda(wo) + lambda(wi));
  }
    
  // Visible microfacet distribution function
  inline float GGX::vmdf(float3 w, float3 wm) const {
    return g1(w) / abs(w.z) * mdf(wm) * abs(dot(w, wm));
  }
  
  // Sample a microfacet from the visible distribution
  inline float3 GGX::sampleVmdf(float3 w, float2 u) const {
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
  float GGX::singleScatterBRDF(float3 wo, float3 wi, float3 wm) const {
    return mdf(wm) * g(wo, wi) / (4 * abs(wo.z) * abs(wi.z));
  }
  
  __attribute__((always_inline))
  float GGX::pdf(float3 wo, float3 wm) const {
    return vmdf(wo, wm) / (4.0f * abs(dot(wo, wm)));
  }
  
  // Surfaces with a very small roughness value are considered perfect specular, this helps deal
  // with the numerical instability at such values and makes almost no visible difference
  inline bool GGX::isSmooth() const {
    return m_alpha.x < 1e-3f && m_alpha.y < 1e-3f;
  }
  
  // Implementation detail for the masking and shadowing functions
  inline float GGX::lambda(float3 w) const {
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
  
  /*
   * Principled BSDF implementation.
   * Loosely based on a subset of the Enterprise PBR spec and Blender's Principled BSDF.
   */
  BSDF::BSDF(
		thread ShadingContext& ctx,
    constant Constants& constants,
    constant Luts& luts
  ) : m_ctx(ctx),
      m_ggx(ctx.roughness, ctx.anisotropy),
      m_ggxCoat(ctx.clearcoatRoughness),
      m_constants(constants),
  		m_luts(luts) {}
    
  /*
   * Evaluate the BSDF, given outgoing and incident light directions. Blends all BSDF lobes
   * based on material parameters and the PDF for each.
   */
  Eval BSDF::eval(float3 wo, float3 wi) {
    if (wo.z < 1.5e-3f || wi.z < 1.5e-3f) return {};

    float metallic = m_ctx.metallic;
    float transparent = (1.0 - metallic) * m_ctx.transmission;
    float opaque = (1.0 - metallic) * (1.0 - transparent);

    Eval result = { .f = 0.0, .Le = 0.0, .pdf = 0.0 };
    if (metallic > 0.0) result += evalMetallic(wo, wi) * metallic;
    if (transparent > 0.0) result += evalTransparentDielectric(wo, wi) * transparent;
    if (opaque > 0.0) result += evalOpaqueDielectric(wo, wi) * opaque; // TODO glossy

    float coat = m_ctx.clearcoat;
    if (coat > 0.0f) {
      float coatFresnel_ss;
      auto coatResult = evalClearcoat(wo, wi, coatFresnel_ss);
      coat *= coatFresnel_ss;
      result = result * (1.0 - coat) + coatResult * coat;
    }

    return result;
  }
  
  /*
   * Given an outgoing light direction, importance sample the
   */
  Sample BSDF::sample(float3 wo, float4 r, float2 rc) {
    float c = m_ctx.clearcoat;
    float m = m_ctx.metallic;
    float t = m_ctx.transmission;

    float pClearcoat = c;
    if (pClearcoat > 0.0f) {
      const float3 wmCoat = m_ggxCoat.isSmooth() ? float3(0, 0, 1) : m_ggxCoat.sampleVmdf(wo, rc);
      pClearcoat *= fresnel(abs(dot(wo, wmCoat)), m_clearcoatIor);
    }
    
    const float pMetallic = pClearcoat + (1.0f - pClearcoat) * m;
    const float pTransparent = pClearcoat + (1.0f - pClearcoat) * (m + (1.0f - m) * t);
    
    if (r.w < pClearcoat) return sampleClearcoat(wo, r.xyz);
    if (r.w < pMetallic) return sampleMetallic(wo, r.xyz);
    if (r.w < pTransparent) return sampleTransparentDielectric(wo, r.xyz);
    return sampleOpaqueDielectric(wo, r.xyz);
  }
  
  /*
   * Multiple scattering multiplier for transparent dielectric GGX BSDF
   * E. Turquin's method (https://blog.selfshadow.com/publications/turquin/ms_comp_final.pdf)
   * This method is not reciprocal, but I couldn't get the K&C method working for a transparent BSDF.
   * TODO: revisit this later
   */
  __attribute__((always_inline))
  float BSDF::transparentMultiscatter(float3 wo, float3 wi, float ior) {
    constexpr sampler s(address::clamp_to_edge, filter::linear);
    
    if (ior < 1.0f) {
      // For IOR < 1, we simply use 1 - eta, which falls nicely in the 0-1 range.
      const auto iorParam = 1.0f - ior;
      const auto E_wo = m_luts.ETransOut.sample(s, float3(abs(wo.z), m_ctx.roughness, iorParam)).r;
      
      return 1.0f / E_wo;
    } else {
      // IOR parametrization optimized for the common IOR range 1 < eta < 2
      // See https://www.desmos.com/calculator/1pkvgrisbx for details.
      const auto iorParam = (ior - 1.0f) / ior;
      const auto E_wo = m_luts.ETransIn.sample(s, float3(abs(wo.z), m_ctx.roughness, iorParam)).r;
      
      return 1.0f / E_wo;
    }
  }
  
  /*
   * Calculate the attenuation factor for the underlying diffuse BRDF in a "glossy" (diffuse + GGX)
   * lobe. Assumes the microfacet BRDF is compensated for multiple scattering.
   * Reference: Enterprise PBR spec
   */
  __attribute__((always_inline))
  float BSDF::diffuseFactor(float3 wo, float3 wi) {
    // IOR parametrization optimized for the common IOR range 1 < eta < 2
    // See https://www.desmos.com/calculator/1pkvgrisbx for details.
    const auto iorParam = (m_ctx.ior - 1.0f) / m_ctx.ior;
    
    constexpr sampler s(address::clamp_to_edge, filter::linear);
    const auto E_ms_wo = m_luts.EMs.sample(s, float3(wo.z, m_ctx.roughness, iorParam)).r;
    const auto E_ms_wi = m_luts.EMs.sample(s, float3(wi.z, m_ctx.roughness, iorParam)).r;
    const auto E_ms_avg = m_luts.EavgMs.sample(s, float2(iorParam, m_ctx.roughness)).r;
    
    return (1.0f - E_ms_wo) * (1.0f - E_ms_wi) / (M_PI_F * (1.0f - E_ms_avg));
  }
  
  /*
   * Blending factor for opaque dielectrics, accounting for a multiple scattering dielectric BRDF.
   * Returns the blending weight of the dielectric component.
   */
  __attribute__((always_inline))
  float BSDF::opaqueDielectricFactor(float3 wo, float F_avg) {
    // IOR parametrization optimized for the common IOR range 1 < eta < 2
    // See https://www.desmos.com/calculator/1pkvgrisbx for details.
    const auto iorParam = (m_ctx.ior - 1.0f) / m_ctx.ior;
    
    constexpr sampler s(address::clamp_to_edge, filter::linear);
    const auto E_wo = m_luts.E.sample(s, float2(wo.z, m_ctx.roughness)).r;
    const auto E_ms_wo = m_luts.EMs.sample(s, float3(wo.z, m_ctx.roughness, iorParam)).r;
    
    const auto fresnel_ms = F_avg * F_avg * E_wo / (1.0f - F_avg * (1.0f - E_wo));
    const auto dielectricFactor = F_avg * E_ms_wo + fresnel_ms * (1.0f - E_ms_wo);
    
    return dielectricFactor;
  }
  
  
  /* ================================================== *
   *
   * BSDF Evaluation functions
   *
   * ================================================== */
  
  /*
   * Evaluate GGX conductor BRDF and PDF
   * Internal version, lets us skip the microfacet normal calculation if we already have it (i.e.
   * if we just sampled the VMDF)
   */
  __attribute__((always_inline))
  Eval BSDF::evalMetallic(float3 wo, float3 wi, float3 wm) {
    const auto fresnel_ss = schlick(m_ctx.albedo, abs(dot(wo, wm)));
    auto brdf = fresnel_ss * m_ggx.singleScatterBRDF(wo, wi, wm);
    
    // Multiple scattering
    if (m_constants.flags & RendererFlags_MultiscatterGGX) {
      const auto F_avg = (20.0f * m_ctx.albedo + 1.0f) / 21.0f;
      brdf += multiscatter(wo, wi, F_avg);
    }
    
    return {
      .f = brdf,
      .pdf = m_ggx.pdf(wo, wm),
    };
  }
  
  /*
   * Evaluate GGX conductor BRDF and PDF
   */
  Eval BSDF::evalMetallic(float3 wo, float3 wi) {
    if (m_ggx.isSmooth()) return {};
    
    float3 wm = normalize(wo + wi);
    if (length_squared(wm) == 0.0f) return {};
    wm *= sign(wm.z);
    
    return evalMetallic(wo, wi, wm);
  }
  
  /*
   * Evaluate GGX transparent dielectric BSDF and PDF
   * Internal version, lets us skip the microfacet normal and fresnel calculations if we already
   * have them (i.e. after sampling the BSDF)
   */
  __attribute__((always_inline))
  Eval BSDF::evalTransparentDielectric(float3 wo, float3 wi, float3 wm, float fresnel_ss, float ior) {
    const bool thin = m_ctx.flags & MaterialGPU::Material_ThinDielectric;
    
    const auto isReflection = wo.z * wi.z > 0.0f;
    
    float3 bsdf;
    float pdf, k = fresnel_ss;
    if (isReflection) {
      bsdf = float3(m_ggx.singleScatterBRDF(wo, wi, wm));
      pdf = m_ggx.pdf(wo, wm);
    } else {
      k = 1.0f - fresnel_ss;
      
      float btdf_ss;
      if (thin) {
        // TODO: thin glass is not energy preserving at high roughness
        btdf_ss = m_ggx.singleScatterBRDF(wo, wi, wm);
        pdf = m_ggx.pdf(wo, wm);
      } else {
        auto denom = dot(wi, wm) * ior + dot(wo, wm);
        denom *= denom;
        
        const auto dwm_dwi = abs(dot(wi, wm)) / denom;
        btdf_ss = m_ggx.mdf(wm) * m_ggx.g(wo, wi) * abs(dot(wi, wm) * dot(wo, wm) / (wi.z * wo.z * denom));
        pdf = m_ggx.vmdf(wo, wm) * dwm_dwi;
      }
      
      bsdf = m_ctx.albedo * btdf_ss;
    }
    
    // Multiple scattering
    if (m_constants.flags & RendererFlags_MultiscatterGGX) {
      bsdf *= transparentMultiscatter(wo, wi, ior);
    }
    
    return {
      .f = k * bsdf,
      .pdf = k * pdf,
    };
  }
  
  /*
   * Evaluate GGX transparent dielectric BSDF and PDF
   */
  Eval BSDF::evalTransparentDielectric(float3 wo, float3 wi) {
    if (m_ggx.isSmooth()) return {};
    const bool thin = m_ctx.flags & MaterialGPU::Material_ThinDielectric;
    const float ior = (!thin && wo.z < 0.0f && wi.z < 0.0f) ? 1.0f / m_ctx.ior : m_ctx.ior;
    
    float3 wm = ior * wi + wo;
    if (wi.z == 0 || wo.z == 0 || wm.z == 0) return {};
    
    wm = normalize(wm * sign(wm.z));
    if (dot(wi, wm) * wi.z < 0.0f || dot(wo, wm) * wo.z < 0.0f) return {};
    
    if (thin) {
        wi = reflect(wi, float3(0.0, 0.0, 1.0));
        wm = normalize(wi + wo);
    }
    
    const auto fresnel_ss = fresnel(dot(wo, wm), ior);
    return evalTransparentDielectric(wo, wi, wm, fresnel_ss, ior);
  }
  
  /*
   * Evaluate GGX opaque dielectric BRDF (diffuse + dielectric GGX) and PDF
   */
  Eval BSDF::evalOpaqueDielectric(float3 wo, float3 wi) {
    // GGX/Diffuse blending factor
    const auto F_avg = avgDielectricFresnelFit(m_ctx.ior);
    const auto blendingFactor = opaqueDielectricFactor(wo, F_avg);

    // Diffuse BRDF
    const float cDiffuse = diffuseFactor(wo, wi);
    const float diffusePdf = abs(wi.z) / M_PI_F;

    if (m_ggx.isSmooth()) return {
      .f = m_ctx.albedo * cDiffuse,
      .pdf = diffusePdf * (1.0f - blendingFactor),
    };

    float3 wm = normalize(wo + wi);
    if (length_squared(wm) == 0.0f) return {};
    wm *= sign(wm.z);

    const float fresnel_ss = fresnel(abs(dot(wo, wm)), m_ctx.ior);

    // Dielectric single scattering BRDF
    float dielectricBrdf = fresnel_ss * m_ggx.singleScatterBRDF(wo, wi, wm);
    
    // Multiple scattering
    if (m_constants.flags & RendererFlags_MultiscatterGGX) {
      dielectricBrdf += multiscatter(wo, wi, F_avg);
    }
    
    return {
      .f = float3(dielectricBrdf) + m_ctx.albedo * cDiffuse,
      .pdf = m_ggx.pdf(wo, wm) * blendingFactor + diffusePdf * (1.0f - blendingFactor),
    };
  }
  
  Eval BSDF::evalClearcoat(float3 wo, float3 wi, thread float& fresnel_ss) {
    if (m_ggxCoat.isSmooth()) return {};
    
    auto wm = wo + wi;
    wm = normalize(wm * sign(wm.z));
    if (length_squared(wm) == 0.0f) return {};
    
    fresnel_ss = fresnel(dot(wo, wm), m_clearcoatIor);
    return {
      .f = float3(m_ggxCoat.singleScatterBRDF(wo, wi, wm)),
      .pdf = m_ggxCoat.pdf(wo, wm),
    };
  }
  
  
  /* ================================================== *
   *
   * BSDF Sampling functions
   *
   * ================================================== */
  
  /*
   * Sample GGX conductor BRDF
   * Get an incident light direction by importance sampling the BRDF, and evaluate it
   */
  Sample BSDF::sampleMetallic(float3 wo, float3 r) {
    m_ctx.lobe = Lobe_Metallic;
    
    // Handle the special case of perfect specular reflection
    if (m_ggx.isSmooth()) {
      const auto fresnel_ss = schlick(m_ctx.albedo, wo.z);
      
      return {
        .flags  = Sample_Reflected | Sample_Specular,
        .f      = fresnel_ss / abs(wo.z),
        .wi     = float3(-wo.xy, wo.z),
        .pdf    = 1.0f,
      };
    }
    
    // Sample the microfacet normal, get incident light direction and evaluate the BRDF
    auto wm = m_ggx.sampleVmdf(wo, r.xy);
    auto wi = reflect(-wo, wm);
    if (wo.z * wi.z < 0.0f) return {};
    
    const auto eval = evalMetallic(wo, wi, wm);
    
    return {
      .flags  = Sample_Reflected | Sample_Glossy,
      .wi     = wi,
      .f      = eval.f,
      .pdf    = eval.pdf,
    };
  }
  
  /*
   * Sample GGX transparent dielectric BSDF
   * Get an incident light direction by importance sampling the BSDF, and evaluate it
   */
  Sample BSDF::sampleTransparentDielectric(float3 wo, float3 r) {
    m_ctx.lobe = Lobe_Transparent;
    
    const bool thin = m_ctx.flags & MaterialGPU::Material_ThinDielectric;
    auto ior = (wo.z < 0.0f && !thin) ? 1.0f / m_ctx.ior : m_ctx.ior;
    
    // Handle the perfect specular edge case
    if (m_ggx.isSmooth()) {
      const auto fresnel_ss = fresnel(abs(wo.z), ior);
      
      float3 wi, color = float3(1.0);
      float pdf = fresnel_ss;
      int flags = Sample_Specular;
      
      if (r.z < fresnel_ss) {
        wi = float3(-wo.xy, wo.z);
        flags |= Sample_Reflected;
      } else {
        wi = thin ? -wo : refract(-wo, float3(0.0, 0.0, sign(wo.z)), 1.0f / ior);
        if (wi.z == 0.0f) return {};
        
        pdf = (1.0f - fresnel_ss);
        color = m_ctx.albedo;
        flags |= Sample_Transmitted;
      }
      
      return {
        .flags  = flags,
        .wi     = wi,
        .f      = pdf * color / abs(wi.z),
        .Le     = float3(0.0),
        .pdf    = pdf,
      };
    }
    
    // Sample the microfacet normal and evaluate single-scattering fresnel
    const auto wm = m_ggx.sampleVmdf(wo, r.xy);
    const auto fresnel_ss = fresnel(abs(dot(wo, wm)), ior);
    
    float3 wi;
    int flags = Sample_Glossy;
    
    // Get the incident light direction and evaluate the BSDF
    if (r.z < fresnel_ss) {
      wi = reflect(-wo, wm);
      if (wo.z * wi.z < 0.0f) return {};
      flags |= Sample_Reflected;
    } else if (thin) {
      wi = reflect(-wo, wm) * float3(1.0, 1.0, -1.0);
      flags |= Sample_Transmitted;
    } else {
      wi = refract(-wo, wm * sign(dot(wo, wm)), 1.0f / ior);
      if (wo.z * wi.z >= 0.0f) return {};
      flags |= Sample_Transmitted;
    }
    
    const auto eval = evalTransparentDielectric(wo, wi, wm, fresnel_ss, ior);
    
    return {
      .flags  = flags,
      .wi     = wi,
      .f      = eval.f,
      .Le     = eval.Le,
      .pdf    = eval.pdf,
    };
  }
  
  /*
   * Sample GGX opaque dielectric BRDF (diffuse + dielectric GGX)
   * Get an incident light direction by importance sampling the BRDF, and evaluate it
   */
  Sample BSDF::sampleOpaqueDielectric(float3 wo, float3 r) {
    const auto F_avg = avgDielectricFresnelFit(m_ctx.ior);
    const auto blendingFactor = opaqueDielectricFactor(wo, F_avg);
    
    if (r.z < blendingFactor) {
      // Sample the dielectric BRDF
      m_ctx.lobe = Lobe_Dielectric;
      
      if (m_ggx.isSmooth()) {
        const auto fresnel_ss = fresnel(abs(wo.z), m_ctx.ior);
        const auto wi = float3(-wo.x, -wo.y, wo.z);
        
        return {
          .flags  = Sample_Reflected | Sample_Specular,
          .wi     = wi,
          .f      = float3(fresnel_ss / abs(wi.z)),
          .pdf    = blendingFactor,
        };
      }
      
      const auto wm = m_ggx.sampleVmdf(wo, r.xy);
      if (length_squared(wm) == 0.0f) return {};
      
      const auto wi = reflect(-wo, wm);
      const auto fresnel_ss = fresnel(abs(dot(wo, wm)), m_ctx.ior);
      auto dielectricBrdf = fresnel_ss * m_ggx.singleScatterBRDF(wo, wi, wm);
      
      if (m_constants.flags & RendererFlags_MultiscatterGGX) {
        dielectricBrdf += multiscatter(wo, wi, F_avg);
      }
      
      return {
        .flags  = Sample_Reflected | Sample_Glossy,
        .wi     = wi,
        .f      = float3(dielectricBrdf),
        .pdf    = m_ggx.pdf(wo, wm) * blendingFactor,
      };
    } else {
      // Sample the underlying diffuse BRDF
      m_ctx.lobe = Lobe_Diffuse;
      
      auto wi = samplers::sampleCosineHemisphere(r.xy);
      if (wo.z < 0.0f) wi *= -1.0f;
      
      const auto cDiffuse = diffuseFactor(wo, wi);
      
      auto flags = Sample_Reflected | Sample_Diffuse;
      if (m_ctx.flags & MaterialGPU::Material_Emissive) flags |= Sample_Emitted;
      return {
        .flags  = flags,
        .wi     = wi,
        .f      = m_ctx.albedo * cDiffuse,
        .Le     = m_ctx.emission / (1.0f - blendingFactor),
        .pdf    = abs(wi.z) / M_PI_F * (1.0f - blendingFactor),
      };
    }
  }
  
  Sample BSDF::sampleClearcoat(float3 wo, float3 r) {
    m_ctx.lobe = Lobe_Clearcoat;
    
    if (m_ggxCoat.isSmooth()) {
      const float fresnel_ss = fresnel(wo.z, m_clearcoatIor);
      const float3 wi(-wo.xy, wo.z);
      
      return {
        .flags 	= Sample_Reflected | Sample_Specular,
        .wi 		= wi,
        .f 			= float3(fresnel_ss / abs(wi.z)),
        .pdf 		= fresnel_ss,
      };
    }
    
    // Sample the microfacet normal and evaluate single-scattering fresnel
    const float3 wm = m_ggxCoat.sampleVmdf(wo, r.xy);
    const float3 wi = reflect(-wo, wm);
    if (wo.z * wi.z < 0.0f) return {};
    
    const float fresnel_ss = fresnel(abs(dot(wo, wm)), m_clearcoatIor);

    return {
      .flags 	= Sample_Reflected | Sample_Glossy,
      .wi 		= wi,
      .f 			= float3(fresnel_ss * m_ggxCoat.singleScatterBRDF(wo, wi, wm)),
      .pdf 		= fresnel_ss * m_ggxCoat.pdf(wo, wm),
    };
  }
}
