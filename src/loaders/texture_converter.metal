#include <metal_stdlib>

using namespace metal;

/*
 * Simple compute shader to map texture channels and perform any necessary conversion. This lets us
 * easily convert outside textures to our own formats. For example, roughness/metallic textures can
 * be stored with only two channels for a 50% memory savings vs using RGBA.
 */
kernel void convertTexture(
  uint2                                                 tid                 [[thread_position_in_grid]],
  constant uint8_t*                                     channelMap          [[buffer(0)]],
  constant uint8_t&                                     nChannels     			[[buffer(1)]],
  constant bool&																				hasAlpha						[[buffer(2)]],
  texture2d<float>                                      src                 [[texture(0)]],
  texture2d<float, access::write>                       dst                 [[texture(1)]]
) {
  float4 srcPixel = src.read(tid);
  
  float4 out;
  for (uint8_t i = 0; i < nChannels; i++) {
    out[i] = srcPixel[channelMap[i]];
  }
  
  // If the source texture doesn't have an alpha channel, ensure the output alpha is one.
  if (!hasAlpha && nChannels == 4) out.a = 1;
  
  dst.write(out, tid);
}
