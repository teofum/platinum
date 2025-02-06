#ifndef PLATINUM_PROPERTIES_HPP
#define PLATINUM_PROPERTIES_HPP

#include <imgui.h>

#include <frontend/window.hpp>
#include <frontend/widgets.hpp>

namespace pt::frontend::windows {

class Properties final : Window {
public:
  constexpr Properties(Store& store, State& state, bool* open = nullptr) noexcept
    : Window(store, state, open) {
  }

  void render() final;

private:
  uint32_t m_selectedMaterialIdx = 0;
  Scene::NodeID m_lastNodeId;
  static constexpr const ImGuiColorEditFlags m_colorFlags =
  	ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_NoSidePreview
  | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_DisplayRGB
  | ImGuiColorEditFlags_DisplayHSV;
  
  void renderNodeProperties(Scene::NodeID id);

  void renderMeshProperties(const Scene::AssetData<Mesh>& mesh);

  void renderCameraProperties(Camera* camera);

  void renderMaterialProperties(Material* material, std::optional<Scene::AssetID> id);
//
//  void renderTextureProperties(Scene::TextureID id);
//  
  std::optional<Scene::AssetID> textureSelect(const char* label, std::optional<Scene::AssetID> selectedId);
  
  void materialTextureSelect(const char* label, Material* material, Material::TextureSlot slot);
};

}

#endif //PLATINUM_PROPERTIES_HPP
