#include "pt_viewport.hpp"

#include <OpenImageIO/imageio.h>
#include <implot.h>

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
  if (widgets::combo("Render kernel", kernelNames[selectedKernel].c_str())) {
    if (widgets::comboItem(kernelNames[0].c_str(), selectedKernel == 0))
      m_renderer->selectKernel(renderer_pt::Renderer::Integrator_Simple);
    if (selectedKernel == 0) ImGui::SetItemDefaultFocus();

    if (widgets::comboItem(kernelNames[1].c_str(), selectedKernel == 1))
      m_renderer->selectKernel(renderer_pt::Renderer::Integrator_MIS);
    if (selectedKernel == 1) ImGui::SetItemDefaultFocus();

    ImGui::EndCombo();
  }

  widgets::dragInt("Samples", &m_nextRenderSampleCount, 1, 0, 1 << 16);

  ImGui::SeparatorText("Options");

  ImGui::CheckboxFlags("Multiscatter GGX", &m_renderFlags, shaders_pt::RendererFlags_MultiscatterGGX);

  ImGui::EndDisabled();
  ImGui::Spacing();

  if (ImGui::CollapsingHeader("Post processing", ImGuiTreeNodeFlags_DefaultOpen)) {
    renderPostprocessSettings();
    ImGui::Spacing();
  }

  ImGui::End();
}

