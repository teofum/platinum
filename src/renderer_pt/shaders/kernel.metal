#include <metal_stdlib>

#include "defs.metal"

#define MAX_BOUNCES 50
#define DIMS_PER_BOUNCE 10

/*
 * Miscellaneous helper functions
 */
__attribute__((always_inline))
float3 transformVec(float3 p, float4x4 transform) {
  return (transform * float4(p.x, p.y, p.z, 0.0f)).xyz;
}

__attribute__((always_inline))
float3 transformPoint(float3 p, float4x4 transform) {
  return (transform * float4(p.x, p.y, p.z, 1.0f)).xyz;
}

__attribute__((always_inline))
float2 rayDirToUv(float3 dir) {
  float phi = atan2(-dir.z, -dir.x);
  
  float theta = acos(dir.y);
  return float2(phi / (2.0 * M_PI_F), theta / M_PI_F);
}

__attribute__((always_inline))
float3 uvToRayDir(float2 uv) {
  float y;
  float r = sincos(uv.y * M_PI_F, y);
  float cosPhi;
  float sinPhi = sincos(uv.x * 2.0f * M_PI_F, cosPhi);
  
  return normalize(float3(-cosPhi * r, y, -sinPhi * r));
}

/*
 * Coordinate frame, we use this over a 4x4 matrix because it allows easy conversion both ways
 * without having to calculate the inverse.
 */
struct Frame {
  float3 x, y, z;
  
  static Frame fromNormal(float3 n) {
    float3 a = abs(n.x) > 0.5 ? float3(0, 0, 1) : float3(1, 0, 0);
    
    float3 b = normalize(cross(n, a));
    float3 t = cross(n, b);
    
    return {t, b, n};
  }
  
  static Frame fromNT(float3 n, float3 t, float sign = 1.0) {
    if (abs(dot(n, t)) > 0.9) return fromNormal(n);

    float3 b = normalize(cross(n, t)) * sign;
    t = cross(b, n);
    
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
  float2 uv;                            // Surface UVs at hit position
  float3 wo;                            // Outgoing light direction (tangent space)
  Frame frame;													// Shading coordinate frame, Z-up normal aligned
  device const MaterialGPU* material;		// Material
};

/*
 * Holds references to all the necessary resources.
 * This is essentially a parameter object to keep the getIntersectionData() function call short.
 */
struct Resources {
  device MTLAccelerationStructureInstanceDescriptor* instances;
  const device VertexResource* vertexResources;
  const device PrimitiveResource* primitiveResources;
  const device InstanceResource* instanceResources;
  device Texture* textures;
  
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
	
    device auto& vertexResource = vertexResources[geometryIdx];
	  device auto& primitiveResource = primitiveResources[geometryIdx];
    device auto& instanceResource = instanceResources[instanceIdx];
    
	  device auto& data = *(device PrimitiveData*) intersection.primitive_data;
	
    auto materialSlot = primitiveResource.materialSlot[intersection.primitive_id];
	  device const auto& material = instanceResource.materials[materialSlot];
	
    float3 vertexPositions[3];
    float3 vertexNormals[3];
    float3 vertexTangents[3];
	  float2 vertexTexCoords[3];
    for (int i = 0; i < 3; i++) {
      vertexPositions[i] = vertexResource.position[data.indices[i]];
      vertexNormals[i] = vertexResource.data[data.indices[i]].normal;
      vertexTangents[i] = vertexResource.data[data.indices[i]].tangent.xyz;
      vertexTexCoords[i] = vertexResource.data[data.indices[i]].texCoords;
    }
    float tangentSign = vertexResource.data[data.indices[0]].tangent.w;
	
	  float2 barycentricCoords = intersection.triangle_barycentric_coord;
    float3 surfaceNormal = interpolate(vertexNormals, barycentricCoords);
    float3 surfaceTangent = interpolate(vertexTangents, barycentricCoords);
    float2 surfaceUV = interpolate(vertexTexCoords, barycentricCoords);
    float3 geometricNormal = normalize(cross(vertexPositions[1] - vertexPositions[0], vertexPositions[2] - vertexPositions[0]));
	
