#ifndef PLATINUM_PRIMITIVES_HPP
#define PLATINUM_PRIMITIVES_HPP

#include "mesh.hpp"

namespace pt::primitives {

[[nodiscard]] Mesh cube(MTL::Device* device, float side);

[[nodiscard]] Mesh sphere(
  MTL::Device* device,
  float radius,
  size_t lat,
  size_t lng
);

}

#endif //PLATINUM_PRIMITIVES_HPP
