#include <metal_stdlib>

#include "../pt_shader_defs.hpp"

using namespace metal;
namespace pp = pt::postprocess;
using Options = pp::PostProcessOptions;

// Screen filling quad in normalized device coordinates
constant float2 quadVertices[] = {
  float2(-1, -1),
  float2(-1,  1),
  float2( 1,  1),
  float2(-1, -1),
  float2( 1,  1),
  float2( 1, -1)
};

// RGB weights for luma calculation
constant float3 lw(0.2126, 0.7152, 0.0722);

struct VertexOut {
  float4 position [[position]];
  float2 uv;
};

float sRGB_channel(float c) {
  if (c < 0.0031308) return 12.92 * c;
  return 1.055 * powr(c, 1.0/2.4) - 0.055;
}

float3 sRGB(float3 val) {
  return float3(sRGB_channel(val.r), sRGB_channel(val.g), sRGB_channel(val.b));
}

/*
 * AgX tonemapping
 * https://iolite-engine.com/blog_posts/minimal_agx_implementation
 */
namespace agx {

constant float3x3 matrix(0.842479062253094, 0.0423282422610123, 0.0423756549057051,
                         0.0784335999999992,  0.878468636469772,  0.0784336,
                         0.0792237451477643, 0.0791661274605434, 0.879142973793104);
constant float3x3 inverse(1.19687900512017, -0.0528968517574562, -0.0529716355144438,
                          -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
                          -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
constant float3 minEv(-12.47393);
constant float3 maxEv(4.026069);

float3 contrast(float3 x) {
  auto x2 = x * x;
  auto x4 = x2 * x2;

  return + 15.5 		* x4 * x2
         - 40.14		* x4 * x
         + 31.96 		* x4
         - 6.868 		* x2 * x
         + 0.4298 	* x2
         + 0.1191 	* x
         - 0.00232;
}

float3 start(float3 val) {
  val = matrix * val;
  val = clamp(log2(val), minEv, maxEv);
  val = (val - minEv) / (maxEv - minEv);

  return contrast(val);
}

float3 end(float3 val) {
  val = inverse * val;
  val = saturate(val);
  return val;
}

float3 applyLook(float3 val, pp::agx::Look look) {
  float luma = dot(val, lw);

  val = pow(val * look.slope + look.offset, look.power);
  return mix(float3(luma), val, look.saturation);
}

float3 apply(float3 val) {
  return end(start(val));
}

float3 apply(float3 val, pp::agx::Options options) {
  return end(applyLook(start(val), options.look));
}

}

namespace khronos_pbr {

float3 apply(float3 val, pp::khronos_pbr::Options options) {
  float compressionStart = options.compressionStart - 0.04;

  float x = min(val.r, min(val.g, val.b));
  float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
  val -= offset;

  float peak = max(val.r, max(val.g, val.b));
  if (peak < compressionStart) return val;

  float d = 1.0 - compressionStart;
  float newPeak = 1.0 - d * d / (peak + d - compressionStart);
  val *= newPeak / peak;

  float g = 1.0 - 1.0 / (options.desaturation * (peak - newPeak) + 1.0);
  return mix(val, float3(newPeak), g);
}

}

// Simple vertex shader that passes through NDC quad positions
vertex VertexOut postprocessVertex(unsigned short vid [[vertex_id]]) {
  float2 position = quadVertices[vid];
  
  VertexOut out;
  out.position = float4(position, 0, 1);
  out.uv = position * float2(0.5f, -0.5f) + 0.5f;
  
  return out;
}

// Simple fragment shader that copies a texture
fragment float4 postprocessFragment(
  VertexOut in [[stage_in]],
  texture2d<float> src,
  constant Options& options [[buffer(0)]]
) {
  constexpr sampler sampler(min_filter::nearest, mag_filter::nearest, mip_filter::none);
  
  float3 color = src.sample(sampler, in.uv).xyz;

  // Exposure
  color *= exp2(options.exposure);

  // Tone mapping
  switch (options.tonemap.tonemapper) {
    case pp::Tonemap::AgX:
      color = agx::apply(color, options.tonemap.agxOptions);
      // No sRGB conversion; AgX has it built in
      break;
    case pp::Tonemap::KhronosPBR:
      color = khronos_pbr::apply(color, options.tonemap.khrOptions);
      color = sRGB(color);
      break;
    default:
      color = sRGB(color);
      break;
  }
  
  return float4(color, 1.0f);
}
