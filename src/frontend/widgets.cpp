#include "widgets.hpp"

namespace pt::frontend::widgets {

float getWidthForItems(uint32_t n) {
  return (
           ImGui::GetContentRegionAvail().x
           - static_cast<float>(n - 1) * ImGui::GetStyle().ItemSpacing.x
         ) / static_cast<float>(n);
}

void removeNodePopup(pt::frontend::State& state, pt::Scene::NodeID id) {
  if (popup("Remove_Popup")) {
    ImGui::PushStyleColor(
      ImGuiCol_FrameBg,
      ImGui::GetStyleColorVec4(ImGuiCol_WindowBg)
    );
    ImGui::CheckboxFlags(
      "Keep orphaned meshes",
      state.removeOptions(),
      Scene::RemoveOptions_KeepOrphanedObjects
    );
    ImGui::PopStyleColor();

    ImGui::Separator();
    ImGui::TextDisabled("Action for children:");
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
    if (selectable("Remove")) {
      state.removeNode(id, Scene::RemoveOptions_RemoveChildrenRecursively);
    }
    if (selectable("Move to root")) {
      state.removeNode(id, Scene::RemoveOptions_MoveChildrenToRoot);
    }
    if (selectable("Move to parent")) {
      state.removeNode(id, Scene::RemoveOptions_MoveChildrenToParent);
    }
    ImGui::PopStyleVar();

    ImGui::EndPopup();
  }
}

void transformEditor(Transform& transform) {
  ImGui::DragFloat3(
    "Translation",
    (float*) &transform.translation,
    0.01f
  );

  ImGui::BeginDisabled(transform.track);
  ImGui::DragFloat3(
    "Rotation",
    (float*) &transform.rotation,
    0.005f,
    0.0f,
    2.0f * std::numbers::pi,
    "%.3f",
    ImGuiSliderFlags_WrapAround
  );
  ImGui::EndDisabled();

  ImGui::DragFloat3(
    "Scale",
    (float*) &transform.scale,
    0.01f
  );

  ImGui::SeparatorText("Constraints");

  ImGui::Checkbox("Track", &transform.track);

  ImGui::BeginDisabled(!transform.track);
  ImGui::DragFloat3(
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
  const bool clicked = ImGui::Button(label, size);
  ImGui::PopStyleColor(4);
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

}
