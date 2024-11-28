#ifndef PLATINUM_GLTF_HPP
#define PLATINUM_GLTF_HPP

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
  explicit GltfLoader(MTL::Device* device, Scene& scene) noexcept
    : m_device(device), m_scene(scene) {
  }

  void load(const fs::path& path, int options = LoadOptions_Default);

private:
  MTL::Device* m_device;
  Scene& m_scene;
  std::unique_ptr<fastgltf::Asset> m_asset;
  std::vector<Scene::MeshID> m_meshIds;
  std::vector<Scene::MeshID> m_cameraIds;
  int m_options;

  void loadMesh(const fastgltf::Mesh& gltfMesh);

  void loadNode(const fastgltf::Node& gltfNode, Scene::NodeID parent = 0);
};


}

#endif //PLATINUM_GLTF_HPP
