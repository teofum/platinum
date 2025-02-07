#include "asset_manager.hpp"

#include <frontend/theme.hpp>

namespace pt::frontend::windows {

void AssetManager::render() {
  ImGui::Begin("Asset Manager");
  
  auto& theme = *theme::Theme::currentTheme;
  
  /*
   * Main panel
   */
  ImGui::PushStyleColor(ImGuiCol_TabActive, frontend::theme::imguiRGBA(theme.bgObject));
  ImGui::PushStyleColor(ImGuiCol_TabHovered, frontend::theme::imguiRGBA(theme.bgObject));
  if (ImGui::BeginTabBar("##AMTabs")) {
    if (ImGui::BeginTabItem("Textures")) {
      if (renderPanel()) {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
        renderTexturesList();
        ImGui::PopStyleVar();
      }
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Materials")) {
      if (renderPanel()) {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
        renderMaterialsList();
        ImGui::PopStyleVar();
      }
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Meshes")) {
      if (renderPanel()) {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
        renderMeshesList();
        ImGui::PopStyleVar();
      }
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::PopStyleColor(2);
  
  ImGui::End();
}

bool AssetManager::renderPanel() {
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {4, 4});
  auto childSize = ImGui::GetContentRegionAvail();
  bool visible = ImGui::BeginChild("##AMView", childSize, ImGuiChildFlags_FrameStyle);
  ImGui::PopStyleVar(2);
  
  return visible;
}

void AssetManager::renderTexturesList() {
  auto availableWidth = ImGui::GetContentRegionAvail().x;
  auto columns = uint32_t((availableWidth - 4 - ImGui::GetStyle().ScrollbarSize) / 72);
  
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {4, 4});
  bool table = ImGui::BeginTable(
		"AMTextureView",
		columns,
		ImGuiTableFlags_ScrollY,
		{0, 0}
	);
  if (table) {
    for (size_t i = 0; i < columns; i++) {
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, i == 0 ? 68 : 64);
    }
    
    for (const auto& texture: m_store.scene().getAll<Texture>()) {
      ImGui::TableNextColumn();
      
      // Funny hack because imgui keeps clipping the first column for some reason
      if (ImGui::TableGetColumnIndex() == 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4);
      
      auto name = texture.asset->name();
      if (name.empty()) name = std::format("[ID: {}]", texture.id);
      
      // Draw the actual selectable
      auto cursorStart = ImGui::GetCursorPos();
      auto label = std::format("##Texture_{}", texture.id);
      widgets::selectable(label.data(), false, 0, { 64, 70 + ImGui::GetTextLineHeightWithSpacing() });
      auto cursorEnd = ImGui::GetCursorPos();
      
      ImGui::SetCursorPos({cursorStart.x + 2, cursorStart.y + 4});
      ImGui::Image((ImTextureID) texture.asset->texture(), { 60, 60 });
      if (ImGui::TableGetColumnIndex() == 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4);
      ImGui::Text("%s", name.data());
      ImGui::SetCursorPos(cursorEnd);
    }
    ImGui::EndTable();
  }
  ImGui::PopStyleVar();
}

void AssetManager::renderMaterialsList() {
  for (const auto& material: m_store.scene().getAll<Material>()) {
    auto flags = m_baseFlags | ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    auto name = material.asset->name;
    if (name.empty()) name = std::format("Material [{}]", material.id);
    
    ImGui::PushStyleColor(
      ImGuiCol_Header,
      ImGui::GetStyleColorVec4(ImGuiCol_FrameBg)
    );
    ImGui::TreeNodeEx(name.data(), flags);
    ImGui::PopStyleColor();
  }
}

void AssetManager::renderMeshesList() {
  for (const auto& mesh: m_store.scene().getAll<Mesh>()) {
    auto flags = m_baseFlags | ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    auto name = std::format("Mesh [{}]", mesh.id);
    
    ImGui::PushStyleColor(
      ImGuiCol_Header,
      ImGui::GetStyleColorVec4(ImGuiCol_FrameBg)
    );
    ImGui::TreeNodeEx(name.data(), flags);
    ImGui::PopStyleColor();
  }
}

}
