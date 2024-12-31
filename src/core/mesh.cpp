#include "mesh.hpp"

#include <mikktspace.h>

namespace pt {

/*
 * Callback functions for mikktspace tangent generation
 */
namespace mikkt {

static int getNumFaces(const SMikkTSpaceContext* ctx) {
  return int(static_cast<Mesh*>(ctx->m_pUserData)->indexCount()) / 3;
}

static int getNumVerticesOfFace(const SMikkTSpaceContext* ctx, int face) {
  return 3; // Only triangles here
}

static void getPosition(const SMikkTSpaceContext* ctx, float* outPos, int face, int vert) {
  auto mesh = static_cast<Mesh*>(ctx->m_pUserData);
  uint32_t vertexIdx = static_cast<uint32_t*>(mesh->indices()->contents())[face * 3 + vert];
  
  auto vertexPos = static_cast<float3*>(mesh->vertexPositions()->contents())[vertexIdx];
  outPos[0] = vertexPos.x;
  outPos[1] = vertexPos.y;
  outPos[2] = vertexPos.z;
}

static void getNormal(const SMikkTSpaceContext* ctx, float* outNormal, int face, int vert) {
  auto mesh = static_cast<Mesh*>(ctx->m_pUserData);
  uint32_t vertexIdx = static_cast<uint32_t*>(mesh->indices()->contents())[face * 3 + vert];
  
  auto vertexData = static_cast<VertexData*>(mesh->vertexData()->contents())[vertexIdx];
  outNormal[0] = vertexData.normal.x;
  outNormal[1] = vertexData.normal.y;
  outNormal[2] = vertexData.normal.z;
}

static void getTexCoord(const SMikkTSpaceContext* ctx, float* outTexCoord, int face, int vert) {
  auto mesh = static_cast<Mesh*>(ctx->m_pUserData);
  uint32_t vertexIdx = static_cast<uint32_t*>(mesh->indices()->contents())[face * 3 + vert];
  
  auto vertexData = static_cast<VertexData*>(mesh->vertexData()->contents())[vertexIdx];
  outTexCoord[0] = vertexData.texCoords.x;
  outTexCoord[1] = vertexData.texCoords.y;
}

static void setTSpaceBasic(const SMikkTSpaceContext* ctx, const float* tangent, float sign, int face, int vert) {
  auto mesh = static_cast<Mesh*>(ctx->m_pUserData);
  uint32_t vertexIdx = static_cast<uint32_t*>(mesh->indices()->contents())[face * 3 + vert];
  
  auto vertexData = static_cast<VertexData*>(mesh->vertexData()->contents()) + vertexIdx;
  vertexData->tangent = float4{ tangent[0], tangent[1], tangent[2], sign };
}

}

Mesh::Mesh(
  MTL::Device* device,
  const std::vector<float3>& vertexPositions,
  const std::vector<VertexData>& vertexData,
  const std::vector<uint32_t>& indices,
  const std::vector<uint32_t>& materialIndices
) noexcept: m_indexCount(indices.size()),
            m_vertexCount(vertexPositions.size()
) {
  size_t vpSize = vertexPositions.size() * sizeof(float3);
  m_vertexPositions = device->newBuffer(vpSize, MTL::ResourceStorageModeShared);
  memcpy(m_vertexPositions->contents(), vertexPositions.data(), vpSize);

  size_t vdSize = vertexData.size() * sizeof(VertexData);
  m_vertexData = device->newBuffer(vdSize, MTL::ResourceStorageModeShared);
  memcpy(m_vertexData->contents(), vertexData.data(), vdSize);

  size_t iSize = indices.size() * sizeof(uint32_t);
  m_indices = device->newBuffer(iSize, MTL::ResourceStorageModeShared);
  memcpy(m_indices->contents(), indices.data(), iSize);
              
  size_t miSize = materialIndices.size() * sizeof(uint32_t);
  m_materialIndices = device->newBuffer(miSize, MTL::ResourceStorageModeShared);
  memcpy(m_materialIndices->contents(), materialIndices.data(), miSize);
}

Mesh::~Mesh() {
  m_vertexPositions->release();
  m_vertexData->release();
  m_indices->release();
  m_materialIndices->release();
}

Mesh::Mesh(Mesh&& m) noexcept {
  m_indexCount = m.m_indexCount;
  m_vertexCount = m.m_vertexCount;
  m_vertexPositions = m.m_vertexPositions;
  m_vertexData = m.m_vertexData;
  m_indices = m.m_indices;
  m_materialIndices = m.m_materialIndices;

  m.m_vertexPositions = nullptr;
  m.m_vertexData = nullptr;
  m.m_indices = nullptr;
  m.m_materialIndices = nullptr;
}

Mesh& Mesh::operator=(Mesh&& m) noexcept {
  m_indexCount = m.m_indexCount;
  m_vertexCount = m.m_vertexCount;
  m_vertexPositions = m.m_vertexPositions;
  m_vertexData = m.m_vertexData;
  m_indices = m.m_indices;
  m_materialIndices = m.m_materialIndices;

  m.m_vertexPositions = nullptr;
  m.m_vertexData = nullptr;
  m.m_indices = nullptr;
  m.m_materialIndices = nullptr;
  return *this;
}

void Mesh::generateTangents() {
  /*
   * TODO: Convert to unindexed vertices before tangent calculation and weld after
   * We run tangent generation on index vertices for simplicity. This is wrong, and may result in
   * incorrect tangents for some cases. It mostly works, but we should fix it later.
   */
  SMikkTSpaceInterface interface {
    .m_getNumFaces = mikkt::getNumFaces,
    .m_getNumVerticesOfFace = mikkt::getNumVerticesOfFace,
    .m_getPosition = mikkt::getPosition,
    .m_getNormal = mikkt::getNormal,
    .m_getTexCoord = mikkt::getTexCoord,
    .m_setTSpaceBasic = mikkt::setTSpaceBasic,
    .m_setTSpace = nullptr,
  };
  
  SMikkTSpaceContext ctx {
    .m_pInterface = &interface,
    .m_pUserData = static_cast<void*>(this),
  };
  
  genTangSpaceDefault(&ctx);
}

}
