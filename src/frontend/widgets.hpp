#ifndef PLATINUM_WIDGETS_HPP
#define PLATINUM_WIDGETS_HPP

#include <imgui.h>

#include <frontend/state.hpp>

namespace pt::frontend::widgets {

float getWidthForItems(uint32_t n);

void removeNodePopup(State& state, Scene::NodeID id);

void transformEditor(Transform& transform);

bool buttonDanger(const char* label, const ImVec2& size);

bool button(const char* label, const ImVec2& size);

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

bool comboItem(const char* label, bool selected = false, ImGuiSelectableFlags flags = 0);

bool popup(const char* str_id, ImGuiWindowFlags flags = 0);

bool context(const char* str_id = nullptr, ImGuiPopupFlags flags = 1);

bool menu(const char* label);

bool menuItem(const char* label, const char* shortcut = nullptr, bool* selected = nullptr);

bool menuItem(const char* label, const char* shortcut, bool selected);

}

#endif //PLATINUM_WIDGETS_HPP
