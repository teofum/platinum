#include "ms_lut_gen.hpp"

#include <OpenImageIO/imageio.h>

#include <utils/utils.hpp>
#include <utils/metal_utils.hpp>

namespace pt::frontend::windows {
using metal_utils::ns_shared;
using metal_utils::operator ""_ns;

void MultiscatterLutGenerator::init(MTL::Device* device, MTL::CommandQueue* commandQueue) {
  m_device = device;
  m_commandQueue = commandQueue;
}

void MultiscatterLutGenerator::render() {
  frame();
  
  ImGui::Begin("Multiscatter GGX LUT Generator", m_open);
  
  if (ImGui::BeginTable("Layout", 2)) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::BeginGroup();
    
    bool working = m_frameIdx != 0 && m_frameIdx < m_accumulateFrames;
    bool done = m_frameIdx == m_accumulateFrames;
    
    for (uint32_t i = 0; i < m_lutOptions.size(); i++) {
      ImGui::BeginDisabled(i > 1);
      ImGui::RadioButton(m_lutOptions[i].displayName, (int*) &m_selectedLut, i);
      ImGui::EndDisabled();
    }
    
    ImGui::Separator();
    
    for (uint32_t size = 16; size <= 1024; size <<= 1) {
      const auto label = std::format("{}px", size);
      ImGui::RadioButton(label.c_str(), (int*) &m_lutSize, size);
    }
    
    ImGui::Separator();
    
    if (ImGui::BeginTable("Buttons", 2)) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::BeginDisabled(working);
      if (ImGui::Button("Generate", {ImGui::GetContentRegionAvail().x, 0})) {
        generate();
      }
      ImGui::EndDisabled();
      
      ImGui::TableNextColumn();
      ImGui::BeginDisabled(!done);
      if (ImGui::Button("Export", {ImGui::GetContentRegionAvail().x, 0})) {
        exportToFile();
      }
      ImGui::EndDisabled();
      
      ImGui::EndTable();
    }
    
    
    ImGui::EndGroup();
    
    ImGui::TableNextColumn();
    ImGui::BeginGroup();
    
