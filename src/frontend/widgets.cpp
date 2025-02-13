#include "widgets.hpp"
#include "theme.hpp"

namespace pt::frontend::widgets {

std::tuple<float, std::string> leftAlignedLabel(const char* label) {
  std::string_view sv(label);
  std::string visibleLabel(sv.substr(0, sv.find('#')));

  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s", visibleLabel.c_str());

  float w = ImGui::CalcItemWidth();
  float available = ImGui::GetContentRegionAvail().x;
  auto hiddenLabel = std::format("##{}", label);

  ImGui::SameLine(available - w);

  return std::make_tuple(w, hiddenLabel);
}

float getWidthForItems(uint32_t n) {
  return (ImGui::GetContentRegionAvail().x
          - static_cast<float>(n - 1) * ImGui::GetStyle().ItemSpacing.x
         ) / static_cast<float>(n);
}

void removeNodePopup(pt::frontend::State& state, pt::Scene::NodeID id) {
  if (popup("Remove_Popup")) {
    ImGui::TextDisabled("Action for children:");
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
    if (selectable("Remove")) {
      state.removeNode(id);
    }
    if (selectable("Move to root")) {
      state.removeNode(id, Scene::RemoveMode::MoveToRoot);
    }
    if (selectable("Move to parent")) {
      state.removeNode(id, Scene::RemoveMode::MoveToParent);
    }
    ImGui::PopStyleVar();

    ImGui::EndPopup();
  }
}

void transformEditor(Transform& transform) {
  dragVec3(
    "Translation",
    (float*) &transform.translation,
    0.01f
  );

  ImGui::BeginDisabled(transform.track);
  dragVec3(
    "Rotation",
    (float*) &transform.rotation,
    0.005f,
    0.0f,
    2.0f * std::numbers::pi,
    "%.3f",
    ImGuiSliderFlags_WrapAround
  );
  ImGui::EndDisabled();

  dragVec3(
    "Scale",
    (float*) &transform.scale,
    0.01f
  );

  ImGui::SeparatorText("Constraints");

  ImGui::Checkbox("Track", &transform.track);

  ImGui::BeginDisabled(!transform.track);
  dragVec3(
    "Target",
    (float*) &transform.target,
    0.01f
  );
  ImGui::EndDisabled();

  if (buttonDanger("Reset", {ImGui::GetContentRegionAvail().x, 0})) {
    transform.translation = {0, 0, 0};
    transform.rotation = {0, 0, 0};
    transform.scale = {1, 1, 1};
    transform.target = {0, 0, 0};
    transform.track = false;
  }
}

bool buttonDanger(const char* label, const ImVec2& size) {
  auto* theme = theme::Theme::currentTheme;

  theme::imguiRGBA(theme->bgObject);

  ImGui::PushStyleColor(
    ImGuiCol_Button,
    theme::imguiRGBA(mix(theme->bgObject, theme->danger, float3(0.10)))
  );
  ImGui::PushStyleColor(
    ImGuiCol_ButtonHovered,
    theme::imguiRGBA(mix(theme->bgObject, theme->danger, float3(0.20)))
  );
  ImGui::PushStyleColor(
    ImGuiCol_ButtonActive,
    theme::imguiRGBA(mix(theme->bgObject, theme->danger, float3(0.25)))
  );
  ImGui::PushStyleColor(ImGuiCol_Border, theme::imguiRGBA(theme::Theme::currentTheme->danger));
  const bool clicked = ImGui::Button(label, size);
  ImGui::PopStyleColor(4);
  return clicked;
}

bool button(const char* label, const ImVec2& size) {
  ImGui::PushStyleColor(ImGuiCol_Border, theme::imguiRGBA(theme::Theme::currentTheme->primary));
  const bool clicked = ImGui::Button(label, size);
  ImGui::PopStyleColor();
  return clicked;
}

bool selectableDanger(const char* label, bool selected, ImGuiSelectableFlags flags, const ImVec2& size) {
  ImGui::PushStyleColor(
    ImGuiCol_HeaderHovered,
    (ImVec4) ImColor::HSV(0.0f, 0.3f, 0.95f)
  );
  ImGui::PushStyleColor(
    ImGuiCol_HeaderActive,
    (ImVec4) ImColor::HSV(0.0f, 0.4f, 0.93f)
  );
  ImGui::PushStyleColor(
    ImGuiCol_Text,
    (ImVec4) ImColor::HSV(0.0f, 0.8f, 0.5f)
  );
  const bool clicked = selectable(label, selected, flags, size);
  ImGui::PopStyleColor(3);
  return clicked;
}

bool selectable(const char* label, bool selected, ImGuiSelectableFlags flags, const ImVec2& size) {
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
  const bool clicked = ImGui::Selectable(label, selected, flags, size);
  ImGui::PopStyleVar();
  return clicked;
}

bool combo(const char* label, const char* preview, ImGuiComboFlags flags) {
  auto [w, hiddenLabel] = leftAlignedLabel(label);
  ImGui::SetNextItemWidth(w + ImGui::GetStyle().ItemSpacing.x);
  return ImGui::BeginCombo(hiddenLabel.c_str(), preview, flags);
}

bool comboItem(const char* label, bool selected, ImGuiSelectableFlags flags) {
  ImGui::SetCursorPosX(10.0f);
  return selectable(label, selected, flags, {ImGui::GetContentRegionAvail().x - 6.0f, 0});
}

bool popup(const char* str_id, ImGuiWindowFlags flags) {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8, 6});
  bool open = ImGui::BeginPopup(str_id, flags);
  ImGui::PopStyleVar();
  return open;
}

