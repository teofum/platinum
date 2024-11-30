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

  if (ImGui::Button("Render") || ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
    auto size = ImGui::GetContentRegionAvail();
    auto cameras = m_store.scene().getAllCameras();

    if (!cameras.empty()) {
      m_renderer->render(
        cameras[0].nodeId,
        {size.x * m_dpiScaling, size.y * m_dpiScaling}
      );
    } else {
      std::println("Cannot render: no camera in scene!");
    }
  }

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