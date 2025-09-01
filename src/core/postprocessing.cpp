#include "postprocessing.hpp"

#include <print>

namespace pt::postprocess {

PostProcessPass::PostProcessPass(
  MTL::Device* device,
  MTL::Library* lib,
  const char* functionName,
  MTL::PixelFormat format
) noexcept: m_device(device) {
  m_pso = metal_utils::createRenderPipeline(
    m_device, std::format("postprocess/{}", m_name),
    {
      .vertexFunction = metal_utils::getFunction(lib, "postprocessVertex"),
      .fragmentFunction = metal_utils::getFunction(lib, functionName),
      .colorAttachments = {format}
    }
  );
}

PostProcessPass::~PostProcessPass() {
  m_pso->release();
}

}