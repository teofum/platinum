#include "material_props.hpp"

#include <misc/cpp/imgui_stdlib.h>

namespace pt::frontend {

void materialProperties(Scene& scene, Material* material, std::optional<Scene::AssetID> id) {
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
  ImGui::BeginDisabled(!id);
  ImGui::InputText("##MaterialNameInput", &material->name);

  ImGui::SeparatorText("Basic properties");

  widgets::color("Base color", (float*) &material->baseColor, COLOR_FLAGS);

  materialTextureSelect(scene, "Base texture", material, Material::TextureSlot::BaseColor);

  widgets::dragFloat("Roughness", &material->roughness, 0.01f, 0.0f, 1.0f);
  widgets::dragFloat("Metallic", &material->metallic, 0.01f, 0.0f, 1.0f);
  widgets::dragFloat("Transmission", &material->transmission, 0.01f, 0.0f, 1.0f);
  widgets::dragFloat("IOR", &material->ior, 0.01f, 0.1f, 5.0f);

  materialTextureSelect(scene, "R/M texture", material, Material::TextureSlot::RoughnessMetallic);
  materialTextureSelect(scene, "Trm. texture", material, Material::TextureSlot::Transmission);

  float alpha = material->baseColor[3];
  if (widgets::dragFloat("Alpha", &alpha, 0.01f, 0.0f, 1.0f)) {
    material->baseColor[3] = alpha;
  }

  materialTextureSelect(scene, "Normal map", material, Material::TextureSlot::Normal);

  ImGui::SeparatorText("Emission");

  widgets::color("Color", (float*) &material->emission, COLOR_FLAGS);
  widgets::dragFloat("Strength", &material->emissionStrength, 0.1f);

  materialTextureSelect(scene, "Texture##EmissionTexture", material, Material::TextureSlot::Emission);

  ImGui::SeparatorText("Clearcoat");

  widgets::dragFloat("Value", &material->clearcoat, 0.01f, 0.0f, 1.0f);
  widgets::dragFloat("Roughness##CoatRoughness", &material->clearcoatRoughness, 0.01f, 0.0f, 1.0f);

  materialTextureSelect(scene, "Texture##CoatTexture", material, Material::TextureSlot::Clearcoat);

  ImGui::SeparatorText("Anisotropy");

  widgets::dragFloat("Anisotropy", &material->anisotropy, 0.01f, 0.0f, 1.0f);
  widgets::dragFloat("Rotation", &material->anisotropyRotation, 0.01f, 0.0f, 1.0f);

  ImGui::SeparatorText("Additional properties");

  ImGui::Checkbox("Thin transmission", &material->thinTransmission);
  ImGui::SameLine();
  ImGui::TextDisabled("[?]");
  if (ImGui::BeginItemTooltip()) {
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
    ImGui::TextUnformatted(
      "Render the surface as a thin sheet, rather than the boundary "
      "of a solid object. Disables refraction for a transmissive material."
    );
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
  ImGui::EndDisabled();
}

void materialTextureSelect(Scene& scene, const char* label, Material* material, Material::TextureSlot slot) {
  scene.updateMaterialTexture(material, slot, widgets::textureSelect(scene, label, material->getTexture(slot)));
}

}
