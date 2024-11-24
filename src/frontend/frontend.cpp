#include "frontend.hpp"

#include <print>
#include <Foundation/Foundation.hpp>
#include <imgui_internal.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_metal.h>

#include <utils/metal_utils.hpp>

namespace pt::frontend {
using metal_utils::operator ""_ns;

bool isExitEvent(const SDL_Event& event, uint32_t windowID) {
  return event.type == SDL_QUIT || (
    event.type == SDL_WINDOWEVENT &&
    event.window.event == SDL_WINDOWEVENT_CLOSE &&
    event.window.windowID == windowID
  );
}

Frontend::Frontend(Store& store) noexcept: m_store(store) {
}

Frontend::~Frontend() {
  m_commandQueue->release();
  m_device->release();
  m_rpd->release();

  m_renderTarget->release();
}

void Frontend::start() noexcept {
  /*
   * Set up ImGui
   */
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

  ImGui::StyleColorsLight();

  /*
   * Initialize SDL and set hints to render using Metal
   */
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
    std::print(stderr, "SDL init failed: {}\n", SDL_GetError());
    return; // TODO return error code
  }

  SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
  SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");

  /*
   * Set up SDL window and renderer
   */
  m_sdlWindow = SDL_CreateWindow(
    m_defaultTitle.c_str(),
    SDL_WINDOWPOS_CENTERED,
    SDL_WINDOWPOS_CENTERED,
    1200,
    800,
    SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
  );
  // TODO check nullptr

  m_sdlRenderer = SDL_CreateRenderer(
    m_sdlWindow,
    -1,
    SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
  );
  // TODO check nullptr

  m_keys = SDL_GetKeyboardState(nullptr);

  /*
   * Scale fonts for high DPI rendering
   * TODO: should rescale on monitor change
   */
  int renderWidth, windowWidth;
  SDL_GetRendererOutputSize(m_sdlRenderer, &renderWidth, nullptr);
  SDL_GetWindowSize(m_sdlWindow, &windowWidth, nullptr);
  m_dpiScaling =
    static_cast<float>(renderWidth) / static_cast<float>(windowWidth);

  io.Fonts->AddFontFromFileTTF(
    "/Users/teo/Library/Fonts/SF-Pro-Text-Regular.otf",
    14.0f * m_dpiScaling
  );
  io.FontGlobalScale = 1.0f / m_dpiScaling;

  /*
   * Set up Metal/SDL2 renderer/platform backend
   */
  m_layer = static_cast<CA::MetalLayer*>(SDL_RenderGetMetalLayer(m_sdlRenderer));
  m_layer->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
  m_device = metal_utils::getDevice(m_layer);

  ImGui_ImplMetal_Init(m_device);
  ImGui_ImplSDL2_InitForMetal(m_sdlWindow);

  m_commandQueue = m_device->newCommandQueue();
  m_rpd = MTL::RenderPassDescriptor::alloc()->init();

  /*
   * Initialize renderer
   */
  m_renderer = std::make_unique<renderer_studio::Renderer>(
    m_device,
    m_commandQueue,
    m_store
  );

  /*
   * Main application loop
   */
  bool exit = false;
  while (!exit) {
    NS::AutoreleasePool* autoreleasePool = NS::AutoreleasePool::alloc()->init();

    // Event polling
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL2_ProcessEvent(&event);
      if (isExitEvent(event, SDL_GetWindowID(m_sdlWindow))) {
        exit = true;
      } else {
        handleInput(event);
      }
    }

    // Handle resize
    int width, height;
    SDL_GetRendererOutputSize(m_sdlRenderer, &width, &height);
    metal_utils::setDrawableSize(m_layer, width, height);

//    m_viewportSize.x() = width;
//    m_viewportSize.y() = height;

    // Draw ImGui
    auto drawable = metal_utils::nextDrawable(m_layer);

    auto cmd = m_commandQueue->commandBuffer();
    auto colorAttachment = m_rpd->colorAttachments()->object(0);
    colorAttachment->setClearColor(
      MTL::ClearColor::Make(
        m_clearColor[0] * m_clearColor[3],
        m_clearColor[1] * m_clearColor[3],
        m_clearColor[2] * m_clearColor[3],
        m_clearColor[3]
      )
    );
    colorAttachment->setTexture(drawable->texture());
    colorAttachment->setLoadAction(MTL::LoadActionClear);
    colorAttachment->setStoreAction(MTL::StoreActionStore);

