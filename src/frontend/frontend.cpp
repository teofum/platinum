#include "frontend.hpp"

#include <print>
#include <Foundation/Foundation.hpp>
#include <imgui_internal.h>
#include <implot.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_metal.h>

#include <utils/metal_utils.hpp>
#include <utils/cocoa_utils.h>

#include "theme.hpp"

namespace pt::frontend {
using metal_utils::operator ""_ns;

static bool isExitEvent(const SDL_Event& event, uint32_t windowID) {
  return event.type == SDL_QUIT || (
    event.type == SDL_WINDOWEVENT &&
    event.window.event == SDL_WINDOWEVENT_CLOSE &&
    event.window.windowID == windowID
  );
}

Frontend::Frontend(Store& store) noexcept
  : m_store(store), m_state(store),
    m_properties(m_store, m_state),
    m_sceneExplorer(m_store, m_state),
    m_assetManager(m_store, m_state),
    m_studioViewport(m_store, m_state, m_dpiScaling),
    m_renderViewport(m_store, m_state, m_dpiScaling),
    m_multiscatterLutGenerator(m_store, m_state, &m_toolMultiscatterLutGeneratorOpen) {
  m_semaphore = dispatch_semaphore_create(1);
}

Frontend::~Frontend() {
  m_commandQueue->release();
  m_device->release();
}

Frontend::InitResult Frontend::init() {
  /*
   * Set up ImGui
   */
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

  ImGui::StyleColorsLight();
  auto& style = ImGui::GetStyle();
  style.FrameRounding = 4.0f;
  style.PopupRounding = 4.0f;
  style.WindowRounding = 4.0f;
  style.IndentSpacing = 12.0f;
  style.ChildBorderSize = 1.0f;
  style.TabBarOverlineSize = 0.0f;
  style.SeparatorTextBorderSize = 1.0f;
  style.GrabRounding = 4.0f;
  style.GrabMinSize = 0.0f;
  style.WindowMenuButtonPosition = ImGuiDir_None;

  theme::apply(style, isSystemDarkModeEnabled() ? theme::platinumDark : theme::platinumLight);

  SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
  SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
  SDL_SetHint(SDL_HINT_TRACKPAD_IS_TOUCH_ONLY, "1");

  /*
   * Initialize SDL and set hints to render using Metal
   */
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
    std::println(stderr, "SDL init failed: {}", SDL_GetError());
    return Frontend::InitResult_SDLInitFailed;
  }

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
  if (!m_sdlWindow) {
    std::println(stderr, "SDL create window failed: {}", SDL_GetError());
    return Frontend::InitResult_SDLCreateWindowFailed;
  }

  setupWindowStyle(m_sdlWindow);

  m_sdlRenderer = SDL_CreateRenderer(
    m_sdlWindow,
    -1,
    SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
  );
  if (!m_sdlRenderer) {
    std::println(stderr, "SDL create renderer failed: {}", SDL_GetError());
    return Frontend::InitResult_SDLCreateRendererFailed;
  }

  m_keys = SDL_GetKeyboardState(nullptr);
  m_sceneExplorer.keys = m_keys;
  m_studioViewport.keys = m_keys;
  m_renderViewport.keys = m_keys;

  /*
   * Scale fonts for high DPI rendering
   * TODO: should rescale on monitor change
   */
  int renderWidth, windowWidth;
  SDL_GetRendererOutputSize(m_sdlRenderer, &renderWidth, nullptr);
  SDL_GetWindowSize(m_sdlWindow, &windowWidth, nullptr);
  m_dpiScaling =
    static_cast<float>(renderWidth) / static_cast<float>(windowWidth);

  // FIXME use a consistent directory for font files!
  //   We can't redistribute these, but we can tell users where to put them in the README
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

  /*
   * Initialize store
   */
  m_store.setDevice(m_device);
  m_store.setCommandQueue(m_commandQueue);

  /*
   * Initialize PT renderer
   */
  m_renderer = std::make_unique<renderer_pt::Renderer>(
    m_device,
    m_commandQueue,
    m_store
  );

