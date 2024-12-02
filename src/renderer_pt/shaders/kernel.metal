#include <metal_stdlib>

// Header files use this guard to only include what the shader needs
#define METAL_SHADER

#include "../../core/mesh.hpp"
#include "../../core/material.hpp"
#include "../pt_shader_defs.hpp"

#define MAX_BOUNCES 15
#define DIMS_PER_BOUNCE 5

using namespace metal;
using namespace raytracing;
using namespace pt::shaders_pt;

constant uint32_t resourcesStride [[function_constant(0)]];

constant float3 backgroundColor(0.0, 0.0, 0.0);

struct VertexResource {
  device pt::VertexData* data;
};

struct PrimitiveResource {
  device uint32_t* materialSlot;
};

struct InstanceResource {
  device pt::Material* materials;
};

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

namespace samplers {
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
}

namespace bsdf {
  inline float3 schlick(float3 f0, float cosTheta) {
    const auto k = 1.0f - cosTheta;
    const auto k2 = k * k;
    return f0 + (float3(1.0) - f0) * (k2 * k2 * k);
  }
  
  class GGX {
  public:
    explicit GGX(float roughness) noexcept
    : m_alpha(roughness * roughness) {}
    
    GGX(float roughness, float anisotropic) noexcept {
      const auto alpha = roughness * roughness;
      const auto aspect = sqrt(1.0f - 0.9f * anisotropic);
      m_alpha = float2(alpha / aspect, alpha * aspect);
    }
    
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
    
    inline float g1(float3 w) const {
      return 1.0f / (1.0f + lambda(w));
    }
    
    inline float g(float3 wo, float3 wi) const {
      return 1.0f / (1.0f + lambda(wo) + lambda(wi));
    }
    
    inline float vmdf(float3 w, float3 wm) const {
      return g1(w) / abs(w.z) * mdf(wm) * abs(dot(w, wm));
    }
    
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
    
    inline bool isSmooth() const {
      return m_alpha.x < 1e-3f && m_alpha.y < 1e-3f;
    }
    
  private:
    float2 m_alpha;
    
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
    
    float3 f;
    float3 Le;
    float3 wi;
    float pdf;
    int flags = 0;
  };
  
  Sample sampleMetallic(thread const pt::Material& mat, float3 wo, thread const GGX& ggx, float3 r) {
    if (ggx.isSmooth()) {
      const auto f_ss = schlick(mat.baseColor.xyz, wo.z);
      
      return {
        .flags  = Sample::Reflected | Sample::Specular,
        .f      = f_ss / abs(wo.z),
        .Le     = float3(0.0),
        .wi     = float3(-wo.xy, wo.z),
        .pdf    = 1.0f,
      };
    }
    
    auto wm = ggx.sampleVmdf(wo, r.xy);
    auto wi = -reflect(wo, wm);
    if (wo.z * wi.z < 0.0f) return {};
    
    const auto woDotWm = abs(dot(wo, wm));
    const auto pdf = ggx.vmdf(wo, wm) / (4.0f * woDotWm);
    
    const auto cosTheta_o = abs(wo.z), cosTheta_i = abs(wi.z);
    const auto f_ss = schlick(mat.baseColor.xyz, woDotWm);
    const auto m_ss = ggx.mdf(wm) * ggx.g(wo, wi) / (4 * cosTheta_o * cosTheta_i);
    
    return {
      .flags	= Sample::Reflected | Sample::Glossy,
      .f 			= f_ss * m_ss,
      .Le 		= float3(0.0),
      .wi			= wi,
      .pdf		= pdf,
    };
  }
  
  Sample sampleDielectric(thread const pt::Material& mat, float3 wo, thread const GGX& ggx, float3 r) {
    // TODO: dielectric
    return {};
  }
  
  Sample sampleGlossy(thread const pt::Material& mat, float3 wo, thread const GGX& ggx, float3 r) {
    // TODO: glossy
    
    auto wi = samplers::sampleCosineHemisphere(r.xy);
    if (wo.z < 0.0f) wi *= -1.0f;
    
//    const auto cosTheta_i = wi.z;
    auto flags = Sample::Reflected | Sample::Diffuse;
    if (mat.flags & pt::Material::Material_Emissive) flags |= Sample::Emitted;
    
    return {
      .flags  = flags,
      .f      = mat.baseColor.xyz,
      .Le     = mat.emission * mat.emissionStrength,
      .wi     = wi,
      .pdf    = abs(wi.z),
    };
  }
  
  Sample sample(thread const pt::Material& material, float3 wo, float2 uv, float4 r) {
    GGX ggx(material.roughness, material.anisotropy);
    
    const auto pMetallic = material.metallic;
    const auto pDielectric = material.metallic + (1.0f - material.metallic) * material.transmission;
    
    if (r.w < pMetallic) return sampleMetallic(material, wo, ggx, r.xyz);
    if (r.w < pDielectric) return sampleDielectric(material, wo, ggx, r.xyz);
    return sampleGlossy(material, wo, ggx, r.xyz);
  }
}

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

template<typename T>
T interpolate(thread T* att, float2 uv) {
  return (1.0f - uv.x - uv.y) * att[0] + uv.x * att[1] + uv.y * att[2];
}

