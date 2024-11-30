#ifndef PLATINUM_WIDGETS_HPP
#define PLATINUM_WIDGETS_HPP

#include <imgui.h>

#include <frontend/state.hpp>

namespace pt::frontend::widgets {

float getWidthForItems(uint32_t n);

void removeNodePopup(State& state, Scene::NodeID id);

void transformEditor(Transform& transform);

bool buttonDanger(const char* label, const ImVec2& size);

bool selectableDanger(
  const char* label,
  bool selected = false,
  ImGuiSelectableFlags flags = 0,
  const ImVec2& size = ImVec2(0, 0));

}

#endif //PLATINUM_WIDGETS_HPP
