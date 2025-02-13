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

  [[nodiscard]] constexpr Scene::RemoveMode& removeMode() {
    return m_removeMode;
  }

  constexpr void selectNode(std::optional<Scene::NodeID> id) {
    m_nextNodeId = id;
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
    if (!m_actionNodeId) return {NodeAction_None, m_store.scene().root().id()};
    return {m_nodeAction, m_actionNodeId.value()};
  }

  constexpr void removeNode(Scene::NodeID id, Scene::RemoveMode mode = Scene::RemoveMode::Recursive) {
    m_removeMode = mode;
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

  int m_nodeAction = NodeAction_None;
  Scene::RemoveMode m_removeMode = Scene::RemoveMode::Recursive;
  bool m_rendering;
};

}

#endif //PLATINUM_STATE_HPP
