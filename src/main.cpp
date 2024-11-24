#include <core/store.hpp>
#include <frontend/frontend.hpp>

int main() {
  pt::Store store;

  pt::Scene& scene = store.scene();

  std::vector<float3> vertices{
    float3{1.0f, 1.0f, -1.0f},
    float3{1.0f, -1.0f, -1.0f},
    float3{1.0f, 1.0f, 1.0f},
    float3{1.0f, -1.0f, 1.0f},
    float3{-1.0f, 1.0f, -1.0f},
    float3{-1.0f, -1.0f, -1.0f},
    float3{-1.0f, 1.0f, 1.0f},
    float3{-1.0f, -1.0f, 1.0f},
  };
  std::vector<uint32_t> indices{
    4, 2, 0, 2, 7, 3,
    6, 5, 7, 1, 7, 5,
    0, 3, 1, 4, 1, 5,
    4, 6, 2, 2, 6, 7,
    6, 4, 5, 1, 3, 7,
    0, 2, 3, 4, 0, 1,
  };

  auto cube = pt::Mesh(std::move(vertices), std::move(indices));
  auto idx = scene.addMesh(std::move(cube));
  scene.addNode(pt::Scene::Node(idx));

  pt::frontend::Frontend fe(store);
  fe.start();

  return 0;
}
