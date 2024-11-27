#include <core/store.hpp>
#include <core/primitives.hpp>
#include <frontend/frontend.hpp>

int main() {
  pt::Store store;

  pt::Scene& scene = store.scene();
  pt::frontend::Frontend fe(store);
  fe.init();

  auto cube = pt::primitives::cube(store.device(), 2.0f);
  auto idx = scene.addMesh(std::move(cube));
  scene.addNode(pt::Scene::Node(idx));

  fe.start();

  return 0;
}
