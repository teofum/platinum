#ifndef PLATINUM_WIDGETS_HPP
#define PLATINUM_WIDGETS_HPP

#include <imgui.h>

#include <frontend/state.hpp>

namespace pt::frontend::widgets {

constexpr ImGuiColorEditFlags COLOR_FLAGS =
  ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_NoSidePreview
  | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_DisplayRGB
  | ImGuiColorEditFlags_DisplayHSV;

std::tuple<float, std::string> leftAlignedLabel(const char* label);

float getWidthForItems(uint32_t n);

void removeNodePopup(State& state, Scene::NodeID id);

void transformEditor(Transform& transform);

bool buttonDanger(const char* label, const ImVec2& size = {0, 0});

bool button(const char* label, const ImVec2& size = {0, 0});

bool selectableDanger(
  const char* label,
  bool selected = false,
  ImGuiSelectableFlags flags = 0,
  const ImVec2& size = ImVec2(0, 0)
);

bool selectable(
  const char* label,
  bool selected = false,
  ImGuiSelectableFlags flags = 0,
  const ImVec2& size = ImVec2(0, 0)
);

bool combo(const char* label, const char* preview, ImGuiComboFlags flags = 0);

bool comboItem(const char* label, bool selected = false, ImGuiSelectableFlags flags = 0);

bool popup(const char* str_id, ImGuiWindowFlags flags = 0);

bool context(const char* str_id = nullptr, ImGuiPopupFlags flags = 1);

bool menu(const char* label);

bool menuItem(const char* label, const char* shortcut = nullptr, bool* selected = nullptr);

bool menuItem(const char* label, const char* shortcut, bool selected);

std::optional<Scene::AssetID> textureSelect(Scene& scene, const char* label, std::optional<Scene::AssetID> selectedId);

bool dragInt(
  const char* label,
  int* i,
  int step = 1.0,
  int min = 0.0,
  int max = 0.0,
  const char* fmt = "%d",
  ImGuiSliderFlags flags = 0
);

bool dragFloat(
  const char* label,
  float* f,
  float step = 1.0,
  float min = 0.0,
  float max = 0.0,
  const char* fmt = "%.3f",
  ImGuiSliderFlags flags = 0
);

bool dragVec2(
  const char* label,
  float* f,
  float step = 1.0,
  float min = 0.0,
  float max = 0.0,
  const char* fmt = "%.3f",
  ImGuiSliderFlags flags = 0
);

bool dragVec3(
  const char* label,
  float* f,
  float step = 1.0,
  float min = 0.0,
  float max = 0.0,
  const char* fmt = "%.3f",
  ImGuiSliderFlags flags = 0
);

bool color(const char* label, float col[3], ImGuiColorEditFlags flags = COLOR_FLAGS);

}

#endif //PLATINUM_WIDGETS_HPP