    // Create render target
    auto texd = MTL::TextureDescriptor::alloc()->init();
    texd->setTextureType(MTL::TextureType2D);
    texd->setWidth(static_cast<uint32_t>(m_viewportSize.x * m_dpiScaling));
    texd->setHeight(static_cast<uint32_t>(m_viewportSize.y * m_dpiScaling));
    texd->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
    texd->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);

    m_renderTarget = m_device->newTexture(texd);
    texd->release();

    m_renderer->render(m_renderTarget);

    auto enc = cmd->renderCommandEncoder(m_rpd);
    enc->pushDebugGroup("ImGui demo"_ns);

    ImGui_ImplMetal_NewFrame(m_rpd);
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    drawImGui();

    ImGui::Render();
    ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), cmd, enc);

    enc->popDebugGroup();
    enc->endEncoding();

    cmd->presentDrawable(drawable);
    cmd->commit();

    autoreleasePool->release();
  }

  /**
   * Cleanup
   */
  ImGui_ImplMetal_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();

  SDL_DestroyRenderer(m_sdlRenderer);
  SDL_DestroyWindow(m_sdlWindow);
  SDL_Quit();
}

void Frontend::drawImGui() noexcept {
  mainDockSpace();

  if (m_showDemo) ImGui::ShowDemoWindow(&m_showDemo);

  {
    ImGui::Begin("Hello World");

    ImGui::Text("Some text here");
    ImGui::Checkbox("Demo Window", &m_showDemo);
    if (ImGui::ColorEdit3("Background Color", m_renderer->clearColor())) {
      m_renderer->updateClearColor();
    }

    ImGui::End();
  }

  {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::Begin("Down");
    ImGui::Text("Some text here");
    ImGui::Text(
      "Application average %.3f ms/frame (%.1f FPS)",
      1000.0f / io.Framerate,
      io.Framerate
    );
    ImGui::End();
  }

  {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 1.0f));
    ImGui::Begin("Viewport");
    ImGui::PopStyleVar();

    auto vMin = ImGui::GetWindowContentRegionMin();
    auto vMax = ImGui::GetWindowContentRegionMax();
    m_viewportSize = {vMax.x - vMin.x, vMax.y - vMin.y};

    ImGui::Image(
      (ImTextureID) m_renderTarget,
      {
        m_viewportSize.x,
        m_viewportSize.y
      }
    );
    ImGui::End();
  }
}

void Frontend::handleInput(const SDL_Event& event) noexcept {

}

void Frontend::mainDockSpace() noexcept {
  ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking;
  windowFlags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration;
  windowFlags |= ImGuiWindowFlags_NoBackground;
  windowFlags |=
    ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

  auto viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->Pos);
  ImGui::SetNextWindowSize(viewport->Size);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::Begin("DockSpace", nullptr, windowFlags);
  ImGui::PopStyleVar(3);

  ImGuiIO& io = ImGui::GetIO();

  if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) {
    auto dockSpaceId = ImGui::GetID("MainDockSpace");
    m_initialized |= ImGui::DockBuilderGetNode(dockSpaceId) != nullptr;

    ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_PassthruCentralNode;
    ImGui::DockSpace(dockSpaceId, {0, 0}, dockspaceFlags);

    if (!m_initialized) {
      m_initialized = true;

      ImGui::DockBuilderRemoveNode(dockSpaceId);
      ImGui::DockBuilderAddNode(
        dockSpaceId,
        dockspaceFlags | ImGuiDockNodeFlags_DockSpace
      );
      ImGui::DockBuilderSetNodeSize(dockSpaceId, viewport->Size);

      auto dockIdLeft = ImGui::DockBuilderSplitNode(
        dockSpaceId, ImGuiDir_Left, 0.4f,
        nullptr, &dockSpaceId
      );
      auto dockIdDown = ImGui::DockBuilderSplitNode(
        dockSpaceId, ImGuiDir_Down, 0.5f,
        nullptr, &dockSpaceId
      );

      ImGui::DockBuilderDockWindow("Hello World", dockIdLeft);
      ImGui::DockBuilderDockWindow("Down", dockIdDown);
      ImGui::DockBuilderDockWindow("Viewport", dockSpaceId);
      ImGui::DockBuilderFinish(dockSpaceId);
    }
  }

  ImGui::End();
}

}
