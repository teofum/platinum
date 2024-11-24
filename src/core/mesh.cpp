#include "mesh.hpp"

namespace pt {

Mesh::Mesh(
  std::vector<float3>&& vertices,
  std::vector<uint32_t>&& indices
) noexcept: m_vertices(std::move(vertices)), m_indices(std::move(indices)) {
}

}
