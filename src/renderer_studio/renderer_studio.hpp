#ifndef PLATINUM_RENDERER_STUDIO_HPP
#define PLATINUM_RENDERER_STUDIO_HPP

#include <Metal/Metal.hpp>

#include <core/store.hpp>
#include "shader_defs.hpp"
#include "camera.hpp"

namespace pt::renderer_studio {

class Renderer {
  struct MeshData {
    size_t vertexOffset;
    size_t vertexCount;
    size_t indexOffset;
    size_t indexCount;
  };

public:
  Renderer(
    MTL::Device* device,
    MTL::CommandQueue* commandQueue,
    Store& store
  ) noexcept;

  ~Renderer();

  void render(MTL::Texture* renderTarget) noexcept;

  float* clearColor();

  void updateClearColor();

  void handleScrollEvent(const float2& delta);

private:
  // Store
  Store& m_store;

  // Metal
  MTL::Device* m_device = nullptr;
  MTL::CommandQueue* m_commandQueue = nullptr;
  MTL::RenderPassDescriptor* m_rpd = nullptr;
  MTL::RenderPipelineState* m_pso = nullptr;
  MTL::DepthStencilState* m_dsso = nullptr;

  // Buffers
  MTL::Buffer* m_vertexBuffer = nullptr;
  MTL::Buffer* m_indexBuffer = nullptr;
  MTL::Buffer* m_transformBuffer = nullptr;

  std::vector<MeshData> m_meshData;

  // Constants
  MTL::Buffer* m_constantsBuffer = nullptr;
  size_t m_constantsSize = 0, m_constantsStride = 0, m_constantsOffset = 0;

  // Frame data
  static constexpr size_t m_maxFramesInFlight = 3;
  size_t m_frameIdx = 0;
  std::chrono::time_point<std::chrono::high_resolution_clock> m_startTime;

  // Camera
  Camera m_camera;
  float m_aspect = 1.0;

  float m_clearColor[4] = {0.45f, 0.55f, 0.60f, 1.0f};

  void buildBuffers();

  void buildShaders();

  void updateConstants();
};

}

#endif //PLATINUM_RENDERER_STUDIO_HPP
