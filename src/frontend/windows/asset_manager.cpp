#include "asset_manager.hpp"

namespace pt::frontend::windows {

void AssetManager::render() {
  ImGui::Begin("Asset Manager");
  
  /*
   * Main panel
   */
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {8, 4});
  auto childSize = ImGui::GetContentRegionAvail();
//  childSize.y -= ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y + 4.0f;
  bool visible = ImGui::BeginChild("##AMView", childSize, ImGuiChildFlags_FrameStyle);
  ImGui::PopStyleVar(2);
  if (visible) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
    
    if (ImGui::BeginTable("AMTable", 3)) {
      ImGui::TableNextColumn();
      renderTexturesList();
      ImGui::TableNextColumn();
      renderMaterialsList();
      ImGui::TableNextColumn();
      renderMeshesList();
      
      ImGui::EndTable();
    }
    
    ImGui::PopStyleVar();
  }
  ImGui::EndChild();
  
  ImGui::End();
}

void AssetManager::renderTexturesList() {
  for (const auto& texture: m_store.scene().getAll<Texture>()) {
    auto flags = m_baseFlags | ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    auto name = texture.asset->name();
    if (name.empty()) name = std::format("Texture [{}]", texture.id);
    
    ImGui::PushStyleColor(
      ImGuiCol_Header,
      ImGui::GetStyleColorVec4(ImGuiCol_FrameBg)
    );
    ImGui::TreeNodeEx(name.data(), flags);
    ImGui::PopStyleColor();
  }
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
