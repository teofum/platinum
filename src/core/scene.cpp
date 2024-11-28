#include "scene.hpp"

namespace pt {

Scene::Scene() noexcept
  : m_nextNodeId(1), m_nextMeshId(0), m_nodes(), m_meshes() {
  m_nodes[0] = std::make_unique<Node>("Scene"); // Create root node
}

Scene::MeshID Scene::addMesh(Mesh&& mesh) {
  MeshID id = m_nextMeshId++;
  m_meshes[id] = std::make_unique<Mesh>(std::move(mesh));
  m_meshRc[id] = 0;
  while (m_meshes.contains(m_nextMeshId)) m_nextMeshId++; // Find next unused ID

  return id;
}

Scene::NodeID Scene::addNode(Node&& node, Scene::NodeID parent) {
  NodeID id = m_nextNodeId++;
  node.parent = parent;
  m_nodes[id] = std::make_unique<Node>(std::move(node));
  while (m_nodes.contains(m_nextNodeId)) m_nextNodeId++; // Find next unused ID

  auto& parentNode = m_nodes[parent];
  parentNode->children.push_back(id);

  // Increase ref count for mesh
  if (node.meshId) m_meshRc[node.meshId.value()]++;
  return id;
}

void Scene::removeNode(Scene::NodeID id, int flags) {
  if (id == 0) return; // Can't remove the root node

  const auto& removed = m_nodes.at(id);

  // Remove the node id from the parent's child list
  const auto& parent = m_nodes.at(removed->parent);
  parent->children.erase(
    std::find(parent->children.begin(), parent->children.end(), id)
  );

  // Handle removal or moving of the children
  auto children = removed->children; // Clone child IDs to prevent modifying while iterating
  for (auto childId: children) {
    const auto& child = m_nodes.at(childId);

    if (flags & RemoveOptions_MoveChildrenToRoot) {
      child->parent = 0;
      m_nodes[0]->children.push_back(childId);
    } else if (flags & RemoveOptions_MoveChildrenToParent) {
      child->parent = removed->parent;
      parent->children.push_back(childId);
    } else {
      removeNode(childId, flags); // Remove children recursively
    }
  }

  // Decrease refcount of mesh if present and handle orphaned mesh
  if (removed->meshId) {
    const auto rc = --m_meshRc[removed->meshId.value()];
    if (rc == 0 && (flags & RemoveOptions_RemoveOrphanedMeshes)) {
      m_meshes.erase(removed->meshId.value());
      m_nextMeshId = removed->meshId.value();
    }
  }

  m_nodes.erase(id); // Remove the node
  m_nextNodeId = id; // Set next ID to removed one to be reused
}

bool Scene::moveNode(Scene::NodeID id, Scene::NodeID targetId) {
  if (targetId == id) return false; // Can't move a node into itself!

  const auto& node = m_nodes.at(id);
  const auto& target = m_nodes.at(targetId);

  // While moving a node into its own parent is technically a valid operation,
  // it's also completely pointless
  if (node->parent == targetId) return false;

  // Make sure we don't move a node into its own children
  NodeID parentId = target->parent;
  while (parentId != 0) {
    if (parentId == id) return false;
    parentId = m_nodes.at(parentId)->parent;
  }

  // Move the node
  const auto& oldParent = m_nodes.at(node->parent);
  oldParent->children.erase(
    std::find(oldParent->children.begin(), oldParent->children.end(), id)
  );
  target->children.push_back(id);
  node->parent = targetId;

  return true;
}

bool Scene::cloneNode(Scene::NodeID id, Scene::NodeID targetId) {
  const auto& node = m_nodes.at(id);
  const auto& target = m_nodes.at(targetId);

  // Make sure we don't clone a node into its own children
  NodeID parentId = target->parent;
  while (parentId != 0) {
    if (parentId == id) return false;
    parentId = m_nodes.at(parentId)->parent;
  }

  auto children = node->children;

  Node clone(node->name, node->meshId);
  clone.transform = node->transform;
  const auto cloneId = addNode(std::move(clone), targetId);

  // Recursively clone children
  for (auto childId: children) {
    cloneNode(childId, cloneId);
  }

  return true;
}

float4x4 Scene::worldTransform(Scene::NodeID id) const {
  const auto* node = m_nodes.at(id).get();
  auto transform = node->transform.matrix();

  while (id != 0) {
    id = node->parent;
    node = m_nodes.at(id).get();
    transform = node->transform.matrix() * transform;
  }

  return transform;
}

std::vector<Scene::MeshData> Scene::getAllMeshes(int filter) const {
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
    if (filter && !(current->flags & filter)) continue;

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
