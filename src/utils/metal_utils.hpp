#ifndef PLATINUM_METAL_UTILS_HPP
#define PLATINUM_METAL_UTILS_HPP

#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

namespace pt::metal_utils {

[[nodiscard]] constexpr NS::String* operator ""_ns(
  const char* cStr,
  size_t len
) noexcept {
  return NS::String::string(cStr, NS::UTF8StringEncoding);
}

MTL::Device* getDevice(CA::MetalLayer* layer) noexcept;

CA::MetalDrawable* nextDrawable(CA::MetalLayer* layer) noexcept;

void setupLayer(CA::MetalLayer* layer) noexcept;

void setDrawableSize(CA::MetalLayer* layer, int width, int height) noexcept;

}

#endif //PLATINUM_METAL_UTILS_HPP
