#include "texture.hpp"

#include <utils/metal_utils.hpp>

namespace pt::loaders::texture {
using metal_utils::ns_shared;
using metal_utils::operator ""_ns;

std::pair<MTL::PixelFormat, std::vector<uint8_t>> TextureLoader::getAttributesForTexture(TextureType type) {
  switch (type) {
    case TextureType::sRGB:
    case TextureType::LinearRGB:
      return std::make_pair(MTL::PixelFormatRGBA8Unorm, std::vector<uint8_t>{0, 1, 2, 3});

    case TextureType::Mono:
      return std::make_pair(MTL::PixelFormatR8Unorm, std::vector<uint8_t>{0});
  
    case TextureType::RoughnessMetallic:
      return std::make_pair(MTL::PixelFormatRG8Unorm, std::vector<uint8_t>{1, 2});
      
    case TextureType::HDR:
      return std::make_pair(MTL::PixelFormatRGBA32Float, std::vector<uint8_t>{0, 1, 2, 3});
  }
}

TextureLoader::TextureLoader(MTL::Device* device, MTL::CommandQueue* commandQueue, Scene& scene) noexcept
: m_device(device), m_commandQueue(commandQueue), m_scene(scene) {
  /*
   * Load the shader library
   */
  NS::Error* error = nullptr;
  MTL::Library* lib = device->newLibrary("loaders.metallib"_ns, &error);
  if (!lib) {
    std::println(stderr,
       "TextureLoader: Failed to load shader library: {}",
      error->localizedDescription()->utf8String()
     );
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
    desc,
    MTL::PipelineOptionNone,
    nullptr,
    &error
  );
  if (!m_textureConverterPso) {
    std::println(stderr,
      "TextureLoader: Failed to create texture converter pipeline: {}",
      error->localizedDescription()->utf8String()
    );
    assert(false);
  }
}

Scene::TextureID TextureLoader::loadFromFile(const fs::path &path, std::string_view name, TextureType type) {
  const auto in = OIIO::ImageInput::open(path.string());
  
  return load(in, name, type);
}

Scene::TextureID TextureLoader::loadFromMemory(const uint8_t *data, uint32_t len, std::string_view name, TextureType type) {
  /*
   * Create a temporary read buffer and decode the image from memory
   * We set the x-stride to four channels so our buffer can take any type of input image. This
   * assumes the image is in PNG format as that is usually the case for glTF, which is currently the
   * only use case.
   */
  OIIO::Filesystem::IOMemReader memReader(data, len);
  const auto in = OIIO::ImageInput::open("a.png", nullptr, &memReader);
  
  return load(in, name, type);
}

Scene::TextureID TextureLoader::load(const std::unique_ptr<OIIO::ImageInput>& in, std::string_view name, TextureType type) {
  const auto& spec = in->spec();
  
  // We use this later to fill the alpha channel with 1 if it's not present.
  // Alpha for HDR texture is unsupported.
  const bool hasAlphaChannel = type != TextureType::HDR && spec.alpha_channel != -1;
  const size_t pixelStride = type == TextureType::HDR ? sizeof(float4) : sizeof(uchar4);
  
  auto readBuffer = m_device->newBuffer(
		pixelStride * spec.width * spec.height,
    MTL::ResourceStorageModeShared
  );
  in->read_image(0, 0, 0, -1, spec.format, readBuffer->contents(), pixelStride);
  in->close();
  
  /*
   * Check if the texture has any pixels with alpha < 1
   */
  bool hasAlpha = false;
  if (hasAlphaChannel) {
    auto contents = static_cast<uchar4*>(readBuffer->contents());
    for (uint32_t i = 0; i < readBuffer->length() / sizeof(uchar4); i++) {
      if (contents[i].a < 255) {
        hasAlpha = true;
        break;
      }
    }
  }
  
  /*
   * Create a temporary texture as input to the texture converter shader. We just make this texture
   * RGBA, since it's only used while loading we don't care about the extra memory use.
   */
  auto srcDesc = metal_utils::makeTextureDescriptor({
    .width = uint32_t(spec.width),
    .height = uint32_t(spec.height),
    .format = type == TextureType::sRGB ? MTL::PixelFormatRGBA8Unorm_sRGB : MTL::PixelFormatRGBA8Unorm,
  });
  
  auto srcTexture = m_device->newTexture(srcDesc);
  srcTexture->replaceRegion(
    MTL::Region(0, 0, 0, spec.width, spec.height, 1),
    0,
    readBuffer->contents(),
		pixelStride * spec.width
  );
  
  /*
   * Create the actual texture we're going to store. The pixel format here depends on usage.
   */
  auto [texturePixelFormat, textureChannels] = getAttributesForTexture(type);
  auto desc = metal_utils::makeTextureDescriptor({
    .width = uint32_t(spec.width),
    .height = uint32_t(spec.height),
    .format = texturePixelFormat,
    .storageMode = MTL::StorageModePrivate,
    .usage = MTL::TextureUsageShaderRead | MTL::TextureUsageShaderWrite,
  });
  auto texture = m_device->newTexture(desc);
  
  /*
   * Run the texture converter shader to store the actual texture
   */
  auto threadsPerThreadgroup = MTL::Size(8, 8, 1);
  auto threadgroups = MTL::Size(
    (spec.width + threadsPerThreadgroup.width - 1) / threadsPerThreadgroup.width,
    (spec.height + threadsPerThreadgroup.height - 1) / threadsPerThreadgroup.height,
    1
  );
  
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
   * Store the actual texture in our scene and return the ID so it can be set on the materials that
   * use it, replacing the placeholder
   */
  return m_scene.addTexture(name, NS::TransferPtr(texture), hasAlpha);
}

}
