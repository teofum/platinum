#include <numbers>

#include <core/store.hpp>
#include <core/primitives.hpp>
#include <frontend/frontend.hpp>

int main() {
  pt::Store store;

  pt::Scene& scene = store.scene();
  pt::frontend::Frontend fe(store);
  fe.init();

  // Default cube
  auto cube = pt::primitives::cube(store.device(), 2.0f);
  auto meshId = scene.addMesh(std::move(cube));
  scene.addNode(pt::Scene::Node("Cube", meshId));

  // Default camera
  auto cameraId = scene.addCamera(pt::Camera::withFocalLength(28.0f));
  pt::Scene::Node cameraNode("Camera");
  cameraNode.cameraId = cameraId;
  cameraNode.transform.translation = {-5, 5, 5};
  cameraNode.transform.track = true;
  scene.addNode(std::move(cameraNode));

  fe.start();

  return 0;
}
