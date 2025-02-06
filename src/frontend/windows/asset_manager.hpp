#ifndef PLATINUM_ASSET_MANAGER_HPP
#define PLATINUM_ASSET_MANAGER_HPP

#include <imgui.h>

#include <frontend/window.hpp>
#include <frontend/widgets.hpp>

namespace pt::frontend::windows {

class AssetManager final : Window {
public:
  constexpr AssetManager(Store& store, State& state, bool* open = nullptr) noexcept
    : Window(store, state, open) {
  }

  void render() final;

private:
  static constexpr const ImGuiTreeNodeFlags m_baseFlags =
    ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
    ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_AllowItemOverlap;
  
  void renderTexturesList();
  void renderMaterialsList();
  void renderMeshesList();
};

}

#endif //PLATINUM_ASSET_MANAGER_HPP
