#include "material_props.hpp"

#include <misc/cpp/imgui_stdlib.h>

namespace pt::frontend {

void materialProperties(Material* material, std::optional<Scene::AssetID> id) {
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
  ImGui::ColorEdit3("Base color", (float*) &material->baseColor, COLOR_FLAGS, {buttonWidth, 0});
  
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
  
  ImGui::ColorEdit3("Color", (float*) &material->emission, COLOR_FLAGS, {buttonWidth, 0});
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

void materialTextureSelect(const char* label, Material* material, Material::TextureSlot slot) {
  
}

}
