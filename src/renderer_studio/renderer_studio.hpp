#ifndef PLATINUM_RENDERER_STUDIO_HPP
#define PLATINUM_RENDERER_STUDIO_HPP

#include <Metal/Metal.hpp>

#include "shader_defs.hpp"

namespace pt::renderer_studio {

class Renderer {
public:
  Renderer(MTL::Device* device, MTL::CommandQueue* commandQueue) noexcept;

  ~Renderer();

  void render(MTL::Texture* renderTarget) noexcept;

  float* clearColor();

  void updateClearColor();

private:
  MTL::Device* m_device = nullptr;
  MTL::CommandQueue* m_commandQueue = nullptr;
  MTL::RenderPassDescriptor* m_rpd = nullptr;

  MTL::RenderPipelineState* m_pso = nullptr;
  MTL::DepthStencilState* m_dsso = nullptr;
  MTL::Buffer* m_vertexBuffer = nullptr;
  MTL::Buffer* m_indexBuffer = nullptr;

  MTL::Buffer* m_constantsBuffer = nullptr;
  size_t m_constantsSize = 0, m_constantsStride = 0, m_constantsOffset = 0;

  static constexpr size_t m_maxFramesInFlight = 3;
  size_t m_frameIdx = 0;

  std::chrono::time_point<std::chrono::high_resolution_clock> m_startTime;

  float3 m_cameraPos = {0.0f, 0.0f, 5.0f};
  float m_fov = 45.0f;
  float m_aspect = 1.0;

  // Temp stuff
  static constexpr const Vertex m_vertexData[] = {
    {{1,  1,  -1}, {1, 1, 0, 1}},
    {{1,  -1, -1}, {1, 0, 0, 1}},
    {{1,  1,  1},  {1, 1, 1, 1}},
    {{1,  -1, 1},  {1, 0, 1, 1}},
    {{-1, 1,  -1}, {0, 1, 0, 1}},
    {{-1, -1, -1}, {0, 0, 0, 1}},
    {{-1, 1,  1},  {0, 1, 1, 1}},
    {{-1, -1, 1},  {0, 0, 1, 1}},
  };
  static constexpr size_t m_vertexCount = sizeof(m_vertexData) / sizeof(Vertex);

  static constexpr const unsigned m_indexData[] = {
    4, 2, 0, 2, 7, 3,
    6, 5, 7, 1, 7, 5,
    0, 3, 1, 4, 1, 5,
    4, 6, 2, 2, 6, 7,
    6, 4, 5, 1, 3, 7,
    0, 2, 3, 4, 0, 1,
  };
  static constexpr size_t m_indexCount = sizeof(m_indexData) / sizeof(unsigned);

  float m_clearColor[4] = {0.45f, 0.55f, 0.60f, 1.0f};

  void buildBuffers();

  void buildShaders();

  void updateConstants();

  int64_t getElapsedMillis();
};

}

#endif //PLATINUM_RENDERER_STUDIO_HPP
