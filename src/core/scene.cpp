#include "scene.hpp"

namespace pt {

Scene::Scene() noexcept: m_meshes(), m_nodes() {
  m_nodes.emplace_back(); // Push root node
}

uint32_t Scene::addMesh(Mesh&& mesh) {
  m_meshes.push_back(std::move(mesh));
  return m_meshes.size() - 1;
}

uint32_t Scene::addNode(Node&& node, uint32_t parent) {
  m_nodes.push_back(std::move(node));
  uint32_t idx = m_nodes.size() - 1;

  auto& parentNode = m_nodes[parent];
  parentNode.children.push_back(idx);
  return idx;
}

std::vector<Scene::MeshData> Scene::getAllMeshes() const {
  std::vector<Scene::MeshData> meshes;
  meshes.reserve(m_meshes.size());

  std::vector<std::pair<uint32_t, float4x4>> stack = {
    {0, mat::identity()}
  };

  while (!stack.empty()) {
    const auto& node = stack.back();
    stack.pop_back();

    uint32_t currentIdx = node.first;
    const Node& current = m_nodes[currentIdx];
    const float4x4& parentMatrix = node.second;

    const float4x4 transformMatrix = parentMatrix * current.transform.matrix();
    if (current.meshIdx) {
      const Mesh& mesh = m_meshes[*current.meshIdx];
      meshes.emplace_back(mesh, transformMatrix, currentIdx);
    }

    for (auto childIdx: current.children) {
      stack.emplace_back(childIdx, transformMatrix);
    }
  }

  return meshes;
}

}
