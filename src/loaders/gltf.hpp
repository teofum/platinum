#ifndef PLATINUM_GLTF_HPP
#define PLATINUM_GLTF_HPP

#include <unordered_dense.h>
#include <filesystem>
#include <simd/simd.h>
#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>

#include <core/scene.hpp>

namespace fs = std::filesystem;
using namespace simd;

template<>
struct fastgltf::ElementTraits<float2>
  : fastgltf::ElementTraitsBase<float2, AccessorType::Vec2, float> {
};

template<>
struct fastgltf::ElementTraits<float3>
  : fastgltf::ElementTraitsBase<float3, AccessorType::Vec3, float> {
};

template<>
struct fastgltf::ElementTraits<float4>
  : fastgltf::ElementTraitsBase<float4, AccessorType::Vec4, float> {
};

namespace pt::loaders::gltf {

enum LoadOptions {
  LoadOptions_None = 0,
  LoadOptions_SkipEmptyNodes = 1 << 0,        // Don't create nodes with no loadable objects or children
  LoadOptions_CreateSceneNodes = 1 << 1,      // Create a root node for each scene instead of appending nodes directly

  LoadOptions_Default = LoadOptions_SkipEmptyNodes | LoadOptions_CreateSceneNodes,
};

class GltfLoader {
public:
  explicit GltfLoader(MTL::Device* device, MTL::CommandQueue* commandQueue, Scene& scene) noexcept;

  void load(const fs::path& path, int options = LoadOptions_Default);

private:
  enum class TextureType {
    Albedo,
    Emission,
    NormalMap,
    Mono,
    RoughnessMetallic,
  };
  
  MTL::Device* m_device;
  MTL::CommandQueue* m_commandQueue;
  MTL::ComputePipelineState* m_textureConverterPso = nullptr;
  
  std::unique_ptr<fastgltf::Asset> m_asset;
  std::vector<Scene::MeshID> m_meshIds;
  ankerl::unordered_dense::map<Scene::MeshID, std::vector<Scene::MaterialID>> m_meshMaterials;
  
  Scene& m_scene;
  std::vector<Scene::CameraID> m_cameraIds;
  std::vector<Scene::MaterialID> m_materialIds;
  std::vector<Scene::TextureID> m_textureIds;
  
  ankerl::unordered_dense::map<uint32_t, TextureType> m_texturesToLoad;
  
  int m_options;
  
  static std::pair<MTL::PixelFormat, std::vector<uint8_t>> getAttributesForTexture(TextureType type);

  void loadMesh(const fastgltf::Mesh& gltfMesh);

  void loadNode(const fastgltf::Node& gltfNode, Scene::NodeID parent = 0);
  
  void loadMaterial(const fastgltf::Material& gltfMat);
  
  [[nodiscard]] Scene::TextureID loadTexture(const fastgltf::Texture& gltfTex, TextureType type);
};


}

#endif //PLATINUM_GLTF_HPP
