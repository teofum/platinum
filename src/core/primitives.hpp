#ifndef PLATINUM_PRIMITIVES_HPP
#define PLATINUM_PRIMITIVES_HPP

#include "mesh.hpp"

namespace pt::primitives {

[[nodiscard]] Mesh plane(MTL::Device* device, float side);

[[nodiscard]] Mesh cube(MTL::Device* device, float side);

[[nodiscard]] Mesh sphere(
  MTL::Device* device,
  float radius,
  uint32_t lat,
  uint32_t lng
);

[[nodiscard]] Mesh cornellBox(MTL::Device* device);

}

#endif //PLATINUM_PRIMITIVES_HPP
