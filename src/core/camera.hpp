#ifndef PLATINUM_CAMERA_HPP
#define PLATINUM_CAMERA_HPP

#include <simd/simd.h>

using namespace simd;

namespace pt {

struct Camera {
  float2 sensorSize = {36.0f, 24.0f};   // Sensor/film size in mm
  float focalLength = 50.0f;            // Lens focal length in mm
  float aperture = 0.0f;                // Lens aperture as f-number (fraction of focal length)
  uint32_t apertureBlades = 7;          // Aperture blade count
  float roundness = 1.0f;               // Aperture shape
  float bokehPower = 0.0f;              // Bokeh profile power
  float focusDistance = 1.0f;           // Focus distance in world units

  static constexpr Camera withFocalLength(
    float f,
    float2 sensorSize = {36.0f, 24.0f},
    float aperture = 0.0f
  ) {
    return {
      .sensorSize = sensorSize,
      .focalLength = f,
      .aperture = aperture,
    };
  }

  static constexpr Camera withFov(
    float yFov,
    float2 sensorSize = {36.0f, 24.0f},
    float aperture = 0.0f
  ) {
    return {
      .sensorSize = sensorSize,
      .focalLength = sensorSize.y / (2.0f * tan(yFov * 0.5f)),
      .aperture = aperture,
    };
  }

  [[nodiscard]] constexpr float yFov() const {
    return 2.0f * atan(sensorSize.y / (2.0f * focalLength));
  }

  [[nodiscard]] constexpr float croppedSensorHeight(float aspect) const {
    float sensorAspect = sensorSize.x / sensorSize.y;
    return sensorSize.x / max(sensorAspect, aspect);
  };
};

}

#endif //PLATINUM_STUDIO_CAMERA_HPP
