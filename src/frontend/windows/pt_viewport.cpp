#include "pt_viewport.hpp"

namespace pt::frontend::windows {


void RenderViewport::init(MTL::Device* device, MTL::CommandQueue* commandQueue) {
  /*
   * Initialize PT renderer
   */
  m_renderer = std::make_unique<renderer_pt::Renderer>(
    device,
    commandQueue,
    m_store
  );
}

void RenderViewport::render() {
  updateScrollAndZoomState();

  ImGui::Begin("Render");

  /*
   * Auto select camera if necessary
   */
  if (m_cameraNodeId && !m_store.scene().hasNode(m_cameraNodeId.value())) {
    m_cameraNodeId = std::nullopt;
  }

  auto cameras = m_store.scene().getAllCameras();
  if (!m_cameraNodeId && !cameras.empty()) {
    m_cameraNodeId = cameras[0].nodeId;
  }

  /*
   * Basic render controls
   */
  const auto& label = m_cameraNodeId
                      ? m_store.scene().node(m_cameraNodeId.value())->name
                      : "[No camera selected]";

  ImGui::SetNextItemWidth(160);
  ImGui::BeginDisabled(cameras.empty());
  if (ImGui::BeginCombo("##CameraSelect", label.c_str())) {
    for (const auto& cd: cameras) {
      auto name = std::format("{}##Camera_{}", m_store.scene().node(cd.nodeId)->name, cd.nodeId);
      const bool isSelected = cd.nodeId == m_cameraNodeId;

      if (widgets::selectable(name.c_str(), isSelected)) {
        m_cameraNodeId = cd.nodeId;
      }

      if (isSelected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::EndDisabled();

  ImGui::SameLine();
  if (ImGui::GetContentRegionAvail().x < 380) ImGui::NewLine();
  ImGui::BeginDisabled(m_useViewportSizeForRender);
  ImGui::SetNextItemWidth(80.0f);
  auto scaledSize = m_viewportSize * m_dpiScaling;
  ImGui::InputFloat2("Render size", (float*) (m_useViewportSizeForRender ? &scaledSize : &m_nextRenderSize), "%.0f");
  ImGui::EndDisabled();

  ImGui::SameLine();
  if (ImGui::GetContentRegionAvail().x < 220) ImGui::NewLine();
  ImGui::Checkbox("Use viewport size", &m_useViewportSizeForRender);

  ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetStyle().ItemSpacing.x - 80);
  ImGui::BeginDisabled(!m_cameraNodeId || m_renderer->isRendering());
  bool render = ImGui::Button("Render", {80, 0})
                || (ImGui::IsKeyPressed(ImGuiKey_Space, false) && !ImGui::IsAnyItemActive());
  ImGui::EndDisabled();

  ImGui::Spacing();

  /*
   * Start or continue render
   */
  auto pos = ImGui::GetCursorScreenPos();
  m_viewportTopLeft = {pos.x, pos.y};

  auto size = ImGui::GetContentRegionAvail();
  size.y -= ImGui::GetFrameHeight();
  m_viewportSize = {size.x, size.y};

  if (render && m_cameraNodeId && !m_renderer->isRendering()) {
    m_renderSize = m_useViewportSizeForRender
                   ? float2{size.x * m_dpiScaling, size.y * m_dpiScaling}
                   : m_nextRenderSize;
    m_renderer->startRender(m_cameraNodeId.value(), m_renderSize);
  }
  m_renderer->render();

  /*
   * Render viewport
   */
  ImGui::PushStyleColor(ImGuiCol_ChildBg, (ImVec4) ImColor::HSV(0.0f, 0.0f, 0.8f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
  ImGui::BeginChild(
    "RenderView",
    size,
    ImGuiChildFlags_Borders,
    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
  );
  m_mouseInViewport = ImGui::IsWindowHovered();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();

  auto renderTarget = m_renderer->presentRenderTarget();
  if (renderTarget != nullptr) {
    ImGui::SetCursorPos({m_offset.x, m_offset.y});
    ImGui::Image(
      (ImTextureID) renderTarget,
      {
        m_renderSize.x * m_zoomFactor / m_dpiScaling,
        m_renderSize.y * m_zoomFactor / m_dpiScaling
      }
    );
  }

  ImGui::EndChild();

  auto [accumulated, total] = m_renderer->renderProgress();
  auto progress = (float) accumulated / (float) total;
  auto progressStr = accumulated == total
                     ? "Done!"
                     : accumulated == 0
                       ? "Ready"
                       : std::format("{} / {}", accumulated, total);
  auto width = min(ImGui::GetContentRegionAvail().x, 300.0f);
  ImGui::ProgressBar(progress, {width, 0}, progressStr.c_str());

  ImGui::End();
}

bool RenderViewport::handleInputs(const SDL_Event& event) {
  ImGuiIO& io = ImGui::GetIO();
  bool allowMouseEvents = !io.WantCaptureMouse || m_mouseInViewport;

  switch (event.type) {
    case SDL_MAC_MAGNIFY: {
      if (!allowMouseEvents) return false;

      // Handle pinch to zoom
      m_scrolling = false;
      m_scrollSpeed = {0.0f, 0.0f};
      m_zooming = true;
      m_zoomSpeed = event.magnify.magnification * m_zoomSensitivity;
      m_zoomCenter = {
        (float) event.magnify.mouseX - m_viewportTopLeft.x,
        (float) event.magnify.mouseY - m_viewportTopLeft.y,
      };

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
  }

  return false;
}

void RenderViewport::updateScrollAndZoomState() {
  if (length_squared(m_scrollSpeed) < m_scrollStop * m_scrollStop) {
    m_scrolling = false;
    m_scrollSpeed = {0.0f, 0.0f};
  } else {
    m_scrollSpeed -= normalize(m_scrollSpeed) * min(m_scrollFriction, length(m_scrollSpeed));
    m_offset += m_scrollSpeed * 50.0f;
  }

  auto lastZoomFactor = m_zoomFactor;
  if (abs(m_zoomSpeed) < m_zoomStop) {
    m_zoomSpeed = 0.0f;
  } else {
    m_zoomSpeed -= sign(m_zoomSpeed) * m_zoomFriction;
    m_zoomFactor *= 1.0f + m_zoomSpeed;
  }

  auto sizeRatio = m_viewportSize * m_dpiScaling / m_renderSize;
  m_minZoomFactor = min(1.0f, min(sizeRatio.x, sizeRatio.y));
  m_zoomFactor = clamp(m_zoomFactor, m_minZoomFactor, m_maxZoomFactor);

  m_offset = (m_offset - m_zoomCenter) * (m_zoomFactor / lastZoomFactor) + m_zoomCenter;

  auto displaySize = m_renderSize * m_zoomFactor / m_dpiScaling;
  m_maxOffset = max(float2{0, 0}, (m_viewportSize - displaySize) * 0.5f);
  m_minOffset = min(m_maxOffset, m_viewportSize - displaySize);
  m_offset = clamp(m_offset, m_minOffset, m_maxOffset);
}

}