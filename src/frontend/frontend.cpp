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

void buttonDanger() {
  ImGui::PushStyleColor(
    ImGuiCol_Button,
    (ImVec4) ImColor::HSV(0.0f, 0.6f, 0.9f)
  );
  ImGui::PushStyleColor(
    ImGuiCol_ButtonHovered,
    (ImVec4) ImColor::HSV(0.0f, 0.7f, 0.8f)
  );
  ImGui::PushStyleColor(
    ImGuiCol_ButtonActive,
    (ImVec4) ImColor::HSV(0.0f, 0.8f, 0.7f)
  );
  ImGui::PushStyleColor(
    ImGuiCol_Text,
    (ImVec4) ImColor::HSV(0.0f, 0.0f, 1.0f)
  );
}

Frontend::Frontend(Store& store) noexcept: m_store(store) {
}

Frontend::~Frontend() {
  m_commandQueue->release();
  m_device->release();
}

void Frontend::init() {
  /*
   * Set up ImGui
   */
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

  ImGui::StyleColorsLight();
  auto& style = ImGui::GetStyle();
  style.FrameRounding = 4.0f;
  style.PopupRounding = 4.0f;
  style.WindowRounding = 4.0f;
  style.IndentSpacing = 12.0f;
  style.TabBarOverlineSize = 0.0f;

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
  m_store.setDevice(m_device);

  ImGui_ImplMetal_Init(m_device);
  ImGui_ImplSDL2_InitForMetal(m_sdlWindow);

  m_commandQueue = m_device->newCommandQueue();

  /*
   * Initialize renderer
   */
  m_renderer = std::make_unique<renderer_studio::Renderer>(
    m_device,
    m_commandQueue,
    m_store
  );
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
      } else {
        handleInput(event);
      }
    }
    handleScrollAndZoomState();

    // Handle resize
    int width, height;
    SDL_GetRendererOutputSize(m_sdlRenderer, &width, &height);
    metal_utils::setDrawableSize(m_layer, width, height);

    // Rendering
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

    // Render scene
    m_renderer->render(m_selectedNodeId.value_or(0));

    // Render ImGui
    auto cmd = m_commandQueue->commandBuffer();
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

      auto objectId = m_renderer->readbackObjectIdAt(x, y);
      if (objectId != 0) {
        m_selectedNodeId = m_nextNodeId = objectId;
      } else {
        m_selectedNodeId = m_nextNodeId = std::nullopt;
      }
      m_selectedMeshId = m_nextMeshId = std::nullopt;
      break;
    }
    case SDL_KEYDOWN:
    case SDL_KEYUP: {
      auto sc = event.key.keysym.scancode;
      if (sc == SDL_SCANCODE_LSHIFT || sc == SDL_SCANCODE_RSHIFT) {
        m_scrolling = false;
        m_scrollSpeed = {0.0f, 0.0f};
        m_zooming = false;
        m_zoomSpeed = 0.0f;
      }
    }
  }
}

