#include <metal_stdlib>

// Header files use this guard to only include what the shader needs
#define METAL_SHADER

#include "../../core/mesh.hpp"
#include "../../core/material.hpp"
#include "../pt_shader_defs.hpp"

#include "defs.metal"

#define MAX_BOUNCES 50
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
constant float3 backgroundColor(0.0);

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

__attribute__((always_inline))
float3 transformPoint(float3 p, float4x4 transform) {
  return (transform * float4(p.x, p.y, p.z, 1.0f)).xyz;
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
 * Groups all the intersection data relevant to us for shading.
 */
struct Hit {
  float3 pos;														// Hit position 						(world space)
  float3 normal;                        // Surface normal           (world space)
  float3 geometricNormal;								// Geometric (face) normal 	(world space)
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
	
    float3 vertexPositions[3];
	  float3 vertexNormals[3];
	  for (int i = 0; i < 3; i++) {
      vertexPositions[i] = vertexResource.position[data.indices[i]];
	    vertexNormals[i] = vertexResource.data[data.indices[i]].normal;
	    // TODO: Interpolate UVs
	  }
	
	  float2 barycentricCoords = intersection.triangle_barycentric_coord;
	  float3 surfaceNormal = interpolate(vertexNormals, barycentricCoords);
    float3 geometricNormal = normalize(cross(vertexPositions[1] - vertexPositions[0], vertexPositions[2] - vertexPositions[0]));
	
    float4x4 objectToWorld = getTransform(instanceIdx);
	
	  float3 wsHitPoint = ray.origin + ray.direction * intersection.distance;
    float3 wsSurfaceNormal = normalize(transformVec(surfaceNormal, objectToWorld));
    float3 wsGeometricNormal = normalize(transformVec(geometricNormal, objectToWorld));
	
	  auto frame = Frame::fromNormal(wsSurfaceNormal);
	  auto wo = frame.worldToLocal(-ray.direction);
	
	  return {
	    .pos      = wsHitPoint,
	    .normal   = wsSurfaceNormal,
      .geometricNormal = wsGeometricNormal,
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
  texture2d<float>                                      src         				[[texture(0)]],
  texture2d<float, access::write>                       dst         				[[texture(1)]],
  texture2d<uint32_t>                                   randomTex   				[[texture(2)]],
  texture2d<float>                                      ggxLutE             [[texture(3)]],
  texture1d<float>                                      ggxLutEavg          [[texture(4)]],
  texture3d<float>                                      ggxLutMsE           [[texture(5)]],
  texture2d<float>                                      ggxLutMsEavg        [[texture(6)]],
  texture3d<float>                                      ggxLutETransIn      [[texture(7)]],
  texture3d<float>                                      ggxLutETransOut     [[texture(8)]],
  texture2d<float>                                      ggxLutEavgTransIn   [[texture(9)]],
  texture2d<float>                                      ggxLutEavgTransOut  [[texture(10)]]
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
      
      auto bsdf = bsdf::BSDF(*hit.material, ggxLutE, ggxLutEavg, ggxLutMsE, ggxLutMsEavg, ggxLutETransIn, ggxLutETransOut, ggxLutEavgTransIn, ggxLutEavgTransOut, constants);
      auto sample = bsdf.sample(hit.wo, float2(0.0), r);
      
      /*
       * Handle light hit
       */
      if (sample.flags & bsdf::Sample_Emitted) {
        L += attenuation * sample.Le;
      }
      
      if (!(sample.flags & (bsdf::Sample_Reflected | bsdf::Sample_Transmitted))) break;
      
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
  float3 normal;  // Sampled light surface normal (world space)
  float3 wi;			// Surface -> light direction		(world space)
  float pdf;			// Light sample PDF at sampled position
};

/*
 * Sample an area light.
 */
LightSample sampleAreaLight(thread const Hit& hit, thread Resources& res, constant LightData& light, float2 r) {
  const device auto& vertices = res.getVertices(light.instanceIdx);
  
  float3 vertexPositions[3];
  for (int i = 0; i < 3; i++) {
    vertexPositions[i] = vertices.position[light.indices[i]];
  }
  
  const float2 sampledCoords = samplers::sampleTriUniform(r);
  const auto transform = res.getTransform(light.instanceIdx);
  
  const float3 osNormal = cross(vertexPositions[1] - vertexPositions[0], vertexPositions[2] - vertexPositions[0]);
  
  const float3 pos = transformPoint(interpolate(vertexPositions, sampledCoords), transform);
  const float3 normal = normalize(transformVec(osNormal, transform));
  
  const float3 wi = normalize(pos - hit.pos);
  return {
    .Li = light.emission,
    .pos = pos,
    .normal = normal,
    .wi = wi,
    .pdf = length_squared(pos - hit.pos) / (abs(dot(normal, wi)) * light.area),
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
  texture2d<uint32_t>                                   randomTex           [[texture(2)]],
  texture2d<float>                                      ggxLutE             [[texture(3)]],
  texture1d<float>																			ggxLutEavg					[[texture(4)]],
  texture3d<float>                                      ggxLutMsE           [[texture(5)]],
  texture2d<float>                                      ggxLutMsEavg        [[texture(6)]],
  texture3d<float>                                      ggxLutETransIn      [[texture(7)]],
  texture3d<float>                                      ggxLutETransOut     [[texture(8)]],
  texture2d<float>                                      ggxLutEavgTransIn   [[texture(9)]],
  texture2d<float>                                      ggxLutEavgTransOut  [[texture(10)]]
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
      
      auto bsdf = bsdf::BSDF(*hit.material, ggxLutE, ggxLutEavg, ggxLutMsE, ggxLutMsEavg, ggxLutETransIn, ggxLutETransOut, ggxLutEavgTransIn, ggxLutEavgTransOut, constants);
      auto sample = bsdf.sample(hit.wo, float2(0.0), r);
      
      /*
       * Handle light hit
       */
      if (sample.flags & bsdf::Sample_Emitted) {
        if (bounce == 0 || lastSample.flags & bsdf::Sample_Specular) {
          L += attenuation * sample.Le;
        } else {
          // Calculate light PDF, BSDF weight and do MIS.
          // Sampling pdf is 1 / area, light sample pdf is power / totalPower
          // Because power = Le * pi * area, the areas cancel each other out and we can simplify
          const float lightPdf = (dot(sample.Le, float3(0, 1, 0)) * M_PI_F / constants.totalLightPower)
                                * length_squared(lastHit.pos - hit.pos)
          											/ abs(dot(ray.direction, hit.geometricNormal));
          const float bsdfWeight = lastSample.pdf / (lastSample.pdf + lightPdf);
          
          L += attenuation * bsdfWeight * sample.Le;
        }
      }
      
      /*
       * Set new ray origin, this is the same used for NEE and for the next bounce
       */
      ray.origin = hit.pos;
      
      /*
       * Calculate direct lighting contribution
       */
      if (!(sample.flags & (bsdf::Sample_Emitted | bsdf::Sample_Specular)) && constants.lightCount > 0) {
        auto r = float3(samplers::halton(offset + constants.frameIdx, 2 + bounce * DIMS_PER_BOUNCE + 4),
                        samplers::halton(offset + constants.frameIdx, 2 + bounce * DIMS_PER_BOUNCE + 5),
                        samplers::halton(offset + constants.frameIdx, 2 + bounce * DIMS_PER_BOUNCE + 6));
        
        const constant auto& light = sampleLightPower(lights, constants, r.z);
        const float pLight = light.power / constants.totalLightPower; // Probability of sampling this light
        
        const auto lightSample = sampleAreaLight(hit, resources, light, r.xy);
        const float3 wi = hit.frame.worldToLocal(lightSample.wi);
        const auto bsdfEval = bsdf.eval(hit.wo, wi, float2(0.0), sample.lobe);
        
        if (length_squared(bsdfEval.f) > 0.0f) {
          ray.direction = lightSample.wi;
          ray.max_distance = length(lightSample.pos - hit.pos) - 1e-3f;
          i.accept_any_intersection(true);
          intersection = i.intersect(ray, accelStruct);
          auto occluded = intersection.type != intersection_type::none;
          i.accept_any_intersection(false);
          
          if (!occluded) {
            float pdfLight = pLight * lightSample.pdf;
            float3 Ld = lightSample.Li * bsdfEval.f * abs(wi.z)  // Base lighting term
                      / (pdfLight + bsdfEval.pdf);               // MIS weight/pdf (simplified)
            L += attenuation * Ld;
          }
        }
      }
      
      /*
       * If the ray wasn't reflected or transmitted, we can end tracing here
       */
      if (!(sample.flags & (bsdf::Sample_Reflected | bsdf::Sample_Transmitted))) break;
      
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
