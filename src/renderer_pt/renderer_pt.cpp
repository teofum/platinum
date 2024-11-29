#include "renderer_pt.hpp"
#include "utils/utils.hpp"

#include <span>

#include <utils/metal_utils.hpp>
#include "pt_shader_defs.hpp"

namespace pt::renderer_pt {
using metal_utils::ns_shared;
using metal_utils::operator ""_ns;

Renderer::Renderer(
  MTL::Device* device,
  MTL::CommandQueue* commandQueue,
  pt::Store& store
) noexcept: m_store(store), m_device(device), m_commandQueue(commandQueue) {
  buildPipelines();
  buildConstantsBuffer();
}

Renderer::~Renderer() {
  if (m_renderTarget != nullptr) m_renderTarget->release();
  if (m_accumulator[0] != nullptr) m_accumulator[0]->release();
  if (m_accumulator[1] != nullptr) m_accumulator[1]->release();

  if (m_pathtracingPipeline != nullptr) m_pathtracingPipeline->release();
  if (m_postprocessPipeline != nullptr) m_postprocessPipeline->release();
  if (m_constantsBuffer != nullptr) m_constantsBuffer->release();
}

void Renderer::render(Scene::NodeID cameraNodeId, float2 viewportSize) {
  if (!equal(viewportSize, m_viewportSize)) {
    m_viewportSize = viewportSize;
    m_aspect = m_viewportSize.x / m_viewportSize.y;
    rebuildRenderTargets();
  }

  updateConstants(cameraNodeId);
  rebuildResourcesBuffer();
  rebuildAccelerationStructures();

  uint2 size{(uint32_t) m_viewportSize.x, (uint32_t) m_viewportSize.y};
  auto threadsPerThreadgroup = MTL::Size(32, 32, 1);
  auto threadgroups = MTL::Size(
    (size.x + threadsPerThreadgroup.width - 1) / threadsPerThreadgroup.width,
    (size.y + threadsPerThreadgroup.height - 1) / threadsPerThreadgroup.height,
    1
  );

  auto cmd = m_commandQueue->commandBuffer();
  auto computeEnc = cmd->computeCommandEncoder();

  computeEnc->setBuffer(m_constantsBuffer, m_constantsOffset, 0);
  computeEnc->setBuffer(m_resourcesBuffer, 0, 1);
  computeEnc->setBuffer(m_instanceBuffer, 0, 2);

  computeEnc->setAccelerationStructure(m_instanceAccelStruct, 3);

  computeEnc->setTexture(m_accumulator[0], 0);
  computeEnc->setTexture(m_accumulator[1], 1);

  for (uint32_t i = 0; i < m_meshAccelStructs->count(); i++) {
    computeEnc->useResource(
      (MTL::AccelerationStructure*) m_meshAccelStructs->object(i),
      MTL::ResourceUsageRead
    );
  }

  computeEnc->setComputePipelineState(m_pathtracingPipeline);
  computeEnc->dispatchThreadgroups(threadgroups, threadsPerThreadgroup);
  computeEnc->endEncoding();

  std::swap(m_accumulator[0], m_accumulator[1]);

  auto rpd = ns_shared<MTL::RenderPassDescriptor>();

  rpd->colorAttachments()->object(0)->setTexture(m_renderTarget);
  rpd->colorAttachments()->object(0)->setLoadAction(MTL::LoadActionClear);
  rpd->colorAttachments()->object(0)->setClearColor(MTL::ClearColor::Make(0.0f, 0.0f, 0.0f, 1.0f));

  auto postEnc = cmd->renderCommandEncoder(rpd);

  postEnc->setRenderPipelineState(m_postprocessPipeline);
  postEnc->setFragmentTexture(m_accumulator[0], 0);

  postEnc->drawPrimitives(MTL::PrimitiveTypeTriangle, (NS::UInteger) 0, 6);
  postEnc->endEncoding();

  cmd->commit();
}

const MTL::Texture* Renderer::presentRenderTarget() const {
  return m_renderTarget;
}

NS::SharedPtr<MTL::AccelerationStructureGeometryDescriptor> Renderer::makeGeometryDescriptor(
  const Mesh* mesh
) {
  auto desc = ns_shared<MTL::AccelerationStructureTriangleGeometryDescriptor>();

  desc->setIndexBuffer(mesh->indices());
  desc->setIndexType(MTL::IndexTypeUInt32);

  desc->setVertexBuffer(mesh->vertexPositions());
  desc->setVertexStride(sizeof(float3));
  desc->setTriangleCount(mesh->indexCount() / 3);

  // TODO: Do we need this? Not documented
  //   (https://developer.apple.com/documentation/metal/mtlaccelerationstructuretrianglegeometrydescriptor)
  // desc->setVertexFormat(MTL::AttributeFormatFloat3);

  // Also not documented, but it seems to hold per-primitive data for use in an intersector
  desc->setPrimitiveDataBuffer(mesh->indices());
  desc->setPrimitiveDataStride(sizeof(PrimitiveData));
  desc->setPrimitiveDataElementSize(sizeof(PrimitiveData));

  return desc;
}

MTL::AccelerationStructure* Renderer::makeAccelStruct(
  MTL::AccelerationStructureDescriptor* desc
) {
  /*
   * Create buffers and accel structure object
   */
  auto sizes = m_device->accelerationStructureSizes(desc);
  auto accelStruct = m_device->newAccelerationStructure(sizes.accelerationStructureSize);

  MTL::Buffer* scratchBuffer = m_device->newBuffer(
    sizes.buildScratchBufferSize,
    MTL::StorageModeShared
  );

  MTL::Buffer* compactedSizeBuffer = m_device->newBuffer(
    sizeof(uint32_t),
    MTL::ResourceStorageModeShared
  );

  /*
   * Build the acceleration structure
   */
  auto cmd = m_commandQueue->commandBuffer();
  auto enc = cmd->accelerationStructureCommandEncoder();

  enc->buildAccelerationStructure(accelStruct, desc, scratchBuffer, 0);
  enc->writeCompactedAccelerationStructureSize(accelStruct, compactedSizeBuffer, 0);
  enc->endEncoding();

  cmd->commit();
  cmd->waitUntilCompleted();

  /*
   * Compact the acceleration structure
   * This reduces memory usage, but requires GPU/CPU sync
   */
  uint32_t compactedSize = *static_cast<uint32_t*>(compactedSizeBuffer->contents());
  auto compactedAccelStruct = m_device->newAccelerationStructure(compactedSize);

  cmd = m_commandQueue->commandBuffer();
  enc = cmd->accelerationStructureCommandEncoder();

  enc->copyAndCompactAccelerationStructure(accelStruct, compactedAccelStruct);
  enc->endEncoding();

  cmd->commit();

  accelStruct->release();
  scratchBuffer->release();
  compactedSizeBuffer->release();

  return compactedAccelStruct;
}

void Renderer::buildPipelines() {
  /*
   * Load the shader library
   */
  NS::Error* error = nullptr;
  MTL::Library* lib = m_device
    ->newLibrary("renderer_pt.metallib"_ns, &error);
  if (!lib) {
    std::println(
      "renderer_pt: Failed to load shader library: {}\n",
      error->localizedDescription()->utf8String()
    );
    assert(false);
  }

  /*
   * Load the PT kernel function and build the compute pipeline
   */
  auto stride = static_cast<uint32_t>(m_resourcesStride);

  auto desc = metal_utils::makeComputePipelineDescriptor(
    {
      .function = metal_utils::getFunction(
        lib, "pathtracingKernel", {
          .constants = {{.value = &stride, .type = MTL::DataTypeUInt}}
        }
      ),
      .threadGroupSizeIsMultipleOfExecutionWidth = true,
    }
  );

  m_pathtracingPipeline = m_device->newComputePipelineState(
    desc,
    MTL::PipelineOptionNone,
    nullptr,
    &error
  );
  if (!m_pathtracingPipeline) {
    std::println(
      "renderer_pt: Failed to create pathtracing pipeline: {}\n",
      error->localizedDescription()->utf8String()
    );
    assert(false);
  }

  /*
   * Build the post-process pipeline
   */
  auto postDesc = metal_utils::makeRenderPipelineDescriptor(
    {
      .vertexFunction = metal_utils::getFunction(lib, "postprocessVertex"),
      .fragmentFunction = metal_utils::getFunction(lib, "postprocessFragment"),
      .colorAttachments = {MTL::PixelFormatRGBA16Float}
    }
  );

  m_postprocessPipeline = m_device->newRenderPipelineState(postDesc, &error);
  if (!m_postprocessPipeline) {
    std::println(
      "renderer_pt: Failed to create postprocess pipeline: {}\n",
      error->localizedDescription()->utf8String()
    );
    assert(false);
  }
}

void Renderer::buildConstantsBuffer() {
  m_constantsSize = sizeof(Constants);
  m_constantsStride = utils::align(m_constantsSize, 256);
  m_constantsOffset = 0;

  m_constantsBuffer = m_device->newBuffer(
    m_constantsStride * m_maxFramesInFlight,
    MTL::ResourceStorageModeShared
  );
}

void Renderer::rebuildResourcesBuffer() {
  // Clear old buffer if present
  if (m_resourcesBuffer != nullptr) m_resourcesBuffer->release();

  auto meshes = m_store.scene().getAllMeshes();
  m_resourcesBuffer = m_device->newBuffer(
    m_resourcesStride * meshes.size(),
    MTL::ResourceStorageModeShared
  );

  size_t idx = 0;
  for (const auto& md: meshes) {
    auto resourceHandle = (uint64_t*) m_resourcesBuffer->contents() + idx++;
    *resourceHandle = md.mesh->vertexData()->gpuAddress();
  }
}

void Renderer::rebuildAccelerationStructures() {
  // Clear old acceleration structures, if any
//  if (m_meshAccelStructs != nullptr) m_meshAccelStructs->release();
  if (m_instanceAccelStruct != nullptr) m_instanceAccelStruct->release();
  if (m_instanceBuffer != nullptr) m_instanceBuffer->release();

  /*
   * Get mesh data and build mesh acceleration structures (BLAS)
   */
  auto meshes = m_store.scene().getAllMeshes();
  std::vector<Scene::MeshID> meshIds;
  meshIds.reserve(meshes.size());
  std::vector<MTL::AccelerationStructure*> meshAccelStructs;
  meshAccelStructs.reserve(meshes.size());

  size_t idx = 0;
  for (const auto& md: meshes) {
    auto geometryDesc = makeGeometryDescriptor(md.mesh);
    geometryDesc->setIntersectionFunctionTableOffset(idx++);

    auto accelDesc = ns_shared<MTL::PrimitiveAccelerationStructureDescriptor>();
    accelDesc->setGeometryDescriptors(NS::Array::array(geometryDesc));

    meshAccelStructs.push_back(makeAccelStruct(accelDesc));
    meshIds.push_back(md.meshId);
  }

  m_meshAccelStructs = NS::Array::array(
    (NS::Object**) meshAccelStructs.data(),
    meshAccelStructs.size()
  );

  /*
   * Get instance data and build instance acceleration structure (TLAS)
   */
  auto instances = m_store.scene().getAllInstances(Scene::NodeFlags_Visible);
  m_instanceBuffer = m_device->newBuffer(
    sizeof(MTL::AccelerationStructureInstanceDescriptor) * instances.size(),
    MTL::ResourceStorageModeShared
  );
  auto instanceDescriptors = static_cast<MTL::AccelerationStructureInstanceDescriptor*>(m_instanceBuffer->contents());

  idx = 0;
  for (const auto& instance: instances) {
    auto& id = instanceDescriptors[idx++];
    uint32_t meshIdx = std::find_if(
      meshIds.begin(), meshIds.end(), [&](Scene::MeshID id) {
        return id == instance.meshId;
      }
    ) - meshIds.begin();

    id.accelerationStructureIndex = meshIdx;
    id.intersectionFunctionTableOffset = 0;
    id.mask = 1;

    for (int32_t j = 0; j < 4; j++) {
      for (int32_t i = 0; i < 3; i++) {
        id.transformationMatrix[j][i] = instance.transform.columns[j][i];
      }
    }
  }

  auto instanceAccelDesc = ns_shared<MTL::InstanceAccelerationStructureDescriptor>();
  instanceAccelDesc->setInstancedAccelerationStructures(m_meshAccelStructs);
  instanceAccelDesc->setInstanceCount(instances.size());
  instanceAccelDesc->setInstanceDescriptorBuffer(m_instanceBuffer);

  m_instanceAccelStruct = makeAccelStruct(instanceAccelDesc);
}

void Renderer::rebuildRenderTargets() {
  if (m_renderTarget != nullptr) m_renderTarget->release();
  if (m_accumulator[0] != nullptr) m_accumulator[0]->release();
  if (m_accumulator[1] != nullptr) m_accumulator[1]->release();

  auto texd = MTL::TextureDescriptor::alloc()->init();
  texd->setTextureType(MTL::TextureType2D);
  texd->setWidth(static_cast<uint32_t>(m_viewportSize.x));
  texd->setHeight(static_cast<uint32_t>(m_viewportSize.y));
  texd->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  texd->setStorageMode(MTL::StorageModeShared);

  texd->setPixelFormat(MTL::PixelFormatRGBA32Float);
  m_accumulator[0] = m_device->newTexture(texd);
  m_accumulator[1] = m_device->newTexture(texd);

  texd->setPixelFormat(MTL::PixelFormatRGBA16Float);
  m_renderTarget = m_device->newTexture(texd);

  texd->release();
}

void Renderer::updateConstants(Scene::NodeID cameraNodeId) {
  auto node = m_store.scene().node(cameraNodeId);
  auto transform = m_store.scene().worldTransform(cameraNodeId);
  auto camera = m_store.scene().camera(node->cameraId.value());

  // Rescale the camera transform
  const float3 scale = {
    length(transform.columns[0]),
    length(transform.columns[1]),
    length(transform.columns[2]),
  };
  transform = {
    transform.columns[0] / scale.x,
    transform.columns[1] / scale.y,
    transform.columns[2] / scale.z,
    transform.columns[3],
  };

  auto vh = camera->croppedSensorHeight(m_aspect) / camera->focalLength;
  auto vw = vh * m_aspect;

  auto u = transform.columns[0].xyz;
  auto v = transform.columns[1].xyz;
  auto w = -transform.columns[2].xyz;
  auto pos = transform.columns[3].xyz;

  auto vu = u * vw;
  auto vv = -v * vh;

  Constants constants = {
    .size = {(uint32_t) m_viewportSize.x, (uint32_t) m_viewportSize.y},
    .camera = {
      .position = pos,
      .topLeft = pos - w - (vu + vv) * 0.5f,
      .pixelDeltaU = vu / m_viewportSize.x,
      .pixelDeltaV = vv / m_viewportSize.y,
    }
  };

  m_constantsOffset = (m_frameIdx % m_maxFramesInFlight) * m_constantsStride;
  void* bufferWrite = (char*) m_constantsBuffer->contents() + m_constantsOffset;
  memcpy(bufferWrite, &constants, m_constantsSize);
}

}
