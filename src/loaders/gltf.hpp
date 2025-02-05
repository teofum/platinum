#ifndef PLATINUM_LOADER_GLTF_HPP
#define PLATINUM_LOADER_GLTF_HPP

#include <unordered_dense.h>
#include <filesystem>
#include <simd/simd.h>
#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>

#include <core/scene.hpp>
#include <loaders/texture.hpp>

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
  MTL::Device* m_device;
  MTL::CommandQueue* m_commandQueue;
  texture::TextureLoader m_textureLoader;
  
  std::unique_ptr<fastgltf::Asset> m_asset;
  std::vector<Scene::AssetID> m_meshIds;
  ankerl::unordered_dense::map<Scene::AssetID, std::vector<Scene::AssetID>> m_meshMaterials;
  
  Scene& m_scene;
//  std::vector<Scene::CameraID> m_cameraIds;
  std::vector<Scene::AssetID> m_materialIds;
  std::vector<Scene::AssetID> m_textureIds;
  
  ankerl::unordered_dense::map<uint32_t, texture::TextureType> m_texturesToLoad;
  
  int m_options;

  void loadMesh(const fastgltf::Mesh& gltfMesh);

  void loadNode(const fastgltf::Node& gltfNode, Scene::NodeID parent = Scene::null);
  
  void loadMaterial(const fastgltf::Material& gltfMat);
  
  [[nodiscard]] Scene::AssetID loadTexture(const fastgltf::Texture& gltfTex, texture::TextureType type);
};


}

#endif //PLATINUM_GLTF_HPP
