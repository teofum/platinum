#include "texture.hpp"

#include <cassert>
#include <cstring>

#include <lodepng.h>
#include <print>
#include <stb_image.h>

#include <loaders/exr.hpp>
#include <utils/metal_utils.hpp>

namespace pt::loaders::texture {
using metal_utils::operator""_ns;

MTL::PixelFormat
TextureLoader::getSourceTextureFormat(pt::loaders::texture::TextureType type,
                                      int format) {
  switch (type) {
  case TextureType::HDR:
    return MTL::PixelFormatRGBA32Float;
  case TextureType::sRGB:
    return MTL::PixelFormatRGBA8Unorm_sRGB;
  case TextureType::LinearRGB:
  case TextureType::RoughnessMetallic:
  case TextureType::Mono:
    return MTL::PixelFormatRGBA8Unorm;
  default:
    return MTL::PixelFormatInvalid;
  }
}

std::tuple<MTL::PixelFormat, std::vector<uint8_t>>
TextureLoader::getAttributesForTexture(TextureType type) {
  switch (type) {
  case TextureType::sRGB:
    return std::make_tuple(MTL::PixelFormatRGBA8Unorm_sRGB,
                           std::vector<uint8_t>{0, 1, 2, 3});
  case TextureType::LinearRGB:
    return std::make_tuple(MTL::PixelFormatRGBA8Unorm,
                           std::vector<uint8_t>{0, 1, 2, 3});
  case TextureType::Mono:
    return std::make_tuple(MTL::PixelFormatR8Unorm, std::vector<uint8_t>{0});
  case TextureType::RoughnessMetallic:
    return std::make_tuple(MTL::PixelFormatRG8Unorm,
                           std::vector<uint8_t>{1, 2});
  case TextureType::HDR:
    return std::make_tuple(MTL::PixelFormatRGBA32Float,
                           std::vector<uint8_t>{0, 1, 2, 3});
  }
}

TextureLoader::TextureLoader(MTL::Device *device,
                             MTL::CommandQueue *commandQueue,
                             Scene &scene) noexcept
    : m_device(device), m_commandQueue(commandQueue), m_scene(scene) {
  /*
   * Load the shader library
   */
  NS::Error *error = nullptr;
  MTL::Library *lib = device->newLibrary("loaders.metallib"_ns, &error);
  if (!lib) {
    std::println(stderr, "TextureLoader: Failed to load shader library: {}",
                 error->localizedDescription()->utf8String());
    assert(false);
  }

  /*
   * Build the texture converter pipeline
   */
  auto desc = metal_utils::makeComputePipelineDescriptor({
      .function = metal_utils::getFunction(lib, "convertTexture"),
      .threadGroupSizeIsMultipleOfExecutionWidth = true,
  });

  m_textureConverterPso = device->newComputePipelineState(
      desc, MTL::PipelineOptionNone, nullptr, &error);
  if (!m_textureConverterPso) {
    std::println(
        stderr,
        "TextureLoader: Failed to create texture converter pipeline: {}",
        error->localizedDescription()->utf8String());
    assert(false);
  }
}

Scene::AssetID TextureLoader::loadFromFile(const fs::path &path,
                                           std::string_view name,
                                           TextureType type) {
  if (type == TextureType::HDR) {
    if (path.extension().string() == ".exr") {
      // Use tinyexr for EXR file support
      int32_t width, height;
      float *rgba;
      const char *err;
      int r = LoadEXR(&rgba, &width, &height, path.c_str(), &err);
      assert(r >= 0);

      return load((uint8_t *)rgba, name, type, width, height, 16, false);
    } else {
      // Otherwise assume Radiance HDR and use stb_image
      int32_t width, height;
      const float *pixels =
          stbi_loadf(path.c_str(), &width, &height, nullptr, 4);

      return load((uint8_t *)pixels, name, type, width, height, 16, false);
    }
  } else {
    int32_t width, height;
    const uint8_t *pixels =
        stbi_load(path.c_str(), &width, &height, nullptr, 4);

    return load(pixels, name, type, width, height, 4, true);
  }
}

Scene::AssetID TextureLoader::loadFromMemory(const uint8_t *data, uint32_t len,
                                             std::string_view name,
                                             TextureType type) {
  int32_t width, height;
  const uint8_t *pixels =
      stbi_load_from_memory(data, len, &width, &height, nullptr, 4);

  return load(pixels, name, type, width, height, 4, true);
}

Scene::AssetID TextureLoader::load(const uint8_t *data, std::string_view name,
                                   TextureType type, uint32_t width,
                                   uint32_t height, size_t pixelStride,
                                   bool hasAlphaChannel) {
  auto readBuffer = m_device->newBuffer(pixelStride * width * height,
                                        MTL::ResourceStorageModeShared);
  memcpy(readBuffer->contents(), data, readBuffer->length());

  /*
   * Check if the texture has any pixels with alpha < 1
   * TODO: this only works for 8 bit per channel textures
   *  alpha is unsupported for textures with >8 bits per channel
   */
  bool hasAlpha = false;
  if (hasAlphaChannel) {
    auto contents = static_cast<uchar4 *>(readBuffer->contents());
    for (uint32_t i = 0; i < readBuffer->length() / sizeof(uchar4); i++) {
      if (contents[i].a < 255) {
        hasAlpha = true;
        break;
      }
    }
  }

  /*
   * Create a temporary texture as input to the texture converter shader. We
   * just make this texture RGBA, since it's only used while loading we don't
   * care about the extra memory use.
   */
  auto srcPixelFormat = getSourceTextureFormat(type, 0);
  auto [texturePixelFormat, textureChannels] = getAttributesForTexture(type);
  auto srcDesc = metal_utils::makeTextureDescriptor({
      .width = uint32_t(width),
      .height = uint32_t(height),
      .format = srcPixelFormat,
  });

  auto srcTexture = m_device->newTexture(srcDesc);
  srcTexture->replaceRegion(MTL::Region(0, 0, 0, width, height, 1), 0,
                            readBuffer->contents(), pixelStride * width);

  /*
   * Create the actual texture we're going to store. The pixel format here
   * depends on usage.
   */
  auto desc = metal_utils::makeTextureDescriptor({
      .width = uint32_t(width),
      .height = uint32_t(height),
      .storageMode = MTL::StorageModeShared,
      .format = texturePixelFormat,
      .usage = MTL::TextureUsageShaderRead | MTL::TextureUsageShaderWrite,
  });
  auto texture = m_device->newTexture(desc);

  /*
   * Run the texture converter shader to store the actual texture
   */
  auto threadsPerThreadgroup = MTL::Size(8, 8, 1);
  auto threadgroups = MTL::Size((width + threadsPerThreadgroup.width - 1) /
                                    threadsPerThreadgroup.width,
                                (height + threadsPerThreadgroup.height - 1) /
                                    threadsPerThreadgroup.height,
                                1);

  auto cmd = m_commandQueue->commandBuffer();
  auto enc = cmd->computeCommandEncoder();

  enc->setComputePipelineState(m_textureConverterPso);

  const uint8_t nChannels = textureChannels.size();
  enc->setBytes(textureChannels.data(), nChannels * sizeof(uint8_t), 0);
  enc->setBytes(&nChannels, sizeof(uint8_t), 1);
  enc->setBytes(&hasAlphaChannel, sizeof(bool), 2);

  enc->setTexture(srcTexture, 0);
  enc->setTexture(texture, 1);

  enc->dispatchThreadgroups(threadgroups, threadsPerThreadgroup);

  enc->endEncoding();
  cmd->commit();

  // Clean up temp resources
  readBuffer->release();
  srcTexture->release();

  /*
   * Store the actual texture in our scene and return the ID so it can be set
   on
   * the materials that use it, replacing the placeholder
   */
  Texture asset(texture, name, hasAlpha);
  return m_scene.createAsset(std::move(asset));
  return 0;
}

} // namespace pt::loaders::texture
