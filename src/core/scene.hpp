#ifndef PLATINUM_SCENE_HPP
#define PLATINUM_SCENE_HPP

#include <vector>
#include <optional>
#include "mesh.hpp"
#include "transform.hpp"

namespace pt {

class Scene {
public:
  struct Node {
    std::optional<uint32_t> meshIdx;
    std::vector<uint32_t> children;
    Transform transform;

    constexpr explicit Node() noexcept
      : meshIdx(std::nullopt), children(), transform() {
    }

    constexpr explicit Node(uint32_t meshIdx) noexcept
      : meshIdx(meshIdx), children(), transform() {
    }
  };

  struct MeshData {
    const Mesh& mesh;
    float4x4 transform;
    uint32_t nodeIdx = 0;
  };

  explicit Scene() noexcept;

  uint32_t addMesh(Mesh&& mesh);

  uint32_t addNode(Node&& node, uint32_t parent = 0);

  [[nodiscard]] constexpr const Node& root() const {
    return m_nodes[0];
  }

  [[nodiscard]] constexpr const Node& node(uint32_t idx) const {
    return m_nodes[idx];
  }

  [[nodiscard]] constexpr Node& node(uint32_t idx) {
    return m_nodes[idx];
  }

  [[nodiscard]] constexpr const Mesh& mesh(uint32_t idx) const {
    return m_meshes[idx];
  }

  [[nodiscard]] constexpr Mesh& mesh(uint32_t idx) {
    return m_meshes[idx];
  }

  [[nodiscard]] std::vector<MeshData> getAllMeshes() const;

private:
  std::vector<Mesh> m_meshes;
  std::vector<Node> m_nodes;
};

}

#endif //PLATINUM_SCENE_HPP
