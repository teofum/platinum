#include "renderer_pt.hpp"
#include "utils/utils.hpp"

#include <span>

#include <utils/metal_utils.hpp>

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

  for (auto pipeline: m_pathtracingPipelines) pipeline->release();
  if (m_postprocessPipeline != nullptr) m_postprocessPipeline->release();
  if (m_constantsBuffer != nullptr) m_constantsBuffer->release();
}

void Renderer::render() {
  if (!m_renderTarget) return;

  auto cmd = m_commandQueue->commandBuffer();

  /*
   * Update frame index and write constants buffer
   */
  m_constants.frameIdx = (uint32_t) m_accumulatedFrames;
  m_constantsOffset = (m_frameIdx % m_maxFramesInFlight) * m_constantsStride;
  void* bufferWrite = (char*) m_constantsBuffer->contents() + m_constantsOffset;
  memcpy(bufferWrite, &m_constants, m_constantsSize);

  /*
   * If rendering the scene, run the path tracing kernel to accumulate samples
   */
  if (m_accumulatedFrames < m_accumulationFrames) {
    uint2 size{(uint32_t) m_viewportSize.x, (uint32_t) m_viewportSize.y};
    auto threadsPerThreadgroup = MTL::Size(32, 32, 1);
    auto threadgroups = MTL::Size(
      (size.x + threadsPerThreadgroup.width - 1) / threadsPerThreadgroup.width,
      (size.y + threadsPerThreadgroup.height - 1) / threadsPerThreadgroup.height,
      1
    );

    auto computeEnc = cmd->computeCommandEncoder();

    computeEnc->setBuffer(m_constantsBuffer, m_constantsOffset, 0);
    computeEnc->setBuffer(m_vertexResourcesBuffer, 0, 1);
    computeEnc->setBuffer(m_primitiveResourcesBuffer, 0, 2);
    computeEnc->setBuffer(m_instanceResourcesBuffer, 0, 3);
    computeEnc->setBuffer(m_instanceBuffer, 0, 4);
    computeEnc->setAccelerationStructure(m_instanceAccelStruct, 5);
    computeEnc->setBuffer(m_lightDataBuffer, 0, 6); 

    computeEnc->setTexture(m_accumulator[0], 0);
    computeEnc->setTexture(m_accumulator[1], 1);
    computeEnc->setTexture(m_randomSource, 2);

    for (uint32_t i = 0; i < m_meshAccelStructs->count(); i++) {
      computeEnc->useResource(
        (MTL::AccelerationStructure*) m_meshAccelStructs->object(i),
        MTL::ResourceUsageRead
      );
    }
    
    for (auto meshVertexDataBuffer: m_meshVertexDataBuffers) {
      computeEnc->useResource(meshVertexDataBuffer, MTL::ResourceUsageRead);
    }
    
    for (auto meshMaterialIdxBuffer: m_meshMaterialIndexBuffers) {
      computeEnc->useResource(meshMaterialIdxBuffer, MTL::ResourceUsageRead);
    }
    
    for (auto instanceMaterialBuffer: m_instanceMaterialBuffers) {
      computeEnc->useResource(instanceMaterialBuffer, MTL::ResourceUsageRead);
    }

    computeEnc->setComputePipelineState(m_pathtracingPipelines[m_selectedPipeline]);
    computeEnc->dispatchThreadgroups(threadgroups, threadsPerThreadgroup);
    computeEnc->endEncoding();

    m_accumulatedFrames++;
    
    auto now = std::chrono::high_resolution_clock::now();
    auto time = now - m_renderStart;
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(time);
    m_timer = millis.count();
  }
  
  if (m_accumulatedFrames <= m_accumulationFrames) {
    std::swap(m_accumulator[0], m_accumulator[1]);
  }

  /*
   * Always run the post processing pass — this lets us change post process
   * settings without rendering again
   */
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

void Renderer::startRender(Scene::NodeID cameraNodeId, float2 viewportSize, uint32_t sampleCount) {
  if (!equal(viewportSize, m_viewportSize)) {
    m_viewportSize = viewportSize;
    m_aspect = m_viewportSize.x / m_viewportSize.y;
  }

  rebuildLightData();
  rebuildRenderTargets();
  rebuildResourceBuffers();
  rebuildAccelerationStructures();
  updateConstants(cameraNodeId);

  m_accumulatedFrames = 0;
  m_accumulationFrames = sampleCount;
  m_timer = 0;
  m_renderStart = std::chrono::high_resolution_clock::now();
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
  desc->setPrimitiveDataStride(sizeof(shaders_pt::PrimitiveData));
  desc->setPrimitiveDataElementSize(sizeof(shaders_pt::PrimitiveData));

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
   * Load the PT kernel functions and build the compute pipelines
   */
  auto stride = static_cast<uint32_t>(m_resourcesStride);

  for (const auto& kernelName: m_pathtracingPipelineFunctions) {
    auto desc = metal_utils::makeComputePipelineDescriptor(
     {
       .function = metal_utils::getFunction(
            lib, kernelName.c_str(), {
            .constants = {{.value = &stride, .type = MTL::DataTypeUInt}}
          }
        ),
       .threadGroupSizeIsMultipleOfExecutionWidth = true,
     }
   );
    
    auto pipeline = m_device->newComputePipelineState(
      desc,
      MTL::PipelineOptionNone,
      nullptr,
      &error
    );
    if (!pipeline) {
      std::println(
       	"renderer_pt: Failed to create pathtracing pipeline {}: {}\n",
        kernelName,
       	error->localizedDescription()->utf8String()
     	);
      assert(false);
    }
    
    m_pathtracingPipelines.push_back(pipeline);
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
  m_constantsSize = sizeof(shaders_pt::Constants);
  m_constantsStride = utils::align(m_constantsSize, 256);
  m_constantsOffset = 0;

  m_constantsBuffer = m_device->newBuffer(
    m_constantsStride * m_maxFramesInFlight,
    MTL::ResourceStorageModeShared
  );
}

void Renderer::rebuildResourceBuffers() {
  // Clear old buffers if present
  if (m_vertexResourcesBuffer != nullptr) m_vertexResourcesBuffer->release();
  m_meshVertexDataBuffers.clear();
  
  if (m_primitiveResourcesBuffer != nullptr) m_primitiveResourcesBuffer->release();
  m_meshMaterialIndexBuffers.clear();
  
  if (m_instanceResourcesBuffer != nullptr) m_instanceResourcesBuffer->release();
  for (MTL::Buffer* buffer: m_instanceMaterialBuffers) buffer->release();
  m_instanceMaterialBuffers.clear();

  /*
   * Create vertex resources buffer, pointing to each mesh's vertex data buffer
   *    and primitive resources buffer, pointing to each mesh's material slot index buffer
   */
  auto meshes = m_store.scene().getAllMeshes();
  
  m_vertexResourcesBuffer = m_device->newBuffer(
    m_resourcesStride * 2 * meshes.size(),
    MTL::ResourceStorageModeShared
  );
  m_primitiveResourcesBuffer = m_device->newBuffer(
    m_resourcesStride * meshes.size(),
    MTL::ResourceStorageModeShared
  );

  size_t idx = 0;
  m_meshVertexDataBuffers.reserve(meshes.size());
  for (const auto& md: meshes) {
    auto vertexResourceHandle = (uint64_t*) m_vertexResourcesBuffer->contents() + idx * 2;
    vertexResourceHandle[0] = md.mesh->vertexPositions()->gpuAddress();
    vertexResourceHandle[1] = md.mesh->vertexData()->gpuAddress();
    
    auto primResourceHandle = (uint64_t*) m_primitiveResourcesBuffer->contents() + idx;
    *primResourceHandle = md.mesh->materialIndices()->gpuAddress();
    
    m_meshVertexDataBuffers.push_back(md.mesh->vertexData());
    m_meshMaterialIndexBuffers.push_back(md.mesh->materialIndices());
    
    idx++;
  }
  
  /*
   * Create instance resources buffer, pointing to each *instance's* materials buffer
   * Also create the materials buffers
   * This duplicates materials across instances, but it's a very small struct, this is ok
   */
  auto instances = m_store.scene().getAllInstances(Scene::NodeFlags_Visible);
  
  m_instanceResourcesBuffer = m_device->newBuffer(
    m_resourcesStride * instances.size(),
    MTL::ResourceStorageModeShared
  );
  
  idx = 0;
  m_instanceMaterialBuffers.reserve(instances.size());
  for (const auto& instance: instances) {
    // Create and fill the materials buffer
    auto materialsBuffer = m_device->newBuffer(
		  instance.materials.size() * sizeof(Material),
		  MTL::ResourceStorageModeShared
		);
    
    size_t materialIdx = 0;
    for (auto mid: instance.materials) {
      auto materialHandle = (Material*) materialsBuffer->contents() + materialIdx++;
      *materialHandle = *m_store.scene().material(mid);
    }
    
    // Add the material buffer addresses to the instance resources buffer
    auto instanceResourceHandle = (uint64_t*) m_instanceResourcesBuffer->contents() + idx++;
    *instanceResourceHandle = materialsBuffer->gpuAddress();
    
    m_instanceMaterialBuffers.push_back(materialsBuffer);
  }
}

void Renderer::rebuildAccelerationStructures() {
  // Clear old acceleration structures, if any
  if (m_meshAccelStructs != nullptr) {
    for (uint32_t i = 0; i < m_meshAccelStructs->count(); i++)
      m_meshAccelStructs->object(i)->release();
    m_meshAccelStructs->release();
  }
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
  )->retain();

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
    auto meshIdx = std::find_if(
      meshIds.begin(), meshIds.end(), [&](Scene::MeshID id) {
        return id == instance.meshId;
      }
    ) - meshIds.begin();

    id.accelerationStructureIndex = (uint32_t) meshIdx;
    id.intersectionFunctionTableOffset = 0;
    id.mask = 1;

    for (int32_t j = 0; j < 4; j++) {
      for (int32_t i = 0; i < 3; i++) {
        id.transformationMatrix.columns[j][i] = instance.transform.columns[j][i];
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

  if (m_randomSource != nullptr) m_randomSource->release();

  auto texd = MTL::TextureDescriptor::alloc()->init();
  texd->setTextureType(MTL::TextureType2D);
  texd->setWidth(static_cast<uint32_t>(m_viewportSize.x));
  texd->setHeight(static_cast<uint32_t>(m_viewportSize.y));
  texd->setStorageMode(MTL::StorageModeShared);

  texd->setUsage(MTL::TextureUsageShaderWrite | MTL::TextureUsageShaderRead);
  texd->setPixelFormat(MTL::PixelFormatRGBA32Float);
  m_accumulator[0] = m_device->newTexture(texd);
  m_accumulator[1] = m_device->newTexture(texd);

  texd->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  texd->setPixelFormat(MTL::PixelFormatRGBA16Float);
  m_renderTarget = m_device->newTexture(texd);

  // Temporary crap way of getting randomness into the shader
  // TODO: get a better source of scrambling (fast owen?)
  texd->setUsage(MTL::TextureUsageShaderRead);
  texd->setPixelFormat(MTL::PixelFormatR32Uint);
  m_randomSource = m_device->newTexture(texd);

  auto k = (size_t) m_viewportSize.x * (size_t) m_viewportSize.y;
  std::vector<uint32_t> random(k);
  for (size_t i = 0; i < k; i++) random[i] = rand() % (1024 * 1024);
  m_randomSource->replaceRegion(
    MTL::Region::Make2D(0, 0, (size_t) m_viewportSize.x, (size_t) m_viewportSize.y),
    0,
    random.data(),
    sizeof(uint32_t) * (size_t) m_viewportSize.x
  );

  texd->release();
}

void Renderer::rebuildLightData() {
  if (m_lightDataBuffer != nullptr) m_lightDataBuffer->release();
  
  /*
   * Iterate all instances, finding the ones with emissive materials.
   * For each instance with emissive materials, iterate its primitives. Any primitives that
   * use an emissive material get added as lights.
   */
  std::vector<shaders_pt::LightData> lights;
  ankerl::unordered_dense::set<Scene::MaterialID> instanceEmissiveMaterials;
  uint32_t instanceIdx = 0;
  m_lightTotalPower = 0.0f;
  for (const auto& instance: m_store.scene().getAllInstances(Scene::NodeFlags_Visible)) {
    instanceEmissiveMaterials.clear();
    for (auto mid: instance.materials) {
      const auto material = m_store.scene().material(mid);
      if (material->flags & Material::Material_Emissive) {
        instanceEmissiveMaterials.insert(mid);
      }
    }
    
    if (!instanceEmissiveMaterials.empty()) {
      auto materialIndices = (uint32_t*) instance.mesh->materialIndices()->contents();
      auto indices = (uint32_t*) instance.mesh->indices()->contents();
      auto vertices = (float3*) instance.mesh->vertexPositions()->contents();
      
      auto triangleCount = instance.mesh->indexCount() / 3;
      for (int i = 0; i < triangleCount; i++) {
        auto materialId = instance.materials[materialIndices[i]];
        if (instanceEmissiveMaterials.contains(materialId)) {
          const auto material = m_store.scene().material(materialId);
          
          // Transform the primitive vertices: this ensures the right area is calculated if the
          // instance is scaled
          const auto v0 = (instance.transform * make_float4(vertices[indices[i * 3 + 0]], 1.0f)).xyz;
          const auto v1 = (instance.transform * make_float4(vertices[indices[i * 3 + 1]], 1.0f)).xyz;
          const auto v2 = (instance.transform * make_float4(vertices[indices[i * 3 + 2]], 1.0f)).xyz;
          
          const auto edge1 = v1 - v0;
          const auto edge2 = v2 - v0;
          const auto area = length(cross(edge1, edge2)) * 0.5f;
          
          const auto lightPower = length(material->emission) * area * std::numbers::pi_v<float>;
          m_lightTotalPower += lightPower;
          
          lights.push_back({
            .instanceIdx = instanceIdx,
            .indices = { indices[i * 3 + 0], indices[i * 3 + 1], indices[i * 3 + 2] },
            .area = area,
            .power = lightPower,
            .cumulativePower = m_lightTotalPower,
            .emission = material->emission * material->emissionStrength,
          });
        }
      }
    }
    instanceIdx++;
  }
  
  m_lightCount = (uint32_t) lights.size();
  
  /*
   * Create and fill the lights buffer
   */
  m_lightDataBuffer = m_device->newBuffer(
    sizeof(shaders_pt::LightData) * lights.size(),
    MTL::ResourceStorageModeShared
  );
  memcpy(m_lightDataBuffer->contents(), lights.data(), sizeof(shaders_pt::LightData) * lights.size());
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
  auto w = transform.columns[2].xyz;
  auto pos = transform.columns[3].xyz;

  auto vu = u * vw;
  auto vv = -v * vh;

  m_constants = {
    .frameIdx = 0,
    .size = {(uint32_t) m_viewportSize.x, (uint32_t) m_viewportSize.y},
    .camera = {
      .position = pos,
      .topLeft = pos - w - (vu + vv) * 0.5f,
      .pixelDeltaU = vu / m_viewportSize.x,
      .pixelDeltaV = vv / m_viewportSize.y,
    },
    .lightCount = m_lightCount,
    .totalLightPower = m_lightTotalPower,
  };
}

bool Renderer::isRendering() const {
  return m_renderTarget != nullptr && m_accumulatedFrames < m_accumulationFrames;
}

std::pair<size_t, size_t> Renderer::renderProgress() const {
  return {m_accumulatedFrames, m_accumulationFrames};
}

size_t Renderer::renderTime() const {
  return m_timer;
}

}
