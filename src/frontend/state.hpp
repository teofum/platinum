#ifndef PLATINUM_STATE_HPP
#define PLATINUM_STATE_HPP

#include <optional>

#include <core/store.hpp>

namespace pt::frontend {

class State {
public:
  enum NodeAction {
    NodeAction_None = 0,
    NodeAction_Remove = 1 << 0,
    NodeAction_CenterCamera = 1 << 1,
  };

  explicit constexpr State(Store& store) noexcept: m_store(store) {
  }

  [[nodiscard]] constexpr std::optional<Scene::NodeID> selectedNode() const {
    return m_selectedNodeId;
  }

  [[nodiscard]] constexpr std::optional<Scene::MeshID> selectedMesh() const {
    return m_selectedMeshId;
  }

  [[nodiscard]] constexpr std::optional<Scene::CameraID> selectedCamera() const {
    return m_selectedCameraId;
  }
  
  [[nodiscard]] constexpr std::optional<Scene::MaterialID> selectedMaterial() const {
    return m_selectedMaterialId;
  }
  
  [[nodiscard]] constexpr std::optional<Scene::TextureID> selectedTexture() const {
    return m_selectedTextureId;
  }

  [[nodiscard]] constexpr int* removeOptions() {
    return &m_removeOptions;
  }

  constexpr void selectNode(std::optional<Scene::NodeID> id) {
    m_nextNodeId = id;
    m_nextMeshId = std::nullopt;
    m_nextCameraId = std::nullopt;
    m_nextMaterialId = std::nullopt;
    m_nextTextureId = std::nullopt;
  }

  constexpr void selectMesh(std::optional<Scene::MeshID> id) {
    m_nextNodeId = std::nullopt;
    m_nextMeshId = id;
    m_nextCameraId = std::nullopt;
    m_nextMaterialId = std::nullopt;
    m_nextTextureId = std::nullopt;
  }

  constexpr void selectCamera(std::optional<Scene::CameraID> id) {
    m_nextNodeId = std::nullopt;
    m_nextMeshId = std::nullopt;
    m_nextCameraId = id;
    m_nextMaterialId = std::nullopt;
    m_nextTextureId = std::nullopt;
  }
  
  constexpr void selectMaterial(std::optional<Scene::MaterialID> id) {
    m_nextNodeId = std::nullopt;
    m_nextMeshId = std::nullopt;
    m_nextCameraId = std::nullopt;
    m_nextMaterialId = id;
    m_nextTextureId = std::nullopt;
  }
  
  constexpr void selectTexture(std::optional<Scene::TextureID> id) {
    m_nextNodeId = std::nullopt;
    m_nextMeshId = std::nullopt;
    m_nextCameraId = std::nullopt;
    m_nextMaterialId = std::nullopt;
    m_nextTextureId = id;
  }

  constexpr void setNodeAction(NodeAction action, Scene::NodeID id) {
    m_nodeAction = action;
    m_actionNodeId = id;
  }

  constexpr void clearNodeAction() {
    m_nodeAction = NodeAction_None;
    m_actionNodeId = std::nullopt;
  }

  [[nodiscard]] constexpr std::pair<int, Scene::NodeID> getNodeAction() const {
    if (!m_actionNodeId) return {NodeAction_None, 0};
    return {m_nodeAction, m_actionNodeId.value()};
  }

  constexpr void removeNode(Scene::NodeID id, int options = 0) {
    m_removeOptions |= options;
    setNodeAction(NodeAction_Remove, id);
  }

  [[nodiscard]] constexpr bool rendering() const {
    return m_rendering;
  }

  constexpr void setRendering(bool rendering) {
    m_rendering = rendering;
  }

  void update();

private:
  Store& m_store;

  std::optional<Scene::NodeID> m_selectedNodeId, m_nextNodeId, m_actionNodeId;
  std::optional<Scene::MeshID> m_selectedMeshId, m_nextMeshId;
  std::optional<Scene::CameraID> m_selectedCameraId, m_nextCameraId;
  std::optional<Scene::MaterialID> m_selectedMaterialId, m_nextMaterialId;
  std::optional<Scene::TextureID> m_selectedTextureId, m_nextTextureId;

  int m_nodeAction = NodeAction_None;
  int m_removeOptions = 0;
  bool m_rendering;
};

}

#endif //PLATINUM_STATE_HPP
