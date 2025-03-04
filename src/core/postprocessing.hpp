#ifndef PLATINUM_POSTPROCESSING_HPP
#define PLATINUM_POSTPROCESSING_HPP

#ifdef __METAL_VERSION__
#define address_space(space) space
#else
#define address_space(space)
#endif

#ifndef __METAL_VERSION__

#include <simd/simd.h>
#include <Metal/Metal.hpp>

#include <utils/metal_utils.hpp>
#include <utils/utils.hpp>

using namespace simd;

#endif

namespace pt {
namespace postprocess {

#ifndef __METAL_VERSION__
using metal_utils::ns_shared;
#endif

namespace agx {

struct Look {
  float3 offset, slope, power;
  float saturation;
};

namespace looks {

address_space(constant) constexpr const Look none = {
  .offset = float3(0.0),
  .slope = float3(1.0),
  .power = float3(1.0),
  .saturation = 1.0,
};

address_space(constant) constexpr const Look golden = {
  .offset = float3(0.0),
  .slope = float3{1.0, 0.9, 0.5},
  .power = float3(0.8),
  .saturation = 0.8,
};

address_space(constant) constexpr const Look punchy = {
  .offset = float3(0.0),
  .slope = float3(1.0),
  .power = float3(1.35),
  .saturation = 1.4,
};

}

struct Options {
  Look look = looks::none;
};

}

namespace khronos_pbr {

struct Options {
  float compressionStart = 0.8;
  float desaturation = 0.15;
};

}

namespace flim {

struct Options {
  float preExposure;
  float3 preFormationFilter;
  float preFormationFilterStrength;

  float3 extendedGamutScale;
  float3 extendedGamutRotation;
  float3 extendedGamutMul;

  float sigmoidLog2Min;
  float sigmoidLog2Max;
  float2 sigmoidToe;
  float2 sigmoidShoulder;

  float negativeExposure;
  float negativeDensity;

  float3 printBacklight;
  float printExposure;
  float printDensity;

  float blackPoint;
  bool autoBlackPoint;
  float3 postFormationFilter;
  float postFormationFilterStrength;

  float midtoneSaturation;
};

namespace presets {

address_space(constant) constexpr const Options flim{
  .preExposure = 4.3,
  .preFormationFilter = {1.0, 1.0, 1.0},
  .preFormationFilterStrength = 0.0,

  .extendedGamutScale = {1.05, 1.12, 1.045},
  .extendedGamutRotation = {0.5, 2.0, 0.1},
  .extendedGamutMul = {1.0, 1.0, 1.0},

  .sigmoidLog2Min = -10.0,
  .sigmoidLog2Max = 22.0,
  .sigmoidToe = {0.440, 0.280},
  .sigmoidShoulder = {0.591, 0.779},

  .negativeExposure = 6.0,
  .negativeDensity = 5.0,

  .printBacklight = {1.0, 1.0, 1.0},
  .printExposure = 6.0,
  .printDensity = 27.5,

  .blackPoint = 0.0,
  .autoBlackPoint = true,
  .postFormationFilter = {1.0, 1.0, 1.0},
  .postFormationFilterStrength = 0.0,

  .midtoneSaturation = 1.02,
};

address_space(constant) constexpr const Options silver{
  .preExposure = 3.9,
  .preFormationFilter = {0.0, 0.5, 1.0},
  .preFormationFilterStrength = 0.05,

  .extendedGamutScale = {1.05, 1.12, 1.045},
  .extendedGamutRotation = {0.5, 2.0, 0.1},
  .extendedGamutMul = {1.0, 1.0, 1.06},

  .sigmoidLog2Min = -10.0,
  .sigmoidLog2Max = 22.0,
  .sigmoidToe = {0.440, 0.280},
  .sigmoidShoulder = {0.591, 0.779},

  .negativeExposure = 4.7,
  .negativeDensity = 7.0,

  .printBacklight = {0.9992, 0.99, 1.0},
  .printExposure = 4.7,
  .printDensity = 30.0,

  .blackPoint = 0.5,
  .autoBlackPoint = false,
  .postFormationFilter = {1.0, 1.0, 0.0},
  .postFormationFilterStrength = 0.04,

  .midtoneSaturation = 1.0,
};

}

}

enum class Tonemapper {
  None,
  AgX,
  KhronosPBR,
  flim,
};

struct ExposureOptions {
  float exposure = 0.0f;
};

struct ToneCurveOptions {
  float k = 1.0f; // Debug option!
  float blacks = 0.0f;
  float shadows = 0.0f;
  float highlights = 0.0f;
  float whites = 0.0f;
};

struct VignetteOptions {
  float amount = 0.0f;
  float midpoint = 0.0f;
  float feather = 50.0f;
  float power = 20.0f;
  float roundness = 100.0f;
};

struct ChromaticAberrationOptions {
  float amount = 0.0f;
  float greenShift = 70.0f;
};

struct ContrastSaturationOptions {
  float contrast = 0.0f;
  float saturation = 0.0f;
};

struct LiftGammaGain {
  float3 shadowColor = {0.5, 0.5, 0.5};
  float3 midtoneColor = {0.5, 0.5, 0.5};
  float3 highlightColor = {0.5, 0.5, 0.5};

