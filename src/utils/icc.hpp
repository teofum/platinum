#ifndef PLATINUM_ICC_HPP
#define PLATINUM_ICC_HPP

#include <simd/simd.h>
#include <string_view>
#include <vector>

namespace pt::icc {
using namespace simd;

struct ICCProfile {
  const std::string_view name;
  const std::vector<uint8_t> data;

  /*
   * Gamma * 100000
   * Used as a fallback for compatibility with viewers that don't support ICC
   * More complex transfer functions (eg sRGB piecewise) must be approximated
   */
  uint32_t fallbackGamma;
};

extern ICCProfile sRGB;
extern ICCProfile DisplayP3;

} // namespace pt::icc

#endif // PLATINUM_ICC_HPP
