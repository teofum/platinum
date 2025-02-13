#ifndef PLATINUM_SCENE_EXPLORER_HPP
#define PLATINUM_SCENE_EXPLORER_HPP

#include <imgui.h>

#include <frontend/window.hpp>
#include <frontend/widgets.hpp>

namespace pt::frontend::windows {

class SceneExplorer final : Window {
public:
  constexpr SceneExplorer(Store& store, State& state, bool* open = nullptr) noexcept
    : Window(store, state, open) {
  }

  void render() final;

  const uint8_t* keys = nullptr;

private:
  static constexpr const ImGuiTreeNodeFlags m_baseFlags =
    ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
    ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_AllowItemOverlap;
  
  void renderNode(const Scene::Node& node, uint32_t level = 1);
};

}

#endif //PLATINUM_SCENE_EXPLORER_HPP
