#include "scene_explorer.hpp"

#include <SDL.h>

#include <core/primitives.hpp>

namespace pt::frontend::windows {

void SceneExplorer::render() {
  ImGui::Begin("Scene Explorer");

  /*
   * Main panel
   */
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {8, 4});
  auto childSize = ImGui::GetContentRegionAvail();
  childSize.y -= ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y + 4.0f;
  childSize.y = max(childSize.y, 300.0f);
  bool visible = ImGui::BeginChild("##SETree", childSize, ImGuiChildFlags_FrameStyle);
  ImGui::PopStyleVar(2);
  if (visible) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
		renderNode(m_store.scene().root());
    ImGui::PopStyleVar();
  }
  ImGui::EndChild();
  
  ImGui::Spacing();
  
  /*
   * Create/import options
   */
  auto buttonWidth = widgets::getWidthForItems(2);
  if (widgets::button("Add...", {buttonWidth, 0})) {
    ImGui::OpenPopup("Add_Popup");
  }
  if (widgets::popup("Add_Popup")) {
//    if (widgets::selectable("Cube", false, 0, {100, 0})) {
//      uint32_t parentId = m_state.selectedNode().value_or(0);
//
//      auto cube = pt::primitives::cube(m_store.device(), 2.0f);
//      auto id = m_store.scene().addMesh(std::move(cube));
//      pt::Scene::Node node("Cube", id);
//      node.materials.push_back(0);
//      m_store.scene().addNode(std::move(node), parentId);
//    }
//    if (widgets::selectable("Sphere", false, 0, {100, 0})) {
//      uint32_t parentId = m_state.selectedNode().value_or(0);
//
//      auto sphere = pt::primitives::sphere(m_store.device(), 1.0f, 24, 32);
//      auto id = m_store.scene().addMesh(std::move(sphere));
//      pt::Scene::Node node("Sphere", id);
//      node.materials.push_back(0);
//      m_store.scene().addNode(std::move(node), parentId);
//    }
    
    ImGui::Separator();
    
//    if (widgets::selectable("Material", false, 0, {100, 0})) {
//      auto name = std::format("Material {}", m_store.scene().getAllMaterials().size() + 1);
//      m_store.scene().addMaterial(name, BSDF{ .baseColor = {0.8, 0.8, 0.8} });
//    }
    
    ImGui::Separator();
    
//    if (widgets::selectable("Camera", false, 0, {100, 0})) {
//      uint32_t parentId = m_state.selectedNode().value_or(0);
//
//      auto id = m_store.scene().addCamera(Camera::withFocalLength(28.0f));
//      pt::Scene::Node node("Camera");
//      node.cameraId = id;
//      node.transform.translation = {-5, 5, 5};
//      node.transform.track = true;
//      m_store.scene().addNode(std::move(node), parentId);
//    }
    ImGui::EndPopup();
  }

  ImGui::SameLine();
  if (widgets::button("Import...", {buttonWidth, 0})) {
    ImGui::OpenPopup("Import_Popup");
  }
  if (widgets::popup("Import_Popup")) {
    if (widgets::menuItem("glTF")) m_store.importGltf();
    
    ImGui::Separator();
    
    if (widgets::menu("Texture")) {
      if (widgets::menuItem("Color")) m_store.importTexture(loaders::texture::TextureType::sRGB);
      if (widgets::menuItem("Normal map")) m_store.importTexture(loaders::texture::TextureType::LinearRGB);
      if (widgets::menuItem("HDR/Env map")) m_store.importTexture(loaders::texture::TextureType::HDR);
      if (widgets::menuItem("Grayscale")) m_store.importTexture(loaders::texture::TextureType::Mono);
      ImGui::EndMenu();
    }
    
    ImGui::EndPopup();
  }

  ImGui::End();
}