    float4x4 objectToWorld = getTransform(instanceIdx);
	
	  float3 wsHitPoint = ray.origin + ray.direction * intersection.distance;
    float3 wsSurfaceNormal = normalize(transformVec(surfaceNormal, objectToWorld));
    float3 wsSurfaceTangent = normalize(transformVec(surfaceTangent, objectToWorld));
    float3 wsGeometricNormal = normalize(transformVec(geometricNormal, objectToWorld));
	
    auto frame = Frame::fromNT(wsSurfaceNormal, wsSurfaceTangent, tangentSign);
    
    if (material.normalTextureId >= 0) {
      constexpr sampler s(address::repeat, filter::linear);
      float3 sampledNormal = textures[material.normalTextureId].tex.sample(s, surfaceUV).rgb * 2.0 - 1.0;
      
      wsSurfaceNormal = frame.localToWorld(sampledNormal);
      frame = Frame::fromNormal(wsSurfaceNormal);
    }
    
	  auto wo = frame.worldToLocal(-ray.direction);
	
	  return {
	    .pos      = wsHitPoint,
	    .normal   = wsSurfaceNormal,
      .geometricNormal = wsGeometricNormal,
      .uv				= surfaceUV,
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
  
  return i;
}

/*
 * Simple path tracing kernel using BSDF importance sampling.
 */
kernel void pathtracingKernel(
  uint2                                                 tid         				[[thread_position_in_grid]],
	constant Arguments&                                   args								[[buffer(0)]],
  texture2d<float>                                      src         				[[texture(0)]],
  texture2d<float, access::write>                       dst         				[[texture(1)]],
  texture2d<uint32_t>                                   randomTex   				[[texture(2)]]
) {
  if (tid.x < args.constants.size.x && tid.y < args.constants.size.y) {
    constant CameraData& camera = args.constants.camera;
    float2 pixel(tid.x, tid.y);
    uint32_t offset = randomTex.read(tid).x;
    
    uint32_t samplerOffset = offset + args.constants.frameIdx;
    float2 r(samplers::halton(samplerOffset, 0),
             samplers::halton(samplerOffset, 1));
    pixel += r;
    
    /*
     * Create the resources struct for extracting intersection data
     */
    Resources resources{
      .instances = args.instances,
      .vertexResources = args.vertexResources,
      .primitiveResources = args.primitiveResources,
      .instanceResources = args.instanceResources,
      .textures = args.textures,
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
      float ir = samplers::halton(samplerOffset, 2 + bounce * DIMS_PER_BOUNCE + 0);
      intersection = i.intersect(ray, args.accelStruct, args.intersectionFunctionTable, ir);
      
      /*
       * Stop on ray miss
       */
      if (intersection.type == intersection_type::none) {
        for (uint32_t i = 0; i < args.constants.envLightCount; i++) {
          device auto& envLight = args.envLights[i];
          device auto& texture = args.textures[envLight.textureId];
          
          constexpr sampler s(address::repeat, filter::linear);
          L += attenuation * texture.tex.sample(s, rayDirToUv(ray.direction)).rgb;
        }
        
        L += attenuation * backgroundColor;
        break;
      }
      
      const auto hit = resources.getIntersectionData(ray, intersection);
      
      /*
       * Sample the BSDF to get the next ray direction
       */
      auto r = float4(samplers::halton(samplerOffset, 2 + bounce * DIMS_PER_BOUNCE + 1),
                      samplers::halton(samplerOffset, 2 + bounce * DIMS_PER_BOUNCE + 2),
                      samplers::halton(samplerOffset, 2 + bounce * DIMS_PER_BOUNCE + 3),
                      samplers::halton(samplerOffset, 2 + bounce * DIMS_PER_BOUNCE + 4));
      
      bsdf::ShadingContext ctx(*hit.material, hit.uv, args.textures);
      auto bsdf = bsdf::BSDF(ctx, args.constants, args.luts);
      auto sample = bsdf.sample(hit.wo, r);
      
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
        if (samplers::halton(samplerOffset, 2 + bounce * DIMS_PER_BOUNCE + 5) < q) break;
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
    if (args.constants.frameIdx > 0) {
      float3 L_prev = src.read(tid).xyz;
      
      L += L_prev * args.constants.frameIdx;
      L /= (args.constants.frameIdx + 1);
    }
    
    dst.write(float4(L, 1.0f), tid);
  }
}

/*
 * Sample a light from the scene, where the probability of sampling a given light is proportional
 * to its total emitted power. Very simple sampler, but much better than uniform sampling.
 */
device AreaLight& sampleLightPower(
  device AreaLight* lights,
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
LightSample sampleAreaLight(thread const Hit& hit, thread Resources& res, device AreaLight& light, float2 r) {
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
 * Sample an environment light
 */
LightSample sampleEnvironmentLight(thread const Hit& hit, const device Texture* textures, device EnvironmentLight& light, float2 r) {
  // Sample the alias table
  auto texture = textures[light.textureId].tex;
  uint64_t w = texture.get_width(), h = texture.get_height();
  uint64_t n = w * h;
  uint64_t i = min(n - 1, size_t(r.x * n));
  
  if (r.y >= light.alias[i].p) i = light.alias[i].aliasIdx;
  
  uint64_t x = i % w, y = i / w;
  float2 uv(float(x) / float(w), float(y) / float(h));
  
  constexpr sampler s(address::repeat, filter::linear);
  const float3 Le = texture.sample(s, uv).rgb;
  
  float3 wi = uvToRayDir(uv);
  
  return {
    .Li = Le,
    .pos = wi * 100.0,
    .normal = -wi,
    .wi = wi,
    .pdf = light.alias[i].pdf / (4.0 * M_PI_F),
  };
}

/*
 * A better path tracing kernel using multiple importance sampling to combine NEE with
 * BSDF importance sampling.
 */
kernel void misKernel(
  uint2                                                 tid                 [[thread_position_in_grid]],
  constant Arguments&                                   args           			[[buffer(0)]],
  texture2d<float>                                      src                 [[texture(0)]],
  texture2d<float, access::write>                       dst                 [[texture(1)]],
  texture2d<uint32_t>                                   randomTex           [[texture(2)]]
) {
  if (tid.x < args.constants.size.x && tid.y < args.constants.size.y) {
    constant CameraData& camera = args.constants.camera;
    float2 pixel(tid.x, tid.y);
    uint32_t offset = randomTex.read(tid).x;
    
    uint32_t samplerOffset = offset + args.constants.frameIdx;
    float2 r(samplers::halton(samplerOffset, 0),
             samplers::halton(samplerOffset, 1));
    pixel += r;
    
    /*
     * Create the resources struct for extracting intersection data
     */
    Resources resources{
      .instances = args.instances,
      .vertexResources = args.vertexResources,
      .primitiveResources = args.primitiveResources,
      .instanceResources = args.instanceResources,
      .textures = args.textures,
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
      float ir = samplers::halton(samplerOffset, 2 + bounce * DIMS_PER_BOUNCE + 0);
      intersection = i.intersect(ray, args.accelStruct, args.intersectionFunctionTable, ir);
      
      /*
       * Stop on ray miss
       */
      if (intersection.type == intersection_type::none) {
        for (uint32_t i = 0; i < args.constants.envLightCount; i++) {
          device auto& envLight = args.envLights[i];
          device auto& texture = args.textures[envLight.textureId].tex;
          
          constexpr sampler s(address::repeat, filter::linear);
          float2 uv = rayDirToUv(ray.direction);
          const float3 Le = texture.sample(s, uv).rgb;
          
          if (bounce == 0 || lastSample.flags & bsdf::Sample_Specular) {
            L += attenuation * Le;
          } else {
            uint32_t w = texture.get_width();
            uint32_t h = texture.get_height();
            uint32_t x = w * uv.x;
            uint32_t y = h * uv.y;
            
            float lightPdf = envLight.alias[y * w + x].pdf * 0.25 * M_1_PI_F;
            float bsdfWeight = lastSample.pdf / (lastSample.pdf + lightPdf);
            
            L += attenuation * bsdfWeight * Le;
          }
        }
        
        L += attenuation * backgroundColor;
        break;
      }
      
      const auto hit = resources.getIntersectionData(ray, intersection);
      
      /*
       * Sample the BSDF to get the next ray direction
       */
      auto r = float4(samplers::halton(samplerOffset, 2 + bounce * DIMS_PER_BOUNCE + 1),
                      samplers::halton(samplerOffset, 2 + bounce * DIMS_PER_BOUNCE + 2),
                      samplers::halton(samplerOffset, 2 + bounce * DIMS_PER_BOUNCE + 3),
                      samplers::halton(samplerOffset, 2 + bounce * DIMS_PER_BOUNCE + 4));
      
      bsdf::ShadingContext ctx(*hit.material, hit.uv, args.textures);
      auto bsdf = bsdf::BSDF(ctx, args.constants, args.luts);
      auto sample = bsdf.sample(hit.wo, r);
      
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
          const float lightPdf = (dot(sample.Le, float3(0, 1, 0)) * M_PI_F / args.constants.totalLightPower)
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
      if (!(sample.flags & (bsdf::Sample_Emitted | bsdf::Sample_Specular))) {
        auto r = float3(samplers::halton(samplerOffset, 2 + bounce * DIMS_PER_BOUNCE + 5),
                        samplers::halton(samplerOffset, 2 + bounce * DIMS_PER_BOUNCE + 6),
                        samplers::halton(samplerOffset, 2 + bounce * DIMS_PER_BOUNCE + 7));
        
        LightSample lightSample;
        float pLight = 0;
        
        size_t envCount = args.constants.envLightCount;
        float pInfinite = args.constants.lightCount == 0 ? 1.0 : float(envCount) / float(envCount + 1);
        
        if (r.z < pInfinite) {
          // Sample an infinite (environment) light
          r.z /= pInfinite;
          size_t idx = min(size_t(envCount - 1), size_t(r.z * envCount));
          pLight = pInfinite / float(envCount); // Probability of sampling this environment light
          lightSample = sampleEnvironmentLight(hit, args.textures, args.envLights[idx], r.xy);
        } else {
          // Sample an area light
          r.z = (r.z - pInfinite) / (1.0f - pInfinite);
          device auto& light = sampleLightPower(args.lights, args.constants, r.z);
          pLight = (1.0 - pInfinite) * light.power / args.constants.totalLightPower; // Probability of sampling this light
          lightSample = sampleAreaLight(hit, resources, light, r.xy);
        }
        
        const float3 wi = hit.frame.worldToLocal(lightSample.wi);
        const auto bsdfEval = bsdf.eval(hit.wo, wi);
        
        if (length_squared(bsdfEval.f) > 0.0f) {
          ray.direction = lightSample.wi;
          ray.max_distance = length(lightSample.pos - hit.pos) - 1e-3f;
          i.accept_any_intersection(true);
          float ir = samplers::halton(samplerOffset, 2 + bounce * DIMS_PER_BOUNCE + 8);
          intersection = i.intersect(ray, args.accelStruct, args.intersectionFunctionTable, ir);
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
        if (samplers::halton(samplerOffset, 2 + bounce * DIMS_PER_BOUNCE + 9) < q) break;
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
    if (args.constants.frameIdx > 0) {
      float3 L_prev = src.read(tid).xyz;
      
      L += L_prev * args.constants.frameIdx;
      L /= (args.constants.frameIdx + 1);
    }
    
    dst.write(float4(L, 1.0f), tid);
  }
}
