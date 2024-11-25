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
  m_constantsSize = sizeof(float4x4);
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

void Renderer::render(MTL::Texture* renderTarget) noexcept {
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
  enc->setVertexBuffer(m_constantsBuffer, m_constantsOffset, 2);

  size_t transformOffset = 0;
  for (const auto& md: m_meshData) {
    enc->setVertexBuffer(m_vertexBuffer, md.vertexOffset, 0);
    enc->setVertexBuffer(m_transformBuffer, transformOffset, 1);
    enc->drawIndexedPrimitives(
      MTL::PrimitiveTypeTriangle,
      md.indexCount,
      MTL::IndexTypeUInt32,
      m_indexBuffer,
      md.indexOffset
    );

    transformOffset += sizeof(float4x4);
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
  if (m_vertexBuffer != nullptr) m_vertexBuffer->release();
  if (m_indexBuffer != nullptr) m_indexBuffer->release();
  if (m_transformBuffer != nullptr) m_transformBuffer->release();

  /*
   * Calculate buffer sizes and create buffers
   */
  auto meshes = m_store.scene().getAllMeshes();
  size_t vertexSize = 0, indexSize = 0, transformCount = meshes.size();

  m_meshData.clear();
  m_meshData.reserve(meshes.size());
  for (const auto& meshAndTransform: meshes) {
    const Mesh& mesh = meshAndTransform.first;

    m_meshData.push_back(
      {
        vertexSize,
        mesh.vertices().size(),
        indexSize,
        mesh.indices().size()
      }
    );

    vertexSize += utils::align(mesh.vertices().size() * sizeof(Vertex), 256);
    indexSize += utils::align(mesh.indices().size() * sizeof(uint32_t), 256);
  }

  size_t vertexBufferSize = vertexSize * sizeof(Vertex);
  m_vertexBuffer = m_device
    ->newBuffer(vertexBufferSize, MTL::ResourceStorageModeManaged);

  size_t indexBufferSize = indexSize * sizeof(uint32_t);
  m_indexBuffer = m_device
    ->newBuffer(indexBufferSize, MTL::ResourceStorageModeShared);

  size_t transformBufferSize = transformCount * sizeof(float4x4);
  m_transformBuffer = m_device
    ->newBuffer(transformBufferSize, MTL::ResourceStorageModeShared);

  /*
   * Fill mesh data and transform buffers
   */
  for (size_t i = 0; i < m_meshData.size(); i++) {
    const Mesh& mesh = meshes[i].first;
    const float4x4& transform = meshes[i].second;
    const MeshData& data = m_meshData[i];

    // Vertices
    void* vbw = (char*) m_vertexBuffer->contents() + data.vertexOffset;
    memcpy(vbw, mesh.vertices().data(), data.vertexCount * sizeof(Vertex));

    // Indices
    void* ibw = (char*) m_indexBuffer->contents() + data.indexOffset;
    memcpy(ibw, mesh.indices().data(), data.indexCount * sizeof(uint32_t));

    // Transform
    void* tbw = (char*) m_transformBuffer->contents() + i * sizeof(float4x4);
    memcpy(tbw, &transform, sizeof(float4x4));
  }

  m_vertexBuffer->didModifyRange(NS::Range::Make(0, m_vertexBuffer->length()));
  m_indexBuffer->didModifyRange(NS::Range::Make(0, m_indexBuffer->length()));
  m_transformBuffer
    ->didModifyRange(NS::Range::Make(0, m_transformBuffer->length()));
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

  /*
   * Set up a vertex attribute descriptor, this tells Metal where each attribute
   * is located
   * TODO: this can be encapsulated in a less verbose API
   */
  auto* vertexDesc = MTL::VertexDescriptor::alloc()->init();

  auto positionAttribDesc = MTL::VertexAttributeDescriptor::alloc()->init();
  positionAttribDesc->setFormat(MTL::VertexFormatFloat3);
  positionAttribDesc->setOffset(offsetof(Vertex, position));
  positionAttribDesc->setBufferIndex(0);

  vertexDesc->attributes()->setObject(positionAttribDesc, 0);

  auto vertexLayout = MTL::VertexBufferLayoutDescriptor::alloc()->init();
  vertexLayout->setStride(sizeof(Vertex));
  vertexDesc->layouts()->setObject(vertexLayout, 0);

  desc->setVertexDescriptor(vertexDesc);

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
  auto viewProjection = m_camera.projection(m_aspect) * m_camera.view();

  m_constantsOffset = (m_frameIdx % m_maxFramesInFlight) * m_constantsStride;
  void* bufferWrite = (char*) m_constantsBuffer->contents() + m_constantsOffset;
  memcpy(bufferWrite, &viewProjection, m_constantsSize);
}

void Renderer::handleScrollEvent(const float2& delta) {
  m_camera.orbit(-delta);
}

void Renderer::handleZoomEvent(float delta) {
  m_camera.moveTowardTarget(delta);
}

}
