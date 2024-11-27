#include "scene.hpp"

namespace pt {

Scene::Scene() noexcept
  : m_nextMeshId(0), m_nextNodeId(1), m_meshes(), m_nodes() {
  m_nodes[0] = std::make_unique<Node>(); // Create root node
}

Scene::MeshID Scene::addMesh(Mesh&& mesh) {
  MeshID id = m_nextMeshId++;
  m_meshes[id] = std::make_unique<Mesh>(std::move(mesh));
  while (m_meshes.contains(m_nextMeshId)) m_nextMeshId++;

  return id;
}

Scene::NodeID Scene::addNode(Node&& node, Scene::NodeID parent) {
  NodeID id = m_nextNodeId++;
  m_nodes[id] = std::make_unique<Node>(std::move(node));
  while (m_nodes.contains(m_nextNodeId)) m_nextNodeId++;

  auto& parentNode = m_nodes[parent];
  parentNode->children.push_back(id);
  return id;
}

std::vector<Scene::MeshData> Scene::getAllMeshes() const {
  std::vector<Scene::MeshData> meshes;
  meshes.reserve(m_meshes.size());

  std::vector<std::pair<NodeID, float4x4>> stack = {
    {0, mat::identity()}
  };

  while (!stack.empty()) {
    const auto& node = stack.back();
    stack.pop_back();

    NodeID currentId = node.first;
    const auto& current = m_nodes.at(currentId);
    const float4x4& parentMatrix = node.second;

    const float4x4 transformMatrix = parentMatrix * current->transform.matrix();
    if (current->meshId) {
      const auto& mesh = m_meshes.at(*current->meshId);
      meshes.emplace_back(mesh.get(), transformMatrix, currentId);
    }

    for (auto childIdx: current->children) {
      stack.emplace_back(childIdx, transformMatrix);
    }
  }

  return meshes;
}

}
