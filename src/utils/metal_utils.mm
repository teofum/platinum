#include <Metal/Metal.h>
#include <QuartzCore/QuartzCore.h>

#include "metal_utils.hpp"

namespace pt::metal_utils {

MTL::Device* getDevice(CA::MetalLayer* layer) noexcept {
  auto metalLayer = ( __bridge CAMetalLayer*) layer;
  auto* cppDevice = ( __bridge MTL::Device*) metalLayer.device;
  return cppDevice;
}

CA::MetalDrawable* nextDrawable(CA::MetalLayer* layer) noexcept {
  auto metalLayer = ( __bridge CAMetalLayer*) layer;
  id <CAMetalDrawable> metalDrawable = [metalLayer nextDrawable];
  auto* cppDrawable = ( __bridge CA::MetalDrawable*) metalDrawable;
  return cppDrawable;
}

void setupLayer(CA::MetalLayer* layer) noexcept {
  auto metalLayer = ( __bridge CAMetalLayer*) layer;
  metalLayer.pixelFormat = MTLPixelFormatRGBA8Unorm_sRGB;
}

void setDrawableSize(CA::MetalLayer* layer, int width, int height) noexcept {
  auto metalLayer = ( __bridge CAMetalLayer*) layer;
  metalLayer.drawableSize = CGSize{float(width), float(height)};
}

NS::SharedPtr<MTL::VertexDescriptor> makeVertexDescriptor(const VertexParams& params) {
  auto desc = ns_shared<MTL::VertexDescriptor>();

  size_t i = 0;
  auto attribDesc = ns_shared<MTL::VertexAttributeDescriptor>();
  for (auto attrib: params.attributes) {
    attribDesc->setFormat(attrib.format);
    attribDesc->setOffset(attrib.offset);
    attribDesc->setBufferIndex(attrib.bufferIndex);
    desc->attributes()->setObject(attribDesc, i++);
  }

  i = 0;
  auto layoutDesc = ns_shared<MTL::VertexBufferLayoutDescriptor>();
  for (auto layout: params.layouts) {
    layoutDesc->setStride(layout.stride);
    layoutDesc->setStepFunction(layout.stepFunction);
    layoutDesc->setStepRate(
      layout.stepFunction == MTL::VertexStepFunctionPerVertex ? 1 :
      layout.stepFunction == MTL::VertexStepFunctionConstant ? 0 :
      layout.stepRate
    );
    desc->layouts()->setObject(layoutDesc, i++);
  }

  return desc;
}

NS::SharedPtr<MTL::TextureDescriptor> makeTextureDescriptor(const TextureParams& params) {
  auto texd = ns_shared<MTL::TextureDescriptor>();
  
  texd->setTextureType(params.type);
  texd->setWidth(params.width);
  texd->setHeight(params.height);
  texd->setDepth(params.depth);
  texd->setStorageMode(params.storageMode);
  texd->setUsage(params.usage);
  texd->setPixelFormat(params.format);
  
  return texd;
}

void enableBlending(
  MTL::RenderPipelineColorAttachmentDescriptor* cad,
  MTL::BlendOperation operation,
  MTL::BlendFactor sourceFactor,
  MTL::BlendFactor destFactor
) {
  cad->setBlendingEnabled(true);
  cad->setRgbBlendOperation(operation);
  cad->setSourceRGBBlendFactor(sourceFactor);
  cad->setDestinationRGBBlendFactor(destFactor);
  cad->setAlphaBlendOperation(operation);
  cad->setSourceAlphaBlendFactor(sourceFactor);
  cad->setDestinationAlphaBlendFactor(destFactor);
}

NS::SharedPtr<MTL::RenderPipelineDescriptor> makeRenderPipelineDescriptor(const RenderPipelineParams& params) {
  auto desc = ns_shared<MTL::RenderPipelineDescriptor>();
  desc->setVertexFunction(params.vertexFunction);
  desc->setFragmentFunction(params.fragmentFunction);

  uint32_t i = 0;
  for (auto format: params.colorAttachments) {
    desc->colorAttachments()->object(i++)->setPixelFormat(format);
  }

  desc->setDepthAttachmentPixelFormat(params.depthFormat);
  desc->setStencilAttachmentPixelFormat(params.stencilFormat);
  return desc;
}

NS::SharedPtr<MTL::ComputePipelineDescriptor> makeComputePipelineDescriptor(const ComputePipelineParams& params) {
  auto desc = ns_shared<MTL::ComputePipelineDescriptor>();
  desc->setComputeFunction(params.function);

  desc->setThreadGroupSizeIsMultipleOfThreadExecutionWidth(params.threadGroupSizeIsMultipleOfExecutionWidth);
  return desc;
}

NS::SharedPtr<MTL::Function> getFunction(MTL::Library* lib, const char* name) {
  auto nsName = NS::String::string(name, NS::UTF8StringEncoding);
  return NS::TransferPtr(lib->newFunction(nsName));
}

NS::SharedPtr<MTL::Function> getFunction(
  MTL::Library* lib,
  const char* name,
  const FunctionParams& params
) {
  auto constantValues = ns_shared<MTL::FunctionConstantValues>();
  size_t idx = 0;
  for (const auto& constant: params.constants) {
    constantValues->setConstantValue(constant.value, constant.type, idx++);
  }

  auto nsName = NS::String::string(name, NS::UTF8StringEncoding);
  return NS::TransferPtr(lib->newFunction(nsName, constantValues, (NS::Error**) nullptr));
}

}
