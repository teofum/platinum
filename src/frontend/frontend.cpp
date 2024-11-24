#include "frontend.hpp"

#include <numbers>
#include <print>
#include <Foundation/Foundation.hpp>
#include <imgui_internal.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_metal.h>

#include <core/primitives.hpp>
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

void Frontend::start() {
  /*
   * Set up ImGui
   */
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

  ImGui::StyleColorsLight();

  SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
  SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
  SDL_SetHint(SDL_HINT_TRACKPAD_IS_TOUCH_ONLY, "1");

  /*
   * Initialize SDL and set hints to render using Metal
   */
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
    std::print(stderr, "SDL init failed: {}\n", SDL_GetError());
    return; // TODO return error code
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

    // Event polling and input
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL2_ProcessEvent(&event);
      if (isExitEvent(event, SDL_GetWindowID(m_sdlWindow))) {
        exit = true;
      } else {
        handleInput(event);
      }
    }
    handleScrollAndZoomState();

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

    // Render scene
    if (!m_renderTarget || m_viewportWasResized) rebuildRenderTargets();
    m_renderer->render(m_renderTarget, m_geometryRenderTarget);

    // Render ImGui
    auto enc = cmd->renderCommandEncoder(m_rpd);
    enc->pushDebugGroup("ImGui"_ns);

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

void Frontend::handleInput(const SDL_Event& event) {
  ImGuiIO& io = ImGui::GetIO();
  bool allowMouseEvents = !io.WantCaptureMouse || m_mouseInViewport;

  switch (event.type) {
    case SDL_MAC_MAGNIFY: {
      if (!allowMouseEvents) return;

      // Handle pinch to zoom
      m_scrolling = false;
      m_scrollSpeed = {0.0f, 0.0f};
      m_zooming = true;
      m_zoomSpeed = event.magnify.magnification * m_zoomSensitivity;

      break;
    }
    case SDL_MULTIGESTURE: {
      if (!allowMouseEvents) return;

      // Handle scroll to move/orbit
      // We don't use the SDL_MOUSEWHEEL event because its precision is ass
      const auto& mgesture = event.mgesture;
      if (mgesture.numFingers == 2 && !m_zooming) {
        if (!m_scrolling) {
          m_scrolling = true;
        } else {
          float2 delta = float2{mgesture.x, mgesture.y} - m_scrollLastPos;
          m_scrollSpeed = delta * m_scrollSensitivity;
        }
        m_scrollLastPos = {mgesture.x, mgesture.y};
      }

      break;
    }
    case SDL_FINGERDOWN: {
      if (!allowMouseEvents) return;
      m_scrolling = false;
      m_scrollSpeed = {0.0f, 0.0f};
      m_zooming = false;
      m_zoomSpeed = 0.0f;
      break;
    }
    case SDL_MOUSEBUTTONUP: {
      if (!allowMouseEvents) return;
      const auto& button = event.button;

      uint32_t x = button.x - static_cast<uint32_t>(m_viewportTopLeft.x);
      uint32_t y = button.y - static_cast<uint32_t>(m_viewportTopLeft.y);

      // TODO this seems afwully inefficient
      auto pixelSize = 2;
      auto readBuf = m_device
        ->newBuffer(pixelSize, MTL::ResourceStorageModeShared);
      auto cmd = m_commandQueue->commandBuffer();
      auto benc = cmd->blitCommandEncoder();

      benc->copyFromTexture(
        m_geometryRenderTarget,
        0,
        0,
        MTL::Origin(x * 2, y * 2, 0),
        MTL::Size(1, 1, 1),
        readBuf,
        0,
        pixelSize,
        pixelSize
      );
      benc->endEncoding();
      cmd->commit();
      cmd->waitUntilCompleted();

      uint16_t objectId;
      auto contents = readBuf->contents();
      memcpy(&objectId, contents, pixelSize);

      if (objectId != 0) {
        m_selectedNodeIdx = m_nextNodeIdx = objectId;
      } else {
        m_selectedNodeIdx = m_nextNodeIdx = std::nullopt;
      }
      m_selectedMeshIdx = m_nextMeshIdx = std::nullopt;
      break;
    }
  }
}

void Frontend::handleScrollAndZoomState() {
  if (length_squared(m_scrollSpeed) < m_scrollStop * m_scrollStop) {
    m_scrolling = false;
    m_scrollSpeed = {0.0f, 0.0f};
  } else {
    m_scrollSpeed -= normalize(m_scrollSpeed) * m_scrollFriction;
    m_renderer->handleScrollEvent(m_scrollSpeed);
  }

  if (abs(m_zoomSpeed) < m_zoomStop) {
    m_zoomSpeed = 0.0f;
  } else {
    m_zoomSpeed -= sign(m_zoomSpeed) * m_zoomFriction;
    m_renderer->handleZoomEvent(m_zoomSpeed);
  }
}

void Frontend::rebuildRenderTargets() {
  if (m_renderTarget != nullptr) m_renderTarget->release();
  if (m_geometryRenderTarget != nullptr) m_geometryRenderTarget->release();

  auto texd = MTL::TextureDescriptor::alloc()->init();
  texd->setTextureType(MTL::TextureType2D);
  texd->setWidth(static_cast<uint32_t>(m_viewportSize.x * m_dpiScaling));
  texd->setHeight(static_cast<uint32_t>(m_viewportSize.y * m_dpiScaling));
  texd->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  texd->setStorageMode(MTL::StorageModeShared);

  texd->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
  m_renderTarget = m_device->newTexture(texd);

  texd->setPixelFormat(MTL::PixelFormatR16Uint);
  m_geometryRenderTarget = m_device->newTexture(texd);

  texd->release();
}

