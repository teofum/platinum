#ifndef PLATINUM_PT_VIEWPORT_HPP
#define PLATINUM_PT_VIEWPORT_HPP

#include <imgui.h>
#include <SDL.h>

#include <frontend/window.hpp>
#include <frontend/widgets.hpp>
#include <renderer_pt/renderer_pt.hpp>

namespace pt::frontend::windows {

class RenderViewport final : Window {
public:
  constexpr RenderViewport(Store& store, State& state, float& dpiScaling, bool* open = nullptr) noexcept
    : Window(store, state, open), m_dpiScaling(dpiScaling) {
  }

  void init(MTL::Device* device, MTL::CommandQueue* commandQueue);

  void render() final;

  bool handleInputs(const SDL_Event& event);

  const uint8_t* keys = nullptr;

private:
  // Renderer
  std::unique_ptr<renderer_pt::Renderer> m_renderer;

  // Scroll state (smooth scrolling/trackpad support)
  static constexpr const float m_scrollSensitivity = 20.0f;
  static constexpr const float m_scrollFriction = 0.005f;
  static constexpr const float m_scrollStop = 0.001f;
  bool m_scrolling = false;
  float2 m_scrollLastPos = {0.0f, 0.0f};
  float2 m_scrollSpeed = {0.0f, 0.0f};
  float2 m_minOffset = {0.0f, 0.0f};
  float2 m_maxOffset = {0.0f, 0.0f};
  float2 m_offset = {0.0f, 0.0f};

  // Pinch state (pinch-to-zoom support)
  static constexpr const float m_zoomSensitivity = 1.0f;
  static constexpr const float m_zoomFriction = 0.001f;
  static constexpr const float m_zoomStop = 0.001f;
  bool m_zooming = false;
  float m_zoomSpeed = 0.0f;
  float m_minZoomFactor = 0.5f;
  float m_maxZoomFactor = 10.0f;
  float m_zoomFactor = 1.0f;
  float2 m_zoomCenter = {0, 0};

  // Viewport properties
  bool m_mouseInViewport = false;
  float2 m_viewportSize = {1, 1};
  float2 m_renderSize = {1, 1};
  float2 m_viewportTopLeft = {0, 0};
  float& m_dpiScaling;

  // Render settings
  std::optional<Scene::NodeID> m_cameraNodeId;
  float2 m_nextRenderSize = {1280, 800};
  bool m_useViewportSizeForRender = true;

  void updateScrollAndZoomState();
};

}

#endif //PLATINUM_PT_VIEWPORT_HPP