bool context(const char* str_id, ImGuiPopupFlags flags) {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8, 6});
  bool open = ImGui::BeginPopupContextItem(str_id, flags);
  ImGui::PopStyleVar();
  return open;
}

bool menu(const char* label) {
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
  bool open = ImGui::BeginMenu(label);
  ImGui::PopStyleVar();
  return open;
}

bool menuItem(const char* label, const char* shortcut, bool* selected) {
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
  bool open = ImGui::MenuItem(label, shortcut, selected);
  ImGui::PopStyleVar();
  return open;
}

bool menuItem(const char* label, const char* shortcut, bool selected) {
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
  bool open = ImGui::MenuItem(label, shortcut, selected);
  ImGui::PopStyleVar();
  return open;
}

std::optional<Scene::AssetID> textureSelect(Scene& scene, const char* label, std::optional<Scene::AssetID> selectedId) {
  auto newId = selectedId;

  auto selectedName = selectedId
    .transform([&](auto id) { return scene.getAsset<Texture>(id)->name(); })
    .value_or("No texture");
  if (selectedName.empty()) selectedName = std::format("Texture [{}]", selectedId.value());

  if (combo(label, selectedName.data())) {
    if (widgets::comboItem("No texture", false)) {
      newId = std::nullopt;
    }

    auto textures = scene.getAll<Texture>();
    if (!textures.empty()) {
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();
    }

    for (const auto& td: textures) {
      auto isSelected = selectedId == td.id;
      auto name = td.asset->name();
      if (name.empty()) name = std::format("Texture [{}]", td.id);

      if (widgets::comboItem(name.data(), isSelected)) {
        newId = td.id;
      }
    }

    ImGui::EndCombo();
  }

  return newId;
}

bool dragInt(
  const char* label,
  int* i,
  int step,
  int min,
  int max,
  const char* fmt,
  ImGuiSliderFlags flags
) {
  auto [w, hiddenLabel] = leftAlignedLabel(label);
  ImGui::SetNextItemWidth(w + ImGui::GetStyle().ItemSpacing.x);
  return ImGui::DragInt(hiddenLabel.c_str(), i, step, min, max, fmt, flags);
}

bool dragFloat(
  const char* label,
  float* f,
  float step,
  float min,
  float max,
  const char* fmt,
  ImGuiSliderFlags flags
) {
  auto [w, hiddenLabel] = leftAlignedLabel(label);
  ImGui::SetNextItemWidth(w + ImGui::GetStyle().ItemSpacing.x);
  return ImGui::DragFloat(hiddenLabel.c_str(), f, step, min, max, fmt, flags);
}

bool dragVec3(
  const char* label,
  float* f,
  float step,
  float min,
  float max,
  const char* fmt,
  ImGuiSliderFlags flags
) {
  float wAll = ImGui::GetFrameHeight() * 2;
  bool change = false;

  auto [w, hiddenLabel] = leftAlignedLabel(label);
  ImGui::SetNextItemWidth(w - wAll);
  change |= ImGui::DragFloat3(hiddenLabel.c_str(), f, step, min, max, fmt, flags);

  ImGui::SameLine();
  ImGui::SetNextItemWidth(wAll);
  auto hiddenLabelAll = std::format("##{}_All", label);
  if (ImGui::DragFloat(hiddenLabelAll.c_str(), f, step, min, max, "U")) {
    change = true;
    f[1] = f[0];
    f[2] = f[0];
  }

  return change;
}

bool color(const char* label, float col[3], ImGuiColorEditFlags flags) {
  auto [w, hiddenLabel] = leftAlignedLabel(label);
  return ImGui::ColorEdit3(hiddenLabel.c_str(), col, flags, {w + ImGui::GetStyle().ItemSpacing.x, 0});
}

}
