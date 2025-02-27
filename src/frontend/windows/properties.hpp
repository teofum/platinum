#ifndef PLATINUM_PROPERTIES_HPP
#define PLATINUM_PROPERTIES_HPP

#include <imgui.h>

#include <frontend/window.hpp>
#include <frontend/widgets.hpp>

namespace pt::frontend::windows {

class Properties final : Window {
public:
  constexpr Properties(Store& store, bool* open = nullptr) noexcept
    : Window(store, open) {
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
};

}

#endif //PLATINUM_PROPERTIES_HPP