    ImGui::PushStyleColor(ImGuiCol_ChildBg, (ImVec4) ImColor::HSV(0.0f, 0.0f, 0.8f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::BeginChild(
      "RenderView",
      {256, 256},
      ImGuiChildFlags_Borders,
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
    );
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    
    if (m_accumulator[0] != nullptr) {
      ImGui::Image(
       (ImTextureID) m_accumulator[0],
       {256, 256}
     );
    }
    
    ImGui::EndChild();
    
    auto progress = (float) m_frameIdx / (float) m_accumulateFrames;
    auto progressStr = m_frameIdx == m_accumulateFrames
                       ? "Done!"
                       : m_frameIdx == 0
                         ? "Ready"
                         : std::format("{} / {}", m_frameIdx, m_accumulateFrames);
    ImGui::ProgressBar(progress, {256, 0}, progressStr.c_str());
    
    ImGui::EndGroup();
    ImGui::EndTable();
  }
  
  ImGui::End();
}

void MultiscatterLutGenerator::frame() {
  if (m_accumulator[1] == nullptr) return;
  
  auto frameStart = std::chrono::high_resolution_clock::now();
  size_t time = 0;
  
  auto dim = m_lutOptions[m_selectedLut].dimensions;
  
  // Allow more than one dispatch per frame
  while (time < 1000 / 120 && m_frameIdx < m_accumulateFrames) {
    auto threadsPerThreadgroup = MTL::Size(32, 32, 1);
    auto threadgroups = MTL::Size(
      (m_lutSize + threadsPerThreadgroup.width - 1) / threadsPerThreadgroup.width,
      ((dim > 1 ? m_lutSize : 1) + threadsPerThreadgroup.height - 1) / threadsPerThreadgroup.height,
      1
    );
    
    auto cmd = m_commandQueue->commandBuffer();
    auto computeEnc = cmd->computeCommandEncoder();
    
    computeEnc->setBytes(&m_lutSize, sizeof(uint32_t), 0);
    computeEnc->setBytes(&m_frameIdx, sizeof(uint32_t), 1);
    
    computeEnc->setTexture(m_accumulator[0], 0);
    computeEnc->setTexture(m_accumulator[1], 1);
    computeEnc->setTexture(m_randomSource, 2);
    
    computeEnc->setComputePipelineState(m_pso);
    computeEnc->dispatchThreadgroups(threadgroups, threadsPerThreadgroup);
    computeEnc->endEncoding();
    
    cmd->commit();
    
    m_frameIdx++;
    
  	auto now = std::chrono::high_resolution_clock::now();
  	time = std::chrono::duration_cast<std::chrono::milliseconds>(now - frameStart).count();
    
    std::swap(m_accumulator[0], m_accumulator[1]);
  }
    
  if (m_frameIdx == m_accumulateFrames) {
    std::swap(m_accumulator[0], m_accumulator[1]);
  }
}

void MultiscatterLutGenerator::generate() {
  /*
   * Build render targets
   */
  if (m_accumulator[0] != nullptr) m_accumulator[0]->release();
  if (m_accumulator[1] != nullptr) m_accumulator[1]->release();
  if (m_randomSource != nullptr) m_randomSource->release();
  
  auto dim = m_lutOptions[m_selectedLut].dimensions;
  auto texd = MTL::TextureDescriptor::alloc()->init();
  texd->setTextureType(m_textureTypes[dim - 1]);
  texd->setWidth(m_lutSize);
  texd->setHeight(dim > 1 ? m_lutSize : 1);
  texd->setDepth(dim > 2 ? m_lutSize : 1);
  texd->setStorageMode(MTL::StorageModeShared);

  texd->setUsage(MTL::TextureUsageShaderWrite | MTL::TextureUsageShaderRead);
  texd->setPixelFormat(MTL::PixelFormatR32Float);
  m_accumulator[0] = m_device->newTexture(texd);
  m_accumulator[1] = m_device->newTexture(texd);
  
  texd->setUsage(MTL::TextureUsageShaderRead);
  texd->setPixelFormat(MTL::PixelFormatR32Uint);
  m_randomSource = m_device->newTexture(texd);

  auto k = 1u;
  for (int i = 0; i < dim; i++) k *= m_lutSize;
  
  std::vector<uint32_t> random(k);
  for (size_t i = 0; i < k; i++) random[i] = rand() % (1024 * 1024);
  auto region = dim < 3 ? MTL::Region::Make2D(0, 0, m_lutSize, dim == 2 ? m_lutSize : 1)
              				  : MTL::Region::Make3D(0, 0, 0, m_lutSize, m_lutSize, m_lutSize);
  m_randomSource->replaceRegion(
    region,
    0,
    random.data(),
    sizeof(uint32_t) * (size_t) m_lutSize
  );

  texd->release();
  
  /*
   * Create compute pipeline
   * This is just an internal dev tool, doesn't need to be very fast so we can just regen the PSO
   * on every render if we need a different one
   */
  if (m_pso != nullptr) m_pso->release();
  
  NS::Error* error = nullptr;
  MTL::Library* lib = m_device->newLibrary("tools.metallib"_ns, &error);
  if (!lib) {
    std::println(
      "ms_lut_gen: Failed to load shader library: {}",
      error == nullptr ? "Unknown error" : error->localizedDescription()->utf8String()
    );
    assert(false);
  }
  
  auto desc = metal_utils::makeComputePipelineDescriptor(
    {
      .function = metal_utils::getFunction(lib, m_lutOptions[m_selectedLut].kernelName),
      .threadGroupSizeIsMultipleOfExecutionWidth = true,
    }
  );

  m_pso = m_device->newComputePipelineState(
    desc,
    MTL::PipelineOptionNone,
    nullptr,
    &error
  );
  if (!m_pso) {
    std::println(
       "ms_lut_gen: Failed to create pipeline: {}",
       error->localizedDescription()->utf8String()
     );
    assert(false);
  }
  
  lib->release();
  
  /*
   * Reset frame idx
   */
  m_frameIdx = 0;
}

void MultiscatterLutGenerator::exportToFile() {
  const auto savePath = utils::fileSave("../out", "exr");
  if (savePath){
    auto out = OIIO::ImageOutput::create(savePath->string());
    
    if (out) {
      auto cmd = m_commandQueue->commandBuffer();
      auto benc = cmd->blitCommandEncoder();
      
      uint3 size{
        (uint32_t) m_accumulator[0]->width(),
        (uint32_t) m_accumulator[0]->height(),
        (uint32_t) m_accumulator[0]->depth(),
      };

      const auto bytesPerRow = sizeof(float) * size.x;
      const auto bytesPerImage = bytesPerRow * size.y * size.z;
      
      const auto readbackBuffer = m_device->newBuffer(bytesPerImage, MTL::ResourceStorageModeShared);
      benc->copyFromTexture(
        m_accumulator[0],
        0,
        0,
        MTL::Origin(0, 0, 0),
        MTL::Size(size.x, size.y, size.z),
        readbackBuffer,
        0,
        bytesPerRow,
        bytesPerImage
      );
      benc->endEncoding();
      cmd->commit();
      cmd->waitUntilCompleted();
      
      OIIO::ImageSpec spec(size.x, size.y, 1, OIIO::TypeDesc::FLOAT);
      spec.depth = size.z;
      
      out->open(savePath->string(), spec);
      out->write_image(OIIO::TypeDesc::FLOAT, readbackBuffer->contents());
      out->close();
    }
  }
}

}
