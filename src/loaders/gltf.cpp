#include "gltf.hpp"

#include <OpenImageIO/imageio.h>
#include <OpenImageIO/filesystem.h>

#include <utils/metal_utils.hpp>

namespace pt::loaders::gltf {
using metal_utils::ns_shared;
using metal_utils::operator ""_ns;

static float3 eulerFromQuat(fastgltf::math::fquat q) {
  float qw = q.w(), qx = q.x(), qy = q.y(), qz = q.z();

  return {
    atan2(
      2.0f * (qw * qx - qy * qz),
      1.0f - 2.0f * (qx * qx + qz * qz)
    ),
    atan2(
      2.0f * (qw * qy - qx * qz),
      1.0f - 2.0f * (qy * qy + qz * qz)
    ),
    asin(2.0f * clamp(qx * qy + qw * qz, -0.5f, 0.5f)),
  };
}

GltfLoader::GltfLoader(MTL::Device* device, MTL::CommandQueue* commandQueue, Scene& scene) noexcept
: m_device(device), m_commandQueue(commandQueue), m_scene(scene) {
  /*
   * Load the shader library
   */
  NS::Error* error = nullptr;
  MTL::Library* lib = m_device->newLibrary("loaders.metallib"_ns, &error);
  if (!lib) {
    std::println(stderr,
      "GltfLoader: Failed to load shader library: {}",
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
  
  m_textureConverterPso = m_device->newComputePipelineState(
    desc,
    MTL::PipelineOptionNone,
    nullptr,
    &error
  );
  if (!m_textureConverterPso) {
    std::println(stderr,
       "GltfLoader: Failed to create texture converter pipeline: {}",
       error->localizedDescription()->utf8String()
     );
    assert(false);
  }
}

std::pair<MTL::PixelFormat, std::vector<uint8_t>> GltfLoader::getAttributesForTexture(TextureType type) {
  switch (type) {
    case TextureType::RGBA:
    case TextureType::RGB:
      return std::make_pair(MTL::PixelFormatRGBA8Unorm, std::vector<uint8_t>{0, 1, 2, 3});

    case TextureType::Mono:
      return std::make_pair(MTL::PixelFormatR8Unorm, std::vector<uint8_t>{0});
  
    case TextureType::RoughnessMetallic:
      return std::make_pair(MTL::PixelFormatRG8Unorm, std::vector<uint8_t>{1, 2});
  }
}

/*
 * Load a scene from glTF.
 */
void GltfLoader::load(const fs::path& path, int options) {
  auto gltfFile = fastgltf::GltfDataBuffer::FromPath(path);
  if (gltfFile.error() != fastgltf::Error::None) {
    std::println(
      stderr,
      "[Error] gltf: ({}) {}",
      getErrorName(gltfFile.error()),
      getErrorMessage(gltfFile.error())
    );
    return;
  }

  auto parser = fastgltf::Parser(
    fastgltf::Extensions::KHR_materials_emissive_strength |
    fastgltf::Extensions::KHR_materials_transmission |
    fastgltf::Extensions::KHR_materials_ior |
    fastgltf::Extensions::KHR_materials_anisotropy |
    fastgltf::Extensions::KHR_materials_clearcoat |
    fastgltf::Extensions::KHR_materials_volume
  );

  auto asset = parser.loadGltf(
    gltfFile.get(),
    path.parent_path(),
    fastgltf::Options::GenerateMeshIndices |
    fastgltf::Options::DecomposeNodeMatrices |
    fastgltf::Options::LoadExternalBuffers
  );

  if (asset.error() != fastgltf::Error::None) {
    std::println(
      stderr,
      "[Error] gltf: ({}) {}",
      getErrorName(gltfFile.error()),
      getErrorMessage(gltfFile.error())
    );
    return;
  }
  m_asset = std::make_unique<fastgltf::Asset>(std::move(asset.get()));
  m_options = options;
  
  m_materialIds.reserve(m_asset->materials.size());
  for (const auto& material: m_asset->materials)
    loadMaterial(material);
  
  m_textureIds.reserve(m_texturesToLoad.size());
  for (const auto& [idx, type]: m_texturesToLoad) {
    auto textureId = loadTexture(m_asset->textures[idx], type);
    
    // Replace the index with the real ID on any materials using the texture
    for (auto mid: m_materialIds) {
      auto* material = m_scene.material(mid);
      if (material->baseTextureId == idx) material->baseTextureId = textureId;
      if (material->rmTextureId == idx) material->rmTextureId = textureId;
      if (material->transmissionTextureId == idx) material->transmissionTextureId = textureId;
      if (material->emissionTextureId == idx) material->emissionTextureId = textureId;
      if (material->clearcoatTextureId == idx) material->clearcoatTextureId = textureId;
    }
  }

  m_meshIds.reserve(m_asset->meshes.size());
  for (const auto& mesh: m_asset->meshes)
    loadMesh(mesh);

  m_cameraIds.reserve(m_asset->cameras.size());
  for (const auto& camera: m_asset->cameras) {
    auto perspective = std::get_if<fastgltf::Camera::Perspective>(&camera.camera);
    if (perspective) {
      float2 size{24.0f * perspective->aspectRatio.value_or(1.5f), 24.0f};
      auto id = m_scene.addCamera(Camera::withFov(perspective->yfov, size));
      m_cameraIds.push_back(id);
    }
  }

  Scene::NodeID localRoot = 0;
  auto filename = path.stem().string();
  uint32_t sceneIdx = 0;
  for (const auto& scene: m_asset->scenes) {
    if (m_options & LoadOptions_CreateSceneNodes) {
      auto nodeName = m_asset->scenes.size() > 1
                      ? std::format("{}.{:3}", filename, sceneIdx++)
                      : filename;
      localRoot = m_scene.addNode(Scene::Node(nodeName));
    }

    for (auto nodeIdx: scene.nodeIndices) {
      loadNode(m_asset->nodes[nodeIdx], localRoot);
    }
  }
}

/*
 * Load a glTF mesh.
 */
void GltfLoader::loadMesh(const fastgltf::Mesh& gltfMesh) {
  std::vector<float3> vertexPositions;
  std::vector<VertexData> vertexData;
  std::vector<uint32_t> indices;
  std::vector<uint32_t> materialSlotIndices;
  std::vector<Scene::MaterialID> materialSlots;

  std::vector<float3> primitiveVertexPositions;
  std::vector<VertexData> primitiveVertexData;
  std::vector<uint32_t> primitiveIndices;
  std::vector<uint32_t> primitiveMaterialSlotIndices;

  uint32_t materialSlotIdx = 0;
  for (const auto& prim: gltfMesh.primitives) {
    // We don't support primitive types other than triangles for the time being
    // TODO: should we add support for other primitive types?
    if (prim.type != fastgltf::PrimitiveType::Triangles) {
      std::println(stderr, "[Warn] gltf: Unsupported primitive type");
      continue;
    }

    const size_t offset = vertexPositions.size();
    primitiveVertexPositions.clear();
    primitiveVertexData.clear();
    primitiveIndices.clear();
    primitiveMaterialSlotIndices.clear();

    /*
     * Copy primitive vertex positions
     */
    const auto posAttrib = prim.findAttribute("POSITION");
    const auto& posAccessor = m_asset->accessors[posAttrib->accessorIndex];
    primitiveVertexPositions.reserve(posAccessor.count);
    primitiveVertexData.reserve(posAccessor.count);

    auto posIt = fastgltf::iterateAccessor<float3>(*m_asset, posAccessor);
    for (auto position: posIt) {
      primitiveVertexPositions.push_back(position);
      primitiveVertexData.emplace_back();
    }

    /*
     * Copy primitive vertex normals
     */
    const auto nmlAttrib = prim.findAttribute("NORMAL");
    if (nmlAttrib != prim.attributes.end()) {
      const auto& nmlAccessor = m_asset->accessors[nmlAttrib->accessorIndex];
      
      size_t i = 0;
      auto normalIt = fastgltf::iterateAccessor<float3>(*m_asset, nmlAccessor);
      for (auto normal: normalIt) primitiveVertexData[i++].normal = normal;
    }

    /*
     * Copy primitive texture coordinates
     * TODO: gltf supports multiple texture coordinates per object, how should we handle that?
     */
    const auto texAttrib = prim.findAttribute("TEXCOORD_0");
    if (texAttrib != prim.attributes.end()) {
      const auto& texAccessor = m_asset->accessors[texAttrib->accessorIndex];
      
      size_t i = 0;
      auto texCoordIt = fastgltf::iterateAccessor<float2>(*m_asset, texAccessor);
      for (auto texCoords: texCoordIt) primitiveVertexData[i++].texCoords = texCoords;
    }
    
    /*
     * Copy primitive vertex tangents, if present
     */
    const auto tanAttrib = prim.findAttribute("TANGENT");
    if (tanAttrib != prim.attributes.end()) {
      const auto& tanAccessor = m_asset->accessors[tanAttrib->accessorIndex];
      
      size_t i = 0;
      auto tangentIt = fastgltf::iterateAccessor<float4>(*m_asset, tanAccessor);
      for (auto tangent: tangentIt) primitiveVertexData[i++].tangent = tangent;
    }
    
    /*
     * Copy primitive data into mesh data buffers
     */
    vertexPositions.insert(vertexPositions.end(), primitiveVertexPositions.begin(), primitiveVertexPositions.end());
    vertexData.insert(vertexData.end(), primitiveVertexData.begin(), primitiveVertexData.end());

    /*
     * Copy primitive indices and material slot indices
     */
    const auto& idxAccesor = m_asset->accessors[prim.indicesAccessor.value()];
    primitiveIndices.reserve(idxAccesor.count);
    auto idxIt = fastgltf::iterateAccessor<uint32_t>(*m_asset, idxAccesor);
    for (uint32_t idx: idxIt) primitiveIndices.push_back(idx + (uint32_t)offset);
    
    primitiveMaterialSlotIndices.resize(idxAccesor.count / 3, materialSlotIdx++);

    indices.insert(indices.end(), primitiveIndices.begin(), primitiveIndices.end());
    materialSlotIndices.insert(materialSlotIndices.end(), primitiveMaterialSlotIndices.begin(), primitiveMaterialSlotIndices.end());
    
    /*
     * Set material ID for slot
     */
    materialSlots.push_back(prim.materialIndex ? m_materialIds[prim.materialIndex.value()] : 0);
  }

  /*
   * Create the mesh and store its ID and materials
   */
  auto id = m_scene.addMesh({m_device, vertexPositions, vertexData, indices, materialSlotIndices});
  m_meshIds.push_back(id);
  m_meshMaterials[id] = materialSlots;
}

/*
 * Load a glTF scene node and all its children recursively
 */
void GltfLoader::loadNode(const fastgltf::Node& gltfNode, Scene::NodeID parent) {
  std::optional<Scene::MeshID> meshId = std::nullopt;
  if (gltfNode.meshIndex) meshId = m_meshIds[gltfNode.meshIndex.value()];

  std::optional<Scene::CameraID> cameraId = std::nullopt;
  if (gltfNode.cameraIndex) cameraId = m_cameraIds[gltfNode.cameraIndex.value()];

  // Skip adding empty nodes
  if ((m_options & LoadOptions_SkipEmptyNodes) && !meshId && !cameraId && gltfNode.children.empty()) {
    return;
  }

  // Create node
  std::string_view name(gltfNode.name);
  Scene::Node node(name, meshId);
  node.cameraId = cameraId;

  // Node transform
  auto trs = std::get_if<fastgltf::TRS>(&gltfNode.transform);
  if (trs) {
    auto& t = trs->translation;
    node.transform.translation = float3{t.x(), t.y(), t.z()};
    auto& s = trs->scale;
    node.transform.scale = float3{s.x(), s.y(), s.z()};
    node.transform.rotation = eulerFromQuat(trs->rotation);
  }
  
  // Node material slots
  if (meshId) node.materials = m_meshMaterials[meshId.value()];

  // Add node and load children
  auto id = m_scene.addNode(std::move(node), parent);
  for (auto childIdx: gltfNode.children) {
    loadNode(m_asset->nodes[childIdx], id);
  }
}

/*
 * Load a material from glTF, filling in all compatible properties.
 * When the material has textures, the glTF texture index is placed into the material's texture ID
 * field as a placeholder and added to a list of textures to be loaded, with some type/usage data.
 * Textures are later loaded and the indices are replaced with the actual IDs. This lets us avoid
 * duplicating textures if they are used by multiple materials.
 */
void GltfLoader::loadMaterial(const fastgltf::Material &gltfMat) {
  Material material;
  
  // Assign base color
  for (uint32_t i = 0; i < 4; i++)
    material.baseColor[i] = gltfMat.pbrData.baseColorFactor[i];
  
  // Set alpha flag if the material has an alpha component
  if (material.baseColor[3] < 1.0f) material.flags |= Material::Material_UseAlpha;
  
  // Load base color texture
  if (gltfMat.pbrData.baseColorTexture) {
    uint16_t id = gltfMat.pbrData.baseColorTexture->textureIndex;
    material.baseTextureId = id;
    m_texturesToLoad[id] = TextureType::RGBA;
  }
  
  // Assign PBR parameters
  material.roughness = gltfMat.pbrData.roughnessFactor;
  material.metallic = gltfMat.pbrData.metallicFactor;
  
  // Load roughness/metallic texture
  if (gltfMat.pbrData.metallicRoughnessTexture) {
    uint16_t id = gltfMat.pbrData.metallicRoughnessTexture->textureIndex;
    material.rmTextureId = id;
    m_texturesToLoad[id] = TextureType::RoughnessMetallic;
  }
  
  if (gltfMat.transmission != nullptr) {
    material.transmission = gltfMat.transmission->transmissionFactor;
    
    if (gltfMat.transmission->transmissionTexture) {
      uint16_t id = gltfMat.transmission->transmissionTexture->textureIndex;
      material.transmissionTextureId = id;
      m_texturesToLoad[id] = TextureType::Mono;
    }
  }
  
  // Assign emission
  material.emissionStrength = gltfMat.emissiveStrength;
  for (uint32_t i = 0; i < 3; i++) material.emission[i] = gltfMat.emissiveFactor[i];
  
  // Set emissive flag if emission strength is greater than zero
  if (length_squared(material.emission) * material.emissionStrength > 0.0f)
    material.flags |= Material::Material_Emissive;
  
  // Load emission texture
  if (gltfMat.emissiveTexture) {
    uint16_t id = gltfMat.emissiveTexture->textureIndex;
    material.emissionTextureId = id;
    m_texturesToLoad[id] = TextureType::RGB;
  }
  
  // Assign additional parameters
  material.ior = gltfMat.ior;
  
  if (gltfMat.anisotropy != nullptr) {
    material.anisotropy = gltfMat.anisotropy->anisotropyStrength;
    material.anisotropyRotation = gltfMat.anisotropy->anisotropyRotation;
    
    // Set anisotropic flag if the material has anisotropy
    if (material.anisotropy != 0.0f) material.flags |= Material::Material_Anisotropic;
  }
  
  if (gltfMat.clearcoat != nullptr) {
    material.clearcoat = gltfMat.clearcoat->clearcoatFactor;
    material.clearcoatRoughness = gltfMat.clearcoat->clearcoatRoughnessFactor;
    
    if (gltfMat.clearcoat->clearcoatTexture) {
      uint16_t id = gltfMat.clearcoat->clearcoatTexture->textureIndex;
      material.clearcoatTextureId = id;
      m_texturesToLoad[id] = TextureType::Mono;
    }
  }
  
  auto id = m_scene.addMaterial(gltfMat.name, material);
  m_materialIds.push_back(id);
}

/*
 * Load a texture from the glTF file.
 */
Scene::TextureID GltfLoader::loadTexture(const fastgltf::Texture &gltfTex, TextureType type) {
  // Assume the texture has an image index: we don't support any of the image type extensions for now
  const auto& image = m_asset->images[gltfTex.imageIndex.value()];
  
  // Get through all the levels of indirection to the actual texture bytes
  const auto* bvi = std::get_if<fastgltf::sources::BufferView>(&image.data);
  const auto& bv = m_asset->bufferViews[bvi->bufferViewIndex];
  const auto& buf = m_asset->buffers[bv.bufferIndex];

  // This only supports one type of data source. TODO: support other data source types
  const auto* bytes = std::get_if<fastgltf::sources::Array>(&buf.data);
  const uint8_t* data = reinterpret_cast<const uint8_t*>(&bytes->bytes[bv.byteOffset]);
  int32_t len = int32_t(bv.byteLength);
  
  /*
   * Create a temporary read buffer and decode the image from memory
   * We set the x-stride to four channels so our buffer can take any type of input image. We are
   * still making some assumptions: that images are in PNG format (usually the case with glTF), and
   * that they are SDR with 8 bits per channel.
   */
  OIIO::Filesystem::IOMemReader memReader(data, len);
  const auto in = OIIO::ImageInput::open("a.png", nullptr, &memReader);
  const auto& spec = in->spec();
  
  // We use this later to fill the alpha channel with 1 if it's not present
  const bool hasAlphaChannel = spec.alpha_channel != -1;
  
  auto readBuffer = m_device->newBuffer(
    sizeof(uchar4) * spec.width * spec.height,
    MTL::ResourceStorageModeShared
  );
  in->read_image(0, 0, 0, -1, spec.format, readBuffer->contents(), sizeof(uchar4));
  in->close();
  
  /*
   * Create a temporary texture as input to the texture converter shader. We just make this texture
   * RGBA, since it's only used while loading we don't care about the extra memory use.
   */
  auto srcDesc = metal_utils::makeTextureDescriptor({
    .width = uint32_t(spec.width),
    .height = uint32_t(spec.height),
    .format = MTL::PixelFormatRGBA8Unorm,
  });
  
  auto srcTexture = m_device->newTexture(srcDesc);
  srcTexture->replaceRegion(
    MTL::Region(0, 0, 0, spec.width, spec.height, 1),
    0,
    readBuffer->contents(),
    sizeof(uchar4) * spec.width
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
  return m_scene.addTexture(gltfTex.name, NS::TransferPtr(texture));
}

}
