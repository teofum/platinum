#ifndef PLATINUM_MS_LUT_GEN_HPP
#define PLATINUM_MS_LUT_GEN_HPP

#include <imgui.h>

#include <frontend/window.hpp>
#include <frontend/widgets.hpp>

namespace pt::frontend::windows {

class MultiscatterLutGenerator final : Window {
public:
  constexpr MultiscatterLutGenerator(Store& store, State& state, bool* open = nullptr) noexcept
    : Window(store, state, open) {
  }

  void init(MTL::Device* device, MTL::CommandQueue* commandQueue);
  
  void render() final;
  
private:
  struct LUTDescriptor {
    const char* displayName;
    const char* kernelName;
    uint32_t dimensions;
  };
  
  static constexpr std::array<LUTDescriptor, 4> m_lutOptions = {{
    {
      .displayName = "Single scatter directional albedo (E)",
      .kernelName = "generateDirectionalAlbedoLookup",
      .dimensions = 2,
    },
    {
      .displayName = "Single scatter hemispherical albedo (E_avg)",
      .kernelName = "generateHemisphericalAlbedoLookup",
      .dimensions = 1,
    },
    {
      .displayName = "Dielectric MS directional albedo (E_base)",
      .kernelName = "generateMultiscatterDirectionalAlbedoLookup",
      .dimensions = 3,
    },
    {
      .displayName = "Dielectric MS hemispherical albedo (E_base_avg)",
      .kernelName = "generateMultiscatterHemisphericalAlbedoLookup",
      .dimensions = 2,
    },
  }};
  
  static constexpr std::array<MTL::TextureType, 3> m_textureTypes = {{
    MTL::TextureType2D, // We use 2D textures for 1D LUTs so ImGui can display them for preview
    MTL::TextureType2D,
    MTL::TextureType3D,
  }};
  
  MTL::Device* m_device = nullptr;
  MTL::CommandQueue* m_commandQueue = nullptr;
  
  MTL::Texture* m_accumulator[2] = {nullptr, nullptr};
  MTL::Texture* m_randomSource = nullptr;
  MTL::Texture* m_viewSlice = nullptr;
  uint32_t m_viewSliceIdx = 0;
  
  MTL::ComputePipelineState* m_pso = nullptr;
  
  uint32_t m_lutSize = 128;
  uint32_t m_frameIdx = 0;
  uint32_t m_accumulateFrames = 65536;
  uint32_t m_selectedLut = 0;
  
  // LUT textures
  struct LUTInfo {
    const char* filename;
    MTL::TextureType type;
  };
  static constexpr std::array<LUTInfo, 2> m_lutInfo = {{
    {.filename = "ggx_E.exr", .type = MTL::TextureType2D},
    {.filename = "ggx_E_avg.exr", .type = MTL::TextureType1D},
  }};
  std::vector<MTL::Texture*> m_luts;
  std::vector<uint32_t> m_lutSizes;
  
  void frame();
  
  void generate();
  
  void exportToFile();
  
  void loadGgxLutTextures();
};

}

#endif // PLATINUM_MS_LUT_GEN_HPP
