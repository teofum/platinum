#ifndef PLATINUM_JSON_HPP
#define PLATINUM_JSON_HPP

#include <simd/simd.h>
#include <json.hpp>

#include <core/transform.hpp>

using namespace simd;

namespace pt::json_utils {
using json = nlohmann::json;

template<typename T>
concept trivially_copyable = requires(T value) {
  std::is_trivially_copyable_v<T> == true;
};

/*
 * Helper function to create JSON arrays of trivially copyable objects, takesparameters by value.
 * The initializer list constructor uses non-const references, which can't be used with vector types.
 */
template<trivially_copyable ...Args>
json array(Args ...args) { return {args...}; }

inline json vec(float2 f) { return array(f.x, f.y); }
inline json vec(float3 f) { return array(f.x, f.y, f.z); }
inline json vec(float4 f) { return array(f.x, f.y, f.z, f.w); }

inline json transform(const Transform& transform) {
  return {
    {"t",     vec(transform.translation)},
    {"r",     vec(transform.rotation)},
    {"s",     vec(transform.scale)},
    {"tgt",   vec(transform.target)},
    {"track", transform.track},
  };
}

inline float2 parseFloat2(const json& j) { return {j.at(0), j.at(1)}; }
inline float3 parseFloat3(const json& j) { return {j.at(0), j.at(1), j.at(2)}; }
inline float4 parseFloat4(const json& j) { return {j.at(0), j.at(1), j.at(2), j.at(3)}; }

inline Transform parseTransform(const json& j) {
  Transform t;

  t.translation = parseFloat3(j.at("t"));
  t.rotation = parseFloat3(j.at("r"));
  t.scale = parseFloat3(j.at("s"));
  t.target = parseFloat3(j.at("tgt"));
  t.track = j.at("track");

  return t;
}

}

#endif //PLATINUM_JSON_HPP
