#include "renderer_studio.hpp"

#include <print>
#include <utils/metal_utils.hpp>
#include <utils/matrices.hpp>
#include <utils/utils.hpp>

namespace pt::renderer_studio {
using metal_utils::operator ""_ns;
using metal_utils::ns_shared;

Renderer::Renderer(
  MTL::Device* device,
  MTL::CommandQueue* commandQueue,
  Store& store
) noexcept
  : m_store(store), m_camera(float3{-3, 3, 3}),
    m_device(device), m_commandQueue(commandQueue) {
  /*
   * Build the constants buffer
   */
  m_constantsSize = sizeof(Constants);
  m_constantsStride = utils::align(m_constantsSize, 256);
  m_constantsOffset = 0;

  m_constantsBuffer = m_device->newBuffer(
    m_constantsStride * m_maxFramesInFlight,
    MTL::ResourceStorageModeShared
  );

  m_simpleQuadVertexBuffer = m_device->newBuffer(
    4 * sizeof(float2),
    MTL::ResourceStorageModeShared
  );
  float2 vertices[] = {{-1.0, 1.0},
                       {1.0,  1.0},
                       {-1.0, -1.0},
                       {1.0,  -1.0}};
  memcpy(m_simpleQuadVertexBuffer->contents(), vertices, 4 * sizeof(float2));

  m_simpleQuadIndexBuffer = m_device->newBuffer(
    6 * sizeof(uint32_t),
    MTL::ResourceStorageModeShared
  );
  uint32_t indices[] = {0, 2, 1, 1, 2, 3};
  memcpy(m_simpleQuadIndexBuffer->contents(), indices, 6 * sizeof(uint32_t));

  /*
   * Build the readback buffer
   */
  m_objectIdReadbackBuffer = m_device
    ->newBuffer(m_objectIdPixelSize, MTL::ResourceStorageModeShared);

  buildShaders();
}

Renderer::~Renderer() {
  m_primaryRenderTarget->release();
  m_auxRenderTarget->release();
  m_objectIdRenderTarget->release();

  m_pso->release();
  m_dsso->release();
  m_postPassPso->release();
  m_postPassSso->release();

  m_constantsBuffer->release();
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

void Renderer::handleResizeViewport(const float2& size) {
  if (!equal(size, m_viewportSize)) {
    m_viewportSize = size;
    m_aspect = m_viewportSize.x / m_viewportSize.y;

    rebuildRenderTargets();
  }
}

const MTL::Texture* Renderer::presentRenderTarget() const {
  return m_primaryRenderTarget;
}

uint16_t Renderer::readbackObjectIdAt(uint32_t x, uint32_t y) const {
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

  return objectId;
}

void Renderer::render(uint16_t selectedNodeId) noexcept {
  // Don't render if the render targets are not initialized: this should only
  // happen in the first frame before handleResizeViewport() is called
  if (!m_primaryRenderTarget) return;

  NS::AutoreleasePool* autoreleasePool = NS::AutoreleasePool::alloc()->init();

  rebuildDataBuffer();
  updateConstants();

  auto cmd = m_commandQueue->commandBuffer();

  auto viewport = MTL::Viewport{
    0.0, 0.0,
    m_viewportSize.x, m_viewportSize.y,
    0.0, 1.0
  };

  /**
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

  auto enc = cmd->renderCommandEncoder(rpd);

  enc->setRenderPipelineState(m_pso);
  enc->setDepthStencilState(m_dsso);
  enc->setFrontFacingWinding(MTL::WindingCounterClockwise);
  enc->setCullMode(MTL::CullModeBack);

  enc->setViewport(viewport);
  enc->setVertexBuffer(m_constantsBuffer, m_constantsOffset, 3);

  size_t dataOffset = 0;
  for (const auto& md: m_meshData) {
    enc->setVertexBuffer(md.mesh->vertexPositions(), 0, 0);
    enc->setVertexBuffer(md.mesh->vertexData(), 0, 1);
    enc->setVertexBuffer(m_dataBuffer, dataOffset, 2);
    enc->drawIndexedPrimitives(
      MTL::PrimitiveTypeTriangle,
      md.mesh->indexCount(),
      MTL::IndexTypeUInt32,
      md.mesh->indices(),
      0
    );

    dataOffset += sizeof(NodeData);
  }

  enc->endEncoding();

  /*
   * Grid pass
   */
  rpd = ns_shared<MTL::RenderPassDescriptor>();
  colorAttachment = rpd->colorAttachments()->object(0);
  colorAttachment->setTexture(m_auxRenderTarget);
  colorAttachment->setLoadAction(MTL::LoadActionLoad);
  colorAttachment->setStoreAction(MTL::StoreActionStore);

  rpd->depthAttachment()->setTexture(m_depthTexture);
  rpd->depthAttachment()->setLoadAction(MTL::LoadActionLoad);

  enc = cmd->renderCommandEncoder(rpd);

  enc->setRenderPipelineState(m_gridPassPso);
  enc->setDepthStencilState(m_gridPassDsso);
  enc->setFrontFacingWinding(MTL::WindingCounterClockwise);
  enc->setCullMode(MTL::CullModeNone);

  enc->setViewport(viewport);
  enc->setVertexBuffer(m_simpleQuadVertexBuffer, 0, 0);
  enc->setVertexBuffer(m_constantsBuffer, m_constantsOffset, 1);
  enc->setFragmentBytes(&m_camera.position, sizeof(m_camera.position), 1);

  auto grid = m_gridProperties;
  for (int i = 0; i < 6; i++) {
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

void Renderer::buildShaders() {
  /*
   * Load the shader library, then load the shader functions
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
   * Set up a render pipeline descriptor (parameter object)
   * Set the vertex and fragment funcs and color attachment format (match view)
   */
  auto desc = metal_utils::makeRenderPipelineDescriptor(
    {
      .vertexFunction = metal_utils::getFunction(lib, "vertexShader"),
      .fragmentFunction = metal_utils::getFunction(lib, "fragmentShader"),
      .colorAttachments = {MTL::PixelFormatRGBA8Unorm, MTL::PixelFormatR16Uint},
      .depthFormat = MTL::PixelFormatDepth32Float,
    }
  );

  /*
   * Set up a vertex attribute descriptor, this tells Metal where each attribute
   * is located
   */
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
   * Create the grid pass pipeline state object
   */
  desc = metal_utils::makeRenderPipelineDescriptor(
    {
      .vertexFunction = metal_utils::getFunction(lib, "gridVertex"),
      .fragmentFunction = metal_utils::getFunction(lib, "gridFragment"),
      .colorAttachments = {MTL::PixelFormatRGBA8Unorm},
      .depthFormat = MTL::PixelFormatDepth32Float,
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
  depthStencilDesc->setDepthWriteEnabled(true);
  depthStencilDesc->setDepthCompareFunction(MTL::CompareFunctionLess);
  m_dsso = m_device->newDepthStencilState(depthStencilDesc);

  depthStencilDesc->setDepthWriteEnabled(false);
  m_gridPassDsso = m_device->newDepthStencilState(depthStencilDesc);

  lib->release();
}

void Renderer::rebuildDataBuffer() {
  /*
   * Discard existing buffer
   */
  if (m_dataBuffer != nullptr) m_dataBuffer->release();

  /*
   * Calculate buffer sizes and create buffers
   */
  m_meshData = m_store.scene().getAllMeshes();
  size_t meshCount = m_meshData.size();

  size_t dataBufferSize = meshCount * sizeof(NodeData);
  m_dataBuffer = m_device
    ->newBuffer(dataBufferSize, MTL::ResourceStorageModeShared);

  /*
   * Fill transform buffer
   */
  float4x4 view = m_camera.view();
  for (size_t i = 0; i < m_meshData.size(); i++) {
    const auto& md = m_meshData[i];

    float4x4 viewModel = view * md.transform;
    float4x4 vmit = transpose(inverse(viewModel));
    float3x3 normalViewModel(
      vmit.columns[0].xyz,
      vmit.columns[1].xyz,
      vmit.columns[2].xyz
    );

    const NodeData nodeData = {
      viewModel,
      normalViewModel,
      md.nodeId,
    };

    // Transform
    void* dbw = (char*) m_dataBuffer->contents() + i * sizeof(NodeData);
    memcpy(dbw, &nodeData, sizeof(NodeData));
  }
}

void Renderer::rebuildRenderTargets() {
  if (m_primaryRenderTarget != nullptr) m_primaryRenderTarget->release();
  if (m_auxRenderTarget != nullptr) m_auxRenderTarget->release();
  if (m_objectIdRenderTarget != nullptr) m_objectIdRenderTarget->release();
  if (m_depthTexture != nullptr) m_depthTexture->release();

  auto texd = MTL::TextureDescriptor::alloc()->init();
  texd->setTextureType(MTL::TextureType2D);
  texd->setWidth(static_cast<uint32_t>(m_viewportSize.x));
  texd->setHeight(static_cast<uint32_t>(m_viewportSize.y));
  texd->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  texd->setStorageMode(MTL::StorageModeShared);

  texd->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
  m_primaryRenderTarget = m_device->newTexture(texd);
  m_auxRenderTarget = m_device->newTexture(texd);

  texd->setPixelFormat(MTL::PixelFormatR16Uint);
  m_objectIdRenderTarget = m_device->newTexture(texd);

  texd->setPixelFormat(MTL::PixelFormatDepth32Float);
  m_depthTexture = m_device->newTexture(texd);

  texd->release();
}

void Renderer::updateConstants() {
  Constants constants = {
    m_camera.projection(m_aspect),
    m_camera.view(),
  };

  m_constantsOffset = (m_frameIdx % m_maxFramesInFlight) * m_constantsStride;
  void* bufferWrite = (char*) m_constantsBuffer->contents() + m_constantsOffset;
  memcpy(bufferWrite, &constants, m_constantsSize);
}

}
