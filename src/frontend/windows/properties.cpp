#include "properties.hpp"

#include <misc/cpp/imgui_stdlib.h>

namespace pt::frontend::windows {

static const char* getTextureFormatName(MTL::Texture* texture) {
  switch (texture->pixelFormat()) {
    case MTL::PixelFormatRGBA8Unorm:
      return "Linear RGBA 8bpc";
    case MTL::PixelFormatRGBA8Unorm_sRGB:
      return "sRGB RGBA 8bpc";
    case MTL::PixelFormatRG8Unorm:
      return "Roughness/Metallic (RG 8bpc)";
    case MTL::PixelFormatR8Unorm:
      return "Single channel";
    case MTL::PixelFormatRGBA32Float:
      return "HDR (RGBA 32bpc)";
    default:
      return "Unknown format";
  }
}

void Properties::render() {
  ImGui::Begin("Properties");
  if (m_state.selectedNode()) {
    renderNodeProperties(m_state.selectedNode().value());
  } else {
    ImGui::Text("[ Nothing selected ]");
  }

  ImGui::End();
}

void Properties::renderNodeProperties(Scene::NodeID id) {
  auto node = m_store.scene().node(id);
  
  if (id != m_lastNodeId) m_selectedMaterialIdx = 0;
  m_lastNodeId = id;

  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
  ImGui::BeginDisabled(node.isRoot());
  ImGui::InputText("##NameInput", &node.name());
  ImGui::EndDisabled();

  ImGui::AlignTextToFramePadding();
  ImGui::Text("Node [id: %u]", id);

//  if (id != 0) {
//    float buttonWidth = 60.0f;
//    ImGui::SameLine(ImGui::GetContentRegionAvail().x - buttonWidth + ImGui::GetStyle().ItemSpacing.x);
//
//    if (widgets::buttonDanger("Remove", {buttonWidth, 0}) ||
//        (ImGui::IsKeyPressed(ImGuiKey_Backspace, false) && !ImGui::IsAnyItemActive())) {
//      if (!node->children.empty()) ImGui::OpenPopup("Remove_Popup");
//      else m_state.removeNode(id);
//    }
//    if (!node->children.empty()) {
//      widgets::removeNodePopup(m_state, id);
//    }
//  }

  ImGui::Spacing();

  if (ImGui::CollapsingHeader("View properties", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox("Visible", &node.visible());
    ImGui::Spacing();
  }

  if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
    widgets::transformEditor(node.transform());
    ImGui::Spacing();
  }
  
  if (node.isRoot()) {
    if (ImGui::CollapsingHeader("Scene")) {
      auto currentValue = m_store.scene().envmap().textureId();
      auto selection = textureSelect("Environment", currentValue);
      
      if (selection && selection != currentValue) {
        m_store.scene().envmap().setTexture(
          selection,
          m_store.device(),
					m_store.scene().getAsset<Texture>(selection.value())->texture()
        );
      }
      
      ImGui::Spacing();
    }
  }

  if (node.mesh()) {
    if (ImGui::CollapsingHeader("Mesh")) {
      renderMeshProperties(node.mesh().value());
      ImGui::Spacing();
    }
  }

  auto camera = node.get<Camera>();
  if (camera) {
    if (ImGui::CollapsingHeader("Camera")) {
      renderCameraProperties(camera.value());
      ImGui::Spacing();
    }
  }
  
  auto materialIds = node.materialIds();
  if (materialIds && !materialIds.value()->empty()) {
    if (ImGui::CollapsingHeader("Materials")) {
      auto selected = node.material(m_selectedMaterialIdx);
      auto materials = *materialIds.value();
      
      /*
       * Material slot selection
       */
      auto nextSlotId = m_selectedMaterialIdx;
      
     	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {8, 6});
      auto open = ImGui::BeginListBox("##SlotSelect", {0, 5 * ImGui::GetTextLineHeightWithSpacing()});
      ImGui::PopStyleVar(2);
      
      if (open) {
        for (uint32_t i = 0; i < materials.size(); i++) {
          auto* material = node.material(i)
            .transform([](auto material){ return material.asset; })
            .value_or(&m_store.scene().defaultMaterial());
          
          auto isSelected = i == m_selectedMaterialIdx;
          auto label = std::format("[{}]: {}", i, material->name);
          
          if (widgets::selectable(label.c_str(), isSelected)) {
            nextSlotId = i;
          }
        }
        ImGui::EndListBox();
      }
      
      /*
       * Material selection: change the material in selected slot
       */
      auto selectedId = selected.transform([](auto s){ return s.id; });
      auto* selectedMaterial = selected
        .transform([](auto material){ return material.asset; })
        .value_or(&m_store.scene().defaultMaterial());
      
      auto nextMaterialId = selectedId;
      ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
      if (ImGui::BeginCombo("##MaterialSelect", selectedMaterial->name.c_str())) {
        for (const auto& md: m_store.scene().getAll<Material>()) {
          auto isSelected = selectedId == md.id;
          if (widgets::comboItem(md.asset->name.c_str(), isSelected))
            nextMaterialId = md.id;
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        if (widgets::comboItem("New material", false)) {
          auto name = std::format("Material {}", m_store.scene().getAll<Material>().size() + 1);
          nextMaterialId = m_store.scene().createAsset(Material { .name = name });
        }
        
        ImGui::EndCombo();
      }
      
      renderMaterialProperties(selectedMaterial, selectedId);
      ImGui::Spacing();
      
      // Update material ID
      node.setMaterial(m_selectedMaterialIdx, nextMaterialId);
      m_selectedMaterialIdx = nextSlotId;
    }
  }
}

void Properties::renderMeshProperties(const Scene::AssetData<Mesh>& mesh) {
  ImGui::AlignTextToFramePadding();
  ImGui::Text("Mesh [id: %llu]", mesh.id);

  auto users = std::format("{} users", m_store.scene().getAssetRc(mesh.id));
  auto availableWidth = ImGui::GetContentRegionAvail().x;
  ImGui::SameLine(availableWidth - ImGui::CalcTextSize(users.c_str()).x);
  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s", users.c_str());

  ImGui::Spacing();

  ImGui::Text("%lu vertices", mesh.asset->vertexCount());
  ImGui::Text("%lu triangles", mesh.asset->indexCount() / 3);
}

void Properties::renderCameraProperties(Camera* camera) {
  ImGui::DragFloat("Focal length", &camera->focalLength, 1.0f, 5.0f, 1200.0f, "%.1fmm");
  ImGui::DragFloat2("Sensor size", (float*) &camera->sensorSize, 1.0f, 0.0f, 100.0f, "%.1fmm");
  ImGui::DragFloat("Aperture", &camera->aperture, 0.1f, 0.0f, 32.0f, "f/%.1f");
  ImGui::Spacing();

  ImGui::SeparatorText("Presets");
  auto buttonWidth = widgets::getWidthForItems(3);
  if (widgets::button("Micro 4/3", {buttonWidth, 0})) camera->sensorSize = float2{18.0f, 13.5f};
  ImGui::SameLine();
  if (widgets::button("APS-C", {buttonWidth, 0})) camera->sensorSize = float2{23.5f, 15.6f};
  ImGui::SameLine();
  if (widgets::button("35mm FF", {buttonWidth, 0})) camera->sensorSize = float2{36.0f, 24.0f};
  ImGui::SameLine();
}

void Properties::renderMaterialProperties(Material* material, std::optional<Scene::AssetID> id) {
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
  ImGui::InputText("##MaterialNameInput", &material->name);
  
  ImGui::AlignTextToFramePadding();
  if (id) {
    ImGui::Text("Material [id: %llu]", id.value());
  } else {
    ImGui::Text("Material [default]");
  }
  
  ImGui::SeparatorText("Basic properties");
  
  auto buttonWidth = ImGui::CalcItemWidth();
  ImGui::ColorEdit3("Base color", (float*) &material->baseColor, m_colorFlags, {buttonWidth, 0});
  
  materialTextureSelect("Base texture", material, Material::TextureSlot::BaseColor);
  
  ImGui::DragFloat("Roughness", &material->roughness, 0.01f, 0.0f, 1.0f);
  ImGui::DragFloat("Metallic", &material->metallic, 0.01f, 0.0f, 1.0f);
  ImGui::DragFloat("Transmission", &material->transmission, 0.01f, 0.0f, 1.0f);
  ImGui::DragFloat("IOR", &material->ior, 0.01f, 0.1f, 5.0f);
  
  materialTextureSelect("R/M texture", material, Material::TextureSlot::RoughnessMetallic);
  materialTextureSelect("Trm. texture", material, Material::TextureSlot::Transmission);
    
  float alpha = material->baseColor[3];
  if (ImGui::DragFloat("Alpha", &alpha, 0.01f, 0.0f, 1.0f)) {
    material->baseColor[3] = alpha;
  }
  
  materialTextureSelect("Normal map", material, Material::TextureSlot::Normal);
  
  ImGui::SeparatorText("Emission");
  
  ImGui::ColorEdit3("Color", (float*) &material->emission, m_colorFlags, {buttonWidth, 0});
  ImGui::DragFloat("Strength", &material->emissionStrength, 0.1f);
  
  materialTextureSelect("Texture##EmissionTexture", material, Material::TextureSlot::Emission);
  
  ImGui::SeparatorText("Clearcoat");
  
  ImGui::DragFloat("Value", &material->clearcoat, 0.01f, 0.0f, 1.0f);
  ImGui::DragFloat("Roughness##CoatRoughness", &material->clearcoatRoughness, 0.01f, 0.0f, 1.0f);
  
  materialTextureSelect("Texture##CoatTexture", material, Material::TextureSlot::Clearcoat);
  
  ImGui::SeparatorText("Anisotropy");
  
  ImGui::DragFloat("Anisotropy", &material->anisotropy, 0.01f, 0.0f, 1.0f);
  ImGui::DragFloat("Rotation", &material->anisotropyRotation, 0.01f, 0.0f, 1.0f);
  
  ImGui::SeparatorText("Additional properties");
  
  ImGui::Checkbox("Thin transmission", &material->thinTransmission);
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

//void Properties::renderTextureProperties(Scene::TextureID id) {
//  MTL::Texture* texture = m_store.scene().texture(id);
//
//  ImGui::AlignTextToFramePadding();
//  ImGui::Text("Texture [id: %u]", id);
//  
//  ImGui::Spacing();
//
//  ImGui::Text("%s", getTextureFormatName(texture));
//  ImGui::SameLine(ImGui::GetContentRegionAvail().x - 80.0);
//  ImGui::Text("%lux%lu", texture->width(), texture->height());
//  
//  ImGui::Separator();
//  
//  const float width = ImGui::GetContentRegionAvail().x;
//  ImGui::PushStyleColor(ImGuiCol_ChildBg, (ImVec4) ImColor::HSV(0.0f, 0.0f, 0.8f));
//  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
//  ImGui::BeginChild(
//    "TextureView",
//    {width, width * texture->height() / texture->width()},
//    ImGuiChildFlags_Borders,
//    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
//  );
//  ImGui::PopStyleVar();
//  ImGui::PopStyleColor();
//  
//  ImGui::Image(
//    (ImTextureID) texture,
//    {width, width * texture->height() / texture->width()}
//  );
//
//  ImGui::EndChild();
//}

std::optional<Scene::AssetID> Properties::textureSelect(const char* label, std::optional<Scene::AssetID> selectedId) {
  auto newId = selectedId;
  
  auto selectedName = selectedId
    .transform([&](auto id){ return m_store.scene().getAsset<Texture>(id)->name(); })
    .value_or("No texture");
  if (selectedName.empty()) selectedName = std::format("Texture [{}]", selectedId.value());
  
  if (ImGui::BeginCombo(label, selectedName.data())) {
    if (widgets::comboItem("No texture", false)) {
      newId = std::nullopt;
    }
    
    auto textures = m_store.scene().getAll<Texture>();
    if (!textures.empty()){
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();
    }
    
    for (const auto& td: textures) {
      auto isSelected = selectedId == td.id;
      auto name = td.asset->name();
      if (name.empty()) name = std::format("Texture [{}]", td.id);
      
      if (widgets::comboItem(name.data(), isSelected)) {
        newId = td.id;
      }
    }
    
    ImGui::EndCombo();
  }
  
  return newId;
}

void Properties::materialTextureSelect(const char *label, Material *material, Material::TextureSlot slot) {
  m_store.scene().updateMaterialTexture(material, slot, textureSelect(label, material->getTexture(slot)));
}

}
