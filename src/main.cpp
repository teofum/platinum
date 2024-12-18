#include <numbers>

#include <core/store.hpp>
#include <core/primitives.hpp>
#include <frontend/frontend.hpp>

int main() {
  NS::AutoreleasePool* autoreleasePool = NS::AutoreleasePool::alloc()->init();

  pt::Store store;

  pt::frontend::Frontend fe(store);

  auto res = fe.init();
  if (res != pt::frontend::Frontend::InitResult_Ok) return res;

  pt::Scene& scene = store.scene();
  
  // Default material
  pt::Material material{
    .baseColor = {0.8, 0.8, 0.8, 1.0},
  };
  auto materialId = scene.addMaterial("Default material", material);

//  // Default sphere
//  auto sphere = pt::primitives::sphere(store.device(), 1.0f, 48, 64);
//  auto meshId = scene.addMesh(std::move(sphere));
//  pt::Scene::Node defaultSphere("Sphere", meshId);
//  defaultSphere.materials.push_back(materialId);
//  scene.addNode(std::move(defaultSphere));
//
//  // Default camera
//  auto cameraId = scene.addCamera(pt::Camera::withFocalLength(28.0f));
//  pt::Scene::Node cameraNode("Camera");
//  cameraNode.cameraId = cameraId;
//  cameraNode.transform.translation = {0, 0, 5};
//  cameraNode.transform.track = true;
//  scene.addNode(std::move(cameraNode));

  fe.start();

  autoreleasePool->release();
  return 0;
}
