#include "asset_manager.hpp"

#include <frontend/theme.hpp>

namespace pt::frontend::windows {

AssetManager::AssetManager(Store& store, State& state, bool* open) noexcept
: Window(store, state, open) {}

void AssetManager::render() {
  m_assets = m_store.scene().getAllAssets([&](const Scene::AssetPtr& asset) {
    return (m_showMeshes && std::holds_alternative<std::unique_ptr<Mesh>>(asset))
    		|| (m_showTextures && std::holds_alternative<std::unique_ptr<Texture>>(asset))
    		|| (m_showMaterials && std::holds_alternative<std::unique_ptr<Material>>(asset));
  });
  m_assetCount = m_assets.size();
  
  auto* theme = theme::Theme::currentTheme;
  
  ImGui::Begin("Asset Manager");
  
  /*
   * Filters and settings
   */
  ImGui::AlignTextToFramePadding();
  ImGui::Text("Show");
  ImGui::SameLine();
  ImGui::Checkbox("Textures", &m_showTextures);
  ImGui::SameLine();
  ImGui::Checkbox("Materials", &m_showMaterials);
  ImGui::SameLine();
  ImGui::Checkbox("Meshes", &m_showMeshes);
  
  ImGui::Spacing();
  
  /*
   * Main panel
   */
 	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
  bool showChild = ImGui::BeginChild("Assets", {0, 0}, ImGuiChildFlags_FrameStyle, ImGuiWindowFlags_NoMove);
  ImGui::PopStyleVar();
  
  if (showChild) {
    float availableWidth = ImGui::GetContentRegionAvail().x;
    updateLayoutSizes(availableWidth);
    
    auto* imguiDrawList = ImGui::GetWindowDrawList();
    
    // Calculate grid start position
    auto startPos = ImGui::GetCursorScreenPos();
    startPos = {startPos.x + m_layoutOuterPadding, startPos.y + m_layoutOuterPadding};
    ImGui::SetCursorScreenPos(startPos);
    
    // Multi select
    ImGuiMultiSelectFlags msFlags = ImGuiMultiSelectFlags_ClearOnEscape | ImGuiMultiSelectFlags_ClearOnClickVoid | ImGuiMultiSelectFlags_BoxSelect2d;
    auto* msIo = ImGui::BeginMultiSelect(msFlags, m_selection.Size, int(m_assetCount));
    
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {m_layoutSelectableSpacing, m_layoutSelectableSpacing});
    
    // Draw grid
    ImGuiListClipper clipper;
    clipper.Begin(m_layoutRowCount, m_layoutItemStep.y);
    
    while (clipper.Step()) {
      for (uint32_t rowIdx = clipper.DisplayStart; rowIdx < clipper.DisplayEnd; rowIdx++) {
        const uint32_t rowBegin = rowIdx * m_layoutColumnCount;
        const uint32_t rowEnd = MIN((rowIdx + 1) * m_layoutColumnCount, uint32_t(m_assetCount));
        for (uint32_t itemIdx = rowBegin; itemIdx < rowEnd; itemIdx++) {
          auto asset = m_assets[itemIdx];
          
          // Push ImGui ID to avoid name conflicts
          ImGui::PushID(int(asset.id));
          
          // Calculate item position in grid
          ImVec2 pos(
            startPos.x + m_layoutItemStep.x * (itemIdx % m_layoutColumnCount),
            startPos.y + m_layoutItemStep.y * rowIdx
          );
          ImGui::SetCursorScreenPos(pos);
          
          // Draw the actual selectable
          ImGui::SetNextItemSelectionUserData(itemIdx);
          bool isSelected = m_selection.Contains(ImGuiID(asset.id));
          bool isVisible = ImGui::IsRectVisible(m_layoutItemSize);

          ImGui::PushStyleColor(ImGuiCol_Header, theme::imguiRGBA(theme->primary));
          ImGui::PushStyleColor(ImGuiCol_HeaderHovered, theme::imguiRGBA(mix(theme->bgObject, theme->primary, float3(0.5))));
          ImGui::PushStyleColor(ImGuiCol_NavCursor, 0);
          ImGui::Selectable("", isSelected, ImGuiSelectableFlags_None, m_layoutItemSize);
          ImGui::PopStyleColor(3);
          
          // Update selection
          if (ImGui::IsItemToggledSelection())
            isSelected = !isSelected;
          
          // TODO: drag-drop source
          
          // Draw item
          if (isVisible) {
            ImVec2 boxMin = pos;
            ImVec2 boxMax(pos.x + m_layoutItemSize.x, pos.y + m_layoutItemSize.y);
            
            // Asset content
            std::visit([&](const auto& asset) {
              using T = std::decay_t<decltype(asset)>;
              
              imguiDrawList->AddRectFilled(boxMin, boxMax, ImGui::GetColorU32(ImGuiCol_WindowBg), 2);
              if constexpr (std::is_same_v<T, Texture*>) {
                imguiDrawList->AddImageRounded(
									(ImTextureID) asset->texture(),
									boxMin, boxMax,
									{0, 0}, {1, 1},
									ImGui::GetColorU32({1, 1, 1, 1}),
									2
                );
              }
            }, asset.asset);
            
            // Type indicator
            // TODO: replace this with an icon
            std::visit([&](const auto& asset) {
              using T = std::decay_t<decltype(asset)>;
              
              ImU32 color;
              if constexpr (std::is_same_v<T, Texture*>) {
                color = theme::imguiU32(theme::sRGB(theme->viewportAxisZ));
              } else if constexpr (std::is_same_v<T, Material*>) {
                color = theme::imguiU32(theme::sRGB(theme->viewportAxisY));
              } else if constexpr (std::is_same_v<T, Mesh*>) {
                color = theme::imguiU32(theme::sRGB(theme->viewportAxisX));
              }
              imguiDrawList->AddRectFilled(
							  {boxMax.x - m_padding - 8, boxMin.y + m_padding},
							  {boxMax.x - m_padding, boxMin.y + m_padding + 8},
							  color,
							  2
							);
              
            }, asset.asset);
            
            // Asset ID
            auto labelColor = ImGui::GetColorU32(isSelected ? ImGuiCol_Text : ImGuiCol_TextDisabled);
            auto label = std::format("{}", asset.id);
            imguiDrawList->AddText(
							{boxMin.x + m_padding, boxMax.y - m_padding - ImGui::GetFontSize()},
							labelColor,
							label.data()
            );
          }
          
          ImGui::PopID();
        }
      }
    }
    
    ImGui::PopStyleVar();
    
    // Apply selection changes
    msIo = ImGui::EndMultiSelect();
    m_selection.ApplyRequests(msIo);
  }
  ImGui::EndChild();
  
  ImGui::End();
}

void AssetManager::updateLayoutSizes(float availableWidth) {
  m_layoutItemSpacing = float(m_spacing);
  m_layoutSelectableSpacing = max(0.0f, m_layoutItemSpacing - m_hitSpacing);
  m_layoutItemSize = {float(m_iconSize), float(m_iconSize)};
  m_layoutItemStep = {m_layoutItemSize.x + m_layoutItemSpacing, m_layoutItemSize.y + m_layoutItemSpacing};
  
  m_layoutColumnCount = MAX(1u, uint32_t(availableWidth / m_layoutItemStep.x));
  m_layoutRowCount = (uint32_t(m_assetCount) + m_layoutColumnCount - 1) / m_layoutColumnCount;
  
  m_layoutOuterPadding = m_spacing * 0.5f;
}

}
