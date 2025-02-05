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

class Scene {
public:
  using NodeID = entt::entity;
  static constexpr NodeID null = entt::null;
  
  using AssetID = uint64_t;
  using AssetPtr = std::variant<std::unique_ptr<Texture>, std::unique_ptr<Mesh>, std::unique_ptr<Material>>;
  
  struct Asset {
    bool retain;
    AssetPtr asset;
  };
  
  template <typename T>
  struct AssetData {
    AssetID id;
    T* asset;
  };
  
  /*
   * Node class. Provides a public interface for interacting with scene nodes.
   */
  class Node {
  public:
    constexpr NodeID id() const { return m_entity; }
    
    std::optional<AssetData<Mesh>> mesh() const;
    void setMesh(std::optional<AssetID> id);
    
    std::optional<std::vector<std::optional<AssetID>>*> materialIds() const;
    std::optional<AssetData<Material>> material(size_t idx) const;
    void setMaterial(size_t idx, std::optional<AssetID> id);
    
    std::string_view name() const;
    Transform& transform() const;
    
    std::optional<Node> parent() const;
    std::vector<Node> children() const;
    bool isRoot() const;
    
    Node createChild(std::string_view name);
    
  private:
    explicit Node(entt::entity entity, Scene& scene) noexcept;
    
    entt::entity m_entity;
    Scene& m_scene;
    
    friend class Scene;
  };

  explicit Scene() noexcept;

  Node createNode(std::string_view name, NodeID parent = null);

  void removeNode(NodeID id);

  bool moveNode(NodeID id, NodeID targetId);

  bool cloneNode(NodeID id, NodeID targetId);

  [[nodiscard]] bool hasNode(NodeID id) const;

  [[nodiscard]] Node node(NodeID id);
  
  [[nodiscard]] Node root();
  
  [[nodiscard]] constexpr Environment& envmap() {
    return m_envmap;
  }

//  [[nodiscard]] float4x4 worldTransform(NodeID id) const;

//  [[nodiscard]] std::vector<MeshData> getAllMeshes() const;
//
//  [[nodiscard]] std::vector<InstanceData> getAllInstances(int filter = 0) const;
//
//  [[nodiscard]] std::vector<CameraData> getAllCameras(int filter = 0) const;
//  
//  [[nodiscard]] std::vector<MaterialData> getAllMaterials() const;
//  
//  [[nodiscard]] std::vector<TextureData> getAllTextures() const;
  
//  void recalculateMaterialFlags(AssetID id);
  
  template <typename T>
  T* getAsset(AssetID id) {
    using ptr_T = std::unique_ptr<T>;
    ptr_T* value = std::get_if<ptr_T>(&m_assets[id].asset);
    
    if (value) return value->get();
    return nullptr;
  }
  
  template <typename T>
  AssetID createAsset(T&& asset, bool retain = true) {
    AssetID id = m_nextAssetId++;
    
    m_assets[id] = {
      .asset = AssetPtr(std::make_unique<T>(std::move(asset))),
      .retain = retain,
    };
    m_assetRc[id] = 0;
    
    
    return id;
  }
  
  void removeAsset(AssetID id);

private:
  /*
   * ECS component structs
   */
  
  // Hierarchy component. Encapsulates parent/child relation data.
  struct Hierarchy {
    std::string name;
    std::vector<NodeID> children;
    NodeID parent;
    
    constexpr explicit Hierarchy(std::string_view name, NodeID parent) noexcept
    : name(name), parent(parent) {}
  };
  
  struct MeshComponent {
    AssetID id;
    std::vector<std::optional<AssetID>> materials;
    
    constexpr explicit MeshComponent(AssetID id, size_t materialCount = 1) noexcept
    : id(id), materials(materialCount, std::nullopt) {}
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
  
  // Envmaps
  Environment m_envmap;

//  void traverseHierarchy(
//    const std::function<void(NodeID id, const Node*, const float4x4&)>& cb,
//    int filter = 0
//  ) const;
};

}

#endif //PLATINUM_SCENE_HPP
