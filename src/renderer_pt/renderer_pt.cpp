#include "renderer_pt.hpp"

#include <span>
#include <filesystem>
#include <OpenImageIO/imageio.h>

#include <utils/metal_utils.hpp>
#include <utils/utils.hpp>

namespace fs = std::filesystem;

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
  loadGgxLutTextures();
}

Renderer::~Renderer() {
  if (m_renderTarget != nullptr) m_renderTarget->release();
  if (m_accumulator != nullptr) m_accumulator->release();
  if (m_postProcessBuffer[0] != nullptr) m_postProcessBuffer[0]->release();
  if (m_postProcessBuffer[1] != nullptr) m_postProcessBuffer[1]->release();

  for (auto pipeline: m_pathtracingPipelines) pipeline->release();
  for (auto ift: m_intersectionFunctionTables) ift->release();
  if (m_constantsBuffer != nullptr) m_constantsBuffer->release();

  for (auto lut: m_luts) lut->release();
}

std::vector<postprocess::PostProcessPass::Options> Renderer::postProcessOptions() {
  std::vector<postprocess::PostProcessPass::Options> options;
  options.reserve(m_postProcessPasses.size());

  for (const auto& pass: m_postProcessPasses) {
    options.push_back(pass->options());
  }

  return options;
}

void Renderer::render() {
  if (m_startRender) {
    rebuildResourceBuffers();
    rebuildLightData();
    rebuildRenderTargets();
    rebuildAccelerationStructures();
    updateConstants(m_cameraNodeId, m_flags);
    rebuildArgumentBuffer();

    m_timer = 0;
    m_renderStart = std::chrono::high_resolution_clock::now();
    m_startRender = false;
  }
  
  if (!m_renderTarget) return;

  auto cmd = m_commandQueue->commandBuffer();

  /*
   * Update frame index
   */
  auto arguments = static_cast<shaders_pt::Arguments*>(m_argumentBuffer->contents());
  arguments->constants.frameIdx = (uint32_t) m_accumulatedFrames;

  /*
   * If rendering the scene, run the path tracing kernel to accumulate samples
   */
  if (m_accumulatedFrames < m_accumulationFrames) {
    uint2 size{(uint32_t) m_currentRenderSize.x, (uint32_t) m_currentRenderSize.y};
    auto threadsPerThreadgroup = MTL::Size(8, 8, 1);
    auto threadgroups = MTL::Size(
      (size.x + threadsPerThreadgroup.width - 1) / threadsPerThreadgroup.width,
      (size.y + threadsPerThreadgroup.height - 1) / threadsPerThreadgroup.height,
      1
    );

    auto computeEnc = cmd->computeCommandEncoder();

    computeEnc->setBuffer(m_argumentBuffer, 0, 0);
    computeEnc->setTexture(m_accumulator, 0);
    computeEnc->setTexture(m_randomSource, 1);

    /*
     * Use resources
     * TODO: clean this up using residency sets
     */
    computeEnc->useResource(m_vertexResourcesBuffer, MTL::ResourceUsageRead);
    computeEnc->useResource(m_primitiveResourcesBuffer, MTL::ResourceUsageRead);
    computeEnc->useResource(m_instanceResourcesBuffer, MTL::ResourceUsageRead);
    computeEnc->useResource(m_instanceBuffer, MTL::ResourceUsageRead);
    computeEnc->useResource(m_instanceAccelStruct, MTL::ResourceUsageRead);
    computeEnc->useResource(m_intersectionFunctionTables[m_selectedPipeline], MTL::ResourceUsageRead);
    computeEnc->useResource(m_lightDataBuffer, MTL::ResourceUsageRead);
    computeEnc->useResource(m_envLightDataBuffer, MTL::ResourceUsageRead);
    computeEnc->useResource(m_texturesBuffer, MTL::ResourceUsageRead);

    for (const auto& m_lut: m_luts) {
      computeEnc->useResource(m_lut, MTL::ResourceUsageRead);
    }

    for (uint32_t i = 0; i < m_meshAccelStructs->count(); i++) {
      computeEnc->useResource(
        (MTL::AccelerationStructure*) m_meshAccelStructs->object(i),
        MTL::ResourceUsageRead
      );
    }

    for (auto meshVertexPositionBuffer: m_meshVertexPositionBuffers) {
      computeEnc->useResource(meshVertexPositionBuffer, MTL::ResourceUsageRead);
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

    for (const auto& texture: m_store.scene().getAll<Texture>()) {
      computeEnc->useResource(texture.asset->texture(), MTL::ResourceUsageRead);
    }

    for (const auto& aliasTable: m_envLightAliasTables) {
      computeEnc->useResource(aliasTable, MTL::ResourceUsageRead);
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

  /*
   * Post processing pipeline
   */
  for (size_t i = 0; i < m_postProcessPasses.size(); i++) {
    auto& pass = m_postProcessPasses[i];

    pass->apply(i == 0 ? m_accumulator : m_postProcessBuffer[0], m_postProcessBuffer[1], cmd);
    std::swap(m_postProcessBuffer[0], m_postProcessBuffer[1]);
  }

  m_tonemapPass->apply(m_postProcessBuffer[0], m_renderTarget, cmd);

  cmd->commit();
}

void Renderer::startRender(
  Scene::NodeID cameraNodeId,
  float2 viewportSize,
  uint32_t sampleCount,
  int flags
) {
  if (!equal(viewportSize, m_currentRenderSize)) {
    m_currentRenderSize = viewportSize;
    m_aspect = m_currentRenderSize.x / m_currentRenderSize.y;
  }

  m_accumulatedFrames = 0;
  m_accumulationFrames = sampleCount;

  m_cameraNodeId = cameraNodeId;
  m_flags = flags;
  m_startRender = true;
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

  /*
   * Set per-primitive data buffer
   */
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
  MTL::Library* lib = m_device->newLibrary("renderer_pt.metallib"_ns, &error);
  if (!lib) {
    std::println(
      stderr,
      "renderer_pt: Failed to load shader library: {}",
      error->localizedDescription()->utf8String()
    );
    assert(false);
  }

  /*
   * Load the PT kernel functions and build the compute pipelines
   */
  auto alphaTestIntersectionFunction = metal_utils::getFunction(lib, "alphaTestIntersectionFunction");

  for (const auto& kernelName: m_pathtracingPipelineFunctions) {
    auto desc = metal_utils::makeComputePipelineDescriptor(
      {
        .function = metal_utils::getFunction(lib, kernelName.c_str()),
        .linkedFunctions = {alphaTestIntersectionFunction},
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
        stderr,
        "renderer_pt: Failed to create pathtracing pipeline {}: {}",
        kernelName,
        error->localizedDescription()->utf8String()
      );
      assert(false);
    }

    auto iftDesc = MTL::IntersectionFunctionTableDescriptor::alloc()->init();
    iftDesc->setFunctionCount(1);
    auto intersectionFunctionTable = pipeline->newIntersectionFunctionTable(iftDesc);
    iftDesc->release();

    intersectionFunctionTable->setFunction(pipeline->functionHandle(alphaTestIntersectionFunction), 0);

    m_pathtracingPipelines.push_back(pipeline);
    m_intersectionFunctionTables.push_back(intersectionFunctionTable);
  }

  /*
   * Build the post-process pipeline
   */
  m_postProcessPasses.push_back(std::make_unique<postprocess::Exposure>(m_device, lib));
  m_postProcessPasses.push_back(std::make_unique<postprocess::ToneCurve>(m_device, lib));
  m_postProcessPasses.push_back(std::make_unique<postprocess::Vignette>(m_device, lib));
  m_tonemapPass = std::make_unique<postprocess::Tonemap>(m_device, lib);
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

void Renderer::loadGgxLutTextures() {
  m_luts.reserve(m_lutInfo.size());
  for (auto lut: m_lutInfo) {
    /*
     * Load the LUT image. For 3d LUTs, load the first slice.
     */
    auto filename = std::format("resource/lut/{}{}.exr", lut.filename, lut.depth > 1 ? "_0" : "");
    auto path = fs::current_path() / filename;

    auto in = OIIO::ImageInput::open(path.string());
    if (!in) {
      std::println(stderr, "renderer_pt: Failed to open file {}", path.string());
      assert(false);
    }
    const auto& spec = in->spec();

    // Create temp buffer for reading the image to
    auto buffer = m_device->newBuffer(
      sizeof(float) * spec.width * spec.height,
      MTL::ResourceStorageModeShared
    );
    in->read_image(0, 0, 0, -1, spec.format, buffer->contents());

    // Create the texture
    auto texd = metal_utils::makeTextureDescriptor(
      {
        .width = (uint32_t) spec.width,
        .height = (uint32_t) spec.height,
        .depth = (uint32_t) lut.depth,
        .type = lut.type,
        .format = MTL::PixelFormatR32Float,
      }
    );
    auto texture = m_device->newTexture(texd);

    // Load the first slice
    auto region = MTL::Region(0, 0, 0, spec.width, spec.height, 1);
    texture->replaceRegion(region, 0, buffer->contents(), sizeof(float) * spec.width);

    /*
     * For 3d LUTs, load each subsequent slice and copy it to the texture
     */
    for (uint32_t zSlice = 1; zSlice < lut.depth; zSlice++) {
      filename = std::format("resource/lut/{}_{}.exr", lut.filename, zSlice);
      path = fs::current_path() / filename;

      in = OIIO::ImageInput::open(path.string());
      if (!in) {
        std::println(stderr, "renderer_pt: Failed to open file {}", path.string());
        assert(false);
      }
      const auto& sliceSpec = in->spec();
      in->read_image(0, 0, 0, -1, sliceSpec.format, buffer->contents());

      auto sliceRegion = MTL::Region(0, 0, zSlice, sliceSpec.width, sliceSpec.height, 1);
      texture->replaceRegion(sliceRegion, 0, buffer->contents(), sizeof(float) * sliceSpec.width);
    }

    m_luts.push_back(texture);
    m_lutSizes.push_back(spec.width);
    buffer->release();
  }
}

void Renderer::rebuildResourceBuffers() {
  // Clear old buffers if present
  if (m_vertexResourcesBuffer != nullptr) m_vertexResourcesBuffer->release();
  m_meshVertexPositionBuffers.clear();
  m_meshVertexDataBuffers.clear();

  if (m_primitiveResourcesBuffer != nullptr) m_primitiveResourcesBuffer->release();
  m_meshMaterialIndexBuffers.clear();

  if (m_instanceResourcesBuffer != nullptr) m_instanceResourcesBuffer->release();
  for (MTL::Buffer* buffer: m_instanceMaterialBuffers) buffer->release();
  m_instanceMaterialBuffers.clear();

  if (m_texturesBuffer != nullptr) m_texturesBuffer->release();

  /*
   * Create vertex resources buffer, pointing to each mesh's vertex data buffer
   * and primitive resources buffer, pointing to each mesh's material slot index buffer
   */
  auto meshes = m_store.scene().getAll<Mesh>();

  m_vertexResourcesBuffer = m_device->newBuffer(
    m_resourcesStride * 2 * meshes.size(),
    MTL::ResourceStorageModeShared
  );
  m_primitiveResourcesBuffer = m_device->newBuffer(
    m_resourcesStride * meshes.size(),
    MTL::ResourceStorageModeShared
  );

  size_t idx = 0;
  m_meshVertexPositionBuffers.reserve(meshes.size());
  m_meshVertexDataBuffers.reserve(meshes.size());
  m_meshMaterialIndexBuffers.reserve(meshes.size());
  for (const auto& mesh: meshes) {
    auto vertexResourceHandle = (uint64_t*) m_vertexResourcesBuffer->contents() + idx * 2;
    vertexResourceHandle[0] = mesh.asset->vertexPositions()->gpuAddress();
    vertexResourceHandle[1] = mesh.asset->vertexData()->gpuAddress();

    auto primResourceHandle = (uint64_t*) m_primitiveResourcesBuffer->contents() + idx;
    *primResourceHandle = mesh.asset->materialIndices()->gpuAddress();

    m_meshVertexPositionBuffers.push_back(mesh.asset->vertexPositions());
    m_meshVertexDataBuffers.push_back(mesh.asset->vertexData());
    m_meshMaterialIndexBuffers.push_back(mesh.asset->materialIndices());

    idx++;
  }

  /*
   * Create texture resource buffer, pointing to each scene texture.
   * Right now we include all textures, even unused ones.
   * TODO: figure out if there's even any perf benefit to including only textures that are used.
   */
  auto textures = m_store.scene().getAll<Texture>();
  std::vector<MTL::ResourceID> texturePointers;

  m_textureIndices.clear();
  for (const auto& texture: textures) {
    m_textureIndices[texture.id] = texturePointers.size();
    texturePointers.push_back(texture.asset->texture()->gpuResourceID());
  }

  m_texturesBuffer = m_device->newBuffer(
    sizeof(MTL::ResourceID) * texturePointers.size(),
    MTL::ResourceStorageModeShared
  );
  memcpy(m_texturesBuffer->contents(), texturePointers.data(), sizeof(MTL::ResourceID) * texturePointers.size());

  /*
   * Create instance resources buffer, pointing to each *instance's* materials buffer
   * Also create the materials buffers
   * This duplicates materials across instances, but it's a very small struct, this is ok
   */
  auto instances = m_store.scene().getInstances();

  m_instanceResourcesBuffer = m_device->newBuffer(
    m_resourcesStride * instances.size(),
    MTL::ResourceStorageModeShared
  );

  idx = 0;
  m_instanceMaterialBuffers.reserve(instances.size());
  for (const auto& instance: instances) {
    // Create and fill the materials buffer
    const auto& materialIds = *instance.node.materialIds().value(); // We know the node has a mesh so there is a value
    auto materialsBuffer = m_device->newBuffer(
      materialIds.size() * sizeof(shaders_pt::MaterialGPU),
      MTL::ResourceStorageModeShared
    );

    size_t materialIdx = 0;
    for (auto materialId: materialIds) {
      auto* material = getMaterialOrDefault(materialId);

      // Create BSDF struct from material
      auto getTextureIdx = [&](std::optional<Scene::AssetID> id) {
        return id
          .transform([&](Scene::AssetID id) { return int32_t(m_textureIndices[id]); })
          .value_or(-1);
      };

      auto bsdfHandle = (shaders_pt::MaterialGPU*) materialsBuffer->contents() + materialIdx++;
      auto& bsdf = *bsdfHandle;
      bsdf = shaders_pt::MaterialGPU{
        .baseColor = material->baseColor,
        .emission = material->emission,
        .emissionStrength = material->emissionStrength,
        .roughness = material->roughness,
        .metallic = material->metallic,
        .transmission = material->transmission,
        .ior = material->ior,
        .anisotropy = material->anisotropy,
        .anisotropyRotation = material->anisotropyRotation,
        .clearcoat = material->clearcoat,
        .clearcoatRoughness = material->clearcoatRoughness,
        .flags = 0,
        .baseTextureId = getTextureIdx(material->getTexture(Material::TextureSlot::BaseColor)),
        .rmTextureId = getTextureIdx(material->getTexture(Material::TextureSlot::RoughnessMetallic)),
        .transmissionTextureId = getTextureIdx(material->getTexture(Material::TextureSlot::Transmission)),
        .clearcoatTextureId = getTextureIdx(material->getTexture(Material::TextureSlot::Clearcoat)),
        .emissionTextureId = getTextureIdx(material->getTexture(Material::TextureSlot::Emission)),
        .normalTextureId = getTextureIdx(material->getTexture(Material::TextureSlot::Normal)),
      };

      auto baseTexture = material->getTexture(Material::TextureSlot::BaseColor)
                                 .transform([&](Scene::AssetID id) { return m_store.scene().getAsset<Texture>(id); })
                                 .value_or(nullptr);

      if (material->thinTransmission)
        bsdf.flags |= shaders_pt::MaterialGPU::Material_ThinDielectric;
      if (material->baseColor[3] < 1.0 || (baseTexture && baseTexture->hasAlpha()))
        bsdf.flags |= shaders_pt::MaterialGPU::Material_UseAlpha;
      if (material->anisotropy != 0.0)
        bsdf.flags |= shaders_pt::MaterialGPU::Material_Anisotropic;
      if (material->isEmissive())
        bsdf.flags |= shaders_pt::MaterialGPU::Material_Emissive;
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
  auto meshes = m_store.scene().getAll<Mesh>();
  std::vector<Scene::AssetID> meshIds;
  meshIds.reserve(meshes.size());
  std::vector<MTL::AccelerationStructure*> meshAccelStructs;
  meshAccelStructs.reserve(meshes.size());

  size_t idx = 0;
  for (const auto& mesh: meshes) {
    auto geometryDesc = makeGeometryDescriptor(mesh.asset);
    geometryDesc->setIntersectionFunctionTableOffset(0);

    auto accelDesc = ns_shared<MTL::PrimitiveAccelerationStructureDescriptor>();
    accelDesc->setGeometryDescriptors(NS::Array::array(geometryDesc));

    meshAccelStructs.push_back(makeAccelStruct(accelDesc));
    meshIds.push_back(mesh.id);
  }

  m_meshAccelStructs = NS::Array::array(
    (NS::Object**) meshAccelStructs.data(),
    meshAccelStructs.size()
  )->retain();

  /*
   * Get instance data and build instance acceleration structure (TLAS)
   */
  auto instances = m_store.scene().getInstances();
  m_instanceBuffer = m_device->newBuffer(
    sizeof(MTL::AccelerationStructureInstanceDescriptor) * instances.size(),
    MTL::ResourceStorageModeShared
  );
  auto instanceDescriptors = static_cast<MTL::AccelerationStructureInstanceDescriptor*>(m_instanceBuffer->contents());

  idx = 0;
  for (const auto& instance: instances) {
    auto& id = instanceDescriptors[idx];
    // TODO: use a map for id -> index instead
    auto meshIdx = std::find_if(
      meshIds.begin(), meshIds.end(), [&](Scene::AssetID id) {
        return id == instance.mesh.id;
      }
    ) - meshIds.begin();

    id.accelerationStructureIndex = (uint32_t) meshIdx;
    id.intersectionFunctionTableOffset = 0;
    id.mask = 1;

    bool anyMaterialHasAlpha = false;
    auto& materials = *instance.node.materialIds().value();
    for (size_t materialIdx = 0; materialIdx < materials.size(); materialIdx++) {
      auto* bsdf = (shaders_pt::MaterialGPU*) m_instanceMaterialBuffers[idx]->contents() + materialIdx;
      if (bsdf->flags & shaders_pt::MaterialGPU::Material_UseAlpha) {
        anyMaterialHasAlpha = true;
        break;
      }
    }

    id.options = anyMaterialHasAlpha ? MTL::AccelerationStructureInstanceOptionNonOpaque
                                     : MTL::AccelerationStructureInstanceOptionOpaque;

    for (int32_t j = 0; j < 4; j++) {
      for (int32_t i = 0; i < 3; i++) {
        id.transformationMatrix.columns[j][i] = instance.transformMatrix.columns[j][i];
      }
    }

    idx++;
  }

  auto instanceAccelDesc = ns_shared<MTL::InstanceAccelerationStructureDescriptor>();
  instanceAccelDesc->setInstancedAccelerationStructures(m_meshAccelStructs);
  instanceAccelDesc->setInstanceCount(instances.size());
  instanceAccelDesc->setInstanceDescriptorBuffer(m_instanceBuffer);

  m_instanceAccelStruct = makeAccelStruct(instanceAccelDesc);
}

void Renderer::rebuildArgumentBuffer() {
  /*
   * Create the arguments buffer if it doesn't exist
   */
  if (m_argumentBuffer == nullptr) {
    m_argumentBuffer = m_device->newBuffer(
      sizeof(shaders_pt::Arguments),
      MTL::ResourceStorageModeShared
    );
  }

  /*
   * Get a handle to the argument buffer as a struct and fill in the args
   */
  auto arguments = static_cast<shaders_pt::Arguments*>(m_argumentBuffer->contents());
  arguments->constants = m_constants;
  arguments->vertexResources = m_vertexResourcesBuffer->gpuAddress();
  arguments->primitiveResources = m_primitiveResourcesBuffer->gpuAddress();
  arguments->instanceResources = m_instanceResourcesBuffer->gpuAddress();
  arguments->instances = m_instanceBuffer->gpuAddress();
  arguments->accelStruct = m_instanceAccelStruct->gpuResourceID();
  arguments->intersectionFunctionTable = m_intersectionFunctionTables[m_selectedPipeline]->gpuResourceID();
  arguments->lights = m_lightDataBuffer->gpuAddress();
  arguments->envLights = m_envLightDataBuffer->gpuAddress();
  arguments->textures = m_texturesBuffer->gpuAddress();

  // GGX Multiscatter LUTs
  arguments->luts.E = m_luts[0]->gpuResourceID();
  arguments->luts.Eavg = m_luts[1]->gpuResourceID();
  arguments->luts.EMs = m_luts[2]->gpuResourceID();
  arguments->luts.EavgMs = m_luts[3]->gpuResourceID();
  arguments->luts.ETransIn = m_luts[4]->gpuResourceID();
  arguments->luts.ETransOut = m_luts[5]->gpuResourceID();
  arguments->luts.EavgTransIn = m_luts[6]->gpuResourceID();
  arguments->luts.EavgTransOut = m_luts[7]->gpuResourceID();

  /*
   * Bind the argument buffer to the active intersection function table
   */
  m_intersectionFunctionTables[m_selectedPipeline]->setBuffer(m_argumentBuffer, 0, 0);
}

void Renderer::rebuildRenderTargets() {
  if (m_renderTarget != nullptr) m_renderTarget->release();
  if (m_accumulator != nullptr) m_accumulator->release();
  if (m_postProcessBuffer[0] != nullptr) m_postProcessBuffer[0]->release();
  if (m_postProcessBuffer[1] != nullptr) m_postProcessBuffer[1]->release();

  if (m_randomSource != nullptr) m_randomSource->release();

  auto texd = MTL::TextureDescriptor::alloc()->init();
  texd->setTextureType(MTL::TextureType2D);
  texd->setWidth(static_cast<uint32_t>(m_currentRenderSize.x));
  texd->setHeight(static_cast<uint32_t>(m_currentRenderSize.y));
  texd->setStorageMode(MTL::StorageModeShared);

  texd->setUsage(MTL::TextureUsageShaderWrite | MTL::TextureUsageShaderRead);
  texd->setPixelFormat(MTL::PixelFormatRGBA32Float);
  m_accumulator = m_device->newTexture(texd);
  m_postProcessBuffer[0] = m_device->newTexture(texd);
  m_postProcessBuffer[1] = m_device->newTexture(texd);

  texd->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  texd->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
  m_renderTarget = m_device->newTexture(texd);

  // Temporary crap way of getting randomness into the shader
  // TODO: get a better source of scrambling (fast owen?)
  texd->setUsage(MTL::TextureUsageShaderRead);
  texd->setPixelFormat(MTL::PixelFormatR32Uint);
  m_randomSource = m_device->newTexture(texd);

  auto k = (size_t) m_currentRenderSize.x * (size_t) m_currentRenderSize.y;
  std::vector<uint32_t> random(k);
  for (size_t i = 0; i < k; i++) random[i] = rand() % (1024 * 1024);
  m_randomSource->replaceRegion(
    MTL::Region::Make2D(0, 0, (size_t) m_currentRenderSize.x, (size_t) m_currentRenderSize.y),
    0,
    random.data(),
    sizeof(uint32_t) * (size_t) m_currentRenderSize.x
  );

  texd->release();
}

void Renderer::rebuildLightData() {
  /*
   * Release light data buffers, if they exist
   */
  if (m_lightDataBuffer != nullptr) m_lightDataBuffer->release();
  if (m_envLightDataBuffer != nullptr) m_envLightDataBuffer->release();

  /*
   * Iterate all instances, finding the ones with emissive materials.
   * For each instance with emissive materials, iterate its primitives. Any primitives that
   * use an emissive material get added as lights.
   */
  std::vector<shaders_pt::AreaLight> lights;
  ankerl::unordered_dense::set<Scene::AssetID> instanceEmissiveMaterials;
  uint32_t instanceIdx = 0;
  m_lightTotalPower = 0.0f;
  for (const auto& instance: m_store.scene().getInstances()) {
    instanceEmissiveMaterials.clear();
    for (auto materialId: *instance.node.materialIds().value()) {
      Material* material = nullptr;
      if (materialId) material = m_store.scene().getAsset<Material>(materialId.value());

      if (material != nullptr && material->isEmissive()) {
        instanceEmissiveMaterials.insert(materialId.value());
      }
    }

    if (!instanceEmissiveMaterials.empty()) {
      auto materialIndices = (uint32_t*) instance.mesh.asset->materialIndices()->contents();
      auto indices = (uint32_t*) instance.mesh.asset->indices()->contents();
      auto vertices = (float3*) instance.mesh.asset->vertexPositions()->contents();

      auto triangleCount = instance.mesh.asset->indexCount() / 3;
      for (int i = 0; i < triangleCount; i++) {
        auto material = instance.node.material(materialIndices[i]).value();
        if (instanceEmissiveMaterials.contains(material.id)) {
          // Transform the primitive vertices: this ensures the right area is calculated if the
          // instance is scaled
          const auto v0 = (instance.transformMatrix * make_float4(vertices[indices[i * 3 + 0]], 1.0f)).xyz;
          const auto v1 = (instance.transformMatrix * make_float4(vertices[indices[i * 3 + 1]], 1.0f)).xyz;
          const auto v2 = (instance.transformMatrix * make_float4(vertices[indices[i * 3 + 2]], 1.0f)).xyz;

          const auto edge1 = v1 - v0;
          const auto edge2 = v2 - v0;
          const auto area = length(cross(edge1, edge2)) * 0.5f;

          const auto emission = material.asset->emission * material.asset->emissionStrength;
          const auto lightPower = dot(emission, float3{0, 1, 0}) * area * std::numbers::pi_v<float>;
          m_lightTotalPower += lightPower;

          lights.push_back(
            {
              .instanceIdx = instanceIdx,
              .indices = {indices[i * 3 + 0], indices[i * 3 + 1], indices[i * 3 + 2]},
              .area = area,
              .power = lightPower,
              .cumulativePower = m_lightTotalPower,
              .emission = emission,
            }
          );
        }
      }
    }
    instanceIdx++;
  }

  m_lightCount = (uint32_t) lights.size();

  /*
   * Create and fill the lights buffer
   */
  size_t lightBufSize = sizeof(shaders_pt::AreaLight) * lights.size();
  m_lightDataBuffer = m_device->newBuffer(lightBufSize, MTL::ResourceStorageModeShared);
  memcpy(m_lightDataBuffer->contents(), lights.data(), lightBufSize);

  /*
   * Load environment lights into the argument buffer.
   * TODO: Right now the scene only supports one environment light, but we build this to support more
   */
  std::vector<shaders_pt::EnvironmentLight> envLights;
  m_envLightAliasTables.clear();

  const auto& envmap = m_store.scene().envmap();
  if (envmap.textureId()) {
    envLights.push_back(
      {
        .textureIdx = uint32_t(m_textureIndices.at(envmap.textureId().value())),
        .alias = envmap.aliasTable()->gpuAddress(),
      }
    );
    m_envLightAliasTables.push_back(envmap.aliasTable());
  }

  m_envLightCount = (uint32_t) envLights.size();

  /*
   * Create and fill the lights buffer
   */
  size_t envLightBufSize = sizeof(shaders_pt::EnvironmentLight) * envLights.size();
  m_envLightDataBuffer = m_device->newBuffer(envLightBufSize, MTL::ResourceStorageModeShared);
  memcpy(m_envLightDataBuffer->contents(), envLights.data(), envLightBufSize);
}

void Renderer::updateConstants(Scene::NodeID cameraNodeId, int flags) {
  auto node = m_store.scene().node(cameraNodeId);
  auto transform = m_store.scene().worldTransform(cameraNodeId);
  auto camera = node.get<Camera>().value();

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
    .lightCount = m_lightCount,
    .envLightCount = m_envLightCount,
    .lutSizeE = m_lutSizes[0],
    .lutSizeEavg = m_lutSizes[1],
    .flags = flags,
    .totalLightPower = m_lightTotalPower,
    .size = {(uint32_t) m_currentRenderSize.x, (uint32_t) m_currentRenderSize.y},
    .camera = {
      .position = pos,
      .topLeft = pos - w - (vu + vv) * 0.5f,
      .pixelDeltaU = vu / m_currentRenderSize.x,
      .pixelDeltaV = vv / m_currentRenderSize.y,
    },
  };
}

int Renderer::status() const {
  if (m_renderTarget != nullptr && m_accumulatedFrames < m_accumulationFrames) return Status_Busy;

  int status = Status_Ready;
  if (m_renderTarget != nullptr) status |= Status_Done;
  return status;
}

std::pair<size_t, size_t> Renderer::renderProgress() const {
  return {m_accumulatedFrames, m_accumulationFrames};
}

size_t Renderer::renderTime() const {
  return m_timer;
}

NS::SharedPtr<MTL::Buffer> Renderer::readbackRenderTarget(uint2* size) const {
  auto cmd = m_commandQueue->commandBuffer();
  auto benc = cmd->blitCommandEncoder();

  *size = {(uint32_t) m_renderTarget->width(), (uint32_t) m_renderTarget->height()};

  const auto bytesPerRow = sizeof(uchar4) * size->x;
  const auto bytesPerImage = bytesPerRow * size->y;

  const auto readbackBuffer = m_device->newBuffer(bytesPerImage, MTL::ResourceStorageModeShared);
  benc->copyFromTexture(
    m_renderTarget,
    0,
    0,
    MTL::Origin(0, 0, 0),
    MTL::Size(size->x, size->y, 1),
    readbackBuffer,
    0,
    bytesPerRow,
    bytesPerImage
  );
  benc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  return NS::TransferPtr(readbackBuffer);
}

Material* Renderer::getMaterialOrDefault(std::optional<Scene::AssetID> id) {
  Material* material = nullptr;
  if (id) material = m_store.scene().getAsset<Material>(id.value());
  if (material == nullptr) material = &m_store.scene().defaultMaterial();

  return material;
}

}
