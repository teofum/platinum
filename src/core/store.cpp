#include "store.hpp"

#include <loaders/gltf.hpp>
#include <utils/utils.hpp>

namespace pt {

Store::Store() noexcept {
  m_scene = std::make_unique<Scene>();
}

Store::~Store() {
  m_device->release();
}

void Store::importGltf() {
  const auto gltfPath = utils::fileOpen("../assets", "gltf,glb");
  if (gltfPath) {
    loaders::gltf::GltfLoader gltf(m_device, *m_scene);
    gltf.load(gltfPath.value());
  }
}

}
