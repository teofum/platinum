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
  Scene::NodeID m_lastNodeId = 0;
  static constexpr const ImGuiColorEditFlags m_colorFlags =
  	ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_NoSidePreview
  | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_DisplayRGB
  | ImGuiColorEditFlags_DisplayHSV;
  
  void renderNodeProperties(Scene::NodeID id);

  void renderMeshProperties(Scene::MeshID id);

  void renderCameraProperties(Scene::CameraID id);
  
  void renderMaterialProperties(Scene::MaterialID id);
  
  void renderTextureProperties(Scene::TextureID id);
  
  void renderSceneProperties();
  
  std::optional<Scene::TextureID> textureSelect(const char* label, std::optional<Scene::TextureID> selectedId);
};

}

#endif //PLATINUM_PROPERTIES_HPP
