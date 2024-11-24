#include "renderer_studio.hpp"

#include <numbers>
#include <print>
#include <utils/metal_utils.hpp>
#include <utils/matrices.hpp>

namespace pt::renderer_studio {
using metal_utils::operator ""_ns;

Renderer::Renderer(
  MTL::Device* device,
  MTL::CommandQueue* commandQueue
) noexcept
  : m_device(device), m_commandQueue(commandQueue) {
  m_rpd = MTL::RenderPassDescriptor::alloc()->init();
  updateClearColor();

  buildBuffers();
  buildShaders();
}

Renderer::~Renderer() {
  m_rpd->release();
}

void Renderer::render(MTL::Texture* renderTarget) noexcept {
  NS::AutoreleasePool* autoreleasePool = NS::AutoreleasePool::alloc()->init();

  m_aspect = static_cast<float>(renderTarget->width()) /
             static_cast<float>(renderTarget->height());

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
    {0.0, 0.0, (double) renderTarget->width(), (double) renderTarget->height(),
     0.0, 1.0}
  );
  enc->setRenderPipelineState(m_pso);
  enc->setVertexBuffer(m_vertexBuffer, 0, 0);
  enc->setVertexBuffer(m_constantsBuffer, m_constantsOffset, 1);

  enc->drawIndexedPrimitives(
    MTL::PrimitiveTypeTriangle,
    m_indexCount,
    MTL::IndexTypeUInt32,
    m_indexBuffer,
    0
  );

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
   * Build the vertex buffer
   */
  size_t vertexBufferSize = m_vertexCount * sizeof(Vertex);
  m_vertexBuffer = m_device
    ->newBuffer(vertexBufferSize, MTL::ResourceStorageModeManaged);

  memcpy(m_vertexBuffer->contents(), m_vertexData, vertexBufferSize);
  m_vertexBuffer->didModifyRange(NS::Range::Make(0, m_vertexBuffer->length()));

  /*
   * Build the index buffer
   */
  size_t indexBufferSize = m_indexCount * sizeof(unsigned);
  m_indexBuffer = m_device
    ->newBuffer(indexBufferSize, MTL::ResourceStorageModeShared);

  memcpy(m_indexBuffer->contents(), m_indexData, indexBufferSize);
  m_indexBuffer->didModifyRange(NS::Range::Make(0, m_indexBuffer->length()));

  /*
   * Build the constants buffer
   */
  m_constantsSize = sizeof(Transforms);
  m_constantsStride = ((m_constantsSize - 1) / 256 + 1) * 256;
  m_constantsOffset = 0;

  m_constantsBuffer = m_device->newBuffer(
    m_constantsStride * m_maxFramesInFlight,
    MTL::ResourceStorageModeShared
  );
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

  auto colorAttribDesc = MTL::VertexAttributeDescriptor::alloc()->init();
  colorAttribDesc->setFormat(MTL::VertexFormatFloat4);
  colorAttribDesc->setOffset(offsetof(Vertex, color));
  colorAttribDesc->setBufferIndex(0);

  vertexDesc->attributes()->setObject(positionAttribDesc, 0);
  vertexDesc->attributes()->setObject(colorAttribDesc, 1);

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
  int64_t time = getElapsedMillis();
  auto angle = static_cast<float>(std::fmod(
    static_cast<double>(time) * 0.0005,
    2.0f * std::numbers::pi_v<double>
  ));

  Transforms transforms;
  transforms.model = mat::rotation(angle, float3{0.5, 1.0, 0.0});
  transforms.view = mat::translation(-m_cameraPos);
  transforms.projection = mat::projection(m_fov, m_aspect, 0.1f, 100.0f);

  m_constantsOffset = (m_frameIdx % m_maxFramesInFlight) * m_constantsStride;
  void* bufferWrite = (char*) m_constantsBuffer->contents() + m_constantsOffset;
  memcpy(bufferWrite, &transforms, m_constantsSize);
}

int64_t Renderer::getElapsedMillis() {
  auto now = std::chrono::high_resolution_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(
    now - m_startTime
  ).count();
}

}
