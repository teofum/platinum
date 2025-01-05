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
  } else if (m_state.selectedTexture()) {
    renderTextureProperties(m_state.selectedTexture().value());
  } else {
    ImGui::Text("Scene");
    renderSceneProperties();
  }

  ImGui::End();
}

void Properties::renderNodeProperties(Scene::NodeID id) {
  Scene::Node* node = m_store.scene().node(id);
  
  if (id != m_lastNodeId) m_selectedMaterialIdx = 0;
  m_lastNodeId = id;

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
  
  if (id == 0) {
    if (ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen)) {
      renderSceneProperties();
      ImGui::Spacing();
    }
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
      auto nextSlotId = m_selectedMaterialIdx;
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
            nextSlotId = i;
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
        for (const auto& md: m_store.scene().getAllMaterials()) {
          auto isSelected = selectedId == md.materialId;
          if (widgets::comboItem(md.name.c_str(), isSelected))
            nextMaterialId = md.materialId;
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        if (widgets::comboItem("New material", false)) {
          auto name = std::format("Material {}", m_store.scene().getAllMaterials().size() + 1);
          nextMaterialId = m_store.scene().addMaterial(name, Material{ .baseColor = {0.8, 0.8, 0.8} });
        }
        
        ImGui::EndCombo();
      }
      
      renderMaterialProperties(selectedId);
      ImGui::Spacing();
      
      // Update material ID
      node->materials[m_selectedMaterialIdx] = nextMaterialId;
      m_selectedMaterialIdx = nextSlotId;
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
  
  bool mustRecalculateFlags = false;
  
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
  ImGui::InputText("##MaterialNameInput", &name);
  
  ImGui::AlignTextToFramePadding();
  ImGui::Text("Material [id: %u]", id);
  
  ImGui::SeparatorText("Basic properties");
  
  auto buttonWidth = ImGui::CalcItemWidth();
  ImGui::ColorEdit3("Base color", (float*) &material->baseColor, m_colorFlags, {buttonWidth, 0});
  
  auto baseTexture = textureSelect("Base texture", material->baseTextureId);
  material->baseTextureId = baseTexture.value_or(-1);
  
  ImGui::DragFloat("Roughness", &material->roughness, 0.01f, 0.0f, 1.0f);
  ImGui::DragFloat("Metallic", &material->metallic, 0.01f, 0.0f, 1.0f);
  ImGui::DragFloat("Transmission", &material->transmission, 0.01f, 0.0f, 1.0f);
  ImGui::DragFloat("IOR", &material->ior, 0.01f, 0.1f, 5.0f);
  
  auto rmTexture = textureSelect("R/M texture", material->rmTextureId);
  material->rmTextureId = rmTexture.value_or(-1);
  auto transmissionTexture = textureSelect("Trm. texture", material->transmissionTextureId);
  material->transmissionTextureId = transmissionTexture.value_or(-1);
    
  float alpha = material->baseColor[3];
  if (ImGui::DragFloat("Alpha", &alpha, 0.01f, 0.0f, 1.0f)) {
    material->baseColor[3] = alpha;
    mustRecalculateFlags = true;
  }
  
  ImGui::SeparatorText("Emission");
  
  mustRecalculateFlags |= ImGui::ColorEdit3("Color", (float*) &material->emission, m_colorFlags, {buttonWidth, 0});
  mustRecalculateFlags |= ImGui::DragFloat("Strength", &material->emissionStrength, 0.1f);
  
  auto emissionTexture = textureSelect("Texture##EmissionTexture", material->emissionTextureId);
  material->emissionTextureId = emissionTexture.value_or(-1);
  
  ImGui::SeparatorText("Clearcoat");
  
  ImGui::DragFloat("Value", &material->clearcoat, 0.01f, 0.0f, 1.0f);
  ImGui::DragFloat("Roughness##CoatRoughness", &material->clearcoatRoughness, 0.01f, 0.0f, 1.0f);
  
  auto clearcoatTexture = textureSelect("Texture##CoatTexture", material->clearcoatTextureId);
  material->clearcoatTextureId = clearcoatTexture.value_or(-1);
  
  ImGui::SeparatorText("Anisotropy");
  
  mustRecalculateFlags |= ImGui::DragFloat("Anisotropy", &material->anisotropy, 0.01f, 0.0f, 1.0f);
  ImGui::DragFloat("Rotation", &material->anisotropyRotation, 0.01f, 0.0f, 1.0f);
  
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
  
  // Recalculate material flags in case properties changed
  if (mustRecalculateFlags) m_store.scene().recalculateMaterialFlags(id);
}

void Properties::renderTextureProperties(Scene::TextureID id) {
  MTL::Texture* texture = m_store.scene().texture(id);

  ImGui::AlignTextToFramePadding();
  ImGui::Text("Texture [id: %u]", id);
  
  ImGui::Spacing();

  ImGui::Text("%lux%lu", texture->width(), texture->height());
  
  ImGui::Separator();
  
  const float width = ImGui::GetContentRegionAvail().x;
  ImGui::PushStyleColor(ImGuiCol_ChildBg, (ImVec4) ImColor::HSV(0.0f, 0.0f, 0.8f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
  ImGui::BeginChild(
    "TextureView",
    {width, width * texture->height() / texture->width()},
    ImGuiChildFlags_Borders,
    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
  );
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  
  ImGui::Image(
    (ImTextureID) texture,
    {width, width * texture->height() / texture->width()}
  );

  ImGui::EndChild();
}

void Properties::renderSceneProperties() {
  ImGui::SeparatorText("Environment Map");
  
  m_store.scene().environmentTexture = textureSelect("Image##EnvmapImage", m_store.scene().environmentTexture);
  
  ImGui::DragFloat(
    "Rotation##EnvmapRotation",
		&m_store.scene().environmentRotation,
    0.005f,
    0.0f,
    2.0f * std::numbers::pi,
    "%.3f",
    ImGuiSliderFlags_WrapAround
  );
}

std::optional<Scene::TextureID> Properties::textureSelect(const char* label, std::optional<Scene::TextureID> selectedId) {
  auto newId = selectedId;
  auto selectedName = selectedId ? m_store.scene().textureName(selectedId.value()) : "No texture";
  if (selectedName.empty()) selectedName = std::format("Texture [{}]", selectedId.value());
  
  if (ImGui::BeginCombo(label, selectedName.c_str())) {
    if (widgets::comboItem("No texture", selectedId == std::nullopt))
      newId = std::nullopt;
    
    auto allTextures = m_store.scene().getAllTextures();
    if (!allTextures.empty()){
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();
    }
    
    for (const auto& td: allTextures) {
      auto isSelected = selectedId == td.textureId;
      auto name = td.name.empty() ? std::format("Texture [{}]", td.textureId) : td.name;
      if (widgets::comboItem(name.c_str(), isSelected))
        newId = td.textureId;
    }
    
    ImGui::EndCombo();
  }
  
  return newId;
}

}
