#ifndef PLATINUM_ENVIRONMENT_HPP
#define PLATINUM_ENVIRONMENT_HPP

#ifndef __METAL_VERSION__

#include <simd/simd.h>
#include <Metal/Metal.hpp>

using namespace simd;

#endif

namespace pt {

struct AliasEntry {
  float pdf;
  float p;
  uint32_t aliasIdx;
};

#ifndef __METAL_VERSION__

class Environment {
public:
  using TextureID = int32_t;
  
  constexpr std::optional<TextureID> textureId() const { return m_textureId; }
  constexpr MTL::Buffer* aliasTable() const { return m_aliasTable; }
  
  void setTexture(std::optional<TextureID> id, MTL::Device* device, MTL::Texture* texture);
  
private:
  std::optional<TextureID> m_textureId = std::nullopt;
  MTL::Buffer* m_aliasTable = nullptr;
  
  void rebuildAliasTable(MTL::Device* device, MTL::Texture* texture);
};

#endif

}

#endif