void Frontend::handleScrollAndZoomState() {
  if (length_squared(m_scrollSpeed) < m_scrollStop * m_scrollStop) {
    m_scrolling = false;
    m_scrollSpeed = {0.0f, 0.0f};
  } else {
    m_scrollSpeed -= normalize(m_scrollSpeed) * m_scrollFriction;
    if (m_keys[SDL_SCANCODE_LSHIFT] || m_keys[SDL_SCANCODE_RSHIFT]) {
      m_renderer->handlePanEvent(m_scrollSpeed);
    } else {
      m_renderer->handleScrollEvent(m_scrollSpeed);
    }
  }

  if (abs(m_zoomSpeed) < m_zoomStop) {
    m_zoomSpeed = 0.0f;
  } else {
    m_zoomSpeed -= sign(m_zoomSpeed) * m_zoomFriction;
    m_renderer->handleZoomEvent(m_zoomSpeed);
  }
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
    m_viewportSize = {size.x, size.y};
    m_renderer->handleResizeViewport(m_viewportSize * m_dpiScaling);

    ImGui::Image(
      (ImTextureID) m_renderer->presentRenderTarget(),
      {m_viewportSize.x, m_viewportSize.y}
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
  if (ImGui::Button("Add Objects...", {ImGui::GetContentRegionAvail().x, 0})) {
    ImGui::OpenPopup("AddObject_Popup");
  }
  if (ImGui::BeginPopup("AddObject_Popup")) {
    if (ImGui::Selectable("Cube")) {
      uint32_t parentIdx = m_selectedNodeId.value_or(0);

      auto cube = pt::primitives::cube(m_device, 2.0f);
      auto idx = m_store.scene().addMesh(std::move(cube));
      m_store.scene().addNode(pt::Scene::Node(idx), parentIdx);

      ImGui::CloseCurrentPopup();
    }
    if (ImGui::Selectable("Sphere")) {
      uint32_t parentIdx = m_selectedNodeId.value_or(0);

      auto sphere = pt::primitives::sphere(m_device, 1.0f, 24, 32);
      auto idx = m_store.scene().addMesh(std::move(sphere));
      m_store.scene().addNode(pt::Scene::Node(idx), parentIdx);

      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {8, 4});
  if (ImGui::BeginChild("##SETree", {0, 0}, ImGuiChildFlags_FrameStyle)) {
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
    sceneExplorerNode(0);
    ImGui::PopStyleVar();
    m_selectedNodeId = m_nextNodeId;
    m_selectedMeshId = m_nextMeshId;
  } else {
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();
  }
  ImGui::EndChild();

  ImGui::End();
}

void Frontend::sceneExplorerNode(Scene::NodeID id) {
  const Scene::Node* node = m_store.scene().node(id);
  static constexpr const ImGuiTreeNodeFlags baseFlags =
    ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
    ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Framed;

  auto nodeFlags = baseFlags;
  bool isSelected = m_selectedNodeId == id;
  if (isSelected) {
    nodeFlags |= ImGuiTreeNodeFlags_Selected;
  }

  bool isLeaf = !node->meshId && node->children.empty();
  if (isLeaf) {
    nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
  }

  auto label = id == 0 ? "Root" : std::format("Node [{}]", id);
  ImGui::PushID(label.c_str());

  if (!isSelected) {
    ImGui::PushStyleColor(
      ImGuiCol_Header,
      ImGui::GetStyleColorVec4(ImGuiCol_FrameBg)
    );
  }
  bool isOpen = ImGui::TreeNodeEx(label.c_str(), nodeFlags) && !isLeaf;
  if (!isSelected) ImGui::PopStyleColor();

  if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
    m_nextNodeId = id;
    m_nextMeshId = std::nullopt;
  }

  if (id != 0 && ImGui::BeginDragDropSource()) {
    ImGui::SetDragDropPayload("PT_NODE", &id, sizeof(Scene::NodeID));

    const bool clone = m_keys[SDL_SCANCODE_LALT] || m_keys[SDL_SCANCODE_RALT];

    ImGui::Text("%s%s", label.c_str(), clone ? " [+]" : "");
    ImGui::EndDragDropSource();
  }

  if (ImGui::BeginDragDropTarget()) {
    if (const auto pl = ImGui::AcceptDragDropPayload("PT_NODE")) {
      IM_ASSERT(pl->DataSize == sizeof(Scene::NodeID));
      const auto plId = *((Scene::NodeID*) pl->Data);
      const bool clone = m_keys[SDL_SCANCODE_LALT] || m_keys[SDL_SCANCODE_RALT];

      if (clone) {
        m_store.scene().cloneNode(plId, id);
      } else {
        m_store.scene().moveNode(plId, id);
      }
    }
    ImGui::EndDragDropTarget();
  }

  if (isOpen) {
    if (node->meshId) {
      auto meshFlags = baseFlags;
      meshFlags |=
        ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

      bool meshSelected = m_selectedMeshId == node->meshId;
      if (meshSelected) {
        meshFlags |= ImGuiTreeNodeFlags_Selected;
      } else {
        ImGui::PushStyleColor(
          ImGuiCol_Header,
          ImGui::GetStyleColorVec4(ImGuiCol_FrameBg)
        );
      }

      auto meshLabel = std::format("Mesh [{}]", node->meshId.value());
      ImGui::TreeNodeEx(meshLabel.c_str(), meshFlags);
      if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        m_nextNodeId = std::nullopt;
        m_nextMeshId = node->meshId;
      }

      if (!meshSelected) ImGui::PopStyleColor();
    }
    for (Scene::NodeID childId: node->children) {
      sceneExplorerNode(childId);
    }
    ImGui::TreePop();
  }

  ImGui::PopID();
}

