#ifndef PLATINUM_LOADER_TEXTURE_HPP
#define PLATINUM_LOADER_TEXTURE_HPP

#include <filesystem>
#include <simd/simd.h>

#include <core/scene.hpp>

namespace fs = std::filesystem;
using namespace simd;

namespace pt::loaders::texture {

enum class TextureType {
  sRGB,
  LinearRGB,
  Mono,
  RoughnessMetallic,
  HDR,
};

class TextureLoader {
public:
  explicit TextureLoader(MTL::Device *device, MTL::CommandQueue *commandQueue,
                         Scene &scene) noexcept;

  Scene::AssetID loadFromFile(const fs::path &path, std::string_view name,
                              TextureType type);

  Scene::AssetID loadFromMemory(const uint8_t *data, uint32_t len,
                                std::string_view name, TextureType type);

private:
  MTL::Device *m_device;
  MTL::CommandQueue *m_commandQueue;
  MTL::ComputePipelineState *m_textureConverterPso = nullptr;

  Scene &m_scene;

  static MTL::PixelFormat getSourceTextureFormat(TextureType type,
                                                 int format); // TODO fix this
  static std::tuple<MTL::PixelFormat, std::vector<uint8_t>>
  getAttributesForTexture(TextureType type);

  Scene::AssetID load(const uint8_t *data, std::string_view name,
                      TextureType type, uint32_t width, uint32_t height,
                      size_t pixelStride, bool hasAlphaChannel);
};

} // namespace pt::loaders::texture

#endif
