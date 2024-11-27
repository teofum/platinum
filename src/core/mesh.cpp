#include "mesh.hpp"

namespace pt {

Mesh::Mesh(
  MTL::Device* device,
  const std::vector<float3>& vertexPositions,
  const std::vector<VertexData>& vertexData,
  const std::vector<uint32_t>& indices
) noexcept: m_indexCount(indices.size()),
            m_vertexCount(vertexPositions.size()) {
  size_t vpSize = vertexPositions.size() * sizeof(float3);
  m_vertexPositions = device->newBuffer(vpSize, MTL::ResourceStorageModeShared);
  memcpy(m_vertexPositions->contents(), vertexPositions.data(), vpSize);

  size_t vdSize = vertexData.size() * sizeof(VertexData);
  m_vertexData = device->newBuffer(vdSize, MTL::ResourceStorageModeShared);
  memcpy(m_vertexData->contents(), vertexData.data(), vdSize);

  size_t iSize = indices.size() * sizeof(uint32_t);
  m_indices = device->newBuffer(iSize, MTL::ResourceStorageModeShared);
  memcpy(m_indices->contents(), indices.data(), iSize);
}

Mesh::~Mesh() {
  m_vertexPositions->release();
  m_vertexData->release();
  m_indices->release();
}

Mesh::Mesh(const Mesh& m) noexcept {
  m_indexCount = m.m_indexCount;
  m_vertexCount = m.m_vertexCount;
  m_vertexPositions = m.m_vertexPositions->retain();
  m_vertexData = m.m_vertexData->retain();
  m_indices = m.m_indices->retain();
}

Mesh::Mesh(Mesh&& m) noexcept {
  m_indexCount = m.m_indexCount;
  m_vertexCount = m.m_vertexCount;
  m_vertexPositions = m.m_vertexPositions;
  m_vertexData = m.m_vertexData;
  m_indices = m.m_indices;

  m.m_vertexPositions = nullptr;
  m.m_vertexData = nullptr;
  m.m_indices = nullptr;
}

Mesh& Mesh::operator=(const Mesh& m) {
  if (&m != this) {
    m_indexCount = m.m_indexCount;
    m_vertexCount = m.m_vertexCount;
    m_vertexPositions = m.m_vertexPositions->retain();
    m_vertexData = m.m_vertexData->retain();
    m_indices = m.m_indices->retain();
  }
  return *this;
}

Mesh& Mesh::operator=(Mesh&& m) noexcept {
  m_indexCount = m.m_indexCount;
  m_vertexCount = m.m_vertexCount;
  m_vertexPositions = m.m_vertexPositions;
  m_vertexData = m.m_vertexData;
  m_indices = m.m_indices;

  m.m_vertexPositions = nullptr;
  m.m_vertexData = nullptr;
  m.m_indices = nullptr;
  return *this;
}

}
