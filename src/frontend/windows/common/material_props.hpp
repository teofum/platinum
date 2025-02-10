#ifndef PLATINUM_MATERIAL_PROPS_HPP
#define PLATINUM_MATERIAL_PROPS_HPP

#include <imgui.h>

#include <frontend/widgets.hpp>
#include <core/material.hpp>

namespace pt::frontend {

constexpr ImGuiColorEditFlags COLOR_FLAGS =
  ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_NoSidePreview
	| ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_DisplayRGB
	| ImGuiColorEditFlags_DisplayHSV;

void materialProperties(Material* material, std::optional<Scene::AssetID> id);

void materialTextureSelect(const char* label, Material* material, Material::TextureSlot slot);

}

#endif //PLATINUM_MATERIAL_PROPS_HPP
