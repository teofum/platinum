#include "pt_viewport.hpp"

#include <OpenImageIO/imageio.h>

#include <utils/utils.hpp>

namespace pt::frontend::windows {

RenderViewport::RenderViewport(Store& store, State& state, float& dpiScaling, bool* open) noexcept
  : Window(store, state, open), m_dpiScaling(dpiScaling) {}

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

  /*
   * Auto select camera if necessary
   */
  if (m_cameraNodeId && !m_store.scene().hasNode(m_cameraNodeId.value())) {
    m_cameraNodeId = std::nullopt;
  }

  auto cameras = m_store.scene().getCameras();
  if (!m_cameraNodeId && !cameras.empty()) {
    m_cameraNodeId = cameras[0].node.id();
  }

  const auto& label = m_cameraNodeId
                      ? m_store.scene().node(m_cameraNodeId.value()).name()
                      : "[No camera selected]";

  auto open = ImGui::Begin("Render");
  if (open && ImGui::IsItemVisible()) {
    /*
     * Basic render controls
     */
    ImGui::SetNextItemWidth(160);
    ImGui::BeginDisabled(cameras.empty());
    if (ImGui::BeginCombo("##CameraSelect", label.c_str())) {
      for (const auto& camera: cameras) {
        auto name = std::format("{}##Camera_{}", camera.node.name(), uint32_t(camera.node.id()));
        const bool isSelected = camera.node.id() == m_cameraNodeId;
        if (widgets::comboItem(name.c_str(), isSelected)) m_cameraNodeId = camera.node.id();

        if (isSelected) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
    ImGui::EndDisabled();

    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 160);
    ImGui::BeginDisabled(!canRender());
    bool render = ImGui::Button("Render", {80, 0});
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!hasImage());
    if (ImGui::Button("Export", {80, 0})) exportImage();
    ImGui::EndDisabled();

    ImGui::Spacing();

    /*
     * Start or continue render
     */
    auto pos = ImGui::GetCursorScreenPos();
    m_viewportTopLeft = {pos.x, pos.y};

    auto size = ImGui::GetContentRegionAvail();
    size.y -= ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;
    m_viewportSize = {size.x, size.y};

    if (render) startRender();
    m_renderer->render();

    /*
     * Render viewport
     */
    ImGui::PushStyleColor(ImGuiCol_ChildBg, (ImVec4) ImColor::HSV(0.0f, 0.0f, 0.5f));
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

    /*
     * Progress info
     */
    auto [accumulated, total] = m_renderer->renderProgress();
    auto progress = (float) accumulated / (float) total;
    auto progressStr = accumulated == total
                       ? "Done!"
                       : accumulated == 0
                         ? "Ready"
                         : std::format("{} / {}", accumulated, total);
    auto width = min(ImGui::GetContentRegionAvail().x - 80.0f, 300.0f);
    ImGui::ProgressBar(progress, {width, 0}, progressStr.c_str());

    if (accumulated == total) m_state.setRendering(false);

    auto time = std::format("{:.3f}s", (float) m_renderer->renderTime() / 1000.0f);
    auto textWidth = ImGui::CalcTextSize(time.c_str()).x;
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetStyle().ItemSpacing.x - textWidth);
    ImGui::Text("%s", time.c_str());
  }

  ImGui::End();

  /*
   * Render settings window
   */
  renderSettingsWindow(cameras, label);
}

