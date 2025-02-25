#include "primitives.hpp"

#include <numbers>

namespace pt::primitives {

Mesh plane(MTL::Device* device, float side) {
  float h = side * 0.5f;

  std::vector<float3> vertices{
    float3{-h, 0.0, -h},
    float3{h, 0.0, -h},
    float3{-h, 0.0, h},
    float3{h, 0.0, h},
  };
  std::vector<VertexData> vData(4);
  for (size_t i = 0; i < 4; i++) {
    vData[i] = {
      {0, 1, 0},
      {1, 0, 0},
      (vertices[i].xz + h) / (2.0 * h)
    };
  }

  std::vector<uint32_t> indices{0, 2, 1, 1, 2, 3};
  std::vector<uint32_t> matIndices{0, 0};

  return {device, vertices, vData, indices, matIndices};
}

Mesh cube(MTL::Device* device, float side) {
  float h = side * 0.5f;
  std::vector<float3> vertices(24);
  std::vector<VertexData> vData(24);
  std::vector<uint32_t> indices(36);
  std::vector<uint32_t> matIndices(12);

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

  for (uint32_t i = 0; i < 6; i++) {
    const float3& fn = faceNormals[i];
    const float3 up = abs(fn.y) == 1.0f ? float3{1, 0, 0} : float3{0, 1, 0};
    const float3 right = cross(up, fn);

    for (uint32_t j = 0; j < 4; j++) {
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

    matIndices[2 * i + 0] = 0;
    matIndices[2 * i + 1] = 0;
  }

  return {device, vertices, vData, indices, matIndices};
}

Mesh sphere(MTL::Device* device, float radius, uint32_t lat, uint32_t lng) {
  std::vector<float3> vertices((lat + 1) * (lng + 1));
  std::vector<VertexData> vData((lat + 1) * (lng + 1));
  std::vector<uint32_t> indices(lat * lng * 6);
  std::vector<uint32_t> matIndices(lat * lng * 2);

  const float pi = std::numbers::pi_v<float>;
  const float dLat = pi / static_cast<float>(lat);
  const float dLng = pi / static_cast<float>(lng) * 2.0f;

  uint32_t t = 0;
  for (uint32_t i = 0; i <= lat; i++) {
    const float phi = 0.5f * pi - static_cast<float>(i) * dLat;
    const float c = cos(phi);

    for (uint32_t j = 0; j <= lng; j++) {
      const float theta = static_cast<float>(j) * dLng;

      float3 pos{c * cos(theta), sin(phi), c * sin(theta)};
      vertices[i * (lng + 1) + j] = pos * radius;
      vData[i * (lng + 1) + j] = {
        pos,
        {-sin(theta), 0, cos(theta), 1.0f},
        {
          static_cast<float>(j) / static_cast<float>(lng),
          static_cast<float>(i) / static_cast<float>(lat),
        }
      };

      if (i > 0 && j > 0) {
        const uint32_t
          v0 = (i - 1) * (lng + 1) + (j - 1),
          v1 = (i - 1) * (lng + 1) + (j),
          v2 = (i) * (lng + 1) + (j - 1),
          v3 = (i) * (lng + 1) + (j);

        indices[6 * t + 0] = v0;
        indices[6 * t + 1] = v1;
        indices[6 * t + 2] = v2;
        indices[6 * t + 3] = v1;
        indices[6 * t + 4] = v3;
        indices[6 * t + 5] = v2;

        matIndices[2 * t + 0] = 0;
        matIndices[2 * t + 1] = 0;
        t++;
      }
    }
  }

  return {device, vertices, vData, indices, matIndices};
}


Mesh cornellBox(MTL::Device* device) {
  float h = 5.0f;
  std::vector<float3> vertices(24);
  std::vector<VertexData> vData(24);
  std::vector<uint32_t> indices(36);
  std::vector<uint32_t> matIndices(12);

  std::array<float3, 5> faceNormals{
    float3{0, 0, 1},
    float3{0, 1, 0},
    float3{0, -1, 0},
    float3{1, 0, 0},
    float3{-1, 0, 0},
  };

  std::array<float2, 4> facePositions{
    float2{1, -1},
    float2{1, 1},
    float2{-1, -1},
    float2{-1, 1},
  };

  for (uint32_t i = 0; i < 5; i++) {
    const float3& fn = faceNormals[i];
    const float3 up = abs(fn.y) == 1.0f ? float3{1, 0, 0} : float3{0, 1, 0};
    const float3 right = cross(up, fn);

    for (uint32_t j = 0; j < 4; j++) {
      const float2& fp = facePositions[j];
      vertices[4 * i + j] = (-fn + up * fp.x + right * fp.y) * h + float3{0, h, 0};
      vData[4 * i + j] = {fn, make_float4(right, 1.0f), fp};
    }

    indices[6 * i + 0] = 4 * i + 0;
    indices[6 * i + 1] = 4 * i + 2;
    indices[6 * i + 2] = 4 * i + 1;
    indices[6 * i + 3] = 4 * i + 1;
    indices[6 * i + 4] = 4 * i + 2;
    indices[6 * i + 5] = 4 * i + 3;

    matIndices[2 * i + 0] = matIndices[2 * i + 1] = i < 3 ? 0 : i - 2;
  }

  for (uint32_t k = 0; k < 4; k++) {
    const float2& fp = facePositions[k];
    vertices[20 + k] = {fp.x, 2 * h - 0.01f, fp.y};
    vData[20 + k] = {float3{0, -1, 0}, float4{0, 0, 1, 1}, fp};
  }
  indices[30] = 20;
  indices[31] = 22;
  indices[32] = 21;
  indices[33] = 21;
  indices[34] = 22;
  indices[35] = 23;
  matIndices[10] = matIndices[11] = 3;

  return {device, vertices, vData, indices, matIndices};
}

}
