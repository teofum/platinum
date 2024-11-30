#include "gltf.hpp"

namespace pt::loaders::gltf {

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

void GltfLoader::loadMesh(const fastgltf::Mesh& gltfMesh) {
  std::vector<float3> vertexPositions;
  std::vector<VertexData> vertexData;
  std::vector<uint32_t> indices;

  std::vector<float3> primitiveVertexPositions;
  std::vector<VertexData> primitiveVertexData;
  std::vector<uint32_t> primitiveIndices;

  for (const auto& prim: gltfMesh.primitives) {
    // We don't support primitive types other than triangles for the time being
    // TODO should we add support for other primitive types?
    if (prim.type != fastgltf::PrimitiveType::Triangles) {
      std::println(stderr, "[Warn] gltf: Unsupported primitive type");
      continue;
    }

    const size_t offset = vertexPositions.size();
    primitiveVertexPositions.clear();
    primitiveVertexData.clear();
    primitiveIndices.clear();

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
    const auto& nmlAccessor = m_asset->accessors[nmlAttrib->accessorIndex];
    primitiveVertexPositions.reserve(nmlAccessor.count);
    primitiveVertexData.reserve(nmlAccessor.count);

    size_t i = 0;
    auto normalIt = fastgltf::iterateAccessor<float3>(*m_asset, nmlAccessor);
    for (auto normal: normalIt) primitiveVertexData[i++].normal = normal;

    /*
     * Copy primitive texture coordinates
     * TODO: gltf supports multiple texture coordinates per object, how should we handle that?
     */
    const auto texAttrib = prim.findAttribute("TEXCOORD_0");
    const auto& texAccessor = m_asset->accessors[texAttrib->accessorIndex];
    primitiveVertexPositions.reserve(texAccessor.count);
    primitiveVertexData.reserve(texAccessor.count);

    i = 0;
    if (texAccessor.type == fastgltf::AccessorType::Vec2) {
      auto texCoordIt = fastgltf::iterateAccessor<float2>(*m_asset, texAccessor);
      for (auto texCoords: texCoordIt) primitiveVertexData[i++].texCoords = texCoords;
    } else {
      auto texCoordIt = fastgltf::iterateAccessor<float3>(*m_asset, texAccessor);
      for (auto texCoords: texCoordIt) primitiveVertexData[i++].texCoords = texCoords.xy;
    }

    /*
     * Copy primitive vertex tangents, if present
     */
    const auto tanAttrib = prim.findAttribute("TANGENT");
    const auto& tanAccessor = m_asset->accessors[tanAttrib->accessorIndex];
    primitiveVertexPositions.reserve(tanAccessor.count);
    primitiveVertexData.reserve(tanAccessor.count);

    i = 0;
    if (tanAccessor.type == fastgltf::AccessorType::Vec4) {
      std::println("[Info] gltf: Tangents are Vec4");
      auto tangentIt = fastgltf::iterateAccessor<float4>(*m_asset, tanAccessor);
      for (auto tangent: tangentIt) {
        primitiveVertexData[i++].tangent = tangent;
        if (i >= primitiveVertexData.size()) {
          std::println(stderr, "[Warn] gltf: Tangent count exceeds vertex count");
          break;
        }
      }
    } else {
      std::println("[Info] gltf: Tangents are Vec3");
      auto tangentIt = fastgltf::iterateAccessor<float3>(*m_asset, tanAccessor);
      for (auto tangent: tangentIt) {
        primitiveVertexData[i++].tangent = make_float4(tangent, 1.0f);
        if (i >= primitiveVertexData.size()) {
          std::println(stderr, "[Warn] gltf: Tangent count exceeds vertex count");
          break;
        }
      }
    }

    /*
     * Copy primitive data into mesh data buffers
     */
    vertexPositions.insert(vertexPositions.end(), primitiveVertexPositions.begin(), primitiveVertexPositions.end());
    vertexData.insert(vertexData.end(), primitiveVertexData.begin(), primitiveVertexData.end());

    /*
     * Copy primitive indices
     */
    const auto& idxAccesor = m_asset->accessors[prim.indicesAccessor.value()];
    primitiveIndices.reserve(idxAccesor.count);
    auto idxIt = fastgltf::iterateAccessor<uint32_t>(*m_asset, idxAccesor);
    for (uint32_t idx: idxIt) primitiveIndices.push_back(idx + (uint32_t)offset);

    indices.insert(indices.end(), primitiveIndices.begin(), primitiveIndices.end());
  }

  /*
   * Create the mesh and store its ID
   */
  auto id = m_scene.addMesh({m_device, vertexPositions, vertexData, indices});
  m_meshIds.push_back(id);
}

void GltfLoader::loadNode(const fastgltf::Node& gltfNode, Scene::NodeID parent) {
  std::optional<Scene::MeshID> meshId = std::nullopt;
  if (gltfNode.meshIndex) meshId = m_meshIds[gltfNode.meshIndex.value()];

  std::optional<Scene::CameraID> cameraId = std::nullopt;
  if (gltfNode.cameraIndex) cameraId = m_cameraIds[gltfNode.cameraIndex.value()];

  // Skip adding empty nodes
  if ((m_options & LoadOptions_SkipEmptyNodes) && !meshId && !cameraId && gltfNode.children.empty()) {
    return;
  }

  std::string_view name(gltfNode.name);
  Scene::Node node(name, meshId);
  node.cameraId = cameraId;

  auto trs = std::get_if<fastgltf::TRS>(&gltfNode.transform);
  if (trs) {
    auto& t = trs->translation;
    node.transform.translation = float3{t.x(), t.y(), t.z()};
    auto& s = trs->scale;
    node.transform.scale = float3{s.x(), s.y(), s.z()};
    node.transform.rotation = eulerFromQuat(trs->rotation);
  }

  auto id = m_scene.addNode(std::move(node), parent);
  for (auto childIdx: gltfNode.children) {
    loadNode(m_asset->nodes[childIdx], id);
  }
}

}
