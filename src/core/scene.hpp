#ifndef PLATINUM_SCENE_HPP
#define PLATINUM_SCENE_HPP

#include <utility>
#include <vector>
#include <optional>
#include <unordered_dense.h>
#include <entt.hpp>

#include "camera.hpp"
#include "material.hpp"
#include "texture.hpp"
#include "mesh.hpp"
#include "transform.hpp"
#include "environment.hpp"

namespace pt {

template<typename T, typename ... U>
concept is_not = (!std::same_as<T, U>, ...);

class Scene {
  struct MeshComponent;
  struct Hierarchy;

public:
  using NodeID = entt::entity;
  static constexpr NodeID null = entt::null;

  using AssetID = uint64_t;
  using AssetPtr = std::variant<std::unique_ptr<Texture>, std::unique_ptr<Mesh>, std::unique_ptr<Material>>;
  using AnyAsset = std::variant<Texture*, Mesh*, Material*>;

  struct Asset {
    bool retain;
    AssetPtr asset;
  };

  template<typename T>
  struct AssetData {
    AssetID id;
    T* asset;
  };

  struct AnyAssetData {
    AssetID id;
    AnyAsset asset;
  };

  enum class RemoveMode {
    Recursive,
    MoveToParent,
    MoveToRoot,
  };

  /*
   * Node class. Provides a public interface for interacting with scene nodes.
   */
  class Node {
  public:
    constexpr NodeID id() const {
      return m_entity;
    }

    std::optional<AssetData<Mesh>> mesh() const;

    void setMesh(std::optional<AssetID> id);

    std::optional<std::vector<std::optional<AssetID>>*> materialIds() const;

    std::optional<AssetData<Material>> material(size_t idx) const;

    void setMaterial(size_t idx, std::optional<AssetID> id);

    std::string& name() const;

    bool& visible() const;

    Transform& transform() const;

    std::optional<Node> parent() const;

    std::vector<Node> children() const;

    bool isRoot() const;

    bool isLeaf() const;

    Node createChild(std::string_view name);

    template<typename T>
    requires is_not<T, MeshComponent, Hierarchy, Transform>
    std::optional<T*> get() {
      bool exists = m_scene->m_registry.all_of<T>(m_entity);
      if (!exists) return std::nullopt;
      return &m_scene->m_registry.get<T>(m_entity);
    }

    template<typename T>
    requires is_not<T, MeshComponent, Hierarchy, Transform>
    void set(T&& component) {
      using ValueType = std::remove_reference_t<T>;
      m_scene->m_registry.emplace_or_replace<ValueType>(m_entity, std::forward<ValueType>(component));
    }

  private:
    explicit Node(entt::entity entity, Scene* scene) noexcept;

    entt::entity m_entity;
    Scene* m_scene;

    friend class Scene;
  };

  struct Instance {
    Node node;
    AssetData<Mesh> mesh;
    float4x4 transformMatrix;
  };

  struct CameraInstance {
    Node node;
    Camera& camera;
    float4x4 transformMatrix;
  };

  explicit Scene() noexcept;

  Node createNode(std::string_view name, NodeID parent = null);

  void removeNode(NodeID id, RemoveMode mode = RemoveMode::Recursive);

  bool moveNode(NodeID id, NodeID targetId);

  bool cloneNode(NodeID id, NodeID targetId);

  [[nodiscard]] bool hasNode(NodeID id) const;

  [[nodiscard]] Node node(NodeID id);

  [[nodiscard]] Node root();

  [[nodiscard]] constexpr Environment& envmap() {
    return m_envmap;
  }

  [[nodiscard]] constexpr Material& defaultMaterial() {
    return m_defaultMaterial;
  }

  [[nodiscard]] float4x4 worldTransform(NodeID id);

  [[nodiscard]] std::vector<Instance> getInstances(const std::function<bool(const Node&)>& filter);

  [[nodiscard]] std::vector<Instance> getInstances();

  [[nodiscard]] std::vector<CameraInstance> getCameras(const std::function<bool(const Node&)>& filter);

  [[nodiscard]] std::vector<CameraInstance> getCameras();

  template<typename T>
  T* getAsset(AssetID id) {
    using ptr_T = std::unique_ptr<T>;

    ptr_T* value = std::get_if<ptr_T>(&m_assets.at(id).asset);

    if (value) return value->get();
    return nullptr;
  }

  template<typename T>
  std::vector<AssetData<T>> getAll() {
    using ptr_T = std::unique_ptr<T>;

    std::vector<AssetData<T>> assets;
    for (auto& [id, asset]: m_assets) {
      ptr_T* value = std::get_if<ptr_T>(&asset.asset);
      if (value) assets.emplace_back(id, value->get());
    }

    return assets;
  }

  template<typename T>
  AssetID createAsset(T&& asset, bool retain = true) {
    AssetID id = m_nextAssetId++;

    m_assets[id] = {
      .retain = retain,
      .asset = AssetPtr(std::make_unique<T>(std::move(asset))),
    };
    m_assetRc[id] = 0;


    return id;
  }

  void removeAsset(AssetID id);

  uint32_t getAssetRc(AssetID id);

  bool& assetRetained(AssetID id);

  bool assetValid(AssetID id);

  size_t assetCount();

  std::vector<AnyAssetData> getAllAssets(std::function<bool(const AssetPtr&)> filter);

  std::vector<AnyAssetData> getAllAssets();

  AnyAsset getAsset(AssetID id);

  void updateMaterialTexture(Material* material, Material::TextureSlot slot, std::optional<AssetID> textureId);

private:
  /*
   * ECS component structs
   */
  // Hierarchy component. Encapsulates parent/child relation data.
  struct Hierarchy {
    std::string name;
    bool visible = true;

    std::vector<NodeID> children;
    NodeID parent;

    constexpr explicit Hierarchy(std::string_view name, NodeID parent) noexcept
      : name(name), parent(parent) {
    }
  };

  struct MeshComponent {
    AssetID id;
    std::vector<std::optional<AssetID>> materials;

    constexpr explicit MeshComponent(AssetID id, size_t materialCount = 1) noexcept
      : id(id), materials(materialCount, std::nullopt) {
    }
  };

  /*
   * Scene ECS
   */
  entt::registry m_registry;
  entt::entity m_root;

  /*
   * Asset management
   */
  AssetID m_nextAssetId;

  ankerl::unordered_dense::map<AssetID, Asset> m_assets;
  ankerl::unordered_dense::map<AssetID, uint32_t> m_assetRc;

  void retainAsset(AssetID id);

  bool releaseAsset(AssetID id);

  void removeAssetImpl(AssetID id);

  /*
   * Additional data
   */
  Material m_defaultMaterial;
  Environment m_envmap;

  void traverseHierarchy(
    const std::function<void(Node, const float4x4&)>& cb,
    const std::function<bool(const Node&)>& filter
  );
};

}

#endif //PLATINUM_SCENE_HPP
