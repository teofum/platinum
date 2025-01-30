#include "theme.hpp"

#include <imgui_internal.h>

namespace pt::frontend::theme {

ImVec4 imguiRGBA(float3 rgb, float a) {
  return ImVec4(rgb.r, rgb.g, rgb.b, a);
}

const Theme* Theme::currentTheme = nullptr;

void apply(ImGuiStyle& style, const Theme& theme) {
  Theme::currentTheme = &theme;
  
  ImVec4* colors = style.Colors;

  colors[ImGuiCol_Text]                   = imguiRGBA(theme.text, 1.00f);
  colors[ImGuiCol_TextDisabled]           = imguiRGBA(theme.text, 0.40f);
  
  colors[ImGuiCol_WindowBg]               = imguiRGBA(theme.bgWindow);
  colors[ImGuiCol_ChildBg]                = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  colors[ImGuiCol_PopupBg]                = imguiRGBA(theme.bgObject, 0.98f);
  
  colors[ImGuiCol_Border]                 = imguiRGBA(theme.border, 0.30f);
  colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  
  colors[ImGuiCol_FrameBg]                = imguiRGBA(theme.bgObject);
  colors[ImGuiCol_FrameBgHovered]         = imguiRGBA(theme.primary, 0.40f);
  colors[ImGuiCol_FrameBgActive]          = imguiRGBA(theme.primary, 0.67f);
  
  colors[ImGuiCol_TitleBg]                = imguiRGBA(theme.bgObject);
  colors[ImGuiCol_TitleBgActive]          = imguiRGBA(theme.bgObject);
  colors[ImGuiCol_TitleBgCollapsed]       = imguiRGBA(theme.bgObject, 0.51f);
  
  colors[ImGuiCol_MenuBarBg]              = imguiRGBA(theme.bgMenuBar);
  
  colors[ImGuiCol_ScrollbarBg]            = imguiRGBA(theme.bgObject, 0.53f);
  colors[ImGuiCol_ScrollbarGrab]          = imguiRGBA(theme.objectLowContrast, 0.80f);
  colors[ImGuiCol_ScrollbarGrabHovered]   = imguiRGBA(theme.objectMediumContrast, 0.80f);
  colors[ImGuiCol_ScrollbarGrabActive]    = imguiRGBA(theme.objectMediumContrast, 1.00f);
  
  colors[ImGuiCol_CheckMark]              = imguiRGBA(theme.primary);
//  colors[ImGuiCol_SliderGrab]             = ImVec4(0.26f, 0.59f, 0.98f, 0.78f);
//  colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.46f, 0.54f, 0.80f, 0.60f);
  colors[ImGuiCol_Button]                 = imguiRGBA(theme.bgObject);
  colors[ImGuiCol_ButtonHovered]          = imguiRGBA(mix(theme.bgObject, theme.primary, float3(0.2)));
  colors[ImGuiCol_ButtonActive]           = imguiRGBA(mix(theme.bgObject, theme.primary, float3(0.25)));
  
  colors[ImGuiCol_Header]                 = imguiRGBA(theme.primary, 0.30f);
  colors[ImGuiCol_HeaderHovered]          = imguiRGBA(theme.primary, 0.50f);
  colors[ImGuiCol_HeaderActive]           = imguiRGBA(theme.primary, 0.60f);
  
  colors[ImGuiCol_Separator]              = imguiRGBA(theme.objectLowContrast, 0.62f);
  colors[ImGuiCol_SeparatorHovered]       = imguiRGBA(theme.primary, 0.67f);
  colors[ImGuiCol_SeparatorActive]        = imguiRGBA(theme.primary, 1.00f);
  colors[ImGuiCol_ResizeGrip]             = imguiRGBA(theme.objectLowContrast, 0.17f);
  colors[ImGuiCol_ResizeGripHovered]      = imguiRGBA(theme.primary, 0.67f);
  colors[ImGuiCol_ResizeGripActive]       = imguiRGBA(theme.primary, 1.00f);
  
  colors[ImGuiCol_TabHovered]             = imguiRGBA(theme.bgWindow);
  colors[ImGuiCol_Tab]                    = imguiRGBA(theme.bgMenuBar);
  colors[ImGuiCol_TabSelected]            = imguiRGBA(theme.bgWindow);
  colors[ImGuiCol_TabSelectedOverline]    = imguiRGBA(theme.primary);
  colors[ImGuiCol_TabDimmed]              = imguiRGBA(theme.bgMenuBar);
  colors[ImGuiCol_TabDimmedSelected]      = imguiRGBA(theme.bgWindow);
  colors[ImGuiCol_TabDimmedSelectedOverline] = imguiRGBA(theme.primary, 0.00f);
  
  colors[ImGuiCol_DockingPreview]         = colors[ImGuiCol_Header];
  colors[ImGuiCol_DockingEmptyBg]         = imguiRGBA(theme.bgWindow);
  
  colors[ImGuiCol_PlotLines]              = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
  colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
  colors[ImGuiCol_PlotHistogram]          = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
  colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.45f, 0.00f, 1.00f);
  
  colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.78f, 0.87f, 0.98f, 1.00f);
  colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.57f, 0.57f, 0.64f, 1.00f);   // Prefer using Alpha=1.0 here
  colors[ImGuiCol_TableBorderLight]       = ImVec4(0.68f, 0.68f, 0.74f, 1.00f);   // Prefer using Alpha=1.0 here
  colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  colors[ImGuiCol_TableRowBgAlt]          = ImVec4(0.30f, 0.30f, 0.30f, 0.09f);
  
  colors[ImGuiCol_TextLink]               = colors[ImGuiCol_HeaderActive];
  colors[ImGuiCol_TextSelectedBg]         = imguiRGBA(theme.primary, 0.35f);
  
  colors[ImGuiCol_DragDropTarget]         = imguiRGBA(theme.primary, 0.95f);
  colors[ImGuiCol_NavCursor]              = colors[ImGuiCol_HeaderHovered];
  colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(0.70f, 0.70f, 0.70f, 0.70f);
  colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.20f, 0.20f, 0.20f, 0.20f);
  colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.20f, 0.20f, 0.20f, 0.35f);
}

Theme platinumDark = {
  .text 			= {0.95, 0.95, 0.95},
  .bgWindow 	= {0.18, 0.18, 0.18},
  .bgObject 	= {0.12, 0.12, 0.12},
  .bgMenuBar 	= {0.24, 0.24, 0.24},
  .border 		= {0.00, 0.00, 0.00},
  .objectLowContrast = {0.30, 0.30, 0.30},
  .objectMediumContrast = {0.38, 0.38, 0.38},
  .primary    = {0.05, 1.00, 0.75},
};

}