void Frontend::drawImGui() {
  mainDockSpace();
  sceneExplorer();
  properties();

  {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 1.0f));
    ImGui::Begin("Viewport");
    ImGui::PopStyleVar();

    auto pos = ImGui::GetCursorScreenPos();
    m_viewportTopLeft = {pos.x, pos.y};

    auto size = ImGui::GetContentRegionAvail();
    float2 viewportSize = {size.x, size.y};
    if (!equal(viewportSize, m_viewportSize)) {
      m_viewportSize = viewportSize;
      m_viewportWasResized = true;
    }

    ImGui::Image(
      (ImTextureID) m_renderTarget,
      {
        m_viewportSize.x,
        m_viewportSize.y
      }
    );
    m_mouseInViewport = ImGui::IsItemHovered();
    ImGui::End();
  }

//  ImGui::Text(
//    "Application average %.3f ms/frame (%.1f FPS)",
//    1000.0f / io.Framerate,
//    io.Framerate
//  );
}

void Frontend::mainDockSpace() {
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
        dockSpaceId, ImGuiDir_Left, 0.25f,
        nullptr, &dockSpaceId
      );
      auto dockIdLeftLower = ImGui::DockBuilderSplitNode(
        dockIdLeft, ImGuiDir_Down, 0.4f,
        nullptr, &dockIdLeft
      );

      ImGui::DockBuilderDockWindow("Scene Explorer", dockIdLeft);
      ImGui::DockBuilderDockWindow("Properties", dockIdLeftLower);
      ImGui::DockBuilderDockWindow("Viewport", dockSpaceId);
      ImGui::DockBuilderFinish(dockSpaceId);
    }
  }

  ImGui::End();
}

void Frontend::sceneExplorer() {
  ImGui::Begin("Scene Explorer");

  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
  if (ImGui::BeginCombo("##", "Add Objects...")) {
    if (ImGui::Button("Cube", {ImGui::GetContentRegionAvail().x, 0})) {
      uint32_t parentIdx = m_selectedNodeIdx.value_or(0);

      auto cube = pt::primitives::cube(2.0f);
      auto idx = m_store.scene().addMesh(std::move(cube));
      m_store.scene().addNode(pt::Scene::Node(idx), parentIdx);

      ImGui::CloseCurrentPopup();
    }
    ImGui::EndCombo();
  }

  sceneExplorerNode(0);
  m_selectedNodeIdx = m_nextNodeIdx;
  m_selectedMeshIdx = m_nextMeshIdx;

  ImGui::End();
}

void Frontend::sceneExplorerNode(uint32_t idx) {
  const Scene::Node& node = m_store.scene().node(idx);
  static ImGuiTreeNodeFlags baseFlags =
    ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
    ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;

  auto nodeFlags = baseFlags;
  bool isSelected = m_selectedNodeIdx == idx;
  if (isSelected) {
    nodeFlags |= ImGuiTreeNodeFlags_Selected;
  }

  bool isLeaf = !node.meshIdx && node.children.empty();
  if (isLeaf) {
    nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
  }

  auto label = idx == 0 ? "Root" : std::format("Node [{}]", idx);
  bool isOpen = ImGui::TreeNodeEx(label.c_str(), nodeFlags);
  if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
    m_nextNodeIdx = idx;
    m_nextMeshIdx = std::nullopt;
  }

  if (isOpen) {
    if (node.meshIdx) {
      auto meshFlags = baseFlags;
      if (m_selectedMeshIdx == idx) {
        meshFlags |= ImGuiTreeNodeFlags_Selected;
      }
      meshFlags |=
        ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

      auto meshLabel = std::format("Mesh [{}]", node.meshIdx.value());
      ImGui::TreeNodeEx(meshLabel.c_str(), meshFlags);
      if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        m_nextNodeIdx = std::nullopt;
        m_nextMeshIdx = idx;
      }
    }
    for (uint32_t childIdx: node.children) {
      sceneExplorerNode(childIdx);
    }
    ImGui::TreePop();
  }
}

void Frontend::properties() {
  static ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen;

  ImGui::Begin("Properties");
  if (m_selectedNodeIdx) {
    Scene::Node& node = m_store.scene().node(m_selectedNodeIdx.value());

    ImGui::Text("Node [index: %u]", m_selectedNodeIdx.value());

    if (ImGui::CollapsingHeader("Transform", flags)) {
      ImGui::DragFloat3(
        "Translation",
        (float*) &node.transform.translation,
        0.01f
      );

      ImGui::DragFloat3(
        "Rotation",
        (float*) &node.transform.rotation,
        0.005f,
        0.0f,
        2.0f * std::numbers::pi,
        "%.3f",
        ImGuiSliderFlags_WrapAround
      );

      ImGui::DragFloat3(
        "Scale",
        (float*) &node.transform.scale,
        0.01f
      );

      if (ImGui::Button("Reset", {ImGui::GetContentRegionAvail().x, 0})) {
        node.transform.translation = {0, 0, 0};
        node.transform.rotation = {0, 0, 0};
        node.transform.scale = {1, 1, 1};
      }
    }
  } else if (m_selectedMeshIdx) {
    ImGui::Text("Mesh [index: %u]", m_selectedMeshIdx.value());
  } else {
    ImGui::Text("[ Nothing selected ]");
  }

  ImGui::End();
}

}
