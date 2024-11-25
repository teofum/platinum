#ifndef PLATINUM_FRONTEND_HPP
#define PLATINUM_FRONTEND_HPP

#include <string>
#include <optional>
#include <SDL.h>
#include <imgui.h>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include <core/store.hpp>
#include <renderer_studio/renderer_studio.hpp>

namespace pt::frontend {

class Frontend {
public:
  explicit Frontend(Store& store) noexcept;

  ~Frontend();

  void start();

private:
  static constexpr const std::string m_defaultTitle = "Pt [SDL2 | Metal]";

  // SDL
  SDL_Window* m_sdlWindow = nullptr;
  SDL_Renderer* m_sdlRenderer = nullptr;
  const uint8_t* m_keys = nullptr;

  // Scroll state (smooth scrolling/trackpad support)
  static constexpr const float m_scrollSensitivity = 10;
  static constexpr const float m_scrollFriction = 0.001f;
  static constexpr const float m_scrollStop = 0.001f;
  bool m_scrolling = false;
  float2 m_scrollLastPos = false;
  float2 m_scrollSpeed = false;

  // Metal
  CA::MetalLayer* m_layer = nullptr;
  MTL::Device* m_device = nullptr;
  MTL::CommandQueue* m_commandQueue = nullptr;
  MTL::RenderPassDescriptor* m_rpd = nullptr;

  // ImGui
  bool m_initialized = false;
  float m_clearColor[4] = {0.45f, 0.55f, 0.6f, 1.0f};
  float m_dpiScaling = 1.0f;
  float2 m_viewportSize = {1, 1};
  bool m_mouseInViewport = false;

  // Scene Explorer state
  std::optional<uint32_t> m_selectedNodeIdx, m_nextNodeIdx;

  // Store
  Store& m_store;

  // Renderer
  bool m_viewportWasResized = false;
  std::unique_ptr<renderer_studio::Renderer> m_renderer;
  MTL::Texture* m_renderTarget = nullptr;

  void drawImGui();

  void handleInput(const SDL_Event& event);

  void handleScrollState();

  void rebuildRenderTarget();

  /**
   * ImGui windows
   */
  void mainDockSpace();

  void sceneExplorer();

  void sceneExplorerNode(uint32_t idx);

  void properties();
};

}

#endif //PLATINUM_FRONTEND_HPP