  float shadowOffset = 0.0f;
  float midtoneOffset = 0.0f;
  float highlightOffset = 0.0f;
};

struct TonemapOptions {
  Tonemapper tonemapper = Tonemapper::AgX;

  agx::Options agxOptions;
  khronos_pbr::Options khrOptions;
  flim::Options flimOptions = flim::presets::flim;

  LiftGammaGain postTonemap;
  float3x3 odt; // Colorspace transform from working -> display space
};

#ifndef __METAL_VERSION__

class PostProcessPass {
public:
  enum class Type {
    Exposure,
    ToneCurve,
    Vignette,
    ChromaticAberration,
    ContrastSaturation,
    Tonemap,
  };

  struct Options {
    Type type = Type::Exposure;
    union {
      ExposureOptions* exposure = nullptr;
      ToneCurveOptions* toneCurve;
      VignetteOptions* vignette;
      ChromaticAberrationOptions* chromaticAberration;
      ContrastSaturationOptions* contrastSaturation;
      TonemapOptions* tonemap;
    };
  };

  PostProcessPass(
    MTL::Device* device,
    MTL::Library* lib,
    const char* functionName,
    MTL::PixelFormat rtFormat
  ) noexcept;

  virtual ~PostProcessPass();

  virtual void apply(MTL::Texture* src, MTL::Texture* dst, MTL::CommandBuffer* cmd) = 0;

  virtual Options options() = 0;

protected:
  MTL::Device* m_device;
  MTL::RenderPipelineState* m_pso;

  std::string m_name;
};

template<typename T, PostProcessPass::Type PassType, utils::StringLiteral FunctionName, bool IsFinal = false>
class BasicPostProcessPass : public PostProcessPass {
public:
  using Options = T;

  [[maybe_unused]] BasicPostProcessPass(MTL::Device* device, MTL::Library* lib) noexcept
    : PostProcessPass(
    device,
    lib,
    FunctionName.value,
    IsFinal ? MTL::PixelFormatRGBA8Unorm : MTL::PixelFormatRGBA32Float
  ) {}

  void apply(MTL::Texture* src, MTL::Texture* dst, MTL::CommandBuffer* cmd) final {
    auto rpd = ns_shared<MTL::RenderPassDescriptor>();

    rpd->colorAttachments()->object(0)->setTexture(dst);
    rpd->colorAttachments()->object(0)->setLoadAction(MTL::LoadActionClear);
    rpd->colorAttachments()->object(0)->setClearColor(MTL::ClearColor::Make(0.0f, 0.0f, 0.0f, 1.0f));

    auto postEnc = cmd->renderCommandEncoder(rpd);

    postEnc->setRenderPipelineState(m_pso);
    postEnc->setFragmentTexture(src, 0);
    postEnc->setFragmentBytes(&m_options, sizeof(Options), 0);

    postEnc->drawPrimitives(MTL::PrimitiveTypeTriangle, (NS::UInteger) 0, 6);
    postEnc->endEncoding();
  }

  PostProcessPass::Options options() final {
    return {
      PassType,
      {(ExposureOptions*) ((void*) &m_options)},
    };
  }

private:
  Options m_options;
};

using Exposure = BasicPostProcessPass<ExposureOptions, PostProcessPass::Type::Exposure, "exposure">;
using Vignette = BasicPostProcessPass<VignetteOptions, PostProcessPass::Type::Vignette, "vignette">;
using ChromaticAberration = BasicPostProcessPass<ChromaticAberrationOptions, PostProcessPass::Type::ChromaticAberration, "chromaticAberration">;
using ContrastSaturation = BasicPostProcessPass<ContrastSaturationOptions, PostProcessPass::Type::ContrastSaturation, "contrastSaturation">;
using ToneCurve = BasicPostProcessPass<ToneCurveOptions, PostProcessPass::Type::ToneCurve, "toneCurve">;
using Tonemap = BasicPostProcessPass<TonemapOptions, PostProcessPass::Type::Tonemap, "tonemap", true>;

#endif

}
}

#endif //PLATINUM_POSTPROCESSING_HPP
