#include "state.hpp"

namespace pt::frontend {

void State::update() {
  m_selectedNodeId = m_nextNodeId;
  m_selectedMeshId = m_nextMeshId;
  m_selectedCameraId = m_nextCameraId;
  m_selectedMaterialId = m_nextMaterialId;

  if (m_nodeAction == NodeAction_Remove && m_actionNodeId) {
    m_store.scene().removeNode(m_actionNodeId.value(), m_removeOptions);
    m_selectedNodeId = m_nextNodeId = m_actionNodeId = std::nullopt;
    m_removeOptions = 0;
  }
}

}
