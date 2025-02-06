#include "scene.hpp"

namespace pt {

Scene::Scene() noexcept
: m_nextAssetId(0), m_assets() {
  /*
   * Initialize the scene
   */
  m_root = m_registry.create();
  m_registry.emplace<Transform>(m_root);
  m_registry.emplace<Hierarchy>(m_root, "Scene", entt::null);
}

void Scene::removeAsset(AssetID id) {
  // Any cleanup specific to manually deleting assets should be done here
  
  removeAssetImpl(id);
}

uint32_t Scene::getAssetRc(AssetID id) {
  return m_assetRc.at(id);
}

bool& Scene::assetRetained(AssetID id) {
  return m_assets.at(id).retain;
}

void Scene::updateMaterialTexture(Material *material, Material::TextureSlot slot, std::optional<AssetID> textureId) {
  if (material->textures.contains(slot)) {
    auto currentId = material->textures.at(slot);
    if (textureId == currentId) return;
    
    releaseAsset(currentId);
  }
  
  if (textureId) {
    retainAsset(textureId.value());
    material->textures[slot] = textureId.value();
  } else {
    material->textures.erase(slot);
  }
}

/*
 * Asset management (internal)
 */
void Scene::retainAsset(AssetID id) {
  m_assetRc[id]++;
}

bool Scene::releaseAsset(AssetID id) {
  uint32_t rc = --m_assetRc[id];
  bool remove = rc == 0 && !m_assets[id].retain;
  
  // Because the refcount is 0 we know there are no dependencies, so we can safely remove the asset.
  if (remove) removeAssetImpl(id);
  return remove;
}

void Scene::removeAssetImpl(AssetID id) {
  // If the asset is a material, it may hold references to other assets (textures) which we need
  // to release
  auto* pMaterial = std::get_if<std::unique_ptr<Material>>(&m_assets[id].asset);
  if (pMaterial) {
    auto& mat = *(pMaterial->get());
    for (const auto& [slot, textureId]: mat.textures) {
      releaseAsset(textureId);
    }
  }
  
  // Remove the asset and force reset refcount to 0. A missing asset with RC 0 is interpreted as
  // an invalid ID.
  m_assets.erase(id);
  m_assetRc[id] = 0;
}

/*
 * Node interface
 */
Scene::Node::Node(entt::entity entity, Scene* scene) noexcept: m_entity(entity), m_scene(scene) {}

std::optional<Scene::AssetData<Mesh>> Scene::Node::mesh() const {
  bool exists = m_scene->m_registry.all_of<MeshComponent>(m_entity);
  if (!exists) return std::nullopt;
  
  auto& mesh = m_scene->m_registry.get<MeshComponent>(m_entity);
  auto* asset = std::get_if<std::unique_ptr<Mesh>>(&m_scene->m_assets[mesh.id].asset);
  if (!asset) return std::nullopt;
  
  AssetData<Mesh> data {
    .id = mesh.id,
    .asset = asset->get(),
  };
  return data;
}

void Scene::Node::setMesh(std::optional<AssetID> id) {
  // Check if there is an existing mesh and release it
  bool exists = m_scene->m_registry.all_of<MeshComponent>(m_entity);
  if (exists) {
    auto mesh = m_scene->m_registry.get<MeshComponent>(m_entity);
    
    for (auto materialId: mesh.materials) {
      if (materialId) m_scene->releaseAsset(materialId.value());
    }
    
    m_scene->releaseAsset(mesh.id);
    m_scene->m_registry.erase<MeshComponent>(m_entity);
  }
  
  // Retain a reference to the new mesh (if present) and set it
  // We lose any material references, but this makes ref counting simpler and any materials are
  // unlikely to work with the previous mesh anyway
  if (id) {
    auto* asset = m_scene->getAsset<Mesh>(id.value());
    
    m_scene->retainAsset(id.value());
    m_scene->m_registry.emplace<MeshComponent>(m_entity, id.value(), asset->materialCount());
  }
}

std::optional<std::vector<std::optional<Scene::AssetID>>*> Scene::Node::materialIds() const {
  bool hasMesh = m_scene->m_registry.all_of<MeshComponent>(m_entity);
  if (!hasMesh) return std::nullopt;
  
  auto& mesh = m_scene->m_registry.get<MeshComponent>(m_entity);
  return &mesh.materials;
}

std::optional<Scene::AssetData<Material>> Scene::Node::material(size_t idx) const {
  bool hasMesh = m_scene->m_registry.all_of<MeshComponent>(m_entity);
  if (!hasMesh) return std::nullopt;
  
  auto& mesh = m_scene->m_registry.get<MeshComponent>(m_entity);
  auto materialId = mesh.materials[idx];
  if (!materialId) return std::nullopt;
  
  auto* asset = std::get_if<std::unique_ptr<Material>>(&m_scene->m_assets[materialId.value()].asset);
  if (!asset) return std::nullopt;
  
  AssetData<Material> data {
    .id = materialId.value(),
    .asset = asset->get(),
  };
  return data;
}

void Scene::Node::setMaterial(size_t idx, std::optional<AssetID> id) {
  bool hasMesh = m_scene->m_registry.all_of<MeshComponent>(m_entity);
  if (!hasMesh) return;
  
  auto& mesh = m_scene->m_registry.get<MeshComponent>(m_entity);
  auto currentId = mesh.materials[idx];
  
  if (currentId) m_scene->releaseAsset(id.value());
  if (id) m_scene->retainAsset(id.value());
  
  mesh.materials[idx] = id;
}

std::string& Scene::Node::name() const {
  return m_scene->m_registry.get<Hierarchy>(m_entity).name;
}

bool& Scene::Node::visible() const {
  return m_scene->m_registry.get<Hierarchy>(m_entity).visible;
}

Transform& Scene::Node::transform() const {
  return m_scene->m_registry.get<Transform>(m_entity);
}

std::optional<Scene::Node> Scene::Node::parent() const {
  auto& hierarchy = m_scene->m_registry.get<Hierarchy>(m_entity);
  
  if (hierarchy.parent == entt::null) return std::nullopt;
  return m_scene->node(hierarchy.parent);
}

std::vector<Scene::Node> Scene::Node::children() const {
  std::vector<Scene::Node> children;
  
  auto& hierarchy = m_scene->m_registry.get<Hierarchy>(m_entity);
  children.reserve(hierarchy.children.size());

  for (auto child: hierarchy.children) {
    children.push_back(m_scene->node(child));
  }
  
  return children;
}

bool Scene::Node::isRoot() const {
  return m_entity == m_scene->m_root;
}

Scene::Node Scene::Node::createChild(std::string_view name) {
  return m_scene->createNode(name, m_entity);
}

/*
 * Node API
 */
Scene::Node Scene::createNode(std::string_view name, Scene::NodeID parent) {
  auto id = m_registry.create();
  
  // Create transform component
  m_registry.emplace<Transform>(id);
  
  // Create hierarchy component
  auto parentId = parent == null ? m_root : parent;
  m_registry.emplace<Hierarchy>(id, name, parentId);
  
  // Append to children of parent node
  auto& parentHierarchy = m_registry.get<Hierarchy>(parentId);
  parentHierarchy.children.push_back(id);
  
  return node(id);
}

void Scene::removeNode(NodeID id) {
  if (!m_registry.valid(id)) return;
  
  // Clean up the node by removing any meshes and materials
  auto removed = node(id);
  removed.setMesh(std::nullopt);
  
  // Remove children recursively
  auto hierarchy = m_registry.get<Hierarchy>(id); // Copy so the child list doesn't get updated as we iterate it
  for (auto& childId: hierarchy.children) {
    removeNode(childId);
  }
  
  // Remove the node from its parent's child list
  // We don't need to check if it has a parent because the root cannot be removed
  auto& parent = m_registry.get<Hierarchy>(hierarchy.parent);
  std::erase_if(parent.children, [&](NodeID child) { return child == id; });
  
  m_registry.destroy(id);
}

bool Scene::moveNode(NodeID id, NodeID targetId) {
  if (targetId == id) return false; // Can't move a node into itself!

  auto& hierarchy = m_registry.get<Hierarchy>(id);
  auto& target = m_registry.get<Hierarchy>(targetId);

  // While moving a node into its own parent is technically a valid operation,
  // it's also completely pointless
  if (hierarchy.parent == targetId) return false;

  // Make sure we don't move a node into its own children
  NodeID parentId = target.parent;
  while (parentId != Scene::null) {
    if (parentId == id) return false;
    parentId = m_registry.get<Hierarchy>(parentId).parent;
  }

  // Move the node
  auto& oldParent = m_registry.get<Hierarchy>(hierarchy.parent);
  oldParent.children.erase(
    std::find(oldParent.children.begin(), oldParent.children.end(), id)
  );
  
  target.children.push_back(id);
  hierarchy.parent = targetId;

  return true;
}

bool Scene::cloneNode(Scene::NodeID id, Scene::NodeID targetId) {
  auto& hierarchy = m_registry.get<Hierarchy>(id);
  auto& target = m_registry.get<Hierarchy>(targetId);

  // Make sure we don't clone a node into its own children
  NodeID parentId = target.parent;
  while (parentId != Scene::null) {
    if (parentId == id) return false;
    parentId = m_registry.get<Hierarchy>(parentId).parent;
  }

  auto children = hierarchy.children;
  auto clone = createNode(hierarchy.name, targetId);
  clone.transform() = node(id).transform();
  
  // Clone any mesh components
  if (m_registry.all_of<MeshComponent>(id)) {
    auto& mesh = m_registry.get<MeshComponent>(id);
    clone.setMesh(mesh.id);
    
    for (size_t i = 0; i < mesh.materials.size(); i++) {
      clone.setMaterial(i, mesh.materials[i]);
    }
  }

  // Recursively clone children
  for (auto childId: children) {
    cloneNode(childId, clone.id());
  }

  return true;
}

bool Scene::hasNode(NodeID id) const {
  return m_registry.valid(id);
}

Scene::Node Scene::node(NodeID id) {
  return Node(id, this);
}

Scene::Node Scene::root() {
  return node(m_root);
}

/*
 * Other scene functions
 */
float4x4 Scene::worldTransform(Scene::NodeID id) {
  auto node = this->node(id);
  auto transform = node.transform().matrix();

  while (!node.isRoot()) {
    node = node.parent().value();
    transform = node.transform().matrix() * transform;
  }

  return transform;
}

std::vector<Scene::Instance> Scene::getInstances(const std::function<bool(const Scene::Node&)>& filter) {
  std::vector<Scene::Instance> instances;

  traverseHierarchy(
    [&](Node node, const float4x4& transformMatrix) {
      auto mesh = node.mesh();
      if (mesh) instances.emplace_back(node, mesh.value(), transformMatrix);
    },
    filter
  );

  return instances;
}

std::vector<Scene::Instance> Scene::getInstances() {
  return getInstances([](const Node& node){ return node.visible(); });
}

std::vector<Scene::CameraInstance> Scene::getCameras(const std::function<bool(const Scene::Node&)>& filter) {
  std::vector<Scene::CameraInstance> cameras;

  traverseHierarchy(
    [&](Node node, const float4x4& transformMatrix) {
      auto camera = node.get<Camera>();
      if (camera) cameras.emplace_back(node, *camera.value(), transformMatrix);
    },
    filter
  );

  return cameras;
}

std::vector<Scene::CameraInstance> Scene::getCameras() {
  return getCameras([](const Node& node){ return node.visible(); });
}

void Scene::traverseHierarchy(
  const std::function<void(Node, const float4x4&)>& cb,
  const std::function<bool(const Node&)>& filter
) {
  std::vector<std::pair<Node, float4x4>> stack = {{root(), mat::identity()}};

  while (!stack.empty()) {
    auto& [current, parentMatrix] = stack.back();
    stack.pop_back();

    if (!filter(current)) continue;

    const float4x4 transformMatrix = parentMatrix * current.transform().matrix();
    cb(current, transformMatrix);

    for (auto child: current.children()) {
      stack.emplace_back(child, transformMatrix);
    }
  }
}

}
