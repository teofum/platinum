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

template<typename T>
constexpr NS::SharedPtr<T> ns_shared() {
  return NS::TransferPtr(T::alloc()->init());
}

MTL::Device* getDevice(CA::MetalLayer* layer) noexcept;

CA::MetalDrawable* nextDrawable(CA::MetalLayer* layer) noexcept;

void setupLayer(CA::MetalLayer* layer) noexcept;

void setDrawableSize(CA::MetalLayer* layer, int width, int height) noexcept;

/**
 * Returns a NS::SharedPtr to a shader function.
 * @param lib library
 * @param name function name
 */
NS::SharedPtr<MTL::Function> getFunction(MTL::Library* lib, const char* name);

struct RenderPipelineParams {
  MTL::Function* vertexFunction;
  MTL::Function* fragmentFunction;
  std::vector<MTL::PixelFormat> colorAttachments;
  MTL::PixelFormat depthFormat = MTL::PixelFormatInvalid;
};

/**
 * Utility function to make a render pipeline descriptor in a less verbose way.
 * @param params
 * @return NS::SharedPtr to a render pipeline descriptor.
 */
NS::SharedPtr<MTL::RenderPipelineDescriptor> makeRenderPipelineDescriptor(const RenderPipelineParams& params);

struct VertexAttribParams {
  MTL::VertexFormat format = MTL::VertexFormatInvalid;
  size_t offset = 0;
  size_t bufferIndex = 0;
};

struct VertexLayoutParams {
  size_t stride = 0;
  MTL::VertexStepFunction stepFunction = MTL::VertexStepFunctionPerVertex;
  size_t stepRate = 1;
};

struct VertexParams {
  std::vector<VertexAttribParams> attributes;
  std::vector<VertexLayoutParams> layouts;
};

/**
 * Utility function to make a vertex descriptor in a less verbose way.
 * @param params
 * @return NS::SharedPtr to a vertex descriptor.
 */
NS::SharedPtr<MTL::VertexDescriptor> makeVertexDescriptor(const VertexParams& params);

/**
 * Utility function to enable alpha blending for a color attachment.
 * @param cad
 * @param operation
 * @param sourceFactor
 * @param destFactor
 */
void enableBlending(
  MTL::RenderPipelineColorAttachmentDescriptor* cad,
  MTL::BlendOperation operation = MTL::BlendOperationAdd,
  MTL::BlendFactor sourceFactor = MTL::BlendFactorSourceAlpha,
  MTL::BlendFactor destFactor = MTL::BlendFactorOneMinusSourceAlpha
);

}

#endif //PLATINUM_METAL_UTILS_HPP
