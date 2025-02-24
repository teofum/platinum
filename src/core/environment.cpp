#include "environment.hpp"

namespace pt {

void Environment::rebuildAliasTable(MTL::Device* device, MTL::Texture* texture) {
  uint64_t width = texture->width();
  uint64_t height = texture->height();
  uint64_t n = width * height;

  /*
   * If there is an existing alias table, release it, then create the buffer for the new table.
   */
  if (m_aliasTable) m_aliasTable->release();

  size_t aliasTableSize = n * sizeof(AliasEntry);
  m_aliasTable = device->newBuffer(aliasTableSize, MTL::ResourceStorageModeShared);
  auto* aliasTableHandle = static_cast<AliasEntry*>(m_aliasTable->contents());

  /*
   * Create a temporary texture buffer, so we can read pixels off the texture
   */
  auto* textureBuffer = device->newBuffer(n * sizeof(float4), MTL::ResourceStorageModeShared);
  texture->getBytes(textureBuffer->contents(), width * sizeof(float4), MTL::Region(0, 0, width, height), 0);
  auto* pixels = static_cast<float4*>(textureBuffer->contents());

  /*
   * First, calculate the probability of sampling any given pixel. We make this proportional to
   * luma (brightness), then scale by number of samples.
   */
  float totalImportance = 0.0f;
  std::vector<float> importance;
  importance.reserve(n);

  const float3 lumaCoeffs{0.2126, 0.7152, 0.0722};
  for (uint64_t i = 0; i < n; i++) {
    float luma = dot(pixels[i].xyz, lumaCoeffs);
    importance.push_back(luma);
    totalImportance += luma;
  }

  float scale = float(n) / totalImportance;
  for (uint64_t i = 0; i < n; i++) {
    importance[i] *= scale;
    aliasTableHandle[i].pdf = importance[i];
  }

  /*
   * Build the alias table using Vose's method (modified for numerical stability)
   * Reference: https://www.keithschwarz.com/darts-dice-coins/
   */
  std::vector<size_t> small, large;
  for (uint64_t i = 0; i < n; i++) {
    if (importance[i] < 1.0f) small.push_back(i);
    else large.push_back(i);
  }

  while (!small.empty() && !large.empty()) {
    size_t l = small.back();
    small.pop_back();
    size_t g = large.back();
    large.pop_back();

    aliasTableHandle[l].p = importance[l];
    aliasTableHandle[l].aliasIdx = uint32_t(g);

    importance[g] = (importance[g] + importance[l]) - 1.0f;
    if (importance[g] < 1.0f) small.push_back(g);
    else large.push_back(g);
  }

  while (!large.empty()) {
    size_t g = large.back();
    large.pop_back();

    aliasTableHandle[g].p = 1.0f;
  }

  // This can only happen due to numerical instability causing probabilities that should be
  // in the "large" worklist to end up in "small", so we treat them as large (= 1)
  while (!small.empty()) {
    size_t l = small.back();
    small.pop_back();

    aliasTableHandle[l].p = 1.0f;
  }

  /*
   * Clean up
   */
  textureBuffer->release();
}

void Environment::setTexture(std::optional<Environment::TextureID> id, MTL::Device* device, MTL::Texture* texture) {
  // If we set the texture to something (non empty) and it's different from the current one, we need
  // to rebuild the alias table
  if (id && id != m_textureId) rebuildAliasTable(device, texture);

  m_textureId = id;
}

void Environment::setTexture(std::optional<TextureID> id, MTL::Buffer* aliasTable) {
  if (m_aliasTable) m_aliasTable->release();
  m_aliasTable = aliasTable;
  m_textureId = id;
}

}
