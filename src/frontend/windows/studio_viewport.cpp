#include "studio_viewport.hpp"

namespace pt::frontend::windows {

void StudioViewport::init(MTL::Device* device, MTL::CommandQueue* commandQueue) {
  /*
   * Initialize studio renderer
   */
  m_renderer = std::make_unique<renderer_studio::Renderer>(
    device,
    commandQueue,
    m_store
  );
}

void StudioViewport::render() {
  updateScrollAndZoomState();

  auto [action, nodeId] = m_state.getNodeAction();
  if (action == State::NodeAction_CenterCamera) {
    m_renderer->cameraTo(m_store.scene().worldTransform(nodeId).columns[3].xyz);
  }

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 1.0f));
  auto open = ImGui::Begin("Viewport");
  ImGui::PopStyleVar();

  if (open && ImGui::IsItemVisible()) {
    auto pos = ImGui::GetCursorScreenPos();
    m_viewportTopLeft = {pos.x, pos.y};

    auto size = ImGui::GetContentRegionAvail();
    m_viewportSize = {size.x, size.y};
    m_renderer->handleResizeViewport(m_viewportSize * m_dpiScaling);

    if (!m_state.rendering()) {
      m_renderer->render(m_state.selectedNode().value_or(Scene::null));
    }

    ImGui::Image(
      (ImTextureID) m_renderer->presentRenderTarget(),
      {m_viewportSize.x, m_viewportSize.y}
    );
    m_mouseInViewport = ImGui::IsItemHovered();
  }

  ImGui::End();
}

bool StudioViewport::handleInputs(const SDL_Event& event) {
  ImGuiIO& io = ImGui::GetIO();
  bool allowMouseEvents = !io.WantCaptureMouse || m_mouseInViewport;
  bool allowKeyboardEvents = !io.WantCaptureKeyboard || m_mouseInViewport;

  switch (event.type) {
    case SDL_MAC_MAGNIFY: {
      if (!allowMouseEvents) return false;

      // Handle pinch to zoom
      m_scrolling = false;
      m_scrollSpeed = {0.0f, 0.0f};
      m_zooming = true;
      m_zoomSpeed = event.magnify.magnification * m_zoomSensitivity;

      return true;
    }
    case SDL_MULTIGESTURE: {
      if (!allowMouseEvents) return false;

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

      return true;
    }
    case SDL_FINGERDOWN: {
      if (!allowMouseEvents) return false;
      m_scrolling = false;
      m_scrollSpeed = {0.0f, 0.0f};
      m_zooming = false;
      m_zoomSpeed = 0.0f;
      return true;
    }
    case SDL_MOUSEBUTTONUP: {
      if (!allowMouseEvents) return false;
      const auto& button = event.button;
      uint32_t x = button.x - static_cast<uint32_t>(m_viewportTopLeft.x);
      uint32_t y = button.y - static_cast<uint32_t>(m_viewportTopLeft.y);

      auto objectId = m_renderer->readbackObjectIdAt(x, y);
      if (objectId != Scene::NodeID(0)) {
        m_state.selectNode(objectId);
      } else {
        m_state.selectNode(std::nullopt);
      }
      return true;
    }
    case SDL_KEYDOWN:
    case SDL_KEYUP: {
      if (!allowKeyboardEvents) return false;

      auto sc = event.key.keysym.scancode;
      if (sc == SDL_SCANCODE_LSHIFT || sc == SDL_SCANCODE_RSHIFT) {
        m_scrolling = false;
        m_scrollSpeed = {0.0f, 0.0f};
        m_zooming = false;
        m_zoomSpeed = 0.0f;
      }
      return true;
    }
  }

  return false;
}

void StudioViewport::updateScrollAndZoomState() {
  if (length_squared(m_scrollSpeed) < m_scrollStop * m_scrollStop) {
    m_scrolling = false;
    m_scrollSpeed = {0.0f, 0.0f};
  } else {
    m_scrollSpeed -= normalize(m_scrollSpeed) * m_scrollFriction;
    if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) {
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

}
