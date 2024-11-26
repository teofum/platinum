#include "renderer_studio.hpp"

#include <print>
#include <utils/metal_utils.hpp>
#include <utils/matrices.hpp>
#include <utils/utils.hpp>

namespace pt::renderer_studio {
using metal_utils::operator ""_ns;

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

  m_vertexPosBuffer->release();
  m_vertexDataBuffer->release();
  m_indexBuffer->release();
  m_constantsBuffer->release();
  m_simpleQuadVertexBuffer->release();
  m_simpleQuadIndexBuffer->release();
  m_objectIdReadbackBuffer->release();
}

float* Renderer::clearColor() {
  return m_clearColor;
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

void Renderer::render() noexcept {
  // Don't render if the render targets are not initialized: this should only
  // happen in the first frame before handleResizeViewport() is called
  if (!m_primaryRenderTarget) return;

  NS::AutoreleasePool* autoreleasePool = NS::AutoreleasePool::alloc()->init();

  buildBuffers();
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
  auto rpd = MTL::RenderPassDescriptor::alloc()->init();
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
  rpd->release();

  enc->setRenderPipelineState(m_pso);
  enc->setDepthStencilState(m_dsso);
  enc->setFrontFacingWinding(MTL::WindingCounterClockwise);
  enc->setCullMode(MTL::CullModeBack);

  enc->setViewport(viewport);
  enc->setVertexBuffer(m_constantsBuffer, m_constantsOffset, 3);

  size_t dataOffset = 0;
  for (const auto& md: m_meshData) {
    enc->setVertexBuffer(m_vertexPosBuffer, md.vertexPosOffset, 0);
    enc->setVertexBuffer(m_vertexDataBuffer, md.vertexDataOffset, 1);
    enc->setVertexBuffer(m_dataBuffer, dataOffset, 2);
    enc->drawIndexedPrimitives(
      MTL::PrimitiveTypeTriangle,
      md.indexCount,
      MTL::IndexTypeUInt32,
      m_indexBuffer,
      md.indexOffset
    );

    dataOffset += sizeof(NodeData);
  }

  enc->endEncoding();

  /*
   * Grid pass
   */
  rpd = MTL::RenderPassDescriptor::alloc()->init();
  colorAttachment = rpd->colorAttachments()->object(0);
  colorAttachment->setTexture(m_auxRenderTarget);
  colorAttachment->setLoadAction(MTL::LoadActionLoad);
  colorAttachment->setStoreAction(MTL::StoreActionStore);

  rpd->depthAttachment()->setTexture(m_depthTexture);
  rpd->depthAttachment()->setLoadAction(MTL::LoadActionLoad);

  auto encGrid = cmd->renderCommandEncoder(rpd);
  rpd->release();

  encGrid->setRenderPipelineState(m_gridPassPso);
  encGrid->setDepthStencilState(m_gridPassDsso);
  encGrid->setFrontFacingWinding(MTL::WindingCounterClockwise);
  encGrid->setCullMode(MTL::CullModeNone);

  encGrid->setViewport(viewport);
  encGrid->setVertexBuffer(m_simpleQuadVertexBuffer, 0, 0);
  encGrid->setVertexBuffer(m_constantsBuffer, m_constantsOffset, 1);
  encGrid->setFragmentBytes(&m_camera.position, sizeof(m_camera.position), 1);

  auto grid = m_gridProperties;
  for (int i = 0; i < 6; i++) {
    encGrid->setVertexBytes(&grid, sizeof(grid), 2);
    encGrid->setFragmentBytes(&grid, sizeof(grid), 0);
    encGrid->drawIndexedPrimitives(
      MTL::PrimitiveTypeTriangle,
      6,
      MTL::IndexTypeUInt32,
      m_simpleQuadIndexBuffer,
      0
    );

    grid.level++;
    grid.spacing *= 10.0f;
  }
  encGrid->endEncoding();

  /*
   * Post process pass
   */
  rpd = MTL::RenderPassDescriptor::alloc()->init();
  colorAttachment = rpd->colorAttachments()->object(0);
  colorAttachment->setTexture(m_primaryRenderTarget);
  colorAttachment->setLoadAction(MTL::LoadActionDontCare);
  colorAttachment->setStoreAction(MTL::StoreActionStore);

  auto encPost = cmd->renderCommandEncoder(rpd);
  rpd->release();

  encPost->setRenderPipelineState(m_postPassPso);
  encPost->setFrontFacingWinding(MTL::WindingCounterClockwise);
  encPost->setCullMode(MTL::CullModeBack);

  encPost->setFragmentTexture(m_auxRenderTarget, 0);
  encPost->setFragmentTexture(m_objectIdRenderTarget, 1);
  encPost->setFragmentSamplerState(m_postPassSso, 0);

  encPost->setViewport(viewport);
  encPost->setVertexBuffer(m_simpleQuadVertexBuffer, 0, 0);
  encPost->setFragmentBytes(&m_viewportSize, sizeof(m_viewportSize), 0);
  encPost->drawIndexedPrimitives(
    MTL::PrimitiveTypeTriangle,
    6,
    MTL::IndexTypeUInt32,
    m_simpleQuadIndexBuffer,
    0
  );

  encPost->endEncoding();
  cmd->commit();

  m_frameIdx++;

  autoreleasePool->release();
}

void Renderer::buildBuffers() {
  /*
   * Discard existing buffers
   */
  if (m_vertexPosBuffer != nullptr) m_vertexPosBuffer->release();
  if (m_vertexDataBuffer != nullptr) m_vertexDataBuffer->release();
  if (m_indexBuffer != nullptr) m_indexBuffer->release();
  if (m_dataBuffer != nullptr) m_dataBuffer->release();

  /*
   * Calculate buffer sizes and create buffers
   */
  auto meshes = m_store.scene().getAllMeshes();
  size_t vertexPosSize = 0, vertexDataSize = 0, indexSize = 0;
  size_t meshCount = meshes.size();

  m_meshData.clear();
  m_meshData.reserve(meshes.size());
  for (const auto& md: meshes) {
    const Mesh& mesh = md.mesh;
    const size_t vc = mesh.vertexPositions().size();

    m_meshData.emplace_back(
      vertexPosSize,
      vertexDataSize,
      vc,
      indexSize,
      mesh.indices().size(),
      static_cast<uint16_t>(md.nodeIdx)
    );

    vertexPosSize += utils::align(vc * sizeof(float3), 256);
    vertexDataSize += utils::align(vc * sizeof(VertexData), 256);
    indexSize += utils::align(mesh.indices().size() * sizeof(uint32_t), 256);
  }

  size_t vertexPosBufferSize = vertexPosSize * sizeof(float3);
  m_vertexPosBuffer = m_device
    ->newBuffer(vertexPosBufferSize, MTL::ResourceStorageModeManaged);

  size_t vertexDataBufferSize = vertexDataSize * sizeof(VertexData);
  m_vertexDataBuffer = m_device
    ->newBuffer(vertexDataBufferSize, MTL::ResourceStorageModeManaged);

  size_t indexBufferSize = indexSize * sizeof(uint32_t);
  m_indexBuffer = m_device
    ->newBuffer(indexBufferSize, MTL::ResourceStorageModeShared);

  size_t dataBufferSize = meshCount * sizeof(NodeData);
  m_dataBuffer = m_device
    ->newBuffer(dataBufferSize, MTL::ResourceStorageModeShared);

  /*
   * Fill mesh data and transform buffers
   */
  float4x4 view = m_camera.view();
  for (size_t i = 0; i < m_meshData.size(); i++) {
    const Mesh& mesh = meshes[i].mesh;
    const MeshData& data = m_meshData[i];

    float4x4 viewModel = view * meshes[i].transform;
    float4x4 vmit = transpose(inverse(viewModel));
    float3x3 normalViewModel(
      vmit.columns[0].xyz,
      vmit.columns[1].xyz,
      vmit.columns[2].xyz
    );

    const NodeData nodeData = {
      viewModel,
      normalViewModel,
      data.nodeIdx,
    };

    // Vertices
    void* vpbw = (char*) m_vertexPosBuffer->contents() + data.vertexPosOffset;
    memcpy(
      vpbw,
      mesh.vertexPositions().data(),
      data.vertexCount * sizeof(float3)
    );

    // Vertices
    void* vdbw = (char*) m_vertexDataBuffer->contents() + data.vertexDataOffset;
    memcpy(
      vdbw,
      mesh.vertexData().data(),
      data.vertexCount * sizeof(VertexData)
    );

    // Indices
    void* ibw = (char*) m_indexBuffer->contents() + data.indexOffset;
    memcpy(ibw, mesh.indices().data(), data.indexCount * sizeof(uint32_t));

    // Transform
    void* dbw = (char*) m_dataBuffer->contents() + i * sizeof(NodeData);
    memcpy(dbw, &nodeData, sizeof(NodeData));
  }
}

void Renderer::buildShaders() {
  /*
   * Load the shader library, then load the shader functions
   */
  NS::Error* error = nullptr;
  MTL::Library* lib = m_device
    ->newLibrary("renderer_studio.metallib"_ns, &error);
  if (!lib) {
    // TODO
//    std::cerr << error->localizedDescription()->utf8String() << "\n";
    assert(false);
  }

  MTL::Function* vertexFunction = lib->newFunction("vertexShader"_ns);
  MTL::Function* fragmentFunction = lib->newFunction("fragmentShader"_ns);

  /*
   * Set up a render pipeline descriptor (parameter object)
   * Set the vertex and fragment funcs and color attachment format (match view)
   */
  auto desc = MTL::RenderPipelineDescriptor::alloc()->init();
  desc->setVertexFunction(vertexFunction);
  desc->setFragmentFunction(fragmentFunction);
  desc->colorAttachments()->object(0)
      ->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
  desc->colorAttachments()->object(1)
      ->setPixelFormat(MTL::PixelFormatR16Uint);
  desc->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);

  /*
   * Set up a vertex attribute descriptor, this tells Metal where each attribute
   * is located
   * TODO: this can be encapsulated in a less verbose API
   */
  auto* vertexDesc = MTL::VertexDescriptor::alloc()->init();

  auto positionAttribDesc = MTL::VertexAttributeDescriptor::alloc()->init();
  positionAttribDesc->setFormat(MTL::VertexFormatFloat3);
  positionAttribDesc->setOffset(0);
  positionAttribDesc->setBufferIndex(0);

  vertexDesc->attributes()->setObject(positionAttribDesc, 0);

  auto normalAttribDesc = MTL::VertexAttributeDescriptor::alloc()->init();
  normalAttribDesc->setFormat(MTL::VertexFormatFloat3);
  normalAttribDesc->setOffset(offsetof(VertexData, normal));
  normalAttribDesc->setBufferIndex(1);

  vertexDesc->attributes()->setObject(normalAttribDesc, 1);

  auto vertexLayout = MTL::VertexBufferLayoutDescriptor::alloc()->init();
  vertexLayout->setStride(sizeof(float3));
  vertexDesc->layouts()->setObject(vertexLayout, 0);

  vertexLayout->setStride(sizeof(VertexData));
  vertexDesc->layouts()->setObject(vertexLayout, 1);

  desc->setVertexDescriptor(vertexDesc);
  vertexLayout->release();
  vertexDesc->release();

  /*
   * Create the main render pipeline state object
   */
  m_pso = m_device->newRenderPipelineState(desc, &error);
  if (!m_pso) {
    // TODO
//    std::cerr << error->localizedDescription()->utf8String() << "\n";
    assert(false);
  }

  vertexFunction->release();
  fragmentFunction->release();
  desc->release();

  /*
   * Create the grid pass pipeline state object
   */
  vertexFunction = lib->newFunction("gridVertex"_ns);
  fragmentFunction = lib->newFunction("gridFragment"_ns);

  desc = MTL::RenderPipelineDescriptor::alloc()->init();
  desc->setVertexFunction(vertexFunction);
  desc->setFragmentFunction(fragmentFunction);
  desc->colorAttachments()->object(0)
      ->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
  desc->colorAttachments()->object(0)->setBlendingEnabled(true);
  desc->colorAttachments()->object(0)
      ->setRgbBlendOperation(MTL::BlendOperationAdd);
  desc->colorAttachments()->object(0)
      ->setSourceRGBBlendFactor(MTL::BlendFactorSourceAlpha);
  desc->colorAttachments()->object(0)
      ->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
  desc->colorAttachments()->object(0)
      ->setAlphaBlendOperation(MTL::BlendOperationAdd);
  desc->colorAttachments()->object(0)
      ->setSourceAlphaBlendFactor(MTL::BlendFactorSourceAlpha);
  desc->colorAttachments()->object(0)
      ->setDestinationAlphaBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
  desc->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);

  positionAttribDesc->setFormat(MTL::VertexFormatFloat2);
  vertexDesc = MTL::VertexDescriptor::alloc()->init();
  vertexDesc->attributes()->setObject(positionAttribDesc, 0);

  vertexLayout = MTL::VertexBufferLayoutDescriptor::alloc()->init();
  vertexLayout->setStride(sizeof(float2));
  vertexDesc->layouts()->setObject(vertexLayout, 0);

  desc->setVertexDescriptor(vertexDesc);
  vertexLayout->release();
  vertexDesc->release();

  m_gridPassPso = m_device->newRenderPipelineState(desc, &error);
  if (!m_gridPassPso) {
//    std::cerr << error->localizedDescription()->utf8String() << "\n";
    assert(false);
  }

  vertexFunction->release();
  fragmentFunction->release();
  desc->release();

  /*
   * Create the edge pass pipeline state object
   */
  vertexFunction = lib->newFunction("edgePassVertex"_ns);
  fragmentFunction = lib->newFunction("edgePassFragment"_ns);

  desc = MTL::RenderPipelineDescriptor::alloc()->init();
  desc->setVertexFunction(vertexFunction);
  desc->setFragmentFunction(fragmentFunction);
  desc->colorAttachments()->object(0)
      ->setPixelFormat(MTL::PixelFormatRGBA8Unorm_sRGB);

  positionAttribDesc->setFormat(MTL::VertexFormatFloat2);
  vertexDesc = MTL::VertexDescriptor::alloc()->init();
  vertexDesc->attributes()->setObject(positionAttribDesc, 0);

  vertexLayout = MTL::VertexBufferLayoutDescriptor::alloc()->init();
  vertexLayout->setStride(sizeof(float2));
  vertexDesc->layouts()->setObject(vertexLayout, 0);

  desc->setVertexDescriptor(vertexDesc);
  vertexLayout->release();
  vertexDesc->release();

  m_postPassPso = m_device->newRenderPipelineState(desc, &error);
  if (!m_postPassPso) {
//    std::cerr << error->localizedDescription()->utf8String() << "\n";
    assert(false);
  }

  auto samplerDesc = MTL::SamplerDescriptor::alloc()->init();
  samplerDesc->setMagFilter(MTL::SamplerMinMagFilterNearest);
  samplerDesc->setMinFilter(MTL::SamplerMinMagFilterNearest);
  samplerDesc->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  samplerDesc->setTAddressMode(MTL::SamplerAddressModeClampToEdge);

  m_postPassSso = m_device->newSamplerState(samplerDesc);
  samplerDesc->release();

  vertexFunction->release();
  fragmentFunction->release();
  desc->release();

  /*
   * Set up the depth/stencil buffer
   */
  auto depthStencilDesc = MTL::DepthStencilDescriptor::alloc()->init();
  depthStencilDesc->setDepthWriteEnabled(true);
  depthStencilDesc->setDepthCompareFunction(MTL::CompareFunctionLess);
  m_dsso = m_device->newDepthStencilState(depthStencilDesc);

  depthStencilDesc->setDepthWriteEnabled(false);
  m_gridPassDsso = m_device->newDepthStencilState(depthStencilDesc);

  positionAttribDesc->release();
  normalAttribDesc->release();
  depthStencilDesc->release();
  lib->release();
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
