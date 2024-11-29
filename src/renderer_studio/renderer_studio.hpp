#ifndef PLATINUM_RENDERER_STUDIO_HPP
#define PLATINUM_RENDERER_STUDIO_HPP

#include <Metal/Metal.hpp>

#include <core/store.hpp>
#include "shader_defs.hpp"
#include "studio_camera.hpp"

namespace pt::renderer_studio {

class Renderer {
public:
  Renderer(
    MTL::Device* device,
    MTL::CommandQueue* commandQueue,
    Store& store
  ) noexcept;

  ~Renderer();

  void render(Scene::NodeID selectedNodeId = 0);

  void handleScrollEvent(const float2& delta);

  void handleZoomEvent(float delta);

  void handlePanEvent(const float2& delta);

  void handleResizeViewport(const float2& size);

  void cameraTo(const float3& pos);

  [[nodiscard]] const MTL::Texture* presentRenderTarget() const;

  [[nodiscard]] Scene::NodeID readbackObjectIdAt(uint32_t x, uint32_t y) const;

private:
  // Store
  Store& m_store;

  // Camera and viewport
  Camera m_camera;
  float2 m_viewportSize = {1, 1};
  float m_aspect = 1.0;
  float m_clearColor[4] = {0.8f, 0.8f, 0.8f, 1.0f};

  // Metal
  MTL::Device* m_device = nullptr;
  MTL::CommandQueue* m_commandQueue = nullptr;

  // Render targets
  MTL::Texture* m_primaryRenderTarget = nullptr;
  MTL::Texture* m_auxRenderTarget = nullptr;
  MTL::Texture* m_objectIdRenderTarget = nullptr;
  MTL::Texture* m_depthTexture = nullptr;
  MTL::Texture* m_stencilTexture = nullptr;

  // Shared buffers
  MTL::Buffer* m_simpleQuadVertexBuffer = nullptr;
  MTL::Buffer* m_simpleQuadIndexBuffer = nullptr;

  // Main pass pipeline state and buffers
  MTL::RenderPipelineState* m_pso = nullptr;
  MTL::DepthStencilState* m_dsso = nullptr;
  MTL::Buffer* m_dataBuffer = nullptr;
  std::vector<Scene::InstanceData> m_meshData;

  // Camera pass pipeline state and buffers
  MTL::RenderPipelineState* m_cameraPso = nullptr;
  MTL::DepthStencilState* m_cameraDsso = nullptr;
  MTL::Buffer* m_cameraVertexBuffer = nullptr;
  MTL::Buffer* m_cameraIndexBuffer = nullptr;
  MTL::Buffer* m_cameraDataBuffer = nullptr;
  std::vector<Scene::CameraData> m_cameraData;

  // Grid pass pipeline state
  MTL::RenderPipelineState* m_gridPassPso = nullptr;
  MTL::DepthStencilState* m_gridPassDsso = nullptr;
  static constexpr const GridProperties m_gridProperties = {
    .size = 10000.0f,
    .spacing = 0.1f,
    .lineWidth = 1.0f,
    .fadeDistance = 1.0f,
    .lineColor = {0.3f, 0.3f, 0.3f},
    .xAxisColor = {0.4f, 0.05f, 0.08f},
    .zAxisColor = {0.05f, 0.08f, 0.4f},
  };

  // Post process pass pipeline state
  MTL::RenderPipelineState* m_postPassPso = nullptr;
  MTL::SamplerState* m_postPassSso = nullptr;

  // Readback buffer
  static constexpr const uint32_t m_objectIdPixelSize = sizeof(uint16_t);
  MTL::Buffer* m_objectIdReadbackBuffer = nullptr;

  // Constants
  MTL::Buffer* m_constantsBuffer = nullptr;
  size_t m_constantsSize = 0, m_constantsStride = 0, m_constantsOffset = 0;

  // Frame data
  static constexpr size_t m_maxFramesInFlight = 3;
  size_t m_frameIdx = 0;
  std::chrono::time_point<std::chrono::high_resolution_clock> m_startTime;

  void rebuildDataBuffers();

  void buildPipelines();

  void rebuildRenderTargets();

  void updateConstants();
};

}

#endif //PLATINUM_RENDERER_STUDIO_HPP
