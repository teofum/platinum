#include "scene.hpp"

#include <utils/json.hpp>

using json = nlohmann::json;

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

bool Scene::assetValid(AssetID id) {
  return m_assets.contains(id);
}

size_t Scene::assetCount() {
  return m_assets.size();
}

std::vector<Scene::AnyAssetData> Scene::getAllAssets(std::function<bool(const AssetPtr&)> filter) {
  std::vector<AnyAssetData> data;
  data.reserve(m_assets.size());

  for (auto& [id, asset]: m_assets) {
    if (filter(asset.asset))
      data.push_back(
        {
          .id = id,
          .asset = std::visit<AnyAsset>([](const auto& it) { return it.get(); }, asset.asset),
        }
      );
  }

  return data;
}

std::vector<Scene::AnyAssetData> Scene::getAllAssets() {
  std::vector<AnyAssetData> data;
  data.reserve(m_assets.size());

  for (auto& [id, asset]: m_assets) {
    data.push_back(
      {
        .id = id,
        .asset = std::visit<AnyAsset>([](const auto& it) { return it.get(); }, asset.asset),
      }
    );
  }

  return data;
}

Scene::AnyAsset Scene::getAsset(AssetID id) {
  return std::visit<AnyAsset>([](const auto& it) { return it.get(); }, m_assets.at(id).asset);
}

