#ifndef PLATINUM_MESH_HPP
#define PLATINUM_MESH_HPP

#include <vector>
#include <simd/simd.h>

using namespace simd;

namespace pt {

struct VertexData {
  float3 normal;
  float4 tangent;
  float2 texCoords;
};

class Mesh {
public:
  explicit Mesh(
    std::vector<float3>&& vertexPositions,
    std::vector<VertexData>&& vertexData,
    std::vector<uint32_t>&& indices
  ) noexcept;

  [[nodiscard]] constexpr const auto& vertexPositions() const {
    return m_vertexPositions;
  }

  [[nodiscard]] constexpr const auto& vertexData() const {
    return m_vertexData;
  }

  [[nodiscard]] constexpr const auto& indices() const {
    return m_indices;
  }

private:
  std::vector<float3> m_vertexPositions;
  std::vector<VertexData> m_vertexData;
  std::vector<uint32_t> m_indices;
};

}

#endif //PLATINUM_MESH_HPP