void RenderViewport::renderSettingsWindow(const std::vector<Scene::CameraInstance>& cameras, const std::string& label) {
  ImGui::Begin("Render Settings");

  ImGui::BeginDisabled(!(m_renderer->status() & renderer_pt::Renderer::Status_Ready));
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
  ImGui::BeginDisabled(cameras.empty());
  if (ImGui::BeginCombo("##CameraSelect", label.c_str())) {
    for (const auto& camera: cameras) {
      auto name = std::format("{}##Camera_{}", camera.node.name(), uint32_t(camera.node.id()));
      const bool isSelected = camera.node.id() == m_cameraNodeId;
      if (widgets::comboItem(name.c_str(), isSelected)) m_cameraNodeId = camera.node.id();

      if (isSelected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::EndDisabled();

  ImGui::SeparatorText("Output size");

  auto scaledSize = m_viewportSize * m_dpiScaling;
  ImGui::BeginDisabled(m_useViewportSizeForRender);
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
  ImGui::InputFloat2("##OutputSize", (float*) (m_useViewportSizeForRender ? &scaledSize : &m_nextRenderSize), "%.0fpx");
  ImGui::EndDisabled();

  ImGui::Checkbox("Use viewport size", &m_useViewportSizeForRender);

  ImGui::SeparatorText("Renderer");

  auto selectedKernel = m_renderer->selectedKernel();
  std::array<std::string, 2> kernelNames = {"Simple BSDF sampler", "MIS + NEE"};
  if (ImGui::BeginCombo("Render kernel", kernelNames[selectedKernel].c_str())) {
    if (widgets::comboItem(kernelNames[0].c_str(), selectedKernel == 0))
      m_renderer->selectKernel(renderer_pt::Renderer::Integrator_Simple);
    if (selectedKernel == 0) ImGui::SetItemDefaultFocus();

    if (widgets::comboItem(kernelNames[1].c_str(), selectedKernel == 1))
      m_renderer->selectKernel(renderer_pt::Renderer::Integrator_MIS);
    if (selectedKernel == 1) ImGui::SetItemDefaultFocus();

    ImGui::EndCombo();
  }

  ImGui::DragInt("Samples", &m_nextRenderSampleCount, 1, 0, 1 << 16);

  ImGui::SeparatorText("Options");

  ImGui::CheckboxFlags("Multiscatter GGX", &m_renderFlags, shaders_pt::RendererFlags_MultiscatterGGX);

  ImGui::EndDisabled();

  if (ImGui::CollapsingHeader("Post processing", ImGuiTreeNodeFlags_DefaultOpen)) {
    renderPostprocessSettings();
    ImGui::Spacing();
  }

  ImGui::End();
}

void RenderViewport::renderPostprocessSettings() {
  auto& ppOptions = m_renderer->postProcessOptions();

  ImGui::DragFloat("Exposure", &ppOptions.exposure, 0.1f, -5.0f, 5.0f, "%.1f EV");

  ImGui::Spacing();
  ImGui::SeparatorText("Tone mapping");
  ImGui::Spacing();

  /*
   * Tonemapper select
   */
  if (ImGui::BeginCombo("Tonemap", m_tonemappers.at(ppOptions.tonemap.tonemapper).c_str())) {
    for (const auto& [tonemapper, name]: m_tonemappers) {
      bool isSelected = ppOptions.tonemap.tonemapper == tonemapper;
      if (widgets::comboItem(name.c_str(), isSelected)) ppOptions.tonemap.tonemapper = tonemapper;

      if (isSelected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  /*
   * Tonemap options
   */
  if (ppOptions.tonemap.tonemapper == postprocess::Tonemap::AgX) {
    ImGui::Spacing();
    auto& look = ppOptions.tonemap.agxOptions.look;
    ImGui::DragFloat3("Offset", (float*) &look.offset, 0.01f, 0.0f, 0.0f, "%.2f");
    ImGui::DragFloat3("Slope", (float*) &look.slope, 0.01f, 0.0f, 0.0f, "%.2f");
    ImGui::DragFloat3("Power", (float*) &look.power, 0.01f, 0.0f, 0.0f, "%.2f");
    ImGui::DragFloat("Saturation", &look.saturation, 0.01f, 0.0f, 0.0f, "%.2f");

    float buttonWidth = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3;
    ImGui::Text("Presets");

    if (widgets::button("None", {buttonWidth, 0})) look = postprocess::agx::looks::none;
    ImGui::SameLine();
    if (widgets::button("Golden", {buttonWidth, 0})) look = postprocess::agx::looks::golden;
    ImGui::SameLine();
    if (widgets::button("Punchy", {buttonWidth, 0})) look = postprocess::agx::looks::punchy;
  }
}

void RenderViewport::startRender() {
  if (canRender()) {
    m_renderSize = m_useViewportSizeForRender ? m_viewportSize * m_dpiScaling : m_nextRenderSize;
    m_renderer->startRender(
      m_cameraNodeId.value(),
      m_renderSize,
      (uint32_t) m_nextRenderSampleCount,
      m_renderFlags
    );
    m_state.setRendering(true);
  }
}

bool RenderViewport::canRender() const {
  return m_cameraNodeId && (m_renderer->status() & renderer_pt::Renderer::Status_Ready);
}

bool RenderViewport::hasImage() const {
  return m_renderer->status() & renderer_pt::Renderer::Status_Done;
}

void RenderViewport::exportImage() const {
  const auto savePath = utils::fileSave("../out", "png");
  if (savePath) {
    auto out = OIIO::ImageOutput::create(savePath->string());

    if (out) {
      uint2 size;
      auto readbackBuffer = m_renderer->readbackRenderTarget(&size);

      OIIO::ImageSpec spec(int(size.x), int(size.y), 4, OIIO::TypeDesc::UINT8);
      out->open(savePath->string(), spec);
      out->write_image(OIIO::TypeDesc::UINT8, readbackBuffer->contents());
      out->close();
    }
  }
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
