#include "colorspace.hpp"

namespace pt::color {

Colorspace BT709 = Colorspace(float2{0.640, 0.330}, float2{0.300, 0.600}, float2{0.150, 0.060}, WHITEPOINT_D65);
Colorspace DisplayP3 = Colorspace(float2{0.680, 0.320}, float2{0.265, 0.690}, float2{0.150, 0.060}, WHITEPOINT_D65);
Colorspace BT2020 = Colorspace(float2{0.708, 0.292}, float2{0.170, 0.797}, float2{0.131, 0.046}, WHITEPOINT_D65);

/*
 * Reference: https://www.ryanjuckett.com/rgb-color-space-conversion/
 */
Colorspace::Colorspace(float2 r, float2 g, float2 b, float2 w) noexcept
  : m_r(r), m_g(g), m_b(b), m_w(w) {
  // Add the missing z coordinate for primaries and whitepoint
  float3 r_xyz = make_float3(r, 1.0f - r.x - r.y);
  float3 g_xyz = make_float3(g, 1.0f - g.x - g.y);
  float3 b_xyz = make_float3(b, 1.0f - b.x - b.y);
  float3 w_xyz = make_float3(w, 1.0f - w.x - w.y);

  // Calculate XYZ value for the whitepoint, knowing that Y = 1
  float3 w_XYZ = w_xyz / w_xyz.y;

  // Create the "base" matrix with xyz primaries as columns.
  float3x3 matrix_xyz(r_xyz, g_xyz, b_xyz);

  // Calculate the scalars that transform xyz to XYZ primaries
  // w_XYZ = matrix_xyz * scale * (1, 1, 1) -> scale = matrix_xyz^-1 * w_XYZ
  float3 scale = inverse(matrix_xyz) * w_XYZ;

  // Create the transform matrix and its inverse
  m_toXYZ = matrix_xyz * float3x3(scale);
  m_fromXYZ = inverse(m_toXYZ);
}

Colorspace makeAgXInset(const Colorspace& base) {
  constexpr float COMPRESSION = 0.20f;
  constexpr float SCALE_FACTOR = 1.0f / (1.0f - COMPRESSION);

  float2 w = base.whitepoint();
  float2 r = (base.red() - w) * SCALE_FACTOR + w;
  float2 g = (base.green() - w) * SCALE_FACTOR + w;
  float2 b = (base.blue() - w) * SCALE_FACTOR + w;

  return {r, g, b, w};
}

Colorspace getColorspace(DisplayColorspace cs) {
  switch (cs) {
    case DisplayColorspace::sRGB: return BT709;
    case DisplayColorspace::DisplayP3: return DisplayP3;
    case DisplayColorspace::BT2020: return BT2020;
  }
}

}