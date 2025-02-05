#ifndef PLATINUM_MESH_HPP
#define PLATINUM_MESH_HPP

#ifndef __METAL_VERSION__

#include <vector>
#include <Metal/Metal.hpp>

#endif

#include <simd/simd.h>

using namespace simd;

namespace pt {

struct VertexData {
  float3 normal;
  float4 tangent;
  float2 texCoords;
};

#ifndef __METAL_VERSION__

class Mesh {
public:
  Mesh(
    MTL::Device* device,
    const std::vector<float3>& vertexPositions,
    const std::vector<VertexData>& vertexData,
    const std::vector<uint32_t>& indices,
    const std::vector<uint32_t>& materialIndices
  ) noexcept;

  Mesh(const Mesh& m) noexcept = delete;
  Mesh(Mesh&& m) noexcept;

  Mesh& operator=(const Mesh& m) = delete;
  Mesh& operator=(Mesh&& m) noexcept;

  ~Mesh();

  [[nodiscard]] constexpr MTL::Buffer* vertexPositions() const { return m_vertexPositions; }
  [[nodiscard]] constexpr MTL::Buffer* vertexData() const { return m_vertexData; }
  [[nodiscard]] constexpr MTL::Buffer* indices() const { return m_indices; }
  [[nodiscard]] constexpr MTL::Buffer* materialIndices() const { return m_materialIndices; }
  
  [[nodiscard]] constexpr size_t indexCount() const { return m_indexCount; }
  [[nodiscard]] constexpr size_t vertexCount() const { return m_vertexCount; }
  [[nodiscard]] constexpr size_t materialCount() const { return m_materialIndices->length() / sizeof(uint32_t); }
  
  void generateTangents();

private:
  size_t m_indexCount, m_vertexCount;

  MTL::Buffer* m_vertexPositions;
  MTL::Buffer* m_vertexData;
  MTL::Buffer* m_indices;
  MTL::Buffer* m_materialIndices;
};

#endif

}

#endif //PLATINUM_MESH_HPP
