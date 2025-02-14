#include <metal_stdlib>

#include "../pt_shader_defs.hpp"

using namespace metal;
namespace pp = pt::postprocess;

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

/*
 * sRGB conversion functions
 */
float sRGB_channel(float c) {
  if (c < 0.0031308) return 12.92 * c;
  return 1.055 * powr(c, 1.0/2.4) - 0.055;
}

float3 sRGB(float3 val) {
  return float3(sRGB_channel(val.r), sRGB_channel(val.g), sRGB_channel(val.b));
}

/*
 * 3x3 matrix inverse
 * Why is this not part of the metal stdlib?
 */
float3x3 inverse(float3x3 m) {
  float a = m[1][1] * m[2][2] - m[2][1] * m[1][2];
  float b = m[1][2] * m[2][0] - m[1][0] * m[2][2];
  float c = m[1][0] * m[2][1] - m[1][1] * m[2][0];

  float det = m[0][0] * a + m[0][1] * b + m[0][2] * c;
  float invdet = 1.0 / det;

  float3x3 inv;
  inv[0][0] = a * invdet;
  inv[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * invdet;
  inv[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * invdet;
  inv[1][0] = b * invdet;
  inv[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * invdet;
  inv[1][2] = (m[1][0] * m[0][2] - m[0][0] * m[1][2]) * invdet;
  inv[2][0] = c * invdet;
  inv[2][1] = (m[2][0] * m[0][1] - m[0][0] * m[2][1]) * invdet;
  inv[2][2] = (m[0][0] * m[1][1] - m[1][0] * m[0][1]) * invdet;

  return inv;
}

/*
 * AgX tonemapping
 * https://iolite-engine.com/blog_posts/minimal_agx_implementation
 */
namespace agx {

constant float3x3 matrix(
  0.842479062253094,  0.0423282422610123, 0.0423756549057051,
  0.0784335999999992, 0.878468636469772,  0.0784336,
  0.0792237451477643, 0.0791661274605434, 0.879142973793104
);
constant float3x3 inverse(
  1.19687900512017,    -0.0528968517574562, -0.0529716355144438,
  -0.0980208811401368, 1.15190312990417,    -0.0980434501171241,
  -0.0990297440797205, -0.0989611768448433, 1.15107367264116
);
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

float3 applyLook(float3 val, constant const pp::agx::Look& look) {
  float luma = dot(val, lw);

  val = pow(val * look.slope + look.offset, look.power);
  return mix(float3(luma), val, look.saturation);
}

float3 apply(float3 val) {
  return end(start(val));
}

float3 apply(float3 val, constant const pp::agx::Options& options) {
  return end(applyLook(start(val), options.look));
}

}

/*
 * Khronos PBR Neutral tonemapping
 * https://github.com/KhronosGroup/ToneMapping/blob/main/PBR_Neutral
 */
namespace khronos_pbr {

float3 apply(float3 val, constant const pp::khronos_pbr::Options& options) {
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

/*
 * flim tonemapping
 * https://github.com/bean-mhm/flim
 */
namespace flim {

float wrap(float x, float start, float end) {
  return start + fmod(x - start, end - start);
}

float invLerp(float x, float start, float end) {
  return saturate((x - start) / (end - start));
}

float rgbSum(float3 color) {
  return color.r + color.g + color.b;
}

float rgbAvg(float3 color) {
  return (color.r + color.g + color.b) / 3.0;
}

float rgbMax(float3 color) {
  return max(color.r, max(color.g, color.b));
}

float rgbMin(float3 color) {
  return min(color.r, min(color.g, color.b));
}

float3 rgbUniformOffset(float3 color, float blackPoint, float whitePoint) {
  float mono = rgbAvg(color);
  float mono2 = invLerp(mono, blackPoint / 1000.0, 1.0 - whitePoint / 1000.0);
  return color * (mono2 / mono);
}

float3 blenderRgbToHsv(float3 rgb) {
  float cmax = rgbMax(rgb);
  float cmin = rgbMin(rgb);
  float cdelta = cmax - cmin;

  float h, s, v;

  v = cmax;
  if (cmax != 0.0) s = cdelta / cmax;
  else s = h = 0.0;

  if (s == 0.0) h = 0.0;
  else {
    float3 c = (float3(cmax) - rgb) / cdelta;

    if (rgb.r == cmax) h = c.b - c.g;
    else if (rgb.g == cmax) h = 2.0 + c.r - c.b;
    else h = 4.0 + c.g - c.r;

    h /= 6.0;
    if (h < 0.0) h += 1.0;
  }

  return float3(h, s, v);
}

float3 blenderHsvToRgb(float3 hsv) {
  float f, p, q, t;
  float h = hsv.x;
  float s = hsv.y;
  float v = hsv.z;

  if (s == 0.0) return float3(v);

  if (h == 1.0) h = 0.0;
  h *= 6.0;
  auto i = int(floor(h));
  f = h - float(i);

  p = v * (1.0 - s);
  q = v * (1.0 - (s * f));
  t = v * (1.0 - (s * (1.0 - f)));

  switch (i) {
    case 0: return float3(v, t, p);
    case 1: return float3(q, v, p);
    case 2: return float3(p, v, t);
    case 3: return float3(p, q, v);
    case 4: return float3(t, p, v);
    default: return float3(v, p, q);
  }
}

float3 blenderHueSat(float3 color, float hue, float sat, float value) {
  float3 hsv = blenderRgbToHsv(color);

  hsv.x = fract(hsv.x + hue + 0.5);
  hsv.y = saturate(hsv.y * sat);
  hsv.z *= value;

  return blenderHsvToRgb(hsv);
}

float3 gamutExtensionMatrixRow(float primaryHue, float scale, float rotate, float mul) {
  float3 result = blenderHsvToRgb(float3(wrap(primaryHue + (rotate / 360.0), 0.0, 1.0), 1.0 / scale, 1.0));
  result /= rgbSum(result);
  result *= mul;
  return result;
}

float3x3 gamutExtensionMatrix(constant const pp::flim::Options& options) {
  float3x3 m;
  m[0] = gamutExtensionMatrixRow(
    0.0 / 3.0,
    options.extendedGamutScale.r,
    options.extendedGamutRotation.r,
    options.extendedGamutMul.r
  );
  m[1] = gamutExtensionMatrixRow(
    1.0 / 3.0,
    options.extendedGamutScale.g,
    options.extendedGamutRotation.g,
    options.extendedGamutMul.g
  );
  m[2] = gamutExtensionMatrixRow(
    2.0 / 3.0,
    options.extendedGamutScale.b,
    options.extendedGamutRotation.b,
    options.extendedGamutMul.b
  );

  return m;
}

float superSigmoid(float x, float2 toe, float2 shoulder) {
  // Clamp
  x = saturate(x);
  toe = saturate(toe);
  shoulder = saturate(shoulder);

  // Calculate slope
  float slope = (shoulder.y - toe.y) / (shoulder.x - toe.x);

  // Toe
  if (x < toe.x) return toe.y * powr(x / toe.x, slope * toe.x / toe.y);

  // Straight segment
  if (x < shoulder.x) return slope * x + toe.y - (slope * toe.x);

  // Shoulder
  float shoulderPow = -slope / ((shoulder.x - 1.0) / powr(1.0 - shoulder.x, 2.0) * (1.0 - shoulder.y));
  return (1.0 - powr(1.0 - (x - shoulder.x) / (1.0 - shoulder.x), shoulderPow)) * (1.0 - shoulder.y) + shoulder.y;
}

float dyeMixFactor(float mono, float maxDensity, constant const pp::flim::Options& options) {
  // log2 and map range
  float offset = exp2(options.sigmoidLog2Min);
  float fac = invLerp(log2(mono + offset), options.sigmoidLog2Min, options.sigmoidLog2Max);

  // Calculate exposure in 0-1 range
  fac = superSigmoid(fac, options.sigmoidToe, options.sigmoidShoulder);

  // Dye density
  fac *= maxDensity;

  // Mix factor
  fac = exp2(-fac);
  return saturate(fac);
}

float3 rgbColorLayer(float3 color, float3 sensitivityTone, float3 dyeTone, float maxDensity, constant const pp::flim::Options& options) {
  // Normalize
  sensitivityTone /= rgbSum(sensitivityTone);
  dyeTone /= rgbMax(dyeTone);

  // Dye mix factor
  float mono = dot(color, sensitivityTone);
  float mixFactor = dyeMixFactor(mono, maxDensity, options);

  return mix(dyeTone, float3(1.0), mixFactor);
}

float3 rgbDevelop(float3 color, float exposure, float maxDensity, constant const pp::flim::Options& options) {
  // Exposure
  color *= exp2(exposure);

  // Blue-sensitive layer
  float3 result = rgbColorLayer(color, float3(0, 0, 1), float3(1, 1, 0), maxDensity, options);

  // Green-sensitive layer
  result *= rgbColorLayer(color, float3(0, 1, 0), float3(1, 0, 1), maxDensity, options);

  // Red-sensitive layer
  result *= rgbColorLayer(color, float3(1, 0, 0), float3(0, 1, 1), maxDensity, options);

  return result;
}

float3 negativeAndPrint(float3 color, float3 backlight, constant const pp::flim::Options& options) {
  // Develop negative
  color = rgbDevelop(color, options.negativeExposure, options.negativeDensity, options);

  // Backlight
  color *= backlight;

  // Develop print
  color = rgbDevelop(color, options.printExposure, options.printDensity, options);
  return color;
}

float3 apply(float3 val, constant const pp::flim::Options& options) {
  val *= exp2(options.preExposure);

  float3x3 extension = gamutExtensionMatrix(options);
  float3x3 extensionInverse = inverse(extension);

  float3 backlight = options.printBacklight * extension;

  constexpr float big = 1e7;
  float3 whiteCap = negativeAndPrint(float3(big), backlight, options);

  // Pre-formation filter
  val = mix(val, val * options.preFormationFilter, options.preFormationFilterStrength);

  // Convert to extended gamut
  val *= extension;

  // Film simulation
  val = negativeAndPrint(val, backlight, options);

  // Convert back from extended gamut
  val *= extensionInverse;

  // White/black point
  val = max(val, float3(0.0));
  val /= whiteCap;

  if (options.autoBlackPoint) {
    float3 blackCap = negativeAndPrint(float3(0.0), backlight, options);
    blackCap /= whiteCap;
    val = rgbUniformOffset(val, rgbAvg(blackCap) * 1000.0, 0.0);
  } else {
    val = rgbUniformOffset(val, options.blackPoint, 0.0);
  }

  // Post-formation filter
  val = mix(val, val * options.postFormationFilter, options.postFormationFilterStrength);

  // Clamp and midtone saturation
  val = saturate(val);

  float mono = rgbAvg(val);
  float mixFactor = (mono < 0.5) ? invLerp(mono, 0.05, 0.5) : invLerp(mono, 0.95, 0.5);
  val = mix(val, blenderHueSat(val, 0.5, options.midtoneSaturation, 1.0), mixFactor);

  val = saturate(val);

  return val;
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

fragment float4 exposure(
  VertexOut in [[stage_in]],
  texture2d<float> src,
  constant pp::ExposureOptions& options [[buffer(0)]]
) {
  constexpr sampler sampler(min_filter::nearest, mag_filter::nearest, mip_filter::none);

  float3 color = src.sample(sampler, in.uv).xyz;
  color *= exp2(options.exposure);

  return float4(color, 1.0f);
}

fragment float4 tonemap(
  VertexOut in [[stage_in]],
  texture2d<float> src,
  constant pp::TonemapOptions& options [[buffer(0)]]
) {
  constexpr sampler sampler(min_filter::nearest, mag_filter::nearest, mip_filter::none);
  
  float3 color = src.sample(sampler, in.uv).xyz;
  switch (options.tonemapper) {
    case pp::Tonemapper::AgX:
      color = agx::apply(color, options.agxOptions);
      // No sRGB conversion; AgX has it built in
      break;
    case pp::Tonemapper::KhronosPBR:
      color = khronos_pbr::apply(color, options.khrOptions);
      color = sRGB(color);
      break;
    case pp::Tonemapper::flim:
      color = flim::apply(color, options.flimOptions);
      color = sRGB(color);
    default:
      color = sRGB(color);
      break;
  }
  
  return float4(color, 1.0f);
}
