#ifndef PLATINUM_MATERIAL_PROPS_HPP
#define PLATINUM_MATERIAL_PROPS_HPP

#include <imgui.h>

#include <frontend/widgets.hpp>
#include <core/material.hpp>

namespace pt::frontend {

void materialProperties(Scene& scene, Material* material, std::optional<Scene::AssetID> id);

void materialTextureSelect(Scene& scene, const char* label, Material* material, Material::TextureSlot slot);

}

#endif //PLATINUM_MATERIAL_PROPS_HPP