__attribute__((always_inline))
float3 transformVec(float3 p, float4x4 transform) {
  return (transform * float4(p.x, p.y, p.z, 0.0f)).xyz;
}

kernel void pathtracingKernel(
  uint2                                                 tid         				[[thread_position_in_grid]],
  constant Constants&                                   constants   				[[buffer(0)]],
  device void*                                          vertexResources     [[buffer(1)]],
  device void*                                          primitiveResources 	[[buffer(2)]],
  device void*                                          instanceResources 	[[buffer(3)]],
  constant MTLAccelerationStructureInstanceDescriptor*  instances   				[[buffer(4)]],
  instance_acceleration_structure                       accelStruct 				[[buffer(5)]],
  texture2d<float>                                      src         				[[texture(0)]],
  texture2d<float, access::write>                       dst         				[[texture(1)]],
  texture2d<uint32_t>                                   randomTex   				[[texture(2)]]
) {
  if (tid.x < constants.size.x && tid.y < constants.size.y) {
    constant CameraData& camera = constants.camera;
    float2 pixel(tid.x, tid.y);
    uint32_t offset = randomTex.read(tid).x;
    
    float2 r(halton(offset + constants.frameIdx, 0),
             halton(offset + constants.frameIdx, 1));
    pixel += r;
    
    /*
     * Spawn ray
     */
    ray ray;
    ray.origin = camera.position;
    ray.direction = normalize((camera.topLeft
                               + pixel.x * camera.pixelDeltaU
                               + pixel.y * camera.pixelDeltaV
                               ) - camera.position);
    ray.max_distance = INFINITY;
    
    float3 attenuation(1.0);
    float3 L(0.0);
    
    /*
     * Create an intersector
     */
    intersector<triangle_data, instancing> i;
    i.accept_any_intersection(false);
    i.assume_geometry_type(geometry_type::triangle);
    i.force_opacity(forced_opacity::opaque);
    
    typename intersector<triangle_data, instancing>::result_type intersection;
    
    /*
     * Path tracing
     */
    for (int bounce = 0; bounce < MAX_BOUNCES; bounce++) {
      intersection = i.intersect(ray, accelStruct);
      
      if (intersection.type == intersection_type::none) {
        L = attenuation * backgroundColor;
        break;
      }
      
      auto instanceIdx = intersection.instance_id;
      auto geometryIdx = instances[instanceIdx].accelerationStructureIndex;
      
      device auto& vertexResource = *(device VertexResource*)((device uint64_t*)vertexResources + geometryIdx);
      device auto& primitiveResource = *(device PrimitiveResource*)((device uint64_t*)primitiveResources + geometryIdx);
      device auto& instanceResource = *(device InstanceResource*)((device uint64_t*)instanceResources + instanceIdx);
      device auto& data = *(device PrimitiveData*) intersection.primitive_data;
      
      auto materialSlot = *primitiveResource.materialSlot;
      auto material = instanceResource.materials[materialSlot];
      
      float3 vertexNormals[3];
      for (int i = 0; i < 3; i++) {
        vertexNormals[i] = vertexResource.data[data.indices[i]].normal;
      }
      
      float2 barycentricCoords = intersection.triangle_barycentric_coord;
      float3 surfaceNormal = interpolate(vertexNormals, barycentricCoords);
      
      float4x4 objectToWorld(1.0);
      for (int i = 0; i < 4; i++)
        for (int j = 0; j < 3; j++)
          objectToWorld[i][j] = instances[instanceIdx].transformationMatrix[i][j];
      
      float3 wsHitPoint = ray.origin + ray.direction * intersection.distance;
      float3 wsSurfaceNormal = normalize(transformVec(surfaceNormal, objectToWorld));
      
      auto frame = Frame::fromNormal(wsSurfaceNormal);
      auto wo = frame.worldToLocal(-ray.direction);
      
      auto r = float4(halton(offset + constants.frameIdx, 2 + bounce * DIMS_PER_BOUNCE + 0),
                 halton(offset + constants.frameIdx, 2 + bounce * DIMS_PER_BOUNCE + 1),
                 halton(offset + constants.frameIdx, 2 + bounce * DIMS_PER_BOUNCE + 2),
                 halton(offset + constants.frameIdx, 2 + bounce * DIMS_PER_BOUNCE + 3));
      
      auto sample = bsdf::sample(material, wo, float2(0.0), r);
      
      ray.origin = wsHitPoint + wsSurfaceNormal * 1e-3f;
      ray.direction = frame.localToWorld(sample.wi);
      
      if (sample.flags & bsdf::Sample::Emitted) {
        L += attenuation * sample.Le;
        break; // TODO: potentially continue path on light hit
      }
      
      if (!(sample.flags & (bsdf::Sample::Reflected | bsdf::Sample::Transmitted))) break;
      
      attenuation *= sample.f * abs(sample.wi.z) / sample.pdf;
      
      /*
       * Russian roulette
       */
      if (bounce > 0) {
        float q = max(0.0, 1.0 - max(attenuation.r, max(attenuation.g, attenuation.b)));
        if (halton(offset + constants.frameIdx, 2 + bounce * DIMS_PER_BOUNCE + 4) < q) break;
        attenuation /= 1.0 - q;
      }
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
