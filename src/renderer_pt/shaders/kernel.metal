#include <metal_stdlib>

// Header files use this guard to only include what the shader needs
#define METAL_SHADER

#include "../../core/mesh.hpp"
#include "../../core/material.hpp"
#include "../pt_shader_defs.hpp"

#define MAX_BOUNCES 15
#define DIMS_PER_BOUNCE 8

using namespace metal;
using namespace raytracing;
using namespace pt::shaders_pt;

/*
 * Type definitions
 */
using triangle_instance_intersection = typename intersector<triangle_data, instancing>::result_type;

/*
 * Constants
 */
constant uint32_t resourcesStride [[function_constant(0)]];
constant float3 backgroundColor(0.0, 0.0, 0.0);

/*
 * Resource structs
 */
struct VertexResource {
  device float3* position;
  device pt::VertexData* data;
};

struct PrimitiveResource {
  device uint32_t* materialSlot;
};

struct InstanceResource {
  device pt::Material* materials;
};

/*
 * Miscellaneous helper functions
 */
template<typename T>
T interpolate(thread T* att, float2 uv) {
  return (1.0f - uv.x - uv.y) * att[0] + uv.x * att[1] + uv.y * att[2];
}

__attribute__((always_inline))
float3 transformVec(float3 p, float4x4 transform) {
  return (transform * float4(p.x, p.y, p.z, 0.0f)).xyz;
}

/*
 * Coordinate frame, we use this over a 4x4 matrix because it allows easy conversion both ways
 * without having to calculate the inverse.
 */
struct Frame {
  float3 x, y, z;
  
  static Frame fromNormal(float3 n) {
    const auto a = abs(n.x) > 0.5 ? float3(0, 0, 1) : float3(1, 0, 0);
    
    const auto b = normalize(cross(n, a));
    const auto t = cross(n, b);
    
    return {t, b, n};
  }
  
  inline float3 worldToLocal(float3 w) const {
    return float3(dot(w, x), dot(w, y), dot(w, z));
  }
  
  inline float3 localToWorld(float3 l) const {
    return x * l.x + y * l.y + z * l.z;
  }
};

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
 * Parametric GGX BSDF implementation
 * Loosely based on Enterprise PBR and Blender's Principled BSDF
 */
namespace bsdf {
  /*
   * Schlick fresnel approximation for conductors
   */
  inline float3 schlick(float3 f0, float cosTheta) {
    const auto k = 1.0f - cosTheta;
    const auto k2 = k * k;
    return f0 + (float3(1.0) - f0) * (k2 * k2 * k);
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
    enum Flags {
      Absorbed 			= 0,
      Emitted 			= 1 << 0,
      Reflected			= 1 << 1,
      Transmitted 	= 1 << 2,
      Diffuse 			= 1 << 3,
      Glossy 				= 1 << 4,
      Specular 			= 1 << 5,
    };
    
    float3 wi;
    float3 f;
    float3 Le;
    float pdf;
    int flags = 0;
  };
  
  struct Eval {
    float3 f;
    float3 Le;
    float pdf = 0.0f;
  };
  
