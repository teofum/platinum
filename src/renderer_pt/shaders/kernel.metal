#include <metal_stdlib>

// Header files use this guard to only include what the shader needs
#define METAL_SHADER

#include <core/mesh.hpp>
#include "../pt_shader_defs.hpp"

using namespace metal;
using namespace raytracing;

constant uint32_t resourcesStride [[function_constant(0)]];

struct VertexResource {
    device pt::VertexData* data;
};

kernel void pathtracingKernel(
    uint2                                                 tid         [[thread_position_in_grid]],
    constant Constants&                                   constants   [[buffer(0)]],
    device void*                                          resources   [[buffer(1)]],
    constant MTLAccelerationStructureInstanceDescriptor*  instances   [[buffer(2)]],
    instance_acceleration_structure                       accelStruct [[buffer(3)]],
    texture2d<float>                                      src         [[texture(0)]],
    texture2d<float, access::write>                       dst         [[texture(1)]]
) {
    if (tid.x < constants.size.x && tid.y < constants.size.y) {
        constant CameraData& camera = constants.camera;
        uint2 pixel(tid.x, constants.size.y - tid.y - 1);
        
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
        
        float3 acc(0.0);
        
        /*
         * Create an intersector and do intersection test
         */
        intersector<triangle_data, instancing> i;
        typename intersector<triangle_data, instancing>::result_type intersection;
        
        i.accept_any_intersection(false);
        
        intersection = i.intersect(ray, accelStruct);
        
        if (intersection.type != intersection_type::none) {
            auto instanceIdx = intersection.instance_id;
            auto geometryIdx = instances[instanceIdx].accelerationStructureIndex;
            
            float2 barycentric_coords = intersection.triangle_barycentric_coord;
            
            device auto& vertexResource = *(device VertexResource*)((device uint64_t*)resources + geometryIdx);
            device auto& data = *(device PrimitiveData*) intersection.primitive_data;
            
            acc = vertexResource.data[data.indices[0]].normal * 0.5 + 0.5;
        }
        
        dst.write(float4(acc, 1.0f), tid);
    }
}
