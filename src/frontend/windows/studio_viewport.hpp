#ifndef PLATINUM_STUDIO_VIEWPORT_HPP
#define PLATINUM_STUDIO_VIEWPORT_HPP

#include <imgui.h>
#include <SDL.h>

#include <frontend/window.hpp>
#include <frontend/widgets.hpp>
#include <renderer_studio/renderer_studio.hpp>

namespace pt::frontend::windows {

class StudioViewport final : Window {
public:
  constexpr StudioViewport(Store& store, float& dpiScaling, bool* open = nullptr) noexcept
    : Window(store, open), m_dpiScaling(dpiScaling) {
  }

  void init(MTL::Device* device, MTL::CommandQueue* commandQueue);

  void render() final;

  bool handleInputs(const SDL_Event& event);

  const uint8_t* keys = nullptr;

private:
  // Renderer
  std::unique_ptr<renderer_studio::Renderer> m_renderer;

  // Scroll state (smooth scrolling/trackpad support)
  static constexpr const float m_scrollSensitivity = 10.0f;
  static constexpr const float m_scrollFriction = 0.001f;
  static constexpr const float m_scrollStop = 0.001f;
  bool m_scrolling = false;
  float2 m_scrollLastPos = {0, 0};
  float2 m_scrollSpeed = {0, 0};

  // Pinch state (pinch-to-zoom support)
  static constexpr const float m_zoomSensitivity = 1.0f;
  static constexpr const float m_zoomFriction = 0.001f;
  static constexpr const float m_zoomStop = 0.001f;
  bool m_zooming = false;
  float m_zoomSpeed = 0.0f;

  // Viewport properties
  bool m_mouseInViewport = false;
  float2 m_viewportSize = {1, 1};
  float2 m_viewportTopLeft = {0, 0};
  float& m_dpiScaling;

  void updateScrollAndZoomState();
};

}

#endif //PLATINUM_STUDIO_VIEWPORT_HPP