void Scene::updateMaterialTexture(Material* material, Material::TextureSlot slot, std::optional<AssetID> textureId) {
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
    auto& mat = *(*pMaterial);
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
Scene::Node::Node(entt::entity entity, Scene* scene) noexcept: m_entity(entity), m_scene(scene) {
}

std::optional<Scene::AssetData<Mesh>> Scene::Node::mesh() const {
  bool exists = m_scene->m_registry.all_of<MeshComponent>(m_entity);
  if (!exists) return std::nullopt;

  auto& mesh = m_scene->m_registry.get<MeshComponent>(m_entity);
  auto* asset = std::get_if<std::unique_ptr<Mesh>>(&m_scene->m_assets[mesh.id].asset);
  if (!asset) return std::nullopt;

  AssetData<Mesh> data{
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
    m_scene->retainAsset(id.value());
    m_scene->m_registry.emplace<MeshComponent>(m_entity, id.value());
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

  AssetData<Material> data{
    .id = materialId.value(),
    .asset = asset->get(),
  };
  return data;
}

void Scene::Node::setMaterial(size_t idx, std::optional<AssetID> id) {
  bool hasMesh = m_scene->m_registry.all_of<MeshComponent>(m_entity);
  if (!hasMesh) return;

  auto& mesh = m_scene->m_registry.get<MeshComponent>(m_entity);

  if (idx < mesh.materials.size()) {
    auto currentId = mesh.materials[idx];
    if (currentId) m_scene->releaseAsset(currentId.value());
  } else {
    mesh.materials.resize(idx + 1, std::nullopt);
  }

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

bool Scene::Node::isLeaf() const {
  auto& hierarchy = m_scene->m_registry.get<Hierarchy>(m_entity);
  return hierarchy.children.empty();
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

void Scene::removeNode(NodeID id, RemoveMode mode) {
  if (!m_registry.valid(id)) return;

  // Clean up the node by removing any meshes and materials
  auto removed = node(id);
  removed.setMesh(std::nullopt);

  // Remove children recursively
  auto hierarchy = m_registry.get<Hierarchy>(id); // Copy so the child list doesn't get updated as we iterate it
  for (auto& childId: hierarchy.children) {
    switch (mode) {
      case RemoveMode::Recursive: {
        removeNode(childId, RemoveMode::Recursive);
        break;
      }
      case RemoveMode::MoveToParent: {
        auto parentId = hierarchy.parent == null ? m_root : hierarchy.parent;
        moveNode(childId, parentId);
        break;
      }
      case RemoveMode::MoveToRoot: {
        moveNode(childId, m_root);
        break;
      }
    }
  }

  // Remove the node from its parent's child list
  // We don't need to check if it has a parent because the root cannot be removed
  auto& parent = m_registry.get<Hierarchy>(hierarchy.parent);
  std::erase_if(
    parent.children, [&](NodeID child) {
      return child == id;
    }
  );

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
  oldParent.children.erase(std::find(oldParent.children.begin(), oldParent.children.end(), id));

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
  return getInstances([](const Node& node) { return node.visible(); });
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
  return getCameras([](const Node& node) { return node.visible(); });
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

void Scene::saveToFile(const fs::path& path) {
  auto binaryFilename = std::format("{}_data.bin", path.stem().string());
  auto binaryPath = path.parent_path() / binaryFilename;

  std::ofstream binaryFile(binaryPath, std::ios::out | std::ios::binary);

  /*
   * Dump all mesh/texture data to a binary file, and store its byte
   * offset/length to write in the scene json
   */
  hashmap<AssetID, BufferData> textureBufferData;
  hashmap<AssetID, MeshBufferData> meshBufferData;

  size_t cumulativeOffset = 0;
  auto dumpBuffer = [&cumulativeOffset, &binaryFile](MTL::Buffer* buf) {
    size_t len = buf->length();
    binaryFile.write((const char*) buf->contents(), std::streamsize(len));
    BufferData data{
      .offset = cumulativeOffset,
      .length = len,
    };

    cumulativeOffset += len;
    return data;
  };

  for (const auto& texture: getAll<Texture>()) {
    // Copy the texture's data so we can access it
    auto format = texture.asset->texture()->pixelFormat();
    size_t bytesPerPixel = 0;
    if (format == MTL::PixelFormatRGBA32Float) bytesPerPixel = 4 * sizeof(float);
    if (format == MTL::PixelFormatRGBA8Unorm) bytesPerPixel = 4 * sizeof(uint8_t);
    if (format == MTL::PixelFormatRG8Unorm) bytesPerPixel = 2 * sizeof(uint8_t);
    if (format == MTL::PixelFormatR8Unorm) bytesPerPixel = 1 * sizeof(uint8_t);

    size_t width = texture.asset->texture()->width();
    size_t height = texture.asset->texture()->height();

    size_t bytesPerRow = bytesPerPixel * width;
    size_t totalBytes = bytesPerRow * height;

    void* data = malloc(totalBytes);
    texture.asset->texture()->getBytes(data, bytesPerRow, MTL::Region(0, 0, width, height), 0);
    binaryFile.write((const char*) data, std::streamsize(totalBytes));
    free(data);

    textureBufferData[texture.id] = {
      .offset = cumulativeOffset,
      .length = totalBytes,
    };
    cumulativeOffset += totalBytes;
  }
  for (const auto& mesh: getAll<Mesh>()) {
    meshBufferData[mesh.id].positions = dumpBuffer(mesh.asset->vertexPositions());
    meshBufferData[mesh.id].vertexData = dumpBuffer(mesh.asset->vertexData());
    meshBufferData[mesh.id].indices = dumpBuffer(mesh.asset->indices());
    meshBufferData[mesh.id].materials = dumpBuffer(mesh.asset->materialIndices());
  }

  /*
   * Serialize scene structure as JSON
   */
  auto rootNode = root();
  json objectJson = nodeToJson(rootNode);

  json assetJson = {
    {"nextId", m_nextAssetId},
    {"assets", json::array()},
  };
  auto& assets = assetJson["assets"];
  for (const auto& asset: getAllAssets()) {
    assets.push_back(toJson(asset, textureBufferData, meshBufferData));
  }

  json sceneJson = {
    {"root",   objectJson},
    {"assets", assetJson},
  };

  /*
   * Store environment map texture ID, if there is one
   */
  if (m_envmap.textureId()) {
    auto envmapBufferData = dumpBuffer(m_envmap.aliasTable());

    sceneJson["envmap"] = {
      {"texture",    m_envmap.textureId().value()},
      {"aliasTable", {envmapBufferData.offset, envmapBufferData.length}},
    };
  }

  std::ofstream file(path);
  file << sceneJson;
}

json Scene::nodeToJson(Scene::Node node) {
  /*
   * Basic node data
   */
  json nodeJson = {
    {"id",        uint64_t(node.id())},
    {"name",      node.name()},
    {"visible",   node.visible()},
    {"transform", json_utils::transform(node.transform())},
    {"children",  json::array()},
  };

  /*
   * Serialize mesh/material data, if present
   */
  if (node.mesh()) {
    auto mesh = node.mesh().value();
    json meshJson = {
      {"id",        mesh.id},
      {"materials", json::array()},
    };

    auto& materials = meshJson["materials"];
    const auto& materialIds = *node.materialIds().value();
    for (const auto& id: materialIds) {
      if (id) materials.push_back(id.value());
      else materials.push_back("default");
    }

    nodeJson["mesh"] = meshJson;
  }

  /*
   * Serialize camera, if present
   */
  if (node.get<Camera>()) {
    auto* camera = node.get<Camera>().value();
    nodeJson["camera"] = {
      {"f",        camera->focalLength},
      {"aperture", camera->aperture},
      {"sensor",   json_utils::vec(camera->sensorSize)},
    };
  }

  /*
   * Recursively serialize children
   */
  auto& children = nodeJson["children"];
  for (auto& child: node.children())
    children.push_back(nodeToJson(child));

  return nodeJson;
}

json Scene::toJson(
  const Scene::AnyAssetData& data,
  const hashmap<AssetID, BufferData>& textureBufferData,
  const hashmap<AssetID, MeshBufferData>& meshBufferData
) {
  json assetJson{
    {"id",     data.id},
    {"retain", assetRetained(data.id)},
    {"rc",     m_assetRc.at(data.id)},
  };

  json dataJson = std::visit(
    [&](const auto& asset) {
      using T = std::decay_t<decltype(asset)>;

      if constexpr (std::is_same_v<T, Texture*>) {
        assetJson["type"] = "texture";
        AssetData<Texture> texture{data.id, asset};
        return toJson(texture, textureBufferData);
      } else if constexpr (std::is_same_v<T, Material*>) {
        assetJson["type"] = "material";
        AssetData<Material> material{data.id, asset};
        return toJson(material);
      } else if constexpr (std::is_same_v<T, Mesh*>) {
        assetJson["type"] = "mesh";
        AssetData<Mesh> mesh{data.id, asset};
        return toJson(mesh, meshBufferData);
      }
    },
    data.asset
  );

  assetJson["data"] = dataJson;
  return assetJson;
}

[[nodiscard]] json Scene::toJson(
  const Scene::AssetData<Texture>& texture,
  const hashmap<AssetID, BufferData>& textureBufferData
) {
  uint32_t width = texture.asset->texture()->width();
  uint32_t height = texture.asset->texture()->height();

  const auto& bd = textureBufferData.at(texture.id);

  return {
    {"name",   texture.asset->name()},
    {"alpha",  texture.asset->hasAlpha()},
    {"size",   {width,     height}},
    {"format", texture.asset->texture()->pixelFormat()},
    {"data",   {bd.offset, bd.length}},
  };
}

[[nodiscard]] json Scene::toJson(const Scene::AssetData<Material>& material) {
  json materialJson{
    {"name",               material.asset->name},
    {"baseColor",          json_utils::vec(material.asset->baseColor)},
    {"roughness",          material.asset->roughness},
    {"metallic",           material.asset->metallic},
    {"transmission",       material.asset->transmission},
    {"ior",                material.asset->ior},
    {"aniso",              material.asset->anisotropy},
    {"anisoRotation",      material.asset->anisotropyRotation},
    {"clearcoat",          material.asset->clearcoat},
    {"clearcoatRoughness", material.asset->clearcoatRoughness},
    {"emission",           json_utils::vec(material.asset->emission)},
    {"emissionStrength",   material.asset->emissionStrength},
    {"thinTransmission",   material.asset->thinTransmission},
    {"textures",           json::array()}
  };

  auto& textures = materialJson["textures"];
  for (const auto& [slot, textureId]: material.asset->textures) {
    textures.push_back({slot, textureId});
  }

  return materialJson;
}

[[nodiscard]] json Scene::toJson(
  const Scene::AssetData<Mesh>& mesh,
  const hashmap<AssetID, MeshBufferData>& meshBufferData
) {
  const auto& bd = meshBufferData.at(mesh.id);

  return {
    {"indexCount",  mesh.asset->indexCount()},
    {"vertexCount", mesh.asset->vertexCount()},
    {"positions",   {bd.positions.offset,  bd.positions.length}},
    {"vertexData",  {bd.vertexData.offset, bd.vertexData.length}},
    {"indices",     {bd.indices.offset,    bd.indices.length}},
    {"materials",   {bd.materials.offset,  bd.materials.length}},
  };
}

}
