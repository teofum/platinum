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
    std::optional<uint16_t> meshId;
    std::vector<uint16_t> children;
    Transform transform;

    constexpr explicit Node() noexcept
      : meshId(std::nullopt), children(), transform() {
    }

    constexpr explicit Node(uint16_t meshId) noexcept
      : meshId(meshId), children(), transform() {
    }
  };

  struct MeshData {
    const Mesh* mesh = nullptr;
    float4x4 transform;
    NodeID nodeId = 0;
  };

  explicit Scene() noexcept;

  MeshID addMesh(Mesh&& mesh);

  NodeID addNode(Node&& node, NodeID parent = 0);

  [[nodiscard]] constexpr const Node* root() const {
    return m_nodes.at(0).get();
  }

  [[nodiscard]] constexpr const Node* node(NodeID id) const {
    return m_nodes.at(id).get();
  }

  [[nodiscard]] constexpr Node* node(NodeID id) {
    return m_nodes[id].get();
  }

  [[nodiscard]] constexpr const Mesh* mesh(MeshID id) const {
    return m_meshes.at(id).get();
  }

  [[nodiscard]] constexpr Mesh* mesh(MeshID id) {
    return m_meshes[id].get();
  }

  [[nodiscard]] std::vector<MeshData> getAllMeshes() const;

private:
  MeshID m_nextMeshId;
  NodeID m_nextNodeId;

  ankerl::unordered_dense::map<MeshID, std::unique_ptr<Mesh>> m_meshes;
  ankerl::unordered_dense::map<NodeID, std::unique_ptr<Node>> m_nodes;
};

}

#endif //PLATINUM_SCENE_HPP
