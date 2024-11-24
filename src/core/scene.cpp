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

std::vector<std::pair<const Mesh&, float4x4>> Scene::getAllMeshes() const {
  std::vector<std::pair<const Mesh&, float4x4>> meshes;
  meshes.reserve(m_meshes.size());

  std::vector<std::pair<const Node&, float4x4>> stack = {
    {root(), mat::identity()}
  };

  while (!stack.empty()) {
    const auto& node = stack.back();
    stack.pop_back();

    const Node& current = node.first;
    const float4x4& parentMatrix = node.second;

    const float4x4 transformMatrix = parentMatrix * current.transform.matrix();
    if (current.meshIdx) {
      const Mesh& mesh = m_meshes[*current.meshIdx];
      meshes.emplace_back(mesh, transformMatrix);
    }

    for (auto idx: current.children) {
      stack.emplace_back(m_nodes[idx], transformMatrix);
    }
  }

  return meshes;
}

}
