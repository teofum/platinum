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
  : m_store(store), m_device(device), m_commandQueue(commandQueue),
    m_camera(float3{-3, 3, 3}) {
  m_rpd = MTL::RenderPassDescriptor::alloc()->init();
  updateClearColor();

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

  buildShaders();
}

Renderer::~Renderer() {
  m_rpd->release();
}

void Renderer::render(
  MTL::Texture* renderTarget,
  MTL::Texture* geometryTarget
) noexcept {
  NS::AutoreleasePool* autoreleasePool = NS::AutoreleasePool::alloc()->init();

  m_aspect = static_cast<float>(renderTarget->width()) /
             static_cast<float>(renderTarget->height());

  buildBuffers();
  updateConstants();

  auto cmd = m_commandQueue->commandBuffer();

  auto colorAttachment = m_rpd->colorAttachments()->object(0);
  colorAttachment->setTexture(renderTarget);
  colorAttachment->setLoadAction(MTL::LoadActionClear);
  colorAttachment->setStoreAction(MTL::StoreActionStore);

  auto geomAttachment = m_rpd->colorAttachments()->object(1);
  geomAttachment->setClearColor(MTL::ClearColor::Make(0.0f, 0.0f, 0.0f, 1.0f));
  geomAttachment->setTexture(geometryTarget);
  geomAttachment->setLoadAction(MTL::LoadActionClear);
  geomAttachment->setStoreAction(MTL::StoreActionStore);

  auto enc = cmd->renderCommandEncoder(m_rpd);

  enc->setDepthStencilState(m_dsso);
  enc->setFrontFacingWinding(MTL::WindingCounterClockwise);
  enc->setCullMode(MTL::CullModeBack);

  enc->setViewport(
    {
      0.0, 0.0, (double) renderTarget->width(), (double) renderTarget->height(),
      0.0, 1.0
    }
  );
  enc->setRenderPipelineState(m_pso);
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
  cmd->commit();

  m_frameIdx++;

  autoreleasePool->release();
}

float* Renderer::clearColor() {
  return m_clearColor;
}

void Renderer::updateClearColor() {
  auto colorAttachment = m_rpd->colorAttachments()->object(0);
  colorAttachment->setClearColor(
    MTL::ClearColor::Make(
      m_clearColor[0] * m_clearColor[3],
      m_clearColor[1] * m_clearColor[3],
      m_clearColor[2] * m_clearColor[3],
      m_clearColor[3]
    )
  );
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

  m_vertexPosBuffer
    ->didModifyRange(NS::Range::Make(0, m_vertexPosBuffer->length()));
  m_indexBuffer->didModifyRange(NS::Range::Make(0, m_indexBuffer->length()));
  m_dataBuffer
    ->didModifyRange(NS::Range::Make(0, m_dataBuffer->length()));
}

void Renderer::buildShaders() {
  /*
   * Load the shader library, then load the shader functions
   */
  NS::Error* error = nullptr;
  MTL::Library* lib = m_device
    ->newLibrary("renderer_studio.metallib"_ns, &error);
  if (!lib) {
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
      ->setPixelFormat(MTL::PixelFormatRGBA8Unorm_sRGB);
  desc->colorAttachments()->object(1)
      ->setPixelFormat(MTL::PixelFormatR16Uint);

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
   * Get the pipeline state object
   */
  m_pso = m_device->newRenderPipelineState(desc, &error);
  if (!m_pso) {
//    std::cerr << error->localizedDescription()->utf8String() << "\n";
    assert(false);
  }

  /*
   * Set up the depth/stencil buffer
   */
  auto depthStencilDesc = MTL::DepthStencilDescriptor::alloc()->init();
  depthStencilDesc->setDepthWriteEnabled(true);
  depthStencilDesc->setDepthCompareFunction(MTL::CompareFunctionLess);
  m_dsso = m_device->newDepthStencilState(depthStencilDesc);

  depthStencilDesc->release();
  vertexFunction->release();
  fragmentFunction->release();
  desc->release();
  lib->release();
}

void Renderer::updateConstants() {
  Constants constants = {
    m_camera.projection(m_aspect),
  };

  m_constantsOffset = (m_frameIdx % m_maxFramesInFlight) * m_constantsStride;
  void* bufferWrite = (char*) m_constantsBuffer->contents() + m_constantsOffset;
  memcpy(bufferWrite, &constants, m_constantsSize);
}

void Renderer::handleScrollEvent(const float2& delta) {
  m_camera.orbit(-delta);
}

void Renderer::handleZoomEvent(float delta) {
  m_camera.moveTowardTarget(delta);
}

}