  /*
   * Evaluate GGX conductor BRDF and PDF
   * Internal version, lets us skip the microfacet normal calculation if we already have it (i.e.
   * if we just sampled the VMDF)
   */
  __attribute__((always_inline))
  Eval evalMetallic(device const pt::Material& mat, float3 wo, float3 wi, float3 wm, thread const GGX& ggx) {
    const auto fresnel_ss = schlick(mat.baseColor.rgb, abs(dot(wo, wm)));
    
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
  Eval evalMetallic(device const pt::Material& mat, float3 wo, float3 wi, thread const GGX& ggx) {
    if (ggx.isSmooth()) return {};
    
    auto wm = wo + wi;
    if (length_squared(wm) == 0.0f) return {};
    wm = normalize(wm * sign(wm.z));
    
    return evalMetallic(mat, wo, wi, wm, ggx);
  }
  
  /*
   * Get an incident light direction by importance sampling the BRDF, and evaluate it
   */
  Sample sampleMetallic(device const pt::Material& mat, float3 wo, thread const GGX& ggx, float3 r) {
    // Handle the special case of perfect specular reflection
    if (ggx.isSmooth()) {
      const auto fresnel_ss = schlick(mat.baseColor.rgb, wo.z);
      
      return {
        .flags  = Sample::Reflected | Sample::Specular,
        .f      = fresnel_ss / abs(wo.z),
        .Le     = float3(0.0),
        .wi     = float3(-wo.xy, wo.z),
        .pdf    = 1.0f,
      };
    }
    
    // Sample the microfacet normal, get incident light direction and evaluate the BRDF
    auto wm = ggx.sampleVmdf(wo, r.xy);
    auto wi = reflect(-wo, wm);
    if (wo.z * wi.z < 0.0f) return {};
    
    const auto eval = evalMetallic(mat, wo, wi, wm, ggx);
    
    return {
      .flags	= Sample::Reflected | Sample::Glossy,
      .wi     = wi,
      .f      = eval.f,
      .pdf    = eval.pdf,
    };
  }
  
  /*
   * Evaluate GGX dielectric BSDF and PDF
   * Internal version, lets us skip the microfacet normal and fresnel calculations if we already
   * have them (i.e. after sampling the BSDF)
   */
  __attribute__((always_inline))
  Eval evalDielectric(device const pt::Material& mat, float3 wo, float3 wi, float3 wm, float fresnel_ss, float ior, thread const GGX& ggx) {
    const bool thin = mat.flags & pt::Material::Material_ThinDielectric;
    
    auto cosTheta_o = wo.z, cosTheta_i = wi.z;
    const auto isReflection = cosTheta_o * cosTheta_i > 0.0f;
    
    // TODO: multiple scattering
    if (isReflection) {
      const auto brdf_ss = ggx.mdf(wm) * ggx.g(wo, wi) / (4 * cosTheta_o * cosTheta_i);
      
      return {
        .f = float3(fresnel_ss * brdf_ss),
        .pdf = fresnel_ss * ggx.vmdf(wo, wm) / (4 * abs(dot(wo, wm))),
      };
    } else if (thin) {
      wi = reflect(wi, float3(0.0, 0.0, 1.0));
      wm = normalize(wi + wo);
      const auto btdf_ss = ggx.mdf(wm) * ggx.g(wo, wi) / (4 * abs(wo.z) * abs(wi.z));
      
      return {
        .f = (1.0f - fresnel_ss) * btdf_ss * mat.baseColor.rgb,
        .pdf = (1.0f - fresnel_ss) * ggx.vmdf(wo, wm) / (4 * abs(dot(wo, wm))),
      };
    } else {
      auto denom = dot(wi, wm) * ior + dot(wo, wm);
      denom *= denom;
      
      const auto dwm_dwi = abs(dot(wi, wm)) / denom;
      const auto btdf_ss = ggx.mdf(wm) * ggx.g(wo, wi) * abs(dot(wi, wm) * dot(wo, wm) / (wi.z * wo.z * denom));
      
      return {
        .f = (1.0f - fresnel_ss) * btdf_ss * mat.baseColor.rgb,
        .pdf = (1.0f - fresnel_ss) * ggx.vmdf(wo, wm) * dwm_dwi,
      };
    }
  }
  
  /*
   * Evaluate GGX dielectric BSDF and PDF
   */
  Eval evalDielectric(device const pt::Material& mat, float3 wo, float3 wi, thread const GGX& ggx) {
    if (ggx.isSmooth()) return {};
    const bool thin = mat.flags & pt::Material::Material_ThinDielectric;
    const auto ior = (!thin && wo.z < 0.0f && wi.z < 0.0f) ? 1.0f / mat.ior : mat.ior;
    
    auto wm = ior * wi + wo;
    if (wi.z == 0 || wo.z == 0 || wm.z == 0) return {};
    
    wm = normalize(wm * sign(wm.z));
    if (dot(wi, wm) * wi.z < 0.0f || dot(wo, wm) * wo.z < 0.0f) return {};
    
    const auto fresnel_ss = fresnel(dot(wo, wm), ior);
    return evalDielectric(mat, wo, wi, wm, fresnel_ss, ior, ggx);
  }
  
  /*
   * Get an incident light direction by importance sampling the BSDF, and evaluate it
   */
  Sample sampleDielectric(device const pt::Material& mat, float3 wo, thread const GGX& ggx, float3 r) {
    const bool thin = mat.flags & pt::Material::Material_ThinDielectric;
    auto ior = (wo.z < 0.0f && !thin) ? 1.0f / mat.ior : mat.ior;
    
    // Handle the perfect specular edge case
    if (ggx.isSmooth()) {
      const auto fresnel_ss = fresnel(abs(wo.z), ior);
      
      float3 wi, color = float3(1.0);
      float pdf = fresnel_ss;
      int flags = Sample::Specular;
      
      if (r.z < fresnel_ss) {
        wi = float3(-wo.xy, wo.z);
        flags |= Sample::Reflected;
      } else {
        wi = thin ? -wo : refract(-wo, float3(0.0, 0.0, sign(wo.z)), 1.0f / ior);
        if (wi.z == 0.0f) return {};
        
        pdf = (1.0f - fresnel_ss);
        color = mat.baseColor.rgb;
        flags |= Sample::Transmitted;
      }
      
      return {
        .flags  = flags,
        .wi     = wi,
        .f      = pdf,
        .Le     = float3(0.0),
        .pdf    = pdf,
      };
    }
    
    // Sample the microfacet normal and evaluate single-scattering fresnel
    const auto wm = ggx.sampleVmdf(wo, r.xy);
    const auto fresnel_ss = fresnel(abs(dot(wo, wm)), ior);
    
    float3 wi;
    int flags = Sample::Glossy;
    
    // TODO: multiple scattering
    
    // Get the incident light direction and evaluate the BSDF
    if (r.z < fresnel_ss) {
      wi = reflect(-wo, wm);
      if (wo.z * wi.z < 0.0f) return {};
      flags |= Sample::Reflected;
    } else if (thin) {
      wi = reflect(-wo, wm) * float3(1.0, 1.0, -1.0);
      flags |= Sample::Transmitted;
    } else {
      wi = refract(-wo, wm * sign(dot(wo, wm)), 1.0f / ior);
      if (wo.z * wi.z >= 0.0f) return {};
      flags |= Sample::Transmitted;
    }
    
    const auto eval = evalDielectric(mat, wo, wi, wm, fresnel_ss, ior, ggx);
      
    return {
      .flags  = flags,
      .wi     = wi,
      .f      = eval.f,
      .Le     = eval.Le,
      .pdf    = eval.pdf,
    };
  }
  
  Eval evalDiffuse(device const pt::Material& mat, float3 wo, float3 wi) {
    // TODO: glossy
    return {
      .f = mat.baseColor.rgb,
      .Le = mat.emission * mat.emissionStrength,
      .pdf = abs(wi.z),
    };
  }
  
  Sample sampleGlossy(device const pt::Material& mat, float3 wo, thread const GGX& ggx, float3 r) {
    // TODO: glossy
    
    auto wi = samplers::sampleCosineHemisphere(r.xy);
    if (wo.z < 0.0f) wi *= -1.0f;
        
    auto diffuse = evalDiffuse(mat, wo, wi);
    
    auto flags = Sample::Reflected | Sample::Diffuse;
    if (mat.flags & pt::Material::Material_Emissive) flags |= Sample::Emitted;
    return {
      .flags  = flags,
      .wi     = wi,
      .f      = diffuse.f,
      .Le     = diffuse.Le,
      .pdf    = diffuse.pdf,
    };
  }
  
  Eval eval(device const pt::Material& material, float3 wo, float3 wi, float2 uv) {
    const auto cMetallic = material.metallic;
    const auto cTransmissive = (1.0f - material.metallic) * material.transmission;
    const auto cDiffuse = (1.0f - material.metallic) * (1.0f - material.transmission);
    
    float3 bsdf(0.0);
    float pdf = 0.0f;
    GGX ggx(material.roughness, material.anisotropy);
    
    if (cMetallic > 0.0f) {
      const auto bsdfMetallic = evalMetallic(material, wo, wi, ggx);
      bsdf += cMetallic * bsdfMetallic.f;
      pdf += cMetallic * bsdfMetallic.pdf;
    }
    if (cTransmissive > 0.0f) {
      const auto bsdfDielectric = evalDielectric(material, wo, wi, ggx);
      bsdf += cTransmissive * bsdfDielectric.f;
      pdf += cTransmissive * bsdfDielectric.pdf;
    }
    if (cDiffuse > 0.0f) {
      const auto bsdfDiffuse = evalDiffuse(material, wo, wi);
      bsdf += cDiffuse * bsdfDiffuse.f;
      pdf += cDiffuse * bsdfDiffuse.pdf;
    }
    
    return {
      .f = bsdf,
      .pdf = pdf,
    };
  }
  
  Sample sample(device const pt::Material& material, float3 wo, float2 uv, float4 r) {
    GGX ggx(material.roughness, material.anisotropy);
    
    const auto pMetallic = material.metallic;
    const auto pDielectric = material.metallic + (1.0f - material.metallic) * material.transmission;
    
    if (r.w < pMetallic) return sampleMetallic(material, wo, ggx, r.xyz);
    if (r.w < pDielectric) return sampleDielectric(material, wo, ggx, r.xyz);
    return sampleGlossy(material, wo, ggx, r.xyz);
  }
}

/*
 * Groups all the intersection data relevant to us for shading.
 */
struct Hit {
  float3 pos;														// Hit position 						(world space)
  float3 normal;												// Surface normal 					(world space)
  float3 wo;														// Outgoing light direction (tangent space)
  Frame frame;													// Shading coordinate frame, Z-up normal aligned
  device const pt::Material* material;	// Material
};

/*
 * Holds references to all the necessary resources.
 * This is essentially a parameter object to keep the getIntersectionData() function call short.
 */
struct Resources {
  const constant MTLAccelerationStructureInstanceDescriptor* instances;
  const device void* vertexResources;
  const device void* primitiveResources;
  const device void* instanceResources;
  
