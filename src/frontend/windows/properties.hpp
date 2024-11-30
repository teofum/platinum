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
  void renderNodeProperties(Scene::NodeID id);

  void renderMeshProperties(Scene::MeshID id);

  void renderCameraProperties(Scene::CameraID id);
};

}

#endif //PLATINUM_PROPERTIES_HPP
