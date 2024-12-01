#include <metal_stdlib>

// Header files use this guard to only include what the shader needs
#define METAL_SHADER

#include "../../core/mesh.hpp"
#include "../pt_shader_defs.hpp"

#define MAX_BOUNCES 15

using namespace metal;
using namespace raytracing;
using namespace pt::shaders_pt;

constant uint32_t resourcesStride [[function_constant(0)]];

constant float3 backgroundColor(0.65, 0.8, 0.9);

struct VertexResource {
  device pt::VertexData* data;
};

constant unsigned int primes[] = {
  2,   3,  5,  7,
  11, 13, 17, 19,
  23, 29, 31, 37,
  41, 43, 47, 53,
  59, 61, 67, 71,
  73, 79, 83, 89
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

inline float3 sampleCosineHemisphere(float2 u) {
  float phi = 2.0f * M_PI_F * u.x;
  
  float cos_phi;
  float sin_phi = sincos(phi, cos_phi);
  
  float cos_theta = sqrt(u.y);
  float sin_theta = sqrt(1.0f - cos_theta * cos_theta);
  
  return float3(sin_theta * cos_phi, cos_theta, sin_theta * sin_phi);
}

inline float3 alignHemisphereWithNormal(float3 sample, float3 normal) {
  const float3 a = abs(normal.x) > 0.5 ? float3(0, 0, 1) : float3(1, 0, 0);
  
  float3 forward = normalize(cross(normal, a));
  float3 right = cross(forward, normal);
  
  return sample.x * right + sample.y * normal + sample.z * forward;
}

template<typename T>
T interpolate(thread T* att, float2 uv) {
  return (1.0f - uv.x - uv.y) * att[0] + uv.x * att[1] + uv.y * att[2];
}

__attribute__((always_inline))
float3 transformVec(float3 p, float4x4 transform) {
  return (transform * float4(p.x, p.y, p.z, 0.0f)).xyz;
}

kernel void pathtracingKernel(
  uint2                                                 tid         [[thread_position_in_grid]],
  constant Constants&                                   constants   [[buffer(0)]],
  device void*                                          resources   [[buffer(1)]],
  constant MTLAccelerationStructureInstanceDescriptor*  instances   [[buffer(2)]],
  instance_acceleration_structure                       accelStruct [[buffer(3)]],
  texture2d<float>                                      src         [[texture(0)]],
  texture2d<float, access::write>                       dst         [[texture(1)]],
  texture2d<uint32_t>                                   randomTex   [[texture(2)]]
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
      
      device auto& vertexResource =
      *(device VertexResource*)((device uint64_t*)resources + geometryIdx);
      device auto& data = *(device PrimitiveData*) intersection.primitive_data;
      
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
      
      r = float2(halton(offset + constants.frameIdx, 2 + bounce * 3 + 0),
                 halton(offset + constants.frameIdx, 2 + bounce * 3 + 1));
      
      float3 wsSampleDirection = sampleCosineHemisphere(r);
      
      ray.origin = wsHitPoint + wsSurfaceNormal * 1e-3f;
      ray.direction = alignHemisphereWithNormal(wsSampleDirection, wsSurfaceNormal);
      
      attenuation *= 0.8;
      
      /*
       * Russian roulette
       */
      if (bounce > 0) {
        float q = max(0.0, 1.0 - max(attenuation.r, max(attenuation.g, attenuation.b)));
        if (halton(offset + constants.frameIdx, 2 + bounce * 3 + 2) < q) break;
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
