#ifndef PLATINUM_THEME_HPP
#define PLATINUM_THEME_HPP

#include <imgui.h>
#include <simd/simd.h>

using namespace simd;

namespace pt::frontend::theme {

struct Theme {
  static const Theme* currentTheme;
  
  float3 text;
  
  float3 bgWindow;
  float3 bgObject;
  float3 bgMenuBar;
  
  float3 border;
  
  float3 objectLowContrast;
  float3 objectMediumContrast;
  
  float3 primary;
  float3 secondary;
  float3 warning;
  float3 danger;
  float3 success;
  
  float3 viewportBackground;
  float3 viewportGrid;
  float3 viewportAxisX;
  float3 viewportAxisY;
  float3 viewportAxisZ;
  float3 viewportModel;
  float3 viewportOutline;
};

extern Theme platinumDark;
extern Theme platinumLight;

ImVec4 imguiRGBA(float3 rgb, float a = 1.0f);

void apply(ImGuiStyle& style, const Theme& theme);

}

#endif // PLATINUM_THEME_HPP
