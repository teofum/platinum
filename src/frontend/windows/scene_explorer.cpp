#include "scene_explorer.hpp"

#include <filesystem>
#include <nfd.h>
#include <SDL.h>
#include <tracy/Tracy.hpp>

#include <core/primitives.hpp>
#include <loaders/gltf.hpp>

namespace fs = std::filesystem;

namespace pt::frontend::windows {

void SceneExplorer::render() {
  ZoneScoped;

  ImGui::Begin("Scene Explorer");

  auto buttonWidth = widgets::getWidthForItems(2);
  if (ImGui::Button("Add Objects...", {buttonWidth, 0})) {
    ImGui::OpenPopup("AddObject_Popup");
  }
  if (widgets::popup("AddObject_Popup")) {
    if (widgets::selectable("Cube", false, 0, {100, 0})) {
      uint32_t parentIdx = m_state.selectedNode().value_or(0);

      auto cube = pt::primitives::cube(m_store.device(), 2.0f);
      auto idx = m_store.scene().addMesh(std::move(cube));
      m_store.scene().addNode(pt::Scene::Node("Cube", idx), parentIdx);
    }
    if (widgets::selectable("Sphere", false, 0, {100, 0})) {
      uint32_t parentIdx = m_state.selectedNode().value_or(0);

      auto sphere = pt::primitives::sphere(m_store.device(), 1.0f, 24, 32);
      auto idx = m_store.scene().addMesh(std::move(sphere));
      m_store.scene().addNode(pt::Scene::Node("Sphere", idx), parentIdx);
    }
    ImGui::EndPopup();
  }

  ImGui::SameLine();
  if (ImGui::Button("Import...", {buttonWidth, 0})) {
    ImGui::OpenPopup("Import_Popup");
  }
  if (widgets::popup("Import_Popup")) {
    if (widgets::selectable("glTF", false, 0, {100, 0})) {
      char* path = nullptr;
      auto result = NFD_OpenDialog("*.glb, *.gltf", "../assets", &path);

      if (result == NFD_OKAY) {
        fs::path filePath(path);
        free(path);

        loaders::gltf::GltfLoader gltf(m_store.device(), m_store.scene());
        gltf.load(filePath);
      } else if (result == NFD_ERROR) {
        // TODO: handle fs error
      }
    }
    ImGui::EndPopup();
  }


  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {8, 4});
  if (ImGui::BeginChild("##SETree", {0, 0}, ImGuiChildFlags_FrameStyle)) {
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
    renderNode(0);
    ImGui::PopStyleVar();
  } else {
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();
  }
  ImGui::EndChild();

  ImGui::End();
}

void SceneExplorer::renderNode(Scene::NodeID id, uint32_t level) {
  Scene::Node* node = m_store.scene().node(id);
  static constexpr const ImGuiTreeNodeFlags baseFlags =
    ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
    ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_AllowItemOverlap;

  auto nodeFlags = baseFlags;
  bool isSelected = m_state.selectedNode() == id;
  if (isSelected) {
    nodeFlags |= ImGuiTreeNodeFlags_Selected;
  }

  bool isLeaf = !node->meshId && !node->cameraId && node->children.empty();
  if (isLeaf) {
    nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
  }

  /*
   * Tree node item
   */
  auto label = std::format("{}##Node_{}", node->name, id);
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
    m_state.selectNode(id);
  }

  /*
   * Context menu
   */
  if (widgets::context()) {
    if (widgets::selectable("Center camera")) {
      m_state.setNodeAction(State::NodeAction_CenterCamera, id);
    }

    if (id != 0) {
      if (widgets::selectableDanger(
        "Remove",
        false,
        ImGuiSelectableFlags_NoAutoClosePopups
      )) {
        if (!node->children.empty()) ImGui::OpenPopup("Remove_Popup");
        else m_state.removeNode(id);
      }

      if (!node->children.empty()) {
        widgets::removeNodePopup(m_state, id);
      }
    }

    ImGui::EndPopup();
  }

  /*
   * Drag and drop support
   */
  if (id != 0 && ImGui::BeginDragDropSource()) {
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
        m_store.scene().cloneNode(plId, id);
      } else {
        m_store.scene().moveNode(plId, id);
      }
    }
    ImGui::EndDragDropTarget();
  }

  /*
   * Inline buttons
   */
  auto visibleLabel = std::format("{}##Node_{}", node->flags & Scene::NodeFlags_Visible ? 'V' : '-', id);
  auto inlineButtonWidth = ImGui::GetFrameHeight();
  auto offset = ImGui::GetStyle().IndentSpacing * static_cast<float>(isOpen ? level + 1 : level);
  ImGui::SameLine(ImGui::GetContentRegionAvail().x + offset - inlineButtonWidth);
  if (ImGui::Button(visibleLabel.c_str(), {inlineButtonWidth, 0})) {
    node->flags ^= Scene::NodeFlags_Visible;
  }

  /*
   * Render contents: mesh and children
   */
  if (isOpen) {
    if (node->meshId) {
      auto meshFlags = baseFlags;
      meshFlags |=
        ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

      bool meshSelected = m_state.selectedMesh() == node->meshId;
      if (meshSelected) {
        meshFlags |= ImGuiTreeNodeFlags_Selected;
      } else {
        ImGui::PushStyleColor(
          ImGuiCol_Header,
          ImGui::GetStyleColorVec4(ImGuiCol_FrameBg)
        );
      }

      auto meshLabel = std::format("Mesh [{}]", node->meshId.value());
      ImGui::TreeNodeEx(meshLabel.c_str(), meshFlags);
      if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        m_state.selectMesh(node->meshId);
      }

      if (!meshSelected) ImGui::PopStyleColor();
    }
    if (node->cameraId) {
      auto cameraFlags = baseFlags;
      cameraFlags |=
        ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

      bool cameraSelected = m_state.selectedCamera() == node->cameraId;
      if (cameraSelected) {
        cameraFlags |= ImGuiTreeNodeFlags_Selected;
      } else {
        ImGui::PushStyleColor(
          ImGuiCol_Header,
          ImGui::GetStyleColorVec4(ImGuiCol_FrameBg)
        );
      }

      auto cameraLabel = std::format("Camera [{}]", node->cameraId.value());
      ImGui::TreeNodeEx(cameraLabel.c_str(), cameraFlags);
      if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        m_state.selectCamera(node->cameraId);
      }

      if (!cameraSelected) ImGui::PopStyleColor();
    }
    for (Scene::NodeID childId: node->children) {
      renderNode(childId, level + 1);
    }
    ImGui::TreePop();
  }

  ImGui::PopID();
}

}
