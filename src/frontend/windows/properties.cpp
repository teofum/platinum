#include "properties.hpp"

#include <misc/cpp/imgui_stdlib.h>

namespace pt::frontend::windows {

void Properties::render() {
  ImGui::Begin("Properties");
  if (m_state.selectedNode()) {
    renderNodeProperties(m_state.selectedNode().value());
  } else if (m_state.selectedMesh()) {
    renderMeshProperties(m_state.selectedMesh().value());
  } else if (m_state.selectedCamera()) {
    renderCameraProperties(m_state.selectedCamera().value());
  } else {
    ImGui::Text("[ Nothing selected ]");
  }

  ImGui::End();
}

void Properties::renderNodeProperties(Scene::NodeID id) {
  Scene::Node* node = m_store.scene().node(id);

  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
  if (id == 0) ImGui::BeginDisabled();
  ImGui::InputText("##NameInput", &node->name);
  if (id == 0) ImGui::EndDisabled();

  ImGui::AlignTextToFramePadding();
  ImGui::Text("Node [id: %u]", id);

  if (id != 0) {
    float buttonWidth = 60.0f;
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - buttonWidth + ImGui::GetStyle().ItemSpacing.x);

    if (widgets::buttonDanger("Remove", {buttonWidth, 0}) ||
        (ImGui::IsKeyPressed(ImGuiKey_Backspace, false) && !ImGui::IsAnyItemActive())) {
      if (!node->children.empty()) ImGui::OpenPopup("Remove_Popup");
      else m_state.removeNode(id);
    }
    if (!node->children.empty()) {
      widgets::removeNodePopup(m_state, id);
    }
  }

  ImGui::Spacing();

  if (ImGui::CollapsingHeader("View properties", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::CheckboxFlags("Visible", &node->flags, Scene::NodeFlags_Visible);
    ImGui::Spacing();
  }

  if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
    widgets::transformEditor(node->transform);
    ImGui::Spacing();
  }

  if (node->meshId) {
    if (ImGui::CollapsingHeader("Mesh")) {
      renderMeshProperties(node->meshId.value());
      ImGui::Spacing();
    }
  }

  if (node->cameraId) {
    if (ImGui::CollapsingHeader("Camera")) {
      renderCameraProperties(node->cameraId.value());
      ImGui::Spacing();
    }
  }
}

void Properties::renderMeshProperties(Scene::MeshID id) {
  Mesh* mesh = m_store.scene().mesh(id);

  ImGui::AlignTextToFramePadding();
  ImGui::Text("Mesh [id: %u]", id);

  auto users = std::format("{} users", m_store.scene().meshUsers(id));
  auto availableWidth = ImGui::GetContentRegionAvail().x;
  ImGui::SameLine(availableWidth - ImGui::CalcTextSize(users.c_str()).x);
  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s", users.c_str());

  ImGui::Spacing();

  ImGui::Text("%lu vertices", mesh->vertexCount());
  ImGui::Text("%lu triangles", mesh->indexCount() / 3);
}

void Properties::renderCameraProperties(Scene::CameraID id) {
  Camera* camera = m_store.scene().camera(id);

  ImGui::AlignTextToFramePadding();
  ImGui::Text("Camera [id: %u]", id);

  auto users = std::format(
    "{} users",
    m_store.scene().cameraUsers(id)
  );
  auto availableWidth = ImGui::GetContentRegionAvail().x;
  ImGui::SameLine(availableWidth - ImGui::CalcTextSize(users.c_str()).x);
  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s", users.c_str());

  ImGui::Spacing();

  ImGui::DragFloat("Focal length", &camera->focalLength, 1.0f, 5.0f, 1200.0f, "%.1fmm");
  ImGui::DragFloat2("Sensor size", (float*) &camera->sensorSize, 1.0f, 0.0f, 100.0f, "%.1fmm");
  ImGui::DragFloat("Aperture", &camera->aperture, 0.1f, 0.0f, 32.0f, "f/%.1f");
  ImGui::Spacing();

  ImGui::SeparatorText("Presets");
  auto buttonWidth = widgets::getWidthForItems(3);
  if (ImGui::Button("Micro 4/3", {buttonWidth, 0})) camera->sensorSize = float2{18.0f, 13.5f};
  ImGui::SameLine();
  if (ImGui::Button("APS-C", {buttonWidth, 0})) camera->sensorSize = float2{23.5f, 15.6f};
  ImGui::SameLine();
  if (ImGui::Button("35mm FF", {buttonWidth, 0})) camera->sensorSize = float2{36.0f, 24.0f};
  ImGui::SameLine();
}

}
