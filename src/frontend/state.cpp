#include "state.hpp"

namespace pt::frontend {

void State::update() {
  m_selectedNodeId = m_nextNodeId;

  if (m_nodeAction == NodeAction_Remove && m_actionNodeId) {
    m_store.scene().removeNode(m_actionNodeId.value(), m_removeMode);
    m_selectedNodeId = m_nextNodeId = m_actionNodeId = std::nullopt;
    m_removeMode = Scene::RemoveMode::Recursive;
  }
}

}
