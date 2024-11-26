#ifndef PLATINUM_RENDERER_STUDIO_HPP
#define PLATINUM_RENDERER_STUDIO_HPP

#include <Metal/Metal.hpp>

#include <core/store.hpp>
#include "shader_defs.hpp"
#include "camera.hpp"

namespace pt::renderer_studio {

class Renderer {
  struct MeshData {
    size_t vertexPosOffset;
    size_t vertexDataOffset;
    size_t vertexCount;
    size_t indexOffset;
    size_t indexCount;
    uint16_t nodeIdx;
  };

public:
  Renderer(
    MTL::Device* device,
    MTL::CommandQueue* commandQueue,
    Store& store
  ) noexcept;

  ~Renderer();

  void render() noexcept;

  float* clearColor();

  void handleScrollEvent(const float2& delta);

  void handleZoomEvent(float delta);

  void handleResizeViewport(const float2& size);

  [[nodiscard]] const MTL::Texture* presentRenderTarget() const;

  [[nodiscard]] uint16_t readbackObjectIdAt(uint32_t x, uint32_t y) const;

private:
  // Store
  Store& m_store;

  // Camera and viewport
  Camera m_camera;
  float2 m_viewportSize = {1, 1};
  float m_aspect = 1.0;
  float m_clearColor[4] = {0.45f, 0.55f, 0.60f, 1.0f};

  // Metal
  MTL::Device* m_device = nullptr;
  MTL::CommandQueue* m_commandQueue = nullptr;

  // Render targets
  MTL::Texture* m_primaryRenderTarget = nullptr;
  MTL::Texture* m_auxRenderTarget = nullptr;
  MTL::Texture* m_objectIdRenderTarget = nullptr;

  // Main pass pipeline state and buffers
  MTL::RenderPipelineState* m_pso = nullptr;
  MTL::DepthStencilState* m_dsso = nullptr;
  MTL::Buffer* m_vertexPosBuffer = nullptr;
  MTL::Buffer* m_vertexDataBuffer = nullptr;
  MTL::Buffer* m_indexBuffer = nullptr;
  MTL::Buffer* m_dataBuffer = nullptr;
  std::vector<MeshData> m_meshData;

  // Post process pass pipeline state and buffers
  MTL::RenderPipelineState* m_postPassPso = nullptr;
  MTL::SamplerState* m_postPassSso = nullptr;
  MTL::Buffer* m_postPassVertexBuffer = nullptr;
  MTL::Buffer* m_postPassIndexBuffer = nullptr;

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

  void buildBuffers();

  void buildShaders();

  void rebuildRenderTargets();

  void updateConstants();
};

}

#endif //PLATINUM_RENDERER_STUDIO_HPP
