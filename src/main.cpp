#include <core/store.hpp>
#include <frontend/frontend.hpp>

int main() {
  pt::Store store;

  pt::Scene& scene = store.scene();

  auto cube = pt::Mesh::make_cube(2.0f);
  auto idx = scene.addMesh(std::move(cube));
  scene.addNode(pt::Scene::Node(idx));

  pt::frontend::Frontend fe(store);
  fe.start();

  return 0;
}
