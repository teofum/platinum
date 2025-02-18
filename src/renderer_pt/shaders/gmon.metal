#include <metal_stdlib>

using namespace metal;

#include "../pt_shader_defs.hpp"

using namespace pt::shaders_pt;

// RGB weights for luma calculation
constant float3 lw(0.2126, 0.7152, 0.0722);

constant int maxBuckets = 32;

kernel void gmon(
  uint2                                 tid         				[[thread_position_in_grid]],
  constant Texture*                     buckets							[[buffer(0)]],
  constant uint32_t&                    nBuckets						[[buffer(1)]],
  constant GmonOptions&                 options						  [[buffer(2)]],
  texture2d<float, access::write>       acc         				[[texture(0)]]
) {
  // Read buffer values into array
  float3 values[maxBuckets];
  for (int i = 0; i < nBuckets; i++) {
    values[i] = buckets[i].tex.read(tid).rgb;
  }

  // Sort values by luma
  for (int i = nBuckets; i > 1; i--) {
    for (int j = 1; j < i; j++) {
      if (dot(values[j], lw) < dot(values[j - 1], lw)) {
        float3 temp = values[j - 1];
        values[j - 1] = values[j];
        values[j] = temp;
      }
    }
  }

  // Calculate the Gini function
  float3 sum(0.0), weightedSum(0.0);
  for (int i = 0; i < nBuckets; i++) {
    sum += values[i];
    weightedSum += float(i + 1) * values[i];
  }
  float G = (2.0 * dot(weightedSum, lw)) / (nBuckets * dot(sum, lw))
            - float(nBuckets + 1) / float(nBuckets);
  G = min(G, options.cap);

  // Use more buckets the lower the Gini function value (higher confidence)
  auto c = int(G * float(nBuckets / 2));
  sum = float3(0.0);
  for (int i = c; i < nBuckets - c; i++) sum += values[i];

  float3 color = sum / float(nBuckets - 2 * c);
  acc.write(float4(color, 1.0), tid);
}