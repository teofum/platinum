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

void Store::open() {
  auto path = utils::fileOpen("/", "json");
  if (path) {
    m_selectedNodeId = m_nextNodeId = std::nullopt;
    m_scene = std::make_unique<Scene>(path.value(), m_device);
  }
}

void Store::saveAs() {
  auto path = utils::fileSave("/", "json");
  if (path) m_scene->saveToFile(path.value());
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

Scene::Node Store::createPrimitive(std::string_view name, Mesh&& mesh) {
  auto parentId = m_selectedNodeId.value_or(Scene::null);
  auto id = m_scene->createAsset(std::move(mesh), false);

  auto node = m_scene->createNode(name, parentId);
  node.setMesh(id);

  return node;
}

void Store::update() {
  m_selectedNodeId = m_nextNodeId;

  if (m_nodeAction == NodeAction::Remove && m_actionNodeId) {
    m_scene->removeNode(m_actionNodeId.value(), m_removeMode);
    m_selectedNodeId = m_nextNodeId = m_actionNodeId = std::nullopt;
    m_removeMode = Scene::RemoveMode::Recursive;
  }

  clearNodeAction();
}

}
