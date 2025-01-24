#ifndef PLATINUM_RENDERER_PT_HPP
#define PLATINUM_RENDERER_PT_HPP

#include <Metal/Metal.hpp>

#include <core/store.hpp>
#include "pt_shader_defs.hpp"

namespace pt::renderer_pt {

class Renderer {
public:
  enum Integrators {
    Integrator_Simple = 0,
    Integrator_MIS,
  };
  
  enum Status {
    Status_Blocked = 0,
    Status_Ready = 1 << 0,
    Status_Busy = 1 << 2,
    Status_Done = 1 << 3,
  };
  
  Renderer(
    MTL::Device* device,
    MTL::CommandQueue* commandQueue,
    Store& store
  ) noexcept;

  ~Renderer();

  void render();

  void startRender(
		Scene::NodeID cameraNodeId,
		float2 viewportSize,
		uint32_t sampleCount,
		const shaders_pt::PostProcessOptions& ppOpts,
		int flags = 0
  );
  
  [[nodiscard]] constexpr int selectedKernel() const {
    return m_selectedPipeline;
  }
  
  constexpr void selectKernel(int kernel) {
    m_selectedPipeline = kernel;
  }

  [[nodiscard]] const MTL::Texture* presentRenderTarget() const;
  
  [[nodiscard]] NS::SharedPtr<MTL::Buffer> readbackRenderTarget(uint2* size) const;

  [[nodiscard]] int status() const;

  [[nodiscard]] std::pair<size_t, size_t> renderProgress() const;

  [[nodiscard]] size_t renderTime() const;

private:
  // Store
  Store& m_store;

  // Camera and viewport
  float2 m_currentRenderSize = {1, 1};
  float m_aspect = 1.0;

  // Metal
  MTL::Device* m_device = nullptr;
  MTL::CommandQueue* m_commandQueue = nullptr;

  // Path tracing pipeline state
  constexpr static const std::array<std::string, 2> m_pathtracingPipelineFunctions = {
    "pathtracingKernel",
    "misKernel",
  };
  int m_selectedPipeline = Integrator_MIS;
  std::vector<MTL::ComputePipelineState*> m_pathtracingPipelines;
  std::vector<MTL::IntersectionFunctionTable*> m_intersectionFunctionTables;
  MTL::RenderPipelineState* m_postprocessPipeline = nullptr;

  // Render targets
  MTL::Texture* m_accumulator[2] = {nullptr, nullptr};
  MTL::Texture* m_renderTarget = nullptr;
  MTL::Texture* m_randomSource = nullptr;

  // Acceleration structures
  NS::Array* m_meshAccelStructs = nullptr;
  MTL::AccelerationStructure* m_instanceAccelStruct = nullptr;
  MTL::Buffer* m_instanceBuffer = nullptr;
  
  // Light data
  uint32_t m_lightCount = 0;
  float m_lightTotalPower = 0.0f;
  MTL::Buffer* m_lightDataBuffer = nullptr;
  
  uint32_t m_envLightCount = 0;
  MTL::Buffer* m_envLightDataBuffer = nullptr;
  std::vector<const MTL::Buffer*> m_envLightAliasTables;

  // Constants and resources
  shaders_pt::Constants m_constants = {};
  MTL::Buffer* m_constantsBuffer = nullptr;
  size_t m_constantsSize = 0, m_constantsStride = 0, m_constantsOffset = 0;

  static constexpr const size_t m_resourcesStride = sizeof(uint64_t);
  
  MTL::Buffer* m_vertexResourcesBuffer = nullptr;
  std::vector<const MTL::Buffer*> m_meshVertexPositionBuffers;
  std::vector<const MTL::Buffer*> m_meshVertexDataBuffers;
  
  MTL::Buffer* m_primitiveResourcesBuffer = nullptr;
  std::vector<const MTL::Buffer*> m_meshMaterialIndexBuffers;
  
  MTL::Buffer* m_instanceResourcesBuffer = nullptr;
  std::vector<MTL::Buffer*> m_instanceMaterialBuffers;
  
  MTL::Buffer* m_texturesBuffer = nullptr;
  MTL::Buffer* m_argumentBuffer = nullptr;
  
  // LUT textures
  struct LUTInfo {
    const char* filename;
    MTL::TextureType type;
    uint32_t depth = 1;
  };
  static constexpr std::array<LUTInfo, 8> m_lutInfo = {{
    {.filename = "ggx_E", .type = MTL::TextureType2D, .depth = 1},
    {.filename = "ggx_E_avg", .type = MTL::TextureType1D, .depth = 1},
    {.filename = "ggx_ms_E", .type = MTL::TextureType3D, .depth = 32},
    {.filename = "ggx_ms_E_avg", .type = MTL::TextureType2D, .depth = 1},
    {.filename = "ggx_E_trans_in", .type = MTL::TextureType3D, .depth = 32},
    {.filename = "ggx_E_trans_out", .type = MTL::TextureType3D, .depth = 32},
    {.filename = "ggx_E_trans_in_avg", .type = MTL::TextureType2D, .depth = 1},
    {.filename = "ggx_E_trans_out_avg", .type = MTL::TextureType2D, .depth = 1},
  }};
  std::vector<MTL::Texture*> m_luts;
  std::vector<uint32_t> m_lutSizes;

  // Frame data
  static constexpr const size_t m_maxFramesInFlight = 3;
  size_t m_frameIdx = 0, m_accumulationFrames = 128, m_accumulatedFrames = 0;
  size_t m_timer = 0;
  std::chrono::time_point<std::chrono::high_resolution_clock> m_renderStart;
  
  // Postprocess options
  shaders_pt::PostProcessOptions m_postProcessOptions;

  MTL::AccelerationStructure* makeAccelStruct(MTL::AccelerationStructureDescriptor* desc);

  static NS::SharedPtr<MTL::AccelerationStructureGeometryDescriptor> makeGeometryDescriptor(const Mesh* mesh);

  void buildPipelines();

  void buildConstantsBuffer();
  
  void loadGgxLutTextures();

  void rebuildResourceBuffers();

  void rebuildAccelerationStructures();
  
  void rebuildArgumentBuffer();

  void rebuildRenderTargets();
  
  void rebuildLightData();

  void updateConstants(Scene::NodeID cameraNodeId, int flags);
};

}

#endif //PLATINUM_RENDERER_PT_HPP