void RenderViewport::renderPostprocessSettings() {
  /*
   * Post processing options
   */
  int32_t idx = 0;
  for (const auto& options: m_renderer->postProcessOptions()) {
    ImGui::PushID(idx++);
    switch (options.type) {
      case postprocess::PostProcessPass::Type::Exposure: {
        auto& exposureOptions = *options.exposure;

        ImGui::SeparatorText("Exposure");

        widgets::dragFloat("Exposure", &exposureOptions.exposure, 0.1f, -5.0f, 5.0f, "%.1f EV");
        break;
      }
      case postprocess::PostProcessPass::Type::ToneCurve: {
        auto& toneCurveOptions = *options.toneCurve;

        ImGui::SeparatorText("Tone Curve");

        widgets::dragFloat("Blacks", &toneCurveOptions.blacks, 1.0f, -100.0f, 100.0f, "%.0f");
        widgets::dragFloat("Shadows", &toneCurveOptions.shadows, 1.0f, -100.0f, 100.0f, "%.0f");
        widgets::dragFloat("Highlights", &toneCurveOptions.highlights, 1.0f, -100.0f, 100.0f, "%.0f");
        widgets::dragFloat("Whites", &toneCurveOptions.whites, 1.0f, -100.0f, 100.0f, "%.0f");
        break;
      }
      case postprocess::PostProcessPass::Type::Vignette: {
        auto& vignetteOptions = *options.vignette;

        ImGui::SeparatorText("Vignetting");

        widgets::dragFloat("Amount", &vignetteOptions.amount, 0.1f, -5.0f, 5.0f, "%.1f EV");
        widgets::dragFloat("Midpoint", &vignetteOptions.midpoint, 1.0f, -100.0f, 100.0f, "%.0f");
        widgets::dragFloat("Feather", &vignetteOptions.feather, 1.0f, 0.0f, 100.0f, "%.0f");
        widgets::dragFloat("Power", &vignetteOptions.power, 1.0f, 0.0f, 100.0f, "%.0f");
        widgets::dragFloat("Roundness", &vignetteOptions.roundness, 1.0f, 0.0f, 100.0f, "%.0f");
        break;
      }
      default: break;
    }
    ImGui::PopID();
  }

  ImGui::Spacing();
  ImGui::SeparatorText("Tone mapping");

  /*
   * Tonemapper select
   */
  auto& tonemapOptions = *m_renderer->tonemapOptions();
  if (widgets::combo("Tonemap", m_tonemappers.at(tonemapOptions.tonemapper).c_str())) {
    for (const auto& [tonemapper, name]: m_tonemappers) {
      bool isSelected = tonemapOptions.tonemapper == tonemapper;
      if (widgets::comboItem(name.c_str(), isSelected)) tonemapOptions.tonemapper = tonemapper;

      if (isSelected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  /*
   * Tonemap options
   */
  switch (tonemapOptions.tonemapper) {
    case postprocess::Tonemapper::AgX: {
      auto& look = tonemapOptions.agxOptions.look;

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      ImGui::Text("Presets");

      float w = ImGui::CalcItemWidth();
      float available = ImGui::GetContentRegionAvail().x;
      ImGui::SameLine(available - w);
      float buttonWidth = (w - ImGui::GetStyle().ItemSpacing.x) / 3;
      if (widgets::button("None", {buttonWidth, 0})) look = postprocess::agx::looks::none;
      ImGui::SameLine();
      if (widgets::button("Golden", {buttonWidth, 0})) look = postprocess::agx::looks::golden;
      ImGui::SameLine();
      if (widgets::button("Punchy", {buttonWidth, 0})) look = postprocess::agx::looks::punchy;

      ImGui::Spacing();

      widgets::dragVec3("Offset", (float*) &look.offset, 0.01f, -10.0f, 10.0f, "%.2f");
      widgets::dragVec3("Slope", (float*) &look.slope, 0.01f, -5.0f, 5.0f, "%.2f");
      widgets::dragVec3("Power", (float*) &look.power, 0.01f, 0.0f, 5.0f, "%.2f");
      widgets::dragFloat("Saturation", &look.saturation, 0.01f, 0.0f, 3.0f, "%.2f");

      break;
    }
    case postprocess::Tonemapper::KhronosPBR: {
      auto& options = tonemapOptions.khrOptions;

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      widgets::dragFloat("Threshold", &options.compressionStart, 0.01f, 0.2f, 1.0f, "%.2f");
      widgets::dragFloat("Desaturation", &options.desaturation, 0.01f, 0.0f, 1.0f, "%.2f");

      if (widgets::button("Reset", {ImGui::GetContentRegionAvail().x, 0})) {
        options.compressionStart = 0.8;
        options.desaturation = 0.15;
      }

      break;
    }
    case postprocess::Tonemapper::flim: {
      auto& options = tonemapOptions.flimOptions;

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      ImGui::Text("Presets");

      float w = ImGui::CalcItemWidth();
      float available = ImGui::GetContentRegionAvail().x;
      ImGui::SameLine(available - w);
      float buttonWidth = w / 2;
      if (widgets::button("Default", {buttonWidth, 0})) options = postprocess::flim::presets::flim;
      ImGui::SameLine();
      if (widgets::button("Silver", {buttonWidth, 0})) options = postprocess::flim::presets::silver;

      ImGui::Spacing();

      widgets::dragFloat("Pre-exposure", &options.preExposure, 0.1f, -10.0f, 10.0f, "%.1f EV");

      ImGui::Spacing();

      widgets::dragFloat("Min EV", &options.sigmoidLog2Min, 0.1f, -20.0f, 50.0f, "%.1f EV");
      widgets::dragFloat("Max EV", &options.sigmoidLog2Max, 0.1f, -20.0f, 50.0f, "%.1f EV");
      widgets::dragVec2("Toe", (float*) &options.sigmoidToe, 0.001f, 0.0f, 1.0f);
      widgets::dragVec2("Shoulder", (float*) &options.sigmoidShoulder, 0.001f, 0.0f, 1.0f);

      ImGui::Spacing();

      widgets::color("Pre filter", (float*) &options.preFormationFilter);
      widgets::dragFloat("Pre strength", &options.preFormationFilterStrength, 0.001f, 0.0f, 1.0f);
      widgets::color("Post filter", (float*) &options.postFormationFilter);
      widgets::dragFloat("Post strength", &options.postFormationFilterStrength, 0.001f, 0.0f, 1.0f);

      ImGui::Spacing();

      widgets::dragFloat("Neg. Exposure", &options.negativeExposure, 0.1f, -10.0f, 10.0f, "%.1f EV");
      widgets::dragFloat("Neg. Density", &options.negativeDensity, 0.5f, 0.0f, 100.0f, "%.1f");
      widgets::dragFloat("Print Exposure", &options.printExposure, 0.1f, -10.0f, 10.0f, "%.1f EV");
      widgets::dragFloat("Print Density", &options.printDensity, 0.5f, 0.0f, 100.0f, "%.1f");
      widgets::color("Backlight", (float*) &options.printBacklight);

      ImGui::Spacing();

      ImGui::Checkbox("Auto black point", &options.autoBlackPoint);
      ImGui::BeginDisabled(options.autoBlackPoint);
      widgets::dragFloat("Black point", &options.blackPoint, 0.01f, 0.0f, 1.0f, "%.2f");
      ImGui::EndDisabled();
      widgets::dragFloat("Midtone Sat.", &options.midtoneSaturation, 0.01f, 0.0f, 10.0f, "%.2f");

      break;
    }
    default: {}
  }

  /*
   * Final grading
   */
  ImGui::Spacing();
  ImGui::SeparatorText("Final grading");

  auto rgbAvg = [](float3 rgb) { return (rgb.r + rgb.g + rgb.b) / 3.0; };

  float3 liftColor = tonemapOptions.postTonemap.shadowColor;
  liftColor -= rgbAvg(liftColor);
  float3 gammaColor = tonemapOptions.postTonemap.midtoneColor;
  gammaColor -= rgbAvg(gammaColor);
  float3 gainColor = tonemapOptions.postTonemap.highlightColor;
  gainColor -= rgbAvg(gainColor);

  float3 lift = liftColor + tonemapOptions.postTonemap.shadowOffset * 0.01;
  float3 gain = 1.0 + gainColor + tonemapOptions.postTonemap.highlightOffset * 0.01;

  float3 midGray = 0.5 + gammaColor + tonemapOptions.postTonemap.midtoneOffset * 0.01;
  float3 gamma = log10((0.5 - lift) / (gain - lift)) / log10(midGray);

  float xs[101], rs[101], gs[101], bs[101];
  for (size_t i = 0; i <= 100; i++) {
    float x = float(i) / 100.0f;
    float3 t = clamp(pow(float3(x), 1.0 / gamma), float3(0), float3(1));
    float3 c = mix(lift, gain, t);

    xs[i] = x;
    rs[i] = c.r;
    gs[i] = c.g;
    bs[i] = c.b;
  }

  if (ImPlot::BeginPlot("##GradingCurves", {-1, 150})) {
    auto flags =
      ImPlotAxisFlags_Lock | ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_NoLabel;
    ImPlot::SetupAxes("x", "y", flags, flags);
    ImPlot::SetNextLineStyle({1, 0, 0, 1});
    ImPlot::PlotLine("", xs, rs, 101);
    ImPlot::SetNextLineStyle({0, 1, 0, 1});
    ImPlot::PlotLine("", xs, gs, 101);
    ImPlot::SetNextLineStyle({0, 0, 1, 1});
    ImPlot::PlotLine("", xs, bs, 101);
    ImPlot::EndPlot();
  }

  widgets::color("Shadows", (float*) &tonemapOptions.postTonemap.shadowColor);
  widgets::color("Midtones", (float*) &tonemapOptions.postTonemap.midtoneColor);
  widgets::color("Highlights", (float*) &tonemapOptions.postTonemap.highlightColor);
  widgets::dragFloat("Shadows", &tonemapOptions.postTonemap.shadowOffset, 1.0f, -100.0f, 100.0f, "%.0f");
  widgets::dragFloat("Midtones", &tonemapOptions.postTonemap.midtoneOffset, 1.0f, -100.0f, 100.0f, "%.0f");
  widgets::dragFloat("Highlights", &tonemapOptions.postTonemap.highlightOffset, 1.0f, -100.0f, 100.0f, "%.0f");
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
