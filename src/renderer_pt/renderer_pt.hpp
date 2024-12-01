#ifndef PLATINUM_RENDERER_PT_HPP
#define PLATINUM_RENDERER_PT_HPP

#include <Metal/Metal.hpp>

#include <core/store.hpp>
#include "pt_shader_defs.hpp"

namespace pt::renderer_pt {

class Renderer {
public:
  Renderer(
    MTL::Device* device,
    MTL::CommandQueue* commandQueue,
    Store& store
  ) noexcept;

  ~Renderer();

  void render();

  void startRender(Scene::NodeID cameraNodeId, float2 viewportSize, uint32_t sampleCount);

  [[nodiscard]] const MTL::Texture* presentRenderTarget() const;

  [[nodiscard]] bool isRendering() const;

  [[nodiscard]] std::pair<size_t, size_t> renderProgress() const;

  [[nodiscard]] size_t renderTime() const;

private:
  // Store
  Store& m_store;

  // Camera and viewport
  float2 m_viewportSize = {1, 1};
  float m_aspect = 1.0;

  // Metal
  MTL::Device* m_device = nullptr;
  MTL::CommandQueue* m_commandQueue = nullptr;

  // Path tracing pipeline state
  MTL::ComputePipelineState* m_pathtracingPipeline = nullptr;
  MTL::RenderPipelineState* m_postprocessPipeline = nullptr;

  // Render targets
  MTL::Texture* m_accumulator[2] = {nullptr, nullptr};
  MTL::Texture* m_renderTarget = nullptr;
  MTL::Texture* m_randomSource = nullptr;

  // Acceleration structures
  NS::Array* m_meshAccelStructs = nullptr;
  MTL::AccelerationStructure* m_instanceAccelStruct = nullptr;
  MTL::Buffer* m_instanceBuffer = nullptr;

  // Constants and resources
  shaders_pt::Constants m_constants = {};
  MTL::Buffer* m_constantsBuffer = nullptr;
  size_t m_constantsSize = 0, m_constantsStride = 0, m_constantsOffset = 0;

  MTL::Buffer* m_resourcesBuffer = nullptr;
  static constexpr const size_t m_resourcesStride = sizeof(uint64_t);
  std::vector<const MTL::Buffer*> m_meshVertexDataBuffers;

  // Frame data
  static constexpr const size_t m_maxFramesInFlight = 3;
  size_t m_frameIdx = 0, m_accumulationFrames = 128, m_accumulatedFrames = 0;
  size_t m_timer = 0;
  std::chrono::time_point<std::chrono::high_resolution_clock> m_renderStart;

  MTL::AccelerationStructure* makeAccelStruct(MTL::AccelerationStructureDescriptor* desc);

  static NS::SharedPtr<MTL::AccelerationStructureGeometryDescriptor> makeGeometryDescriptor(const Mesh* mesh);

  void buildPipelines();

  void buildConstantsBuffer();

  void rebuildResourcesBuffer();

  void rebuildAccelerationStructures();

  void rebuildRenderTargets();

  void updateConstants(Scene::NodeID cameraNodeId);

};

}

#endif //PLATINUM_RENDERER_PT_HPP