void SceneExplorer::renderNode(const Scene::Node& node, uint32_t level) {
  auto nodeFlags = m_baseFlags;
  bool isSelected = m_state.selectedNode() == node.id();
  if (isSelected) {
    nodeFlags |= ImGuiTreeNodeFlags_Selected;
  }

  bool isLeaf = node.children().empty();
  if (isLeaf) {
    nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
  }

  /*
   * Tree node item
   */
  auto label = std::format("{}##Node_{}", node.name(), uint32_t(node.id()));
  ImGui::PushID(label.c_str());

  if (!isSelected) {
    ImGui::PushStyleColor(
      ImGuiCol_Header,
      ImGui::GetStyleColorVec4(ImGuiCol_FrameBg)
    );
  }
  bool isOpen = ImGui::TreeNodeEx(label.c_str(), nodeFlags) && !isLeaf;
  if (!isSelected) ImGui::PopStyleColor();

  if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
    m_state.selectNode(node.id());
  }

  /*
   * Context menu
   */
  if (widgets::context()) {
    if (widgets::selectable("Center camera")) {
      m_state.setNodeAction(State::NodeAction_CenterCamera, node.id());
    }

    if (!node.isRoot()) {
      if (widgets::selectableDanger(
        "Remove",
        false,
        ImGuiSelectableFlags_NoAutoClosePopups
      )) {
        if (!node.children().empty()) ImGui::OpenPopup("Remove_Popup");
        else m_state.removeNode(node.id());
      }

      if (!node.children().empty()) {
        widgets::removeNodePopup(m_state, node.id());
      }
    }

    ImGui::EndPopup();
  }

  /*
   * Drag and drop support
   */
  if (!node.isRoot() && ImGui::BeginDragDropSource()) {
    auto id = uint32_t(node.id());
    ImGui::SetDragDropPayload("PT_NODE", &id, sizeof(Scene::NodeID));

    const bool clone = keys[SDL_SCANCODE_LALT] || keys[SDL_SCANCODE_RALT];

    ImGui::Text("%s%s", label.c_str(), clone ? " [+]" : "");
    ImGui::EndDragDropSource();
  }

  if (ImGui::BeginDragDropTarget()) {
    if (const auto pl = ImGui::AcceptDragDropPayload("PT_NODE")) {
      IM_ASSERT(pl->DataSize == sizeof(Scene::NodeID));
      const auto plId = *((Scene::NodeID*) pl->Data);
      const bool clone = keys[SDL_SCANCODE_LALT] || keys[SDL_SCANCODE_RALT];

      if (clone) {
        m_store.scene().cloneNode(plId, node.id());
      } else {
        m_store.scene().moveNode(plId, node.id());
      }
    }
    ImGui::EndDragDropTarget();
  }

  /*
   * Inline buttons
   */
  bool& visible = node.visible();
  auto visibleLabel = std::format("{}##Node_{}", visible ? 'V' : '-', uint32_t(node.id()));
  auto inlineButtonWidth = ImGui::GetFrameHeight();
  auto offset = ImGui::GetStyle().IndentSpacing * (isOpen ? level + 1 : level);
  
  ImGui::SameLine(ImGui::GetContentRegionAvail().x + offset - inlineButtonWidth);
  if (widgets::button(visibleLabel.c_str(), {inlineButtonWidth, 0})) {
    visible = !visible;
  }

  /*
   * Render children
   */
  if (isOpen) {
    for (const auto& child: node.children()) renderNode(child, level + 1);
    ImGui::TreePop();
  }

  ImGui::PopID();
}

//void SceneExplorer::renderMeshesList() {
//  for (const auto& md: m_store.scene().getAllMeshes()) {
//    auto flags = m_baseFlags | ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
//    
//    bool selected = m_state.selectedMesh() == md.meshId;
//    if (selected) {
//      flags |= ImGuiTreeNodeFlags_Selected;
//    } else {
//      ImGui::PushStyleColor(
//        ImGuiCol_Header,
//        ImGui::GetStyleColorVec4(ImGuiCol_FrameBg)
//      );
//    }
//    
//    auto label = std::format("Mesh [{}]", md.meshId);
//    ImGui::TreeNodeEx(label.c_str(), flags);
//    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
//      m_state.selectMesh(md.meshId);
//    }
//
//    if (!selected) ImGui::PopStyleColor();
//  }
//}

//void SceneExplorer::renderMaterialsList() {
//  for (const auto& md: m_store.scene().getAllMaterials()) {
//    auto flags = m_baseFlags | ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
//    
//    bool selected = m_state.selectedMaterial() == md.materialId;
//    if (selected) {
//      flags |= ImGuiTreeNodeFlags_Selected;
//    } else {
//      ImGui::PushStyleColor(
//        ImGuiCol_Header,
//        ImGui::GetStyleColorVec4(ImGuiCol_FrameBg)
//      );
//    }
//    
//    auto name = std::format("{}{}", md.name, md.material->flags & Material::Material_UseAlpha ? " *" : "");
//    ImGui::TreeNodeEx(name.c_str(), flags);
//    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
//      m_state.selectMaterial(md.materialId);
//    }
//
//    if (!selected) ImGui::PopStyleColor();
//  }
//}

}
