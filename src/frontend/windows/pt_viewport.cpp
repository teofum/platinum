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
  ImGui::Begin("Render");

  if (m_cameraNodeId && !m_store.scene().hasNode(m_cameraNodeId.value())) {
    m_cameraNodeId = std::nullopt;
  }

  auto cameras = m_store.scene().getAllCameras();
  if (!m_cameraNodeId && !cameras.empty()) {
    m_cameraNodeId = cameras[0].nodeId;
  }

  const auto& label = m_cameraNodeId
                      ? m_store.scene().node(m_cameraNodeId.value())->name
                      : "[No camera selected]";

  ImGui::SetNextItemWidth(160.0);
  ImGui::BeginDisabled(cameras.empty());
  if (ImGui::BeginCombo("##CameraSelect", label.c_str())) {
    for (const auto& cd: cameras) {
      const auto& name = m_store.scene().node(cd.nodeId)->name;
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
  ImGui::BeginDisabled(!m_cameraNodeId);
  if (ImGui::Button("Render") || ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
    if (m_cameraNodeId) {
      auto size = ImGui::GetContentRegionAvail();
      m_renderer->render(
        m_cameraNodeId.value(),
        {size.x * m_dpiScaling, size.y * m_dpiScaling}
      );
    }
  }
  ImGui::EndDisabled();

  auto vsize = ImGui::GetContentRegionAvail();
  auto renderTarget = m_renderer->presentRenderTarget();
  if (renderTarget != nullptr) {
    ImGui::Image(
      (ImTextureID) renderTarget,
      {vsize.x, vsize.y}
    );
  }

  ImGui::End();
}

bool RenderViewport::handleInputs(const SDL_Event& event) {
  return false;
}

void RenderViewport::updateScrollAndZoomState() {

}

}