#ifndef PLATINUM_SCENE_HPP
#define PLATINUM_SCENE_HPP

#include <vector>
#include <optional>
#include <unordered_dense.h>

#include "mesh.hpp"
#include "transform.hpp"

namespace pt {

class Scene {
public:
  using NodeID = uint16_t;
  using MeshID = uint16_t;

  struct Node {
    std::optional<MeshID> meshId;
    std::vector<NodeID> children;
    NodeID parent;
    Transform transform;

    constexpr explicit Node(std::optional<MeshID> meshId = std::nullopt) noexcept
      : meshId(meshId), children(), parent(0), transform() {
    }
  };

  struct MeshData {
    const Mesh* mesh = nullptr;
    float4x4 transform;
    NodeID nodeId = 0;
  };

  enum RemoveOptions {
    RemoveOptions_KeepOrphanedMeshes = 0,
    RemoveOptions_RemoveOrphanedMeshes = 1 << 0,
    RemoveOptions_RemoveChildrenRecursively = 0,
    RemoveOptions_MoveChildrenToRoot = 1 << 1,
    RemoveOptions_MoveChildrenToParent = 1 << 2,
  };

  explicit Scene() noexcept;

  MeshID addMesh(Mesh&& mesh);

  NodeID addNode(Node&& node, NodeID parent = 0);

  void removeNode(
    NodeID id,
    int flags = RemoveOptions_RemoveOrphanedMeshes |
                RemoveOptions_RemoveChildrenRecursively
  );

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

  [[nodiscard]] float4x4 worldTransform(NodeID id) const;

  [[nodiscard]] std::vector<MeshData> getAllMeshes() const;

private:
  NodeID m_nextNodeId;
  MeshID m_nextMeshId;

  ankerl::unordered_dense::map<NodeID, std::unique_ptr<Node>> m_nodes;
  ankerl::unordered_dense::map<MeshID, std::unique_ptr<Mesh>> m_meshes;
  ankerl::unordered_dense::map<MeshID, uint16_t> m_meshRc; // Refcount
};

}

#endif //PLATINUM_SCENE_HPP