  /*
   * Initialize windows that need it
   */
  m_studioViewport.init(m_device, m_commandQueue);
  m_renderViewport.init(m_renderer.get());
  m_multiscatterLutGenerator.init(m_device, m_commandQueue);

  return Frontend::InitResult_Ok;
}

void Frontend::start() {
  /*
   * Main application loop
   */
  bool exit = false;
  while (!exit) {
    NS::AutoreleasePool* autoreleasePool = NS::AutoreleasePool::alloc()->init();

    // Event polling and input
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL2_ProcessEvent(&event);
      if (isExitEvent(event, SDL_GetWindowID(m_sdlWindow))) {
        exit = true;
      } else if (event.type == SDL_WINDOWEVENT) {
        setupWindowStyle(m_sdlWindow);
      } else {
        handleInput(event);
      }
    }

    // PT Renderer
    m_renderer->render();

    // Rendering
    auto cmd = m_commandQueue->commandBuffer();
    dispatch_semaphore_wait(m_semaphore, DISPATCH_TIME_FOREVER);
    cmd->addCompletedHandler([&](MTL::CommandBuffer* cmd) { dispatch_semaphore_signal(m_semaphore); });

    auto drawable = metal_utils::nextDrawable(m_layer);

    auto rpd = metal_utils::ns_shared<MTL::RenderPassDescriptor>();
    auto colorAttachment = rpd->colorAttachments()->object(0);
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

    // Render ImGui
    auto enc = cmd->renderCommandEncoder(rpd);
    enc->pushDebugGroup("ImGui"_ns);

    ImGui_ImplMetal_NewFrame(rpd);
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    drawImGui();

    ImGui::Render();
    ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), cmd, enc);

    enc->popDebugGroup();
    enc->endEncoding();

    cmd->presentDrawable(drawable);
    cmd->commit();

    // Update frontend shared state
    //  We do this after the frame has been drawn to avoid issues with deleted assets being added
    //  to drawlists. This introduces a 1-frame delay to see changes reflected in the viewport.
    m_state.update();

    autoreleasePool->release();
  }

  /*
   * Cleanup
   */
  ImGui_ImplMetal_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImPlot::DestroyContext();
  ImGui::DestroyContext();

  SDL_DestroyRenderer(m_sdlRenderer);
  SDL_DestroyWindow(m_sdlWindow);
  SDL_Quit();
}

void Frontend::handleInput(const SDL_Event& event) {
  if (m_studioViewport.handleInputs(event)) return;
  if (m_renderViewport.handleInputs(event)) return;
}

void Frontend::drawImGui() {
  // Render main dockspace
  mainDockSpace();

  // Render controls windows
  m_sceneExplorer.render();
  m_assetManager.render();
  m_properties.render();

  // Additional windows
  if (m_toolMultiscatterLutGeneratorOpen) m_multiscatterLutGenerator.render();

  // Render view windows
  m_studioViewport.render();
  m_renderViewport.render();

//  ImGui::Text(
//    "Application average %.3f ms/frame (%.1f FPS)",
//    1000.0f / io.Framerate,
//    io.Framerate
//  );
}

void Frontend::mainDockSpace() {
  ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking;
  windowFlags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration;
  windowFlags |= ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_MenuBar;
  windowFlags |=
    ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

  auto viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->Pos);
  ImGui::SetNextWindowSize(viewport->Size);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 7.0f));
  ImGui::Begin("DockSpace", nullptr, windowFlags);
  ImGui::PopStyleVar(4);

  renderMenuBar();

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
        dockSpaceId, ImGuiDir_Left, 0.25f,
        nullptr, &dockSpaceId
      );
      auto dockIdLeftLower = ImGui::DockBuilderSplitNode(
        dockIdLeft, ImGuiDir_Down, 0.4f,
        nullptr, &dockIdLeft
      );
      auto dockIdRight = ImGui::DockBuilderSplitNode(
        dockSpaceId, ImGuiDir_Right, 0.35f,
        nullptr, &dockSpaceId
      );
      auto dockIdRightLower = ImGui::DockBuilderSplitNode(
        dockIdRight, ImGuiDir_Down, 0.6f,
        nullptr, &dockIdRight
      );
      auto dockIdBottom = ImGui::DockBuilderSplitNode(
        dockSpaceId, ImGuiDir_Down, 0.25f,
        nullptr, &dockSpaceId
      );

      ImGui::DockBuilderDockWindow("Scene Explorer", dockIdLeft);
      ImGui::DockBuilderDockWindow("Properties", dockIdLeftLower);
      ImGui::DockBuilderDockWindow("Render", dockIdRight);
      ImGui::DockBuilderDockWindow("Render Settings", dockIdRightLower);
      ImGui::DockBuilderDockWindow("Asset Manager", dockIdBottom);
      ImGui::DockBuilderDockWindow("Viewport", dockSpaceId);
      ImGui::DockBuilderFinish(dockSpaceId);
    }
  }

  ImGui::End();
}

