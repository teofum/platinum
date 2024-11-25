#ifndef PLATINUM_MESH_HPP
#define PLATINUM_MESH_HPP

#include <vector>
#include <simd/simd.h>

using namespace simd;

namespace pt {

class Mesh {
public:
  [[nodiscard]] static Mesh make_cube(float side);

  explicit Mesh(
    std::vector<float3>&& vertices,
    std::vector<uint32_t>&& indices
  ) noexcept;

  [[nodiscard]] constexpr const auto& vertices() const {
    return m_vertices;
  }

  [[nodiscard]] constexpr const auto& indices() const {
    return m_indices;
  }

private:
  std::vector<float3> m_vertices;
  std::vector<uint32_t> m_indices;
};

}

#endif //PLATINUM_MESH_HPP
