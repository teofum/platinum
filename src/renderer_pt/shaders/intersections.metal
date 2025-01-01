#include <metal_stdlib>

#include "defs.metal"

using namespace metal;
using namespace raytracing;

[[intersection(triangle, triangle_data, instancing)]]
bool alphaTestIntersectionFunction(
	uint32_t geometryIdx 											[[geometry_id]],
  uint32_t primitiveIdx 										[[primitive_id]],
	uint32_t instanceIdx 											[[instance_id]],
	float2 barycentricCoords 									[[barycentric_coord]],
  const device PrimitiveData* primitiveData [[primitive_data]],
	const ray_data float& r 									[[payload]],
	constant Arguments& args 									[[buffer(0)]]
) {
  device auto& vertexResource = args.vertexResources[geometryIdx];
  device auto& primitiveResource = args.primitiveResources[geometryIdx];
  device auto& instanceResource = args.instanceResources[instanceIdx];
  
  auto materialSlot = primitiveResource.materialSlot[primitiveIdx];
  device const auto& material = instanceResource.materials[materialSlot];
  
  float alpha = material.baseColor.a;
  if (material.baseTextureId >= 0) {
    float2 vertexTexCoords[3];
    for (int i = 0; i < 3; i++)
      vertexTexCoords[i] = vertexResource.data[primitiveData->indices[i]].texCoords;
    
    float2 surfaceUV = interpolate(vertexTexCoords, barycentricCoords);
    
    constexpr sampler s(address::repeat, filter::linear);
    alpha *= args.textures[material.baseTextureId].tex.sample(s, surfaceUV).a;
  }
  
  // TODO: pass random sample for stochastic alpha testing
  return alpha > r;
}
