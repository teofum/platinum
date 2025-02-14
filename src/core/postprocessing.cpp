#include "postprocessing.hpp"

#include <print>

namespace pt::postprocess {

PostProcessPass::PostProcessPass(
  MTL::Device* device,
  MTL::Library* lib,
  const char* functionName,
  MTL::PixelFormat format
) noexcept: m_device(device) {
  NS::Error* error = nullptr;

  auto desc = metal_utils::makeRenderPipelineDescriptor(
    {
      .vertexFunction = metal_utils::getFunction(lib, "postprocessVertex"),
      .fragmentFunction = metal_utils::getFunction(lib, functionName),
      .colorAttachments = {format}
    }
  );

  m_pso = m_device->newRenderPipelineState(desc, &error);
  if (!m_pso) {
    std::println(
      stderr,
      "renderer_pt: Failed to create postprocess pipeline for {}: {}",
      m_name,
      error->localizedDescription()->utf8String()
    );
    assert(false);
  }
}

PostProcessPass::~PostProcessPass() {
  m_pso->release();
}

}