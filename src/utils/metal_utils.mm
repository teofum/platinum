#include <Metal/Metal.h>
#include <QuartzCore/QuartzCore.h>

#include "metal_utils.hpp"

namespace pt::metal_utils {

MTL::Device* getDevice(CA::MetalLayer* layer) noexcept {
  auto metalLayer = (CAMetalLayer*) layer;
  auto* cppDevice = ( __bridge MTL::Device*) metalLayer.device;
  return cppDevice;
}

CA::MetalDrawable* nextDrawable(CA::MetalLayer* layer) noexcept {
  auto metalLayer = (CAMetalLayer*) layer;
  id <CAMetalDrawable> metalDrawable = [metalLayer nextDrawable];
  auto* cppDrawable = ( __bridge CA::MetalDrawable*) metalDrawable;
  return cppDrawable;
}

void setupLayer(CA::MetalLayer* layer) noexcept {
  auto metalLayer = (CAMetalLayer*) layer;
  metalLayer.pixelFormat = MTLPixelFormatRGBA8Unorm_sRGB;
}

void setDrawableSize(CA::MetalLayer* layer, int width, int height) noexcept {
  auto metalLayer = (CAMetalLayer*) layer;
  metalLayer.drawableSize = CGSize{float(width), float(height)};
}

}