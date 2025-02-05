#include "renderer_studio.hpp"

#include <print>

#include <utils/metal_utils.hpp>
#include <utils/matrices.hpp>
#include <utils/utils.hpp>
#include <frontend/theme.hpp>

namespace pt::renderer_studio {
using metal_utils::operator ""_ns;
using metal_utils::ns_shared;

Renderer::Renderer(
  MTL::Device* device,
  MTL::CommandQueue* commandQueue,
  Store& store
) noexcept
  : m_store(store), m_camera(float3{2, 3, 5}),
    m_device(device), m_commandQueue(commandQueue) {
  /*
   * Build the constants buffer
   */
  m_constantsSize = sizeof(shaders_studio::Constants);
  m_constantsStride = utils::align(m_constantsSize, 256);
  m_constantsOffset = 0;

  m_constantsBuffer = m_device->newBuffer(
    m_constantsStride * m_maxFramesInFlight,
    MTL::ResourceStorageModeShared
  );

  /*
   * Build quad buffers, for rendering grid and postprocess pass
   */
  m_simpleQuadVertexBuffer = m_device->newBuffer(
    4 * sizeof(float2),
    MTL::ResourceStorageModeShared
  );
  float2 quadVertices[] = {{-1.0, 1.0},
                           {1.0,  1.0},
                           {-1.0, -1.0},
                           {1.0,  -1.0}};
  memcpy(m_simpleQuadVertexBuffer->contents(), quadVertices, 4 * sizeof(float2));

  m_simpleQuadIndexBuffer = m_device->newBuffer(
    6 * sizeof(uint32_t),
    MTL::ResourceStorageModeShared
  );
  uint32_t quadIndices[] = {0, 2, 1, 1, 2, 3};
  memcpy(m_simpleQuadIndexBuffer->contents(), quadIndices, 6 * sizeof(uint32_t));

  /*
   * Build the camera object buffer
   */
  m_cameraVertexBuffer = m_device->newBuffer(
    5 * sizeof(float3),
    MTL::ResourceStorageModeShared
  );
  float3 cameraVertices[] = {{0,    0,    0},
                             {-0.5, 0.5,  -1},
                             {0.5,  0.5,  -1},
                             {-0.5, -0.5, -1},
                             {0.5,  -0.5, -1}};
  memcpy(m_cameraVertexBuffer->contents(), cameraVertices, 5 * sizeof(float3));

  m_cameraIndexBuffer = m_device->newBuffer(
    16 * sizeof(uint32_t),
    MTL::ResourceStorageModeShared
  );
  uint32_t cameraIndices[] = {0, 1, 0, 2, 0, 3, 0, 4, 1, 2, 3, 4, 1, 3, 2, 4};
  memcpy(m_cameraIndexBuffer->contents(), cameraIndices, 16 * sizeof(uint32_t));

  /*
   * Build the readback buffer
   */
  m_objectIdReadbackBuffer = m_device
    ->newBuffer(m_objectIdPixelSize, MTL::ResourceStorageModeShared);

  buildPipelines();
}

Renderer::~Renderer() {
  m_primaryRenderTarget->release();
  m_auxRenderTarget->release();
  m_objectIdRenderTarget->release();

  m_pso->release();
  m_dsso->release();
  m_cameraPso->release();
  m_cameraDsso->release();
  m_postPassPso->release();
  m_postPassSso->release();

  m_constantsBuffer->release();
  m_dataBuffer->release();
  m_cameraDataBuffer->release();
  m_cameraVertexBuffer->release();
  m_cameraIndexBuffer->release();
  m_simpleQuadVertexBuffer->release();
  m_simpleQuadIndexBuffer->release();
  m_objectIdReadbackBuffer->release();
}

void Renderer::handleScrollEvent(const float2& delta) {
  m_camera.orbit(-delta);
}

void Renderer::handleZoomEvent(float delta) {
  m_camera.moveTowardTarget(delta);
}

void Renderer::handlePanEvent(const float2& delta) {
  m_camera.pan(delta, m_aspect);
}

void Renderer::handleResizeViewport(const float2& size) {
  if (!equal(size, m_viewportSize)) {
    m_viewportSize = size;
    m_aspect = m_viewportSize.x / m_viewportSize.y;

    rebuildRenderTargets();
  }
}

void Renderer::cameraTo(const float3& pos) {
  float3 delta = m_camera.position - m_camera.target;
  m_camera.target = pos;
  m_camera.position = pos + delta;
}

const MTL::Texture* Renderer::presentRenderTarget() const {
  return m_primaryRenderTarget;
}

Scene::NodeID Renderer::readbackObjectIdAt(uint32_t x, uint32_t y) const {
  auto cmd = m_commandQueue->commandBuffer();
  auto benc = cmd->blitCommandEncoder();

  benc->copyFromTexture(
    m_objectIdRenderTarget,
    0,
    0,
    MTL::Origin(x * 2, y * 2, 0),
    MTL::Size(1, 1, 1),
    m_objectIdReadbackBuffer,
    0,
    m_objectIdPixelSize, // * 1 pixel per row
    m_objectIdPixelSize  // * 1 pixel in the image
  );
  benc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  uint16_t objectId;
  auto contents = m_objectIdReadbackBuffer->contents();
  memcpy(&objectId, contents, m_objectIdPixelSize);

  return Scene::NodeID(objectId);
}

void Renderer::render(Scene::NodeID selectedNodeId) {
  // Don't render if the render targets are not initialized: this should only
  // happen in the first frame before handleResizeViewport() is called
  if (!m_primaryRenderTarget) return;

  NS::AutoreleasePool* autoreleasePool = NS::AutoreleasePool::alloc()->init();

  rebuildDataBuffers();
  updateTheme();
  updateConstants();

  auto cmd = m_commandQueue->commandBuffer();

  auto viewport = MTL::Viewport{
    0.0, 0.0,
    m_viewportSize.x, m_viewportSize.y,
    0.0, 1.0
  };

  /*
   * Main pass
   */
  auto rpd = ns_shared<MTL::RenderPassDescriptor>();
  auto colorAttachment = rpd->colorAttachments()->object(0);
  colorAttachment->setTexture(m_auxRenderTarget);
  colorAttachment->setClearColor(
    MTL::ClearColor::Make(
      m_clearColor[0] * m_clearColor[3],
      m_clearColor[1] * m_clearColor[3],
      m_clearColor[2] * m_clearColor[3],
      m_clearColor[3]
    )
  );
  colorAttachment->setLoadAction(MTL::LoadActionClear);
  colorAttachment->setStoreAction(MTL::StoreActionStore);

  auto geomAttachment = rpd->colorAttachments()->object(1);
  geomAttachment->setClearColor(MTL::ClearColor::Make(0.0f, 0.0f, 0.0f, 1.0f));
  geomAttachment->setTexture(m_objectIdRenderTarget);
  geomAttachment->setLoadAction(MTL::LoadActionClear);
  geomAttachment->setStoreAction(MTL::StoreActionStore);

  rpd->depthAttachment()->setTexture(m_depthTexture);
  rpd->depthAttachment()->setStoreAction(MTL::StoreActionStore);

  rpd->stencilAttachment()->setTexture(m_stencilTexture);
  rpd->stencilAttachment()->setStoreAction(MTL::StoreActionStore);

  auto enc = cmd->renderCommandEncoder(rpd);

  enc->setRenderPipelineState(m_pso);
  enc->setDepthStencilState(m_dsso);
  enc->setFrontFacingWinding(MTL::WindingCounterClockwise);
  enc->setCullMode(MTL::CullModeNone);

  enc->setViewport(viewport);
  enc->setVertexBuffer(m_constantsBuffer, m_constantsOffset, 3);
  enc->setFragmentBytes(&m_camera.position, sizeof(m_camera.position), 0);
  enc->setFragmentBuffer(m_constantsBuffer, m_constantsOffset, 1);

  size_t dataOffset = 0;
//  for (const auto& md: m_meshData) {
//    enc->setVertexBuffer(md.mesh->vertexPositions(), 0, 0);
//    enc->setVertexBuffer(md.mesh->vertexData(), 0, 1);
//    enc->setVertexBuffer(m_dataBuffer, dataOffset, 2);
//    enc->drawIndexedPrimitives(
//      MTL::PrimitiveTypeTriangle,
//      md.mesh->indexCount(),
//      MTL::IndexTypeUInt32,
//      md.mesh->indices(),
//      0
//    );
//
//    dataOffset += sizeof(shaders_studio::NodeData);
//  }

  enc->endEncoding();

  /*
   * Camera pass
   */
  rpd = ns_shared<MTL::RenderPassDescriptor>();
  colorAttachment = rpd->colorAttachments()->object(0);
  colorAttachment->setTexture(m_auxRenderTarget);
  colorAttachment->setLoadAction(MTL::LoadActionLoad);
  colorAttachment->setStoreAction(MTL::StoreActionStore);

  rpd->depthAttachment()->setTexture(m_depthTexture);
  rpd->depthAttachment()->setLoadAction(MTL::LoadActionLoad);
  rpd->depthAttachment()->setStoreAction(MTL::StoreActionStore);

  rpd->stencilAttachment()->setTexture(m_stencilTexture);
  rpd->stencilAttachment()->setLoadAction(MTL::LoadActionLoad);
  rpd->stencilAttachment()->setStoreAction(MTL::StoreActionStore);

  enc = cmd->renderCommandEncoder(rpd);

  enc->setRenderPipelineState(m_cameraPso);
  enc->setDepthStencilState(m_cameraDsso);
  enc->setFrontFacingWinding(MTL::WindingCounterClockwise);
  enc->setCullMode(MTL::CullModeNone);

  enc->setViewport(viewport);
  enc->setVertexBuffer(m_cameraVertexBuffer, 0, 0);
  enc->setVertexBuffer(m_constantsBuffer, m_constantsOffset, 2);
  enc->setFragmentBytes(&selectedNodeId, sizeof(selectedNodeId), 0);
  enc->setFragmentBytes(&m_edgeConstants, sizeof(m_edgeConstants), 1);

  // TODO this can use instanced rendering
  // probably no big deal because we won't ever have more than a few cameras
  dataOffset = 0;
//  for ([[maybe_unused]] const auto& cd: m_cameraData) {
//    enc->setVertexBuffer(m_cameraDataBuffer, dataOffset, 1);
//    enc->drawIndexedPrimitives(
//      MTL::PrimitiveTypeLine,
//      16,
//      MTL::IndexTypeUInt32,
//      m_cameraIndexBuffer,
//      0
//    );
//
//    dataOffset += sizeof(shaders_studio::NodeData);
//  }

  /*
   * Grid pass
   * The pass is identical, so we can reuse the same command encoder
   */
  enc->setRenderPipelineState(m_gridPassPso);
  enc->setDepthStencilState(m_gridPassDsso);
  enc->setFrontFacingWinding(MTL::WindingCounterClockwise);
  enc->setCullMode(MTL::CullModeNone);
  enc->setStencilReferenceValue(1);

  enc->setViewport(viewport);
  enc->setVertexBuffer(m_simpleQuadVertexBuffer, 0, 0);
  enc->setVertexBuffer(m_constantsBuffer, m_constantsOffset, 1);
  enc->setFragmentBytes(&m_camera.position, sizeof(m_camera.position), 1);

  auto grid = m_gridProperties;
  for (int i = 0; i < 4; i++) {
    enc->setVertexBytes(&grid, sizeof(grid), 2);
    enc->setFragmentBytes(&grid, sizeof(grid), 0);
    enc->drawIndexedPrimitives(
      MTL::PrimitiveTypeTriangle,
      6,
      MTL::IndexTypeUInt32,
      m_simpleQuadIndexBuffer,
      0
    );

    grid.level++;
    grid.spacing *= 10.0f;
  }
  enc->endEncoding();

  /*
   * Post process pass
   */
  rpd = ns_shared<MTL::RenderPassDescriptor>();
  colorAttachment = rpd->colorAttachments()->object(0);
  colorAttachment->setTexture(m_primaryRenderTarget);
  colorAttachment->setLoadAction(MTL::LoadActionDontCare);
  colorAttachment->setStoreAction(MTL::StoreActionStore);

  enc = cmd->renderCommandEncoder(rpd);

  enc->setRenderPipelineState(m_postPassPso);
  enc->setFrontFacingWinding(MTL::WindingCounterClockwise);
  enc->setCullMode(MTL::CullModeBack);

  enc->setFragmentTexture(m_auxRenderTarget, 0);
  enc->setFragmentTexture(m_objectIdRenderTarget, 1);
  enc->setFragmentSamplerState(m_postPassSso, 0);

  enc->setViewport(viewport);
  enc->setVertexBuffer(m_simpleQuadVertexBuffer, 0, 0);
  enc->setFragmentBytes(&m_viewportSize, sizeof(m_viewportSize), 0);
  enc->setFragmentBytes(&selectedNodeId, sizeof(selectedNodeId), 1);
  enc->setFragmentBytes(&m_edgeConstants, sizeof(m_edgeConstants), 2);
  enc->drawIndexedPrimitives(
    MTL::PrimitiveTypeTriangle,
    6,
    MTL::IndexTypeUInt32,
    m_simpleQuadIndexBuffer,
    0
  );

  enc->endEncoding();
  cmd->commit();

  m_frameIdx++;

  autoreleasePool->release();
}

void Renderer::buildPipelines() {
  /*
   * Load the shader library
   */
  NS::Error* error = nullptr;
  MTL::Library* lib = m_device
    ->newLibrary("renderer_studio.metallib"_ns, &error);
  if (!lib) {
    std::println(
      "renderer_studio: Failed to load shader library: {}\n",
      error->localizedDescription()->utf8String()
    );
    assert(false);
  }

  /*
   * Create the main render pass pipeline
   */
  auto desc = metal_utils::makeRenderPipelineDescriptor(
    {
      .vertexFunction = metal_utils::getFunction(lib, "vertexShader"),
      .fragmentFunction = metal_utils::getFunction(lib, "fragmentShader"),
      .colorAttachments = {MTL::PixelFormatRGBA8Unorm, MTL::PixelFormatR16Uint},
      .depthFormat = MTL::PixelFormatDepth32Float,
      .stencilFormat = MTL::PixelFormatStencil8,
    }
  );
  auto vertexDesc = metal_utils::makeVertexDescriptor(
    {
      .attributes = {
        {
          .format = MTL::VertexFormatFloat3,
          .bufferIndex = 0,
        },
        {
          .format = MTL::VertexFormatFloat3,
          .offset = offsetof(VertexData, normal),
          .bufferIndex = 1,
        }
      },
      .layouts = {
        {.stride = sizeof(float3)},
        {.stride = sizeof(VertexData)},
      }
    }
  );

  desc->setVertexDescriptor(vertexDesc);

  /*
   * Create the main render pipeline state object
   */
  m_pso = m_device->newRenderPipelineState(desc, &error);
  if (!m_pso) {
    std::println(
      "renderer_studio: Failed to create main render pass PSO: {}\n",
      error->localizedDescription()->utf8String()
    );
    assert(false);
  }

  /*
   * Create the camera pass pipeline state object
   */
  desc = metal_utils::makeRenderPipelineDescriptor(
    {
      .vertexFunction = metal_utils::getFunction(lib, "cameraVertex"),
      .fragmentFunction = metal_utils::getFunction(lib, "cameraFragment"),
      .colorAttachments = {MTL::PixelFormatRGBA8Unorm},
      .depthFormat = MTL::PixelFormatDepth32Float,
      .stencilFormat = MTL::PixelFormatStencil8,
    }
  );

  vertexDesc = metal_utils::makeVertexDescriptor(
    {
      .attributes = {
        {.format = MTL::VertexFormatFloat3},
      },
      .layouts = {
        {.stride = sizeof(float3)},
      }
    }
  );

  desc->setVertexDescriptor(vertexDesc);

  m_cameraPso = m_device->newRenderPipelineState(desc, &error);
  if (!m_cameraPso) {
    std::println(
      "renderer_studio: Failed to create camera render pass PSO: {}\n",
      error->localizedDescription()->utf8String()
    );
    assert(false);
  }

  /*
   * Create the grid pass pipeline state object
   */
  desc = metal_utils::makeRenderPipelineDescriptor(
    {
      .vertexFunction = metal_utils::getFunction(lib, "gridVertex"),
      .fragmentFunction = metal_utils::getFunction(lib, "gridFragment"),
      .colorAttachments = {MTL::PixelFormatRGBA8Unorm},
      .depthFormat = MTL::PixelFormatDepth32Float,
      .stencilFormat = MTL::PixelFormatStencil8,
    }
  );

  vertexDesc = metal_utils::makeVertexDescriptor(
    {
      .attributes = {
        {.format = MTL::VertexFormatFloat2},
      },
      .layouts = {
        {.stride = sizeof(float2)},
      }
    }
  );

  desc->setVertexDescriptor(vertexDesc);
  metal_utils::enableBlending(desc->colorAttachments()->object(0));

  m_gridPassPso = m_device->newRenderPipelineState(desc, &error);
  if (!m_gridPassPso) {
    std::println(
      "renderer_studio: Failed to create grid render pass PSO: {}\n",
      error->localizedDescription()->utf8String()
    );
    assert(false);
  }

  /*
   * Create the edge pass pipeline state object
   */
  desc = metal_utils::makeRenderPipelineDescriptor(
    {
      .vertexFunction = metal_utils::getFunction(lib, "edgePassVertex"),
      .fragmentFunction = metal_utils::getFunction(lib, "edgePassFragment"),
      .colorAttachments = {MTL::PixelFormatRGBA8Unorm_sRGB},
    }
  );

  vertexDesc = metal_utils::makeVertexDescriptor(
    {
      .attributes = {
        {.format = MTL::VertexFormatFloat2},
      },
      .layouts = {
        {.stride = sizeof(float2)},
      }
    }
  );

  desc->setVertexDescriptor(vertexDesc);

  m_postPassPso = m_device->newRenderPipelineState(desc, &error);
  if (!m_postPassPso) {
    std::println(
      "renderer_studio: Failed to create post process pass PSO: {}\n",
      error->localizedDescription()->utf8String()
    );
    assert(false);
  }

  auto samplerDesc = ns_shared<MTL::SamplerDescriptor>();
  samplerDesc->setMagFilter(MTL::SamplerMinMagFilterNearest);
  samplerDesc->setMinFilter(MTL::SamplerMinMagFilterNearest);
  samplerDesc->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  samplerDesc->setTAddressMode(MTL::SamplerAddressModeClampToEdge);

  m_postPassSso = m_device->newSamplerState(samplerDesc);

  /*
   * Set up the depth/stencil buffer
   */
  auto depthStencilDesc = ns_shared<MTL::DepthStencilDescriptor>();
  auto stencilDesc = ns_shared<MTL::StencilDescriptor>();

  // Main pass
  stencilDesc->setDepthStencilPassOperation(MTL::StencilOperationReplace);
  depthStencilDesc->setFrontFaceStencil(stencilDesc);
  depthStencilDesc->setBackFaceStencil(stencilDesc);
  depthStencilDesc->setDepthWriteEnabled(true);
  depthStencilDesc->setDepthCompareFunction(MTL::CompareFunctionLess);
  m_dsso = m_device->newDepthStencilState(depthStencilDesc);

  // Camera pass
  stencilDesc->setDepthStencilPassOperation(MTL::StencilOperationKeep);
  depthStencilDesc->setFrontFaceStencil(stencilDesc);
  depthStencilDesc->setBackFaceStencil(stencilDesc);
  depthStencilDesc->setDepthWriteEnabled(true);
  depthStencilDesc->setDepthCompareFunction(MTL::CompareFunctionLess);
  m_cameraDsso = m_device->newDepthStencilState(depthStencilDesc);

  // Grid pass
  stencilDesc->setStencilCompareFunction(MTL::CompareFunctionGreater);
  stencilDesc->setDepthStencilPassOperation(MTL::StencilOperationKeep);
  depthStencilDesc->setFrontFaceStencil(stencilDesc);
  depthStencilDesc->setBackFaceStencil(stencilDesc);
  depthStencilDesc->setDepthWriteEnabled(false);
  m_gridPassDsso = m_device->newDepthStencilState(depthStencilDesc);

  lib->release();
}

void Renderer::rebuildDataBuffers() {
  /*
   * Discard existing buffers
   */
  if (m_dataBuffer != nullptr) m_dataBuffer->release();
  if (m_cameraDataBuffer != nullptr) m_cameraDataBuffer->release();

  /*
   * Calculate buffer sizes and create buffers
   */
//  m_meshData = m_store.scene().getAllInstances(Scene::NodeFlags_Visible);
//  size_t meshCount = m_meshData.size();
//
//  size_t dataBufferSize = meshCount * sizeof(shaders_studio::NodeData);
//  m_dataBuffer = m_device
//    ->newBuffer(dataBufferSize, MTL::ResourceStorageModeShared);
//
//  m_cameraData = m_store.scene().getAllCameras(Scene::NodeFlags_Visible);
//  size_t cameraCount = m_cameraData.size();
//
//  size_t cameraDataBufferSize = cameraCount * sizeof(shaders_studio::NodeData);
//  m_cameraDataBuffer = m_device
//    ->newBuffer(cameraDataBufferSize, MTL::ResourceStorageModeShared);

  /*
   * Fill transform buffers
   */
//  float4x4 view = m_camera.view();
//  for (size_t i = 0; i < m_meshData.size(); i++) {
//    const auto& md = m_meshData[i];
//
//    float4x4 vmit = transpose(inverse(view * md.transform));
//    float3x3 normalViewModel(
//      vmit.columns[0].xyz,
//      vmit.columns[1].xyz,
//      vmit.columns[2].xyz
//    );
//
//    const shaders_studio::NodeData nodeData = {
//      .model = md.transform,
//      .normalViewModel = normalViewModel,
//      .nodeIdx = md.nodeId,
//    };
//
//    // Transform
//    void* dbw = (char*) m_dataBuffer->contents() + i * sizeof(shaders_studio::NodeData);
//    memcpy(dbw, &nodeData, sizeof(shaders_studio::NodeData));
//  }

//  for (size_t i = 0; i < m_cameraData.size(); i++) {
//    const auto& cd = m_cameraData[i];
//
//    // Rescale the camera according to its parameters
//    const float3 scale = {
//      length(cd.transform.columns[0]),
//      length(cd.transform.columns[1]),
//      length(cd.transform.columns[2]),
//    };
//    float4x4 transform = {
//      cd.transform.columns[0] / scale.x,
//      cd.transform.columns[1] / scale.y,
//      cd.transform.columns[2] / scale.z,
//      cd.transform.columns[3],
//    };
//
//    auto newScale = make_float3(cd.camera->sensorSize, cd.camera->focalLength) * 0.1f;
//    transform *= mat::scaling(newScale);
//
//    // Don't need a normal transform matrix, leave it empty
//    const shaders_studio::NodeData nodeData = {
//      .model = transform,
//      .nodeIdx = cd.nodeId,
//    };

//    // Transform
//    void* dbw = (char*) m_cameraDataBuffer->contents() + i * sizeof(shaders_studio::NodeData);
//    memcpy(dbw, &nodeData, sizeof(shaders_studio::NodeData));
//  }
}

void Renderer::rebuildRenderTargets() {
  if (m_primaryRenderTarget != nullptr) m_primaryRenderTarget->release();
  if (m_auxRenderTarget != nullptr) m_auxRenderTarget->release();
  if (m_objectIdRenderTarget != nullptr) m_objectIdRenderTarget->release();
  if (m_depthTexture != nullptr) m_depthTexture->release();
  if (m_stencilTexture != nullptr) m_stencilTexture->release();

  auto texd = MTL::TextureDescriptor::alloc()->init();
  texd->setTextureType(MTL::TextureType2D);
  texd->setWidth(static_cast<uint32_t>(m_viewportSize.x));
  texd->setHeight(static_cast<uint32_t>(m_viewportSize.y));
  texd->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  texd->setStorageMode(MTL::StorageModeShared);

  texd->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
  m_auxRenderTarget = m_device->newTexture(texd);
  //  texd->setPixelFormat(MTL::PixelFormatRGBA8Unorm_sRGB);
  m_primaryRenderTarget = m_device->newTexture(texd);

  texd->setPixelFormat(MTL::PixelFormatR16Uint);
  m_objectIdRenderTarget = m_device->newTexture(texd);

  texd->setPixelFormat(MTL::PixelFormatDepth32Float);
  m_depthTexture = m_device->newTexture(texd);

  texd->setPixelFormat(MTL::PixelFormatStencil8);
  m_stencilTexture = m_device->newTexture(texd);

  texd->release();
}

void Renderer::updateConstants() {
  shaders_studio::Constants constants = {
    .projection = m_camera.projection(m_aspect),
    .view = m_camera.view(),
    .objectColor = m_objectColor,
  };

  m_constantsOffset = (m_frameIdx % m_maxFramesInFlight) * m_constantsStride;
  void* bufferWrite = (char*) m_constantsBuffer->contents() + m_constantsOffset;
  memcpy(bufferWrite, &constants, m_constantsSize);
}

void Renderer::updateTheme() {
  m_clearColor.xyz = frontend::theme::Theme::currentTheme->viewportBackground;
  m_objectColor = frontend::theme::Theme::currentTheme->viewportModel;
  
  m_gridProperties.lineColor = frontend::theme::Theme::currentTheme->viewportGrid;
  m_gridProperties.xAxisColor = frontend::theme::Theme::currentTheme->viewportAxisX;
  m_gridProperties.zAxisColor = frontend::theme::Theme::currentTheme->viewportAxisZ;
  
  m_edgeConstants.selectionColor = frontend::theme::Theme::currentTheme->primary;
  m_edgeConstants.outlineColor = frontend::theme::Theme::currentTheme->viewportOutline;
}

}