  inline device VertexResource& getVertices(uint32_t instanceIdx) {
    auto geometryIdx = instances[instanceIdx].accelerationStructureIndex;
   	return *(device VertexResource*)((device uint64_t*)vertexResources + geometryIdx * 2);
  }
  
  inline float4x4 getTransform(uint32_t instanceIdx) {
    float4x4 objectToWorld(1.0);
    for (int i = 0; i < 4; i++)
      for (int j = 0; j < 3; j++)
        objectToWorld[i][j] = instances[instanceIdx].transformationMatrix[i][j];
    
    return objectToWorld;
  }
  
	/*
	 * Helper function to get relevant data from an intersection: world space position, surface normal,
	 * material, tangent space outgoing light direction (opposite ray direction) for shading.
	 * We take this outside of the PT kernel to keep it tidier and so it can be reused by multiple kernels.
	 */
	inline Hit getIntersectionData(
	  const thread ray& ray,
	  const thread triangle_instance_intersection& intersection
	) {
	  auto instanceIdx = intersection.instance_id;
	  auto geometryIdx = instances[instanceIdx].accelerationStructureIndex;
	
	  device auto& vertexResource = *(device VertexResource*)((device uint64_t*)vertexResources + geometryIdx * 2);
	  device auto& primitiveResource = *(device PrimitiveResource*)((device uint64_t*)primitiveResources + geometryIdx);
	  device auto& instanceResource = *(device InstanceResource*)((device uint64_t*)instanceResources + instanceIdx);
	  device auto& data = *(device PrimitiveData*) intersection.primitive_data;
	
	  auto materialSlot = *primitiveResource.materialSlot;
	  device const auto& material = instanceResource.materials[materialSlot];
	
	  float3 vertexNormals[3];
	  for (int i = 0; i < 3; i++) {
	    vertexNormals[i] = vertexResource.data[data.indices[i]].normal;
	    // TODO: Interpolate UVs
	  }
	
	  float2 barycentricCoords = intersection.triangle_barycentric_coord;
	  float3 surfaceNormal = interpolate(vertexNormals, barycentricCoords);
	
    float4x4 objectToWorld = getTransform(instanceIdx);
	
	  float3 wsHitPoint = ray.origin + ray.direction * intersection.distance;
	  float3 wsSurfaceNormal = normalize(transformVec(surfaceNormal, objectToWorld));
	
	  auto frame = Frame::fromNormal(wsSurfaceNormal);
	  auto wo = frame.worldToLocal(-ray.direction);
	
	  return {
	    .pos      = wsHitPoint,
	    .normal   = wsSurfaceNormal,
	    .wo       = wo,
      .frame		= frame,
	    .material = &material,
	  };
	}
};

/*
 * Spawn ray from camera
 * Utility function to reuse in multiple kernels
 */
__attribute__((always_inline))
ray spawnRayFromCamera(constant CameraData& camera, float2 pixel) {
  ray ray;
  ray.origin = camera.position;
  ray.direction = normalize((camera.topLeft
                             + pixel.x * camera.pixelDeltaU
                             + pixel.y * camera.pixelDeltaV
                             ) - camera.position);
  ray.max_distance = INFINITY;
  ray.min_distance = 1e-3f;
  
  return ray;
}

/*
 * Create an intersector marked to intersect opaque triangle geometry
 * Utility function to reuse in multiple kernels
 */
__attribute__((always_inline))
intersector<triangle_data, instancing> createTriangleIntersector() {
  intersector<triangle_data, instancing> i;
  i.accept_any_intersection(false);
  i.assume_geometry_type(geometry_type::triangle);
  i.force_opacity(forced_opacity::opaque);
  
  return i;
}

/*
 * Simple path tracing kernel using BSDF importance sampling.
 */
kernel void pathtracingKernel(
  uint2                                                 tid         				[[thread_position_in_grid]],
  constant Constants&                                   constants   				[[buffer(0)]],
  device void*                                          vertexResources     [[buffer(1)]],
  device void*                                          primitiveResources 	[[buffer(2)]],
  device void*                                          instanceResources 	[[buffer(3)]],
  constant MTLAccelerationStructureInstanceDescriptor*  instances   				[[buffer(4)]],
  instance_acceleration_structure                       accelStruct 				[[buffer(5)]],
//constant ::LightData*                   lights              [[buffer(6)]],
  texture2d<float>                                      src         				[[texture(0)]],
  texture2d<float, access::write>                       dst         				[[texture(1)]],
  texture2d<uint32_t>                                   randomTex   				[[texture(2)]]
) {
  if (tid.x < constants.size.x && tid.y < constants.size.y) {
    constant CameraData& camera = constants.camera;
    float2 pixel(tid.x, tid.y);
    uint32_t offset = randomTex.read(tid).x;
    
    float2 r(samplers::halton(offset + constants.frameIdx, 0),
             samplers::halton(offset + constants.frameIdx, 1));
    pixel += r;
    
    /*
     * Create the resources struct for extracting intersection data
     */
    Resources resources{
      .instances = instances,
      .vertexResources = vertexResources,
      .primitiveResources = primitiveResources,
      .instanceResources = instanceResources,
    };
    
    /*
     * Spawn ray and create an intersector
     */
    auto ray = spawnRayFromCamera(camera, pixel);
    auto i = createTriangleIntersector();
    triangle_instance_intersection intersection;
    
    /*
     * Path tracing
     */
    float3 attenuation(1.0);
    float3 L(0.0);
    for (int bounce = 0; bounce < MAX_BOUNCES; bounce++) {
      intersection = i.intersect(ray, accelStruct);
      
      /*
       * Stop on ray miss
       */
      if (intersection.type == intersection_type::none) {
        L += attenuation * backgroundColor;
        break;
      }
      
      const auto hit = resources.getIntersectionData(ray, intersection);
      
      /*
       * Sample the BSDF to get the next ray direction
       */
      auto r = float4(samplers::halton(offset + constants.frameIdx, 2 + bounce * DIMS_PER_BOUNCE + 0),
                      samplers::halton(offset + constants.frameIdx, 2 + bounce * DIMS_PER_BOUNCE + 1),
                      samplers::halton(offset + constants.frameIdx, 2 + bounce * DIMS_PER_BOUNCE + 2),
                      samplers::halton(offset + constants.frameIdx, 2 + bounce * DIMS_PER_BOUNCE + 3));
      
      auto sample = bsdf::sample(*hit.material, hit.wo, float2(0.0), r);
      
      /*
       * Handle light hit
       */
      if (sample.flags & bsdf::Sample::Emitted) {
        L += attenuation * sample.Le;
      }
      
      if (!(sample.flags & (bsdf::Sample::Reflected | bsdf::Sample::Transmitted))) break;
      
      /*
       * Update attenuation
       */
      attenuation *= sample.f * abs(sample.wi.z) / sample.pdf;
      
      /*
       * Russian roulette
       */
      if (bounce > 0) {
        float q = max(0.0, 1.0 - max(attenuation.r, max(attenuation.g, attenuation.b)));
        if (samplers::halton(offset + constants.frameIdx, 2 + bounce * DIMS_PER_BOUNCE + 4) < q) break;
        attenuation /= 1.0 - q;
      }
      
      /*
       * Update ray and continue on path
       */
      ray.origin = hit.pos;
      ray.direction = normalize(hit.frame.localToWorld(sample.wi));
    }
    
    /*
     * Accumulate samples
     */
    if (constants.frameIdx > 0) {
      float3 L_prev = src.read(tid).xyz;
      
      L += L_prev * constants.frameIdx;
      L /= (constants.frameIdx + 1);
    }
    
    dst.write(float4(L, 1.0f), tid);
  }
}

/*
 * Sample a light from the scene, where the probability of sampling a given light is proportional
 * to its total emitted power. Very simple sampler, but much better than uniform sampling.
 */
constant LightData& sampleLightPower(
  constant LightData* lights,
  constant Constants& constants,
  float r
) {
  r *= constants.totalLightPower;
  
  // Find the first light with cumulative power >= r using a quick binary search
  auto sz = constants.lightCount - 1, idx = 0u;
  while (sz > 0) {
   	auto h = sz >> 1, middle = idx + h;
    auto res = lights[middle].cumulativePower < r;
    idx = res ? (middle + 1) : idx;
  	sz = res ? sz - (h + 1) : h;
  }
  idx = clamp(idx, 0u, constants.lightCount - 1);
  
  return lights[idx];
}

struct LightSample {
  float3 Li;			// Emitted light
  float3 pos;			// Sampled light position				(world space)
  float3 normal;	// Sampled light surface normal (world space)
  float pdf;			// Light sample PDF at sampled position
};

/*
 *
 */
LightSample sampleAreaLight(thread Resources& res, constant LightData& light, float2 r) {
  const device auto& vertices = res.getVertices(light.instanceIdx);
  
  float3 vertexPositions[3];
  float3 vertexNormals[3];
  for (int i = 0; i < 3; i++) {
    vertexPositions[i] = vertices.position[light.indices[i]];
    vertexNormals[i] = vertices.data[light.indices[i]].normal;
  }
  
  auto sampledCoords = samplers::sampleTriUniform(r);
  auto transform = res.getTransform(light.instanceIdx);
  
  auto pos = (transform * float4(interpolate(vertexPositions, sampledCoords), 1.0)).xyz;
  auto normal = normalize((transform * float4(interpolate(vertexNormals, sampledCoords), 0.0)).xyz);
  
  return {
    .Li = light.emission,
    .pos = pos,
    .normal = normal,
    .pdf = 1.0f / light.area,
  };
}

/*
 * A better path tracing kernel using multiple importance sampling to combine NEE with
 * BSDF importance sampling.
 */
kernel void misKernel(
  uint2                                                 tid                 [[thread_position_in_grid]],
  constant Constants&                                   constants           [[buffer(0)]],
  device void*                                          vertexResources     [[buffer(1)]],
  device void*                                          primitiveResources  [[buffer(2)]],
  device void*                                          instanceResources   [[buffer(3)]],
  constant MTLAccelerationStructureInstanceDescriptor*  instances           [[buffer(4)]],
  instance_acceleration_structure                       accelStruct         [[buffer(5)]],
  constant LightData*                   								lights              [[buffer(6)]],
  texture2d<float>                                      src                 [[texture(0)]],
  texture2d<float, access::write>                       dst                 [[texture(1)]],
  texture2d<uint32_t>                                   randomTex           [[texture(2)]]
) {
  if (tid.x < constants.size.x && tid.y < constants.size.y) {
    constant CameraData& camera = constants.camera;
    float2 pixel(tid.x, tid.y);
    uint32_t offset = randomTex.read(tid).x;
    
    float2 r(samplers::halton(offset + constants.frameIdx, 0),
             samplers::halton(offset + constants.frameIdx, 1));
    pixel += r;
    
    /*
     * Create the resources struct for extracting intersection data
     */
    Resources resources{
      .instances = instances,
      .vertexResources = vertexResources,
      .primitiveResources = primitiveResources,
      .instanceResources = instanceResources,
    };
    
    /*
     * Spawn ray and create an intersector
     */
    auto ray = spawnRayFromCamera(camera, pixel);
    auto i = createTriangleIntersector();
    triangle_instance_intersection intersection;
    
    /*
     * Path tracing
     */
    float3 attenuation(1.0);
    float3 L(0.0);
    Hit lastHit;
    bsdf::Sample lastSample;
    for (int bounce = 0; bounce < MAX_BOUNCES; bounce++) {
      intersection = i.intersect(ray, accelStruct);
      
      /*
       * Stop on ray miss
       */
      if (intersection.type == intersection_type::none) {
        L += attenuation * backgroundColor;
        break;
      }
      
      const auto hit = resources.getIntersectionData(ray, intersection);
      
      /*
       * Sample the BSDF to get the next ray direction
       */
      auto r = float4(samplers::halton(offset + constants.frameIdx, 2 + bounce * DIMS_PER_BOUNCE + 0),
                      samplers::halton(offset + constants.frameIdx, 2 + bounce * DIMS_PER_BOUNCE + 1),
                      samplers::halton(offset + constants.frameIdx, 2 + bounce * DIMS_PER_BOUNCE + 2),
                      samplers::halton(offset + constants.frameIdx, 2 + bounce * DIMS_PER_BOUNCE + 3));
      
      auto sample = bsdf::sample(*hit.material, hit.wo, float2(0.0), r);
      
      /*
       * Handle light hit
       */
      if (sample.flags & bsdf::Sample::Emitted && intersection.triangle_front_facing) {
        if (bounce == 0 || lastSample.flags & bsdf::Sample::Specular) {
          L += attenuation * sample.Le;
        } else {
          // TODO: find a way to get an index to the light, so we don't have to calculate the area here
          const device auto& vertices = resources.getVertices(intersection.instance_id);
          const device auto& data = *(device PrimitiveData*) intersection.primitive_data;
          const auto transform = resources.getTransform(intersection.instance_id);
          
          float3 wsVertexPositions[3];
          for (int i = 0; i < 3; i++)
            wsVertexPositions[i] = (transform * float4(vertices.position[data.indices[i]], 1.0)).xyz;
          
          const auto edge1 = wsVertexPositions[1] - wsVertexPositions[0];
          const auto edge2 = wsVertexPositions[2] - wsVertexPositions[0];
          const auto area = length(cross(edge1, edge2)) * 0.5f;
          const auto power = sample.Le * area * M_PI_F;
          
          // Calculate light PDF, BSDF weight and do MIS
          const auto lightPdf = (1.0f / area) * (power / constants.totalLightPower)
          											/ abs(dot(ray.direction, hit.normal));
          const auto bsdfWeight = lastSample.pdf / (lastSample.pdf + lightPdf);
          
          L += attenuation * bsdfWeight * sample.Le / length_squared(lastHit.pos - hit.pos);
        }
      }
      
      if (!(sample.flags & (bsdf::Sample::Reflected | bsdf::Sample::Transmitted))) break;
      
      /*
       * Set new ray origin, this is the same used for NEE and for the next bounce
       */
      ray.origin = hit.pos;
      
      /*
       * Calculate direct lighting contribution
       */
      if (!(sample.flags & (bsdf::Sample::Emitted | bsdf::Sample::Specular)) && constants.lightCount > 0) {
        auto r = float3(samplers::halton(offset + constants.frameIdx, 2 + bounce * DIMS_PER_BOUNCE + 4),
                        samplers::halton(offset + constants.frameIdx, 2 + bounce * DIMS_PER_BOUNCE + 5),
                        samplers::halton(offset + constants.frameIdx, 2 + bounce * DIMS_PER_BOUNCE + 6));
        
        const constant auto& light = sampleLightPower(lights, constants, r.z);
        const auto pLight = light.power / constants.totalLightPower; // Probability of sampling this light
        
        const auto lightSample = sampleAreaLight(resources, light, r.xy);
        const auto wiWorldSpace = normalize(lightSample.pos - hit.pos);
        const auto wi = hit.frame.worldToLocal(wiWorldSpace);
        const auto bsdfEval = bsdf::eval(*hit.material, hit.wo, wi, float2(0.0));
        
        ray.direction = wiWorldSpace;
        ray.max_distance = length(lightSample.pos - hit.pos) - 1e-3f;
        i.accept_any_intersection(true);
        intersection = i.intersect(ray, accelStruct);
        auto occluded = intersection.type != intersection_type::none;
        i.accept_any_intersection(false);
        
        if (length_squared(bsdfEval.f) > 0.0f && !occluded) {
          auto pdfLight = pLight * lightSample.pdf / abs(dot(lightSample.normal, wiWorldSpace));
          auto Ld = lightSample.Li * bsdfEval.f * abs(dot(hit.normal, wiWorldSpace)) 	// Base lighting term
          					/ length_squared(lightSample.pos - hit.pos)												// Distance attenuation
          					/ (pdfLight + bsdfEval.pdf);																			// MIS weight/pdf (simplified)
          L += attenuation * Ld;
        }
      }
      
      /*
       * Update attenuation
       */
      attenuation *= sample.f * abs(sample.wi.z) / sample.pdf;
      
      /*
       * Russian roulette
       */
      if (bounce > 0) {
        float q = max(0.0, 1.0 - max(attenuation.r, max(attenuation.g, attenuation.b)));
        if (samplers::halton(offset + constants.frameIdx, 2 + bounce * DIMS_PER_BOUNCE + 7) < q) break;
        attenuation /= 1.0 - q;
      }
      
      /*
       * Update ray and continue on path
       */
      ray.max_distance = INFINITY;
      ray.direction = normalize(hit.frame.localToWorld(sample.wi));
      lastHit = hit;
      lastSample = sample;
    }
    
    /*
     * Accumulate samples
     */
    if (constants.frameIdx > 0) {
      float3 L_prev = src.read(tid).xyz;
      
      L += L_prev * constants.frameIdx;
      L /= (constants.frameIdx + 1);
    }
    
    dst.write(float4(L, 1.0f), tid);
  }
}