void Frontend::properties() {
  static constexpr const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen;

  bool markedForDelete = false;
  int removeOptions = 0;
  ImGui::Begin("Properties");
  if (m_selectedNodeId) {
    Scene::Node* node = m_store.scene().node(m_selectedNodeId.value());

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Node [id: %u]", m_selectedNodeId.value());

    if (m_selectedNodeId != 0) {
      float buttonWidth = 80.0f;
      ImGui::SameLine(ImGui::GetContentRegionAvail().x - buttonWidth + 12.0f);

      buttonDanger();
      if (ImGui::Button("Remove", {buttonWidth, 0})) {
        if (!node->children.empty()) ImGui::OpenPopup("Delete_Popup");
        else markedForDelete = true;
      }
      ImGui::PopStyleColor(4);
      if (!node->children.empty() && ImGui::BeginPopup("Delete_Popup")) {
        ImGui::PushStyleColor(
          ImGuiCol_FrameBg,
          ImGui::GetStyleColorVec4(ImGuiCol_WindowBg)
        );
        ImGui::Checkbox("Keep orphaned meshes", &m_keepOrphanedMeshes);
        ImGui::PopStyleColor();

        ImGui::Separator();
        ImGui::Text("Action for children:");
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
        if (ImGui::Selectable("Remove")) {
          removeOptions = Scene::RemoveOptions_RemoveChildrenRecursively;
          markedForDelete = true;
        }
        if (ImGui::Selectable("Move to root")) {
          removeOptions = Scene::RemoveOptions_MoveChildrenToRoot;
          markedForDelete = true;
        }
        if (ImGui::Selectable("Move to parent")) {
          removeOptions = Scene::RemoveOptions_MoveChildrenToParent;
          markedForDelete = true;
        }
        ImGui::PopStyleVar();

        ImGui::EndPopup();
      }
    }

    ImGui::Spacing();

    if (ImGui::CollapsingHeader("Transform", flags)) {
      ImGui::DragFloat3(
        "Translation",
        (float*) &node->transform.translation,
        0.01f
      );

      ImGui::DragFloat3(
        "Rotation",
        (float*) &node->transform.rotation,
        0.005f,
        0.0f,
        2.0f * std::numbers::pi,
        "%.3f",
        ImGuiSliderFlags_WrapAround
      );

      ImGui::DragFloat3(
        "Scale",
        (float*) &node->transform.scale,
        0.01f
      );

      if (ImGui::Button("Reset", {ImGui::GetContentRegionAvail().x, 0})) {
        node->transform.translation = {0, 0, 0};
        node->transform.rotation = {0, 0, 0};
        node->transform.scale = {1, 1, 1};
      }
    }
  } else if (m_selectedMeshId) {
    Mesh* mesh = m_store.scene().mesh(m_selectedMeshId.value());

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Mesh [id: %u]", m_selectedMeshId.value());

    auto users = std::format(
      "{} users",
      m_store.scene().meshUsers(m_selectedMeshId.value())
    );
    auto availableWidth = ImGui::GetContentRegionAvail().x;
    ImGui::SameLine(availableWidth - ImGui::CalcTextSize(users.c_str()).x);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", users.c_str());

    ImGui::Spacing();

    ImGui::Text("%lu vertices", mesh->vertexCount());
    ImGui::Text("%lu triangles", mesh->indexCount() / 3);
  } else {
    ImGui::Text("[ Nothing selected ]");
  }

  ImGui::End();

  if (markedForDelete) {
    if (!m_keepOrphanedMeshes) {
      removeOptions |= Scene::RemoveOptions_RemoveOrphanedMeshes;
    }

    m_store.scene().removeNode(m_selectedNodeId.value(), removeOptions);
    m_selectedNodeId = m_nextNodeId = std::nullopt;
  }
}

}
