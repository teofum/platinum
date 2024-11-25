#include "primitives.hpp"

namespace pt::primitives {

Mesh cube(float side) {
  float h = side * 0.5f;
  std::vector<float3> vertices(24);
  std::vector<VertexData> vData(24);
  std::vector<uint32_t> indices(36);

  std::array<float3, 6> faceNormals{
    float3{0, 0, 1},
    float3{1, 0, 0},
    float3{0, 0, -1},
    float3{-1, 0, 0},
    float3{0, 1, 0},
    float3{0, -1, 0},
  };

  std::array<float2, 4> facePositions{
    float2{1, -1},
    float2{1, 1},
    float2{-1, -1},
    float2{-1, 1},
  };

  for (size_t i = 0; i < 6; i++) {
    const float3& fn = faceNormals[i];
    const float3 up = abs(fn.y) == 1.0f ? float3{1, 0, 0} : float3{0, 1, 0};
    const float3 right = cross(up, fn);

    for (size_t j = 0; j < 4; j++) {
      const float2& fp = facePositions[j];
      vertices[4 * i + j] = (fn + up * fp.x + right * fp.y) * h;
      vData[4 * i + j] = {fn, make_float4(right, 1.0f), fp};
    }

    indices[6 * i + 0] = 4 * i + 0;
    indices[6 * i + 1] = 4 * i + 2;
    indices[6 * i + 2] = 4 * i + 1;
    indices[6 * i + 3] = 4 * i + 1;
    indices[6 * i + 4] = 4 * i + 2;
    indices[6 * i + 5] = 4 * i + 3;
  }

  return Mesh(std::move(vertices), std::move(vData), std::move(indices));
}

}
