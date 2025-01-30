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
};

extern Theme platinumDark;

ImVec4 imguiRGBA(float3 rgb, float a = 1.0f);

void apply(ImGuiStyle& style, const Theme& theme);

}

#endif // PLATINUM_THEME_HPP
