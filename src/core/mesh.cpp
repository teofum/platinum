#include "mesh.hpp"

namespace pt {

Mesh::Mesh(
  std::vector<float3>&& vertices,
  std::vector<uint32_t>&& indices
) noexcept: m_vertices(std::move(vertices)), m_indices(std::move(indices)) {
}

Mesh Mesh::make_cube(float side) {
  float h = side * 0.5f;
  std::vector<float3> vertices{
    float3{h, h, -h},
    float3{h, -h, -h},
    float3{h, h, h},
    float3{h, -h, h},
    float3{-h, h, -h},
    float3{-h, -h, -h},
    float3{-h, h, h},
    float3{-h, -h, h},
  };

  std::vector<uint32_t> indices{
    4, 2, 0, 2, 7, 3,
    6, 5, 7, 1, 7, 5,
    0, 3, 1, 4, 1, 5,
    4, 6, 2, 2, 6, 7,
    6, 4, 5, 1, 3, 7,
    0, 2, 3, 4, 0, 1,
  };

  return Mesh(std::move(vertices), std::move(indices));
}

}
