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
  } else if (m_state.selectedMaterial()) {
    renderMaterialProperties(m_state.selectedMaterial().value());
  } else {
    ImGui::Text("[ Nothing selected ]");
  }

  ImGui::End();
}

void Properties::renderNodeProperties(Scene::NodeID id) {
  Scene::Node* node = m_store.scene().node(id);

  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
  ImGui::BeginDisabled(id == 0);
  ImGui::InputText("##NameInput", &node->name);
  ImGui::EndDisabled();

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
  
  if (!node->materials.empty()) {
    if (ImGui::CollapsingHeader("Materials")) {
      auto selectedId = node->materials[m_selectedMaterialIdx];
      auto& selectedName = m_store.scene().materialName(selectedId);
      
      /*
       * Material slot selection
       */
     	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {8, 6});
      auto open = ImGui::BeginListBox("##SlotSelect", {0, 5 * ImGui::GetTextLineHeightWithSpacing()});
      ImGui::PopStyleVar(2);
      if (open) {
        for (uint32_t i = 0; i < node->materials.size(); i++) {
          auto isSelected = i == m_selectedMaterialIdx;
          auto& name = m_store.scene().materialName(node->materials[i]);
          auto label = std::format("[{}]: {}", i, name);
          
          if (widgets::selectable(label.c_str(), isSelected)) {
            m_selectedMaterialIdx = i;
          }
        }
        ImGui::EndListBox();
      }
      
      /*
       * Material selection: change the material in selected slot
       */
      auto nextMaterialId = selectedId;
      ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
      if (ImGui::BeginCombo("##MaterialSelect", selectedName.c_str())) {
        float width = ImGui::GetContentRegionAvail().x - 12.0f;
        for (const auto& md: m_store.scene().getAllMaterials()) {
          auto isSelected = selectedId == md.materialId;
          
          ImGui::SetCursorPosX(10.0f);
          if (widgets::selectable(md.name.c_str(), isSelected, 0, {width, 0})) {
            nextMaterialId = md.materialId;
          }
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        ImGui::SetCursorPosX(10.0f);
        if (widgets::selectable("New material", false, 0, {width, 0})) {
          auto name = std::format("Material {}", m_store.scene().getAllMaterials().size() + 1);
          nextMaterialId = m_store.scene().addMaterial(name, Material{ .baseColor = {0.8, 0.8, 0.8} });
        }
        
        ImGui::EndCombo();
      }
      
      renderMaterialProperties(selectedId);
      ImGui::Spacing();
      
      // Update material ID
      node->materials[m_selectedMaterialIdx] = nextMaterialId;
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

void Properties::renderMaterialProperties(Scene::MaterialID id) {
  auto material = m_store.scene().material(id);
  auto& name = m_store.scene().materialName(id);
  
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
  ImGui::InputText("##MaterialNameInput", &name);
  
  ImGui::AlignTextToFramePadding();
  ImGui::Text("Material [id: %u]", id);
  
  ImGui::SeparatorText("Basic properties");
  
  auto buttonWidth = ImGui::CalcItemWidth();
  ImGui::ColorEdit3("Base color", (float*) &material->baseColor, m_colorFlags, {buttonWidth, 0});
  
  ImGui::SliderFloat("Roughness", &material->roughness, 0.0f, 1.0f);
  ImGui::SliderFloat("Metallic", &material->metallic, 0.0f, 1.0f);
  ImGui::SliderFloat("Transmission", &material->transmission, 0.0f, 1.0f);
  ImGui::SliderFloat("IOR", &material->ior, 0.1f, 5.0f);
  
  float alpha = material->baseColor[3];
  if (ImGui::SliderFloat("Alpha", &alpha, 0.0f, 1.0f)) {
    material->baseColor[3] = alpha;
    if (alpha > 0.0) {
      material->flags |= Material::Material_UseAlpha;
    } else {
      material->flags &= ~Material::Material_UseAlpha;
    }
  }
  
  ImGui::SeparatorText("Emission");
  
  auto emissionChanged = false;
  emissionChanged |= ImGui::ColorEdit3("Color", (float*) &material->emission, m_colorFlags, {buttonWidth, 0});
  emissionChanged |= ImGui::SliderFloat("Strength", &material->emissionStrength, 0.0f, 1.0f);
  if (emissionChanged) {
    if (length_squared(material->emission) > 0.0f && material->emissionStrength > 0.0f) {
      material->flags |= Material::Material_Emissive;
    } else {
      material->flags &= ~Material::Material_Emissive;
    }
  }
  
  ImGui::SeparatorText("Clearcoat");
  
  ImGui::SliderFloat("Value", &material->clearcoat, 0.0f, 1.0f);
  ImGui::SliderFloat("Roughness##CoatRoughness", &material->clearcoatRoughness, 0.0f, 1.0f);
  
  ImGui::SeparatorText("Anisotropy");
  
  if (ImGui::SliderFloat("Anisotropy", &material->anisotropy, 0.0f, 1.0f)) {
    if (material->anisotropy > 0.0f) {
      material->flags |= Material::Material_Anisotropic;
    } else {
      material->flags &= ~Material::Material_Anisotropic;
    }
  }
  ImGui::SliderFloat("Rotation", &material->anisotropyRotation, 0.0f, 1.0f);
  
  ImGui::SeparatorText("Additional properties");
  
  ImGui::CheckboxFlags("Thin transmission", &material->flags, Material::Material_ThinDielectric);
  ImGui::SameLine();
  ImGui::TextDisabled("[?]");
  if (ImGui::BeginItemTooltip()) {
      ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
      ImGui::TextUnformatted("Render the surface as a thin sheet, rather than the boundary "
                             "of a solid object. Disables refraction for a transmissive material.");
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
  }
}

}
