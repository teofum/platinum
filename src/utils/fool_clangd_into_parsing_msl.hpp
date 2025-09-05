#ifndef PLATINUM_FOOL_CLANGD_INTO_PARSING_MSL_HPP
#define PLATINUM_FOOL_CLANGD_INTO_PARSING_MSL_HPP

#ifndef __METAL_VERSION__

// Address spaces
#define device
#define constant
#define thread
#define ray_data
#define object_data

// Definitions
#define __METAL__
#define __HAVE_RAYTRACING__
#define __HAVE_RAYTRACING_MULTI_LEVEL_INSTANCING__
#define __HAVE_RAYTRACING_INTERSECTION_QUERY__
#define __HAVE_RAYTRACING_USER_INSTANCE_ID__
#define __HAVE_RAYTRACING_MOTION__
#define __HAVE_RAYTRACING_INDIRECT_INSTANCE_BUILD__
#define __HAVE_RAYTRACING_CURVES__
#define __HAVE_INDIRECT_ARGUMENT_BUFFER__
#define __HAVE_MESH__

#define __METAL_VERSION__
#define METAL_FUNC

#include <simd/simd.h>

using namenamespace simd;

#endif
#endif
