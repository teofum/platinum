#ifndef PLATINUM_MESH_HPP
#define PLATINUM_MESH_HPP

#include <vector>
#include <simd/simd.h>
#include <Metal/Metal.hpp>

using namespace simd;

namespace pt {

struct VertexData {
  float3 normal;
  float4 tangent;
  float2 texCoords;
};

class Mesh {
public:
  Mesh(
    MTL::Device* device,
    const std::vector<float3>& vertexPositions,
    const std::vector<VertexData>& vertexData,
    const std::vector<uint32_t>& indices
  ) noexcept;

  Mesh(const Mesh& m) noexcept;

  Mesh(Mesh&& m) noexcept;

  Mesh& operator=(const Mesh& m);

  Mesh& operator=(Mesh&& m) noexcept;

  ~Mesh();

  [[nodiscard]] constexpr const MTL::Buffer* vertexPositions() const {
    return m_vertexPositions;
  }

  [[nodiscard]] constexpr const MTL::Buffer* vertexData() const {
    return m_vertexData;
  }

  [[nodiscard]] constexpr const MTL::Buffer* indices() const {
    return m_indices;
  }

  [[nodiscard]] constexpr size_t indexCount() const {
    return m_indexCount;
  }

  [[nodiscard]] constexpr size_t vertexCount() const {
    return m_vertexCount;
  }

private:
  size_t m_indexCount, m_vertexCount;

  MTL::Buffer* m_vertexPositions;
  MTL::Buffer* m_vertexData;
  MTL::Buffer* m_indices;
};

}

#endif //PLATINUM_MESH_HPP
