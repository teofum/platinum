#ifndef PLATINUM_COLORSPACE_HPP
#define PLATINUM_COLORSPACE_HPP

#include <simd/simd.h>

using namespace simd;

namespace pt::color {

/*
 * Helper enum to identify a display colorspace
 */
enum class DisplayColorspace {
  sRGB,
  DisplayP3,
  BT2020
};

/*
 * Representation of an RGB colorspace
 */
class Colorspace {
public:
  /*
   * Create a colorspace from the CIE 1931 xy chromaticities of its
   * three primaries and whitepoint
   */
  Colorspace(float2 r, float2 g, float2 b, float2 w) noexcept;

  [[nodiscard]] constexpr float2 red() const { return m_r; }
  [[nodiscard]] constexpr float2 green() const { return m_g; }
  [[nodiscard]] constexpr float2 blue() const { return m_b; }
  [[nodiscard]] constexpr float2 whitepoint() const { return m_w; }

  [[nodiscard]] constexpr float3x3 toXYZ() const { return m_toXYZ; }
  [[nodiscard]] constexpr float3x3 fromXYZ() const { return m_fromXYZ; }

private:
  float2 m_r, m_g, m_b, m_w;
  float3x3 m_toXYZ, m_fromXYZ;
};

/*
 * Common whitepoint definitions
 */
constexpr float2 WHITEPOINT_D65 = {0.3127, 0.3290};

/*
 * Common colorspace definitions
 */
extern Colorspace BT709;
extern Colorspace DisplayP3;
extern Colorspace BT2020;

[[nodiscard]] Colorspace makeAgXInset(const Colorspace& base);
[[nodiscard]] Colorspace getColorspace(DisplayColorspace cs);

/*
 * Get a transformation matrix from one colorspace to another
 */
[[nodiscard]] constexpr float3x3 transform(const Colorspace& src, const Colorspace& dst) {
  return dst.fromXYZ() * src.toXYZ();
}

}

#endif //PLATINUM_COLORSPACE_HPP