void Frontend::renderMenuBar() {
  /*
   * Process global keyboard shortcuts. We do this here since it keeps the handling code close to
   * the UI where the shortcuts are displayed
   */
  if (!ImGui::IsItemActive()) {
    // File
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O))
      m_store.open();

    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
      m_store.saveAs();

    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_I))
      m_store.importGltf();

    // Render
    if (ImGui::IsKeyPressed(ImGuiKey_Space, ImGuiInputFlags_None))
      m_renderViewport.startRender();

    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_E))
      m_renderViewport.exportImage();
  }

  /*
   * Render menu bar
   */
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 7.0f));
  bool menubar = ImGui::BeginMenuBar();
  ImGui::PopStyleVar();
  if (menubar) {
    if (!isFullscreenEnabled(m_sdlWindow)) {
      ImGui::SetCursorPosX(80.0f);
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8, 6});

    ImGui::SetNextWindowSize({160, 0});
    if (widgets::menu("File")) {
      if (widgets::menuItem("Open", "Cmd + O")) m_store.open();
      if (widgets::menuItem("Save As...", "Cmd + S")) m_store.saveAs();

      ImGui::Separator();

      if (widgets::menu("Import")) {
        if (widgets::menuItem("glTF", "Cmd + I")) m_store.importGltf();

        ImGui::Separator();

        if (widgets::menu("Texture")) {
          if (widgets::menuItem("Color")) m_store.importTexture(loaders::texture::TextureType::sRGB);
          if (widgets::menuItem("Normal map")) m_store.importTexture(loaders::texture::TextureType::LinearRGB);
          if (widgets::menuItem("HDR/Env map")) m_store.importTexture(loaders::texture::TextureType::HDR);
          if (widgets::menuItem("Grayscale")) m_store.importTexture(loaders::texture::TextureType::Mono);
          ImGui::EndMenu();
        }

        ImGui::EndMenu();
      }

      ImGui::EndMenu();
    }

    ImGui::SetNextWindowSize({160, 0});
    if (widgets::menu("View")) {
      if (widgets::menu("Theme")) {
        if (widgets::menuItem("Light", nullptr, theme::Theme::currentTheme == &theme::platinumLight)) {
          theme::apply(ImGui::GetStyle(), theme::platinumLight);
        }
        if (widgets::menuItem("Dark", nullptr, theme::Theme::currentTheme == &theme::platinumDark)) {
          theme::apply(ImGui::GetStyle(), theme::platinumDark);
        }

        ImGui::EndMenu();
      }

      ImGui::EndMenu();
    }

    ImGui::SetNextWindowSize({160, 0});
    if (widgets::menu("Render")) {
      ImGui::BeginDisabled(!m_renderViewport.canRender());
      if (widgets::menuItem("Render", "Space")) m_renderViewport.startRender();
      ImGui::EndDisabled();

      ImGui::Separator();

      ImGui::BeginDisabled(!m_renderViewport.hasImage());
      if (widgets::menuItem("Export to PNG", "Cmd + E")) m_renderViewport.exportImage();
      ImGui::EndDisabled();

      ImGui::EndMenu();
    }

    ImGui::SetNextWindowSize({160, 0});
    if (widgets::menu("Tools")) {
      if (widgets::menuItem("Multiscatter GGX LUTs")) m_toolMultiscatterLutGeneratorOpen = true;

      ImGui::EndMenu();
    }

    ImGui::PopStyleVar();
    ImGui::EndMenuBar();
  }
}

}
