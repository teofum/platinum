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

    constexpr explicit Node() noexcept
      : meshId(std::nullopt), children(), parent(0), transform() {
    }

    constexpr explicit Node(MeshID meshId) noexcept
      : meshId(meshId), children(), parent(0), transform() {
    }
  };

  struct MeshData {
    const Mesh* mesh = nullptr;
    float4x4 transform;
    NodeID nodeId = 0;
  };

  enum RemoveOptions {
    OrphanedMeshes_Keep = 0,
    OrphanedMeshes_Remove = 1 << 0,
    Children_RemoveRecursively = 0,
    Children_MoveToRoot = 1 << 1,
    Children_MoveToParent = 1 << 2,
  };

  explicit Scene() noexcept;

  MeshID addMesh(Mesh&& mesh);

  NodeID addNode(Node&& node, NodeID parent = 0);

  void removeNode(
    NodeID id,
    int flags = OrphanedMeshes_Remove | Children_RemoveRecursively
  );

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
