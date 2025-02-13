#include "store.hpp"

#include <loaders/gltf.hpp>
#include <utils/utils.hpp>

namespace pt {

Store::Store() noexcept {
  m_scene = std::make_unique<Scene>();
}

Store::~Store() {
  m_device->release();
  m_commandQueue->release();
}

void Store::open(const fs::path& path) {
  m_scene = std::make_unique<Scene>(path, m_device);
}

void Store::saveAs(const fs::path& path) {
  m_scene->saveToFile(path);
}

void Store::importGltf() {
  const auto gltfPath = utils::fileOpen("/", "gltf,glb");
  if (gltfPath) {
    loaders::gltf::GltfLoader gltf(m_device, m_commandQueue, *m_scene);
    gltf.load(gltfPath.value());
  }
}

void Store::importTexture(loaders::texture::TextureType type) {
  const auto extensions = type == loaders::texture::TextureType::HDR ? "hdr,exr" : "png,jpg,jpeg";
  const auto texturePath = utils::fileOpen("/", extensions);
  if (texturePath) {
    loaders::texture::TextureLoader loader(m_device, m_commandQueue, *m_scene);
    loader.loadFromFile(texturePath.value(), texturePath->stem().string(), type);
  }
}

}
