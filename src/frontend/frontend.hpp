#ifndef PLATINUM_FRONTEND_HPP
#define PLATINUM_FRONTEND_HPP

#include <string>
#include <optional>
#include <SDL.h>
#include <imgui.h>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include <core/store.hpp>
#include <renderer_pt/renderer_pt.hpp>
#include <frontend/state.hpp>
#include <frontend/windows/properties.hpp>
#include <frontend/windows/scene_explorer.hpp>
#include <frontend/windows/studio_viewport.hpp>
#include <frontend/windows/pt_viewport.hpp>
#include <frontend/windows/tools/ms_lut_gen.hpp>

namespace pt::frontend {

class Frontend {
public:
  enum InitResult {
    InitResult_Ok = 0,
    InitResult_SDLInitFailed,
    InitResult_SDLCreateWindowFailed,
    InitResult_SDLCreateRendererFailed,
  };

  explicit Frontend(Store& store) noexcept;

  ~Frontend();

  InitResult init();

  void start();

private:
  static constexpr const std::string m_defaultTitle = "Pt [SDL2 | Metal]";

  // SDL
  SDL_Window* m_sdlWindow = nullptr;
  SDL_Renderer* m_sdlRenderer = nullptr;
  const uint8_t* m_keys = nullptr;

  // Metal
  CA::MetalLayer* m_layer = nullptr;
  MTL::Device* m_device = nullptr;
  MTL::CommandQueue* m_commandQueue = nullptr;

  // Store and frontend shared state
  Store& m_store;
  State m_state;

  // Windows
  windows::Properties m_properties;
  windows::SceneExplorer m_sceneExplorer;
  windows::StudioViewport m_studioViewport;
  windows::RenderViewport m_renderViewport;
  
  windows::MultiscatterLutGenerator m_multiscatterLutGenerator;
  
  // Closeable windows open state
  bool m_toolMultiscatterLutGeneratorOpen = false;

  // ImGui
  bool m_initialized = false;
  float m_clearColor[4] = {0.45f, 0.55f, 0.6f, 1.0f};
  float m_dpiScaling = 1.0f;

  void drawImGui();

  void handleInput(const SDL_Event& event);

  void mainDockSpace();
  
  void renderMenuBar();
};

}

#endif //PLATINUM_FRONTEND_HPP
