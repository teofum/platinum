#ifndef PLATINUM_PT_VIEWPORT_HPP
#define PLATINUM_PT_VIEWPORT_HPP

#include <imgui.h>
#include <SDL.h>

#include <core/postprocessing.hpp>
#include <frontend/window.hpp>
#include <frontend/widgets.hpp>
#include <renderer_pt/renderer_pt.hpp>
#include <renderer_pt/pt_shader_defs.hpp>

namespace pt::frontend::windows {

class RenderViewport final : Window {
public:
  RenderViewport(Store& store, State& state, float& dpiScaling, bool* open = nullptr) noexcept;

  void init(MTL::Device* device, MTL::CommandQueue* commandQueue);

  void render() final;

  void startRender();

  [[nodiscard]] bool canRender() const;

  [[nodiscard]] bool hasImage() const;

  void exportImage() const;

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
  int32_t m_nextRenderSampleCount = 128;
  bool m_useViewportSizeForRender = true;
  int m_renderFlags = shaders_pt::RendererFlags_MultiscatterGGX;

  // Post process settings
  const hashmap<postprocess::Tonemapper, std::string> m_tonemappers = {
    {postprocess::Tonemapper::None,       "None"},
    {postprocess::Tonemapper::AgX,        "AgX"},
    {postprocess::Tonemapper::KhronosPBR, "Khronos PBR Neutral"},
    {postprocess::Tonemapper::flim,       "flim"},
  };

  void updateScrollAndZoomState();

  void renderSettingsWindow(const std::vector<Scene::CameraInstance>& cameras, const std::string& label);

  void renderPostprocessSettings();
};

}

#endif //PLATINUM_PT_VIEWPORT_HPP
