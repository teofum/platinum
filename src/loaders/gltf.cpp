#include "gltf.hpp"

#include <utils/metal_utils.hpp>

namespace pt::loaders::gltf {
using metal_utils::ns_shared;
using metal_utils::operator""_ns;

static float3 eulerFromQuat(fastgltf::math::fquat q) {
  float qw = q.w(), qx = q.x(), qy = q.y(), qz = q.z();

  return {
      atan2(2.0f * (qw * qx - qy * qz), 1.0f - 2.0f * (qx * qx + qz * qz)),
      atan2(2.0f * (qw * qy - qx * qz), 1.0f - 2.0f * (qy * qy + qz * qz)),
      asin(2.0f * clamp(qx * qy + qw * qz, -0.5f, 0.5f)),
  };
}

GltfLoader::GltfLoader(MTL::Device *device, MTL::CommandQueue *commandQueue,
                       Scene &scene) noexcept
    : m_device(device), m_commandQueue(commandQueue),
      m_textureLoader(device, commandQueue, scene), m_scene(scene) {}

/*
 * Load a scene from glTF.
 */
void GltfLoader::load(const fs::path &path, int options) {
  auto start = std::chrono::high_resolution_clock::now();

  auto gltfFile = fastgltf::GltfDataBuffer::FromPath(path);
  if (gltfFile.error() != fastgltf::Error::None) {
    std::println(stderr, "[Error] gltf: ({}) {}",
                 getErrorName(gltfFile.error()),
                 getErrorMessage(gltfFile.error()));
    return;
  }

  auto parser =
      fastgltf::Parser(fastgltf::Extensions::KHR_materials_emissive_strength |
                       fastgltf::Extensions::KHR_materials_transmission |
                       fastgltf::Extensions::KHR_materials_ior |
                       fastgltf::Extensions::KHR_materials_anisotropy |
                       fastgltf::Extensions::KHR_materials_clearcoat |
                       fastgltf::Extensions::KHR_materials_volume);

  auto asset = parser.loadGltf(gltfFile.get(), path.parent_path(),
                               fastgltf::Options::GenerateMeshIndices |
                                   fastgltf::Options::DecomposeNodeMatrices |
                                   fastgltf::Options::LoadExternalBuffers);

  if (asset.error() != fastgltf::Error::None) {
    std::println(stderr, "[Error] gltf: ({}) {}",
                 getErrorName(gltfFile.error()),
                 getErrorMessage(gltfFile.error()));
    return;
  }
  m_asset = std::make_unique<fastgltf::Asset>(std::move(asset.get()));
  m_options = options;

  m_materialIds.reserve(m_asset->materials.size());
  for (const auto &material : m_asset->materials)
    loadMaterial(material);

  m_textureIds.reserve(m_texturesToLoad.size());
  for (const auto &[idx, desc] : m_texturesToLoad) {
    auto textureId = loadTexture(m_asset->textures[idx], desc.type);

    // Set texture ID on materials using the texture
    for (const auto &[materialId, slot] : desc.users) {
      auto *material = m_scene.getAsset<Material>(materialId);
      m_scene.updateMaterialTexture(material, slot, textureId);
    }
  }

  m_meshIds.reserve(m_asset->meshes.size());
  for (const auto &mesh : m_asset->meshes)
    loadMesh(mesh);

  m_cameras.reserve(m_asset->cameras.size());
  for (const auto &camera : m_asset->cameras) {
    auto perspective =
        std::get_if<fastgltf::Camera::Perspective>(&camera.camera);
    if (perspective) {
      float2 size{24.0f * perspective->aspectRatio.value_or(1.5f), 24.0f};
      m_cameras.push_back(Camera::withFov(perspective->yfov, size));
    }
  }

  Scene::NodeID localRoot = m_scene.root().id();
  auto filename = path.stem().string();
  uint32_t sceneIdx = 0;
  for (const auto &scene : m_asset->scenes) {
    if (m_options & LoadOptions_CreateSceneNodes) {
      auto nodeName = m_asset->scenes.size() > 1
                          ? std::format("{}.{:3}", filename, sceneIdx++)
                          : filename;
      localRoot = m_scene.createNode(nodeName).id();
    }

    for (auto nodeIdx : scene.nodeIndices) {
      loadNode(m_asset->nodes[nodeIdx], localRoot);
    }
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  std::println("Import glTF {} in {} ms", path.stem().string(), millis.count());
}

/*
 * Load a glTF mesh.
 */
void GltfLoader::loadMesh(const fastgltf::Mesh &gltfMesh) {
  std::vector<float3> vertexPositions;
  std::vector<VertexData> vertexData;
  std::vector<uint32_t> indices;
  std::vector<uint32_t> materialSlotIndices;
  std::vector<Scene::AssetID> materialSlots;

  std::vector<float3> primitiveVertexPositions;
  std::vector<VertexData> primitiveVertexData;
  std::vector<uint32_t> primitiveIndices;
  std::vector<uint32_t> primitiveMaterialSlotIndices;

  bool didLoadTangents = false;

  uint32_t materialSlotIdx = 0;
  for (const auto &prim : gltfMesh.primitives) {
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
    const auto &posAccessor = m_asset->accessors[posAttrib->accessorIndex];
    primitiveVertexPositions.reserve(posAccessor.count);
    primitiveVertexData.reserve(posAccessor.count);

    auto posIt = fastgltf::iterateAccessor<float3>(*m_asset, posAccessor);
    for (auto position : posIt) {
      primitiveVertexPositions.push_back(position);
      primitiveVertexData.emplace_back();
    }

    /*
     * Copy primitive vertex normals
     */
    const auto nmlAttrib = prim.findAttribute("NORMAL");
    if (nmlAttrib != prim.attributes.end()) {
      const auto &nmlAccessor = m_asset->accessors[nmlAttrib->accessorIndex];

      size_t i = 0;
      auto normalIt = fastgltf::iterateAccessor<float3>(*m_asset, nmlAccessor);
      for (auto normal : normalIt)
        primitiveVertexData[i++].normal = normal;
    }

    /*
     * Copy primitive texture coordinates
     * TODO: gltf supports multiple texture coordinates per object, how should
     * we handle that?
     */
    const auto texAttrib = prim.findAttribute("TEXCOORD_0");
    if (texAttrib != prim.attributes.end()) {
      const auto &texAccessor = m_asset->accessors[texAttrib->accessorIndex];

      size_t i = 0;
      auto texCoordIt =
          fastgltf::iterateAccessor<float2>(*m_asset, texAccessor);
      for (auto texCoords : texCoordIt)
        primitiveVertexData[i++].texCoords = texCoords;
    }

    /*
     * Copy primitive vertex tangents, if present
     */
    const auto tanAttrib = prim.findAttribute("TANGENT");
    if (tanAttrib != prim.attributes.end()) {
      const auto &tanAccessor = m_asset->accessors[tanAttrib->accessorIndex];

      size_t i = 0;
      auto tangentIt = fastgltf::iterateAccessor<float4>(*m_asset, tanAccessor);
      for (auto tangent : tangentIt)
        primitiveVertexData[i++].tangent = tangent;

      didLoadTangents = true;
    }

    /*
     * Copy primitive data into mesh data buffers
     */
    vertexPositions.insert(vertexPositions.end(),
                           primitiveVertexPositions.begin(),
                           primitiveVertexPositions.end());
    vertexData.insert(vertexData.end(), primitiveVertexData.begin(),
                      primitiveVertexData.end());

    /*
     * Copy primitive indices and material slot indices
     */
    const auto &idxAccesor = m_asset->accessors[prim.indicesAccessor.value()];
    primitiveIndices.reserve(idxAccesor.count);
    auto idxIt = fastgltf::iterateAccessor<uint32_t>(*m_asset, idxAccesor);
    for (uint32_t idx : idxIt)
      primitiveIndices.push_back(idx + (uint32_t)offset);

    primitiveMaterialSlotIndices.resize(idxAccesor.count / 3,
                                        materialSlotIdx++);

    indices.insert(indices.end(), primitiveIndices.begin(),
                   primitiveIndices.end());
    materialSlotIndices.insert(materialSlotIndices.end(),
                               primitiveMaterialSlotIndices.begin(),
                               primitiveMaterialSlotIndices.end());

    /*
     * Set material ID for slot
     */
    materialSlots.push_back(
        prim.materialIndex ? m_materialIds[prim.materialIndex.value()] : 0);
  }

  /*
   * Create the mesh and store its ID and materials
   */
  Mesh mesh(m_device, vertexPositions, vertexData, indices,
            materialSlotIndices);
  if (!didLoadTangents)
    mesh.generateTangents();

  auto id = m_scene.createAsset(std::move(mesh));
  m_scene.assetRetained(id) = false;
  m_meshIds.push_back(id);
  m_meshMaterials[id] = materialSlots;
}

/*
 * Load a glTF scene node and all its children recursively
 */
void GltfLoader::loadNode(const fastgltf::Node &gltfNode,
                          Scene::NodeID parentId) {
  std::optional<Scene::AssetID> meshId = std::nullopt;
  if (gltfNode.meshIndex)
    meshId = m_meshIds[gltfNode.meshIndex.value()];

  // Skip adding empty nodes
  if ((m_options & LoadOptions_SkipEmptyNodes) && !meshId &&
      !gltfNode.cameraIndex && gltfNode.children.empty()) {
    return;
  }

  // Create node
  auto node = m_scene.createNode(gltfNode.name, parentId);
  if (gltfNode.cameraIndex)
    node.set(m_cameras[gltfNode.cameraIndex.value()]);

  // Node transform
  auto trs = std::get_if<fastgltf::TRS>(&gltfNode.transform);
  if (trs) {
    auto &t = trs->translation;
    node.transform().translation = float3{t.x(), t.y(), t.z()};
    auto &s = trs->scale;
    node.transform().scale = float3{s.x(), s.y(), s.z()};
    node.transform().rotation = eulerFromQuat(trs->rotation);
  }

  // Node mesh and material slots
  if (meshId) {
    node.setMesh(meshId);

    auto &materials = m_meshMaterials[meshId.value()];
    for (size_t i = 0; i < materials.size(); i++) {
      node.setMaterial(i, materials[i]);
    }
  }

  // Load children
  for (auto childIdx : gltfNode.children) {
    loadNode(m_asset->nodes[childIdx], node.id());
  }
}

/*
 * Load a material from glTF, filling in all compatible properties.
 * When the material has textures, the glTF texture index is placed into the
 * material's texture ID field as a placeholder and added to a list of textures
 * to be loaded, with some type/usage data. Textures are later loaded and the
 * indices are replaced with the actual IDs. This lets us avoid duplicating
 * textures if they are used by multiple materials.
 */
void GltfLoader::loadMaterial(const fastgltf::Material &gltfMat) {
  Material material;
  material.name = gltfMat.name;

  // Assign base color
  for (uint32_t i = 0; i < 4; i++)
    material.baseColor[i] = gltfMat.pbrData.baseColorFactor[i];

  // Assign PBR parameters
  material.roughness = gltfMat.pbrData.roughnessFactor;
  material.metallic = gltfMat.pbrData.metallicFactor;

  if (gltfMat.transmission != nullptr) {
    material.transmission = gltfMat.transmission->transmissionFactor;
  }

  // Assign emission
  material.emissionStrength = gltfMat.emissiveStrength;
  for (uint32_t i = 0; i < 3; i++)
    material.emission[i] = gltfMat.emissiveFactor[i];

  // Assign additional parameters
  material.ior = gltfMat.ior;

  if (gltfMat.anisotropy != nullptr) {
    material.anisotropy = gltfMat.anisotropy->anisotropyStrength;
    material.anisotropyRotation = gltfMat.anisotropy->anisotropyRotation;
  }

  if (gltfMat.clearcoat != nullptr) {
    material.clearcoat = gltfMat.clearcoat->clearcoatFactor;
    material.clearcoatRoughness = gltfMat.clearcoat->clearcoatRoughnessFactor;
  }

  Scene::AssetID materialId = m_scene.createAsset(std::move(material));
  m_scene.assetRetained(materialId) = false;
  m_materialIds.push_back(materialId);

  /*
   * Load textures
   */

  // Load base color texture
  if (gltfMat.pbrData.baseColorTexture) {
    uint16_t textureIdx = gltfMat.pbrData.baseColorTexture->textureIndex;
    m_texturesToLoad[textureIdx].type = texture::TextureType::sRGB;
    m_texturesToLoad[textureIdx].users.emplace_back(
        materialId, Material::TextureSlot::BaseColor);
  }

  // Load roughness/metallic texture
  if (gltfMat.pbrData.metallicRoughnessTexture) {
    uint16_t textureIdx =
        gltfMat.pbrData.metallicRoughnessTexture->textureIndex;
    m_texturesToLoad[textureIdx].type = texture::TextureType::RoughnessMetallic;
    m_texturesToLoad[textureIdx].users.emplace_back(
        materialId, Material::TextureSlot::RoughnessMetallic);
  }

  // Load normal texture
  if (gltfMat.normalTexture) {
    uint16_t textureIdx = gltfMat.normalTexture->textureIndex;
    m_texturesToLoad[textureIdx].type = texture::TextureType::LinearRGB;
    m_texturesToLoad[textureIdx].users.emplace_back(
        materialId, Material::TextureSlot::Normal);
  }

  // Load emission texture
  if (gltfMat.emissiveTexture) {
    uint16_t textureIdx = gltfMat.emissiveTexture->textureIndex;
    m_texturesToLoad[textureIdx].type = texture::TextureType::sRGB;
    m_texturesToLoad[textureIdx].users.emplace_back(
        materialId, Material::TextureSlot::Emission);
  }

  if (gltfMat.transmission != nullptr &&
      gltfMat.transmission->transmissionTexture) {
    uint16_t textureIdx =
        gltfMat.transmission->transmissionTexture->textureIndex;
    m_texturesToLoad[textureIdx].type = texture::TextureType::Mono;
    m_texturesToLoad[textureIdx].users.emplace_back(
        materialId, Material::TextureSlot::Transmission);
  }

  if (gltfMat.clearcoat != nullptr && gltfMat.clearcoat->clearcoatTexture) {
    uint16_t textureIdx = gltfMat.clearcoat->clearcoatTexture->textureIndex;
    m_texturesToLoad[textureIdx].type = texture::TextureType::Mono;
    m_texturesToLoad[textureIdx].users.emplace_back(
        materialId, Material::TextureSlot::Clearcoat);
  }
}

/*
 * Load a texture from the glTF file.
 */
Scene::AssetID GltfLoader::loadTexture(const fastgltf::Texture &gltfTex,
                                       texture::TextureType type) {
  // Assume the texture has an image index: we don't support any of the image
  // type extensions for now
  const auto &image = m_asset->images[gltfTex.imageIndex.value()];

  // Get through all the levels of indirection to the actual texture bytes
  const auto *bvi = std::get_if<fastgltf::sources::BufferView>(&image.data);
  const auto &bv = m_asset->bufferViews[bvi->bufferViewIndex];
  const auto &buf = m_asset->buffers[bv.bufferIndex];

  // This only supports one type of data source. TODO: support other data source
  // types
  const auto *bytes = std::get_if<fastgltf::sources::Array>(&buf.data);
  const auto *data =
      reinterpret_cast<const uint8_t *>(&bytes->bytes[bv.byteOffset]);
  auto len = int32_t(bv.byteLength);

  auto id = m_textureLoader.loadFromMemory(data, len, gltfTex.name, type);
  m_scene.assetRetained(id) = false;
  return id;
}

} // namespace pt::loaders::gltf
