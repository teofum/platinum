#ifndef PLATINUM_LOADER_TEXTURE_HPP
#define PLATINUM_LOADER_TEXTURE_HPP

#include <filesystem>
#include <simd/simd.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/filesystem.h>

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
  explicit TextureLoader(MTL::Device* device, MTL::CommandQueue* commandQueue, Scene& scene) noexcept;
  
  Scene::TextureID loadFromFile(const fs::path& path, std::string_view name, TextureType type);
  
  Scene::TextureID loadFromMemory(const uint8_t* data, uint32_t len, std::string_view name, TextureType type);
  
private:
  MTL::Device* m_device;
  MTL::CommandQueue* m_commandQueue;
  MTL::ComputePipelineState* m_textureConverterPso = nullptr;
  
  Scene& m_scene;
  
  static std::tuple<MTL::PixelFormat, MTL::PixelFormat, std::vector<uint8_t>> getAttributesForTexture(TextureType type);
  
  Scene::TextureID load(const std::unique_ptr<OIIO::ImageInput>& in, std::string_view name, TextureType type);
};

}

#endif
