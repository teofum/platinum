#include "mesh.hpp"

namespace pt {

Mesh::Mesh(
  std::vector<float3>&& vertexPositions,
  std::vector<VertexData>&& vertexData,
  std::vector<uint32_t>&& indices
) noexcept: m_vertexPositions(std::move(vertexPositions)),
            m_vertexData(std::move(vertexData)),
            m_indices(std::move(indices)) {
}

}
