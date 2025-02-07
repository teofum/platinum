#ifndef PLATINUM_ASSET_MANAGER_HPP
#define PLATINUM_ASSET_MANAGER_HPP

#include <imgui.h>

#include <frontend/window.hpp>
#include <frontend/widgets.hpp>

namespace pt::frontend::windows {

class AssetManager final : Window {
public:
  AssetManager(Store& store, State& state, bool* open = nullptr) noexcept;

  void render() final;

private:
  static constexpr const ImGuiTreeNodeFlags m_baseFlags =
    ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
    ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_AllowItemOverlap;
  
  std::vector<Scene::AnyAssetData> m_assets;
  size_t m_assetCount = 0;
  ImGuiSelectionBasicStorage m_selection;
  
  uint32_t m_iconSize = 48;
  uint32_t m_spacing = 8;
  uint32_t m_hitSpacing = 4;
  uint32_t m_padding = 2;
  
  ImVec2 m_layoutItemSize;
  ImVec2 m_layoutItemStep;
  float m_layoutItemSpacing = 0.0;
  float m_layoutSelectableSpacing = 0.0;
  float m_layoutOuterPadding = 0.0;
  uint32_t m_layoutColumnCount = 0;
  uint32_t m_layoutRowCount = 0;
  
  void updateLayoutSizes(float availableWidth);
};

}

#endif //PLATINUM_ASSET_MANAGER_HPP
