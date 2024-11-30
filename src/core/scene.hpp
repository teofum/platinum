#ifndef PLATINUM_SCENE_HPP
#define PLATINUM_SCENE_HPP

#include <utility>
#include <vector>
#include <optional>
#include <unordered_dense.h>

#include "mesh.hpp"
#include "camera.hpp"
#include "transform.hpp"

namespace pt {

class Scene {
public:
  using NodeID = uint16_t;
  using MeshID = uint16_t;
  using CameraID = uint16_t;

  enum NodeFlags {
    NodeFlags_None = 0,
    NodeFlags_Visible = 1 << 0,
    NodeFlags_Default = NodeFlags_Visible,
  };

  struct Node {
    std::string name;
    std::optional<MeshID> meshId;
    std::optional<CameraID> cameraId;
    std::vector<NodeID> children;
    NodeID parent = 0;
    int flags = NodeFlags_Default;
    Transform transform;

    constexpr explicit Node(std::string_view name, std::optional<MeshID> meshId = std::nullopt) noexcept
      : name(name), meshId(meshId) {
    }
  };

  struct MeshData {
    const Mesh* mesh = nullptr;
    MeshID meshId = 0;
  };

  struct InstanceData {
    const Mesh* mesh = nullptr;
    NodeID nodeId = 0;
    MeshID meshId = 0;
    float4x4 transform;
  };

  struct CameraData {
    const Camera* camera = nullptr;
    float4x4 transform;
    NodeID nodeId = 0;
  };

  enum RemoveOptions {
    RemoveOptions_RemoveOrphanedObjects = 0,
    RemoveOptions_KeepOrphanedObjects = 1 << 0,
    RemoveOptions_RemoveChildrenRecursively = 0,
    RemoveOptions_MoveChildrenToRoot = 1 << 1,
    RemoveOptions_MoveChildrenToParent = 1 << 2,
  };

  explicit Scene() noexcept;

  MeshID addMesh(Mesh&& mesh);

  CameraID addCamera(Camera camera);

  NodeID addNode(Node&& node, NodeID parent = 0);

  void removeNode(NodeID id, int flags = 0);

  bool moveNode(NodeID id, NodeID targetId);

  bool cloneNode(NodeID id, NodeID targetId);

  [[nodiscard]] constexpr const Node* root() const {
    return m_nodes.at(0).get();
  }

  [[nodiscard]] constexpr const Node* node(NodeID id) const {
    return m_nodes.at(id).get();
  }

  [[nodiscard]] constexpr Node* node(NodeID id) {
    return m_nodes.at(id).get();
  }

  [[nodiscard]] constexpr const Mesh* mesh(MeshID id) const {
    return m_meshes.at(id).get();
  }

  [[nodiscard]] constexpr Mesh* mesh(MeshID id) {
    return m_meshes.at(id).get();
  }

  [[nodiscard]] constexpr uint16_t meshUsers(MeshID id) {
    return m_meshRc.at(id);
  }

  [[nodiscard]] constexpr const Camera* camera(CameraID id) const {
    return &m_cameras.at(id);
  }

  [[nodiscard]] constexpr Camera* camera(CameraID id) {
    return &m_cameras.at(id);
  }

  [[nodiscard]] constexpr uint16_t cameraUsers(CameraID id) {
    return m_cameraRc.at(id);
  }

  [[nodiscard]] float4x4 worldTransform(NodeID id) const;

  [[nodiscard]] std::vector<MeshData> getAllMeshes() const;

  [[nodiscard]] std::vector<InstanceData> getAllInstances(int filter = 0) const;

  [[nodiscard]] std::vector<CameraData> getAllCameras(int filter = 0) const;

private:
  NodeID m_nextNodeId;
  MeshID m_nextMeshId;
  CameraID m_nextCameraId;

  ankerl::unordered_dense::map<NodeID, std::unique_ptr<Node>> m_nodes;
  ankerl::unordered_dense::map<MeshID, std::unique_ptr<Mesh>> m_meshes;
  ankerl::unordered_dense::map<MeshID, uint16_t> m_meshRc; // Refcount
  ankerl::unordered_dense::map<CameraID, Camera> m_cameras;
  ankerl::unordered_dense::map<CameraID, uint16_t> m_cameraRc; // Refcount

  void traverseHierarchy(
    const std::function<void(NodeID id, const Node*, const float4x4&)>& cb,
    int filter = 0
  ) const;
};

}

#endif //PLATINUM_SCENE_HPP
