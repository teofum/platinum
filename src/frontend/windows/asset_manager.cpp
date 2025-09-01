#include "asset_manager.hpp"

#include <frontend/theme.hpp>
#include <frontend/windows/common/material_props.hpp>

namespace pt::frontend::windows {

static const char *getTextureFormatName(MTL::Texture *texture) {
  switch (texture->pixelFormat()) {
  case MTL::PixelFormatRGBA8Unorm:
    return "Linear RGBA 8bpc";
  case MTL::PixelFormatRGBA8Unorm_sRGB:
    return "sRGB RGBA 8bpc";
  case MTL::PixelFormatRG8Unorm:
    return "Roughness/Metallic (RG 8bpc)";
  case MTL::PixelFormatR8Unorm:
    return "Grayscale 8bit";
  case MTL::PixelFormatRGBA32Float:
    return "HDR (RGBA 32bpc)";
  default:
    return "Unknown format";
  }
}

AssetManager::AssetManager(Store &store, bool *open) noexcept
    : Window(store, open) {}

void AssetManager::render() {
  m_assets = m_store.scene().getAllAssets([&](const Scene::AssetPtr &asset) {
    return (m_showMeshes &&
            std::holds_alternative<std::unique_ptr<Mesh>>(asset)) ||
           (m_showTextures &&
            std::holds_alternative<std::unique_ptr<Texture>>(asset)) ||
           (m_showMaterials &&
            std::holds_alternative<std::unique_ptr<Material>>(asset));
  });
  m_assetCount = m_assets.size();

  ImGui::Begin("Asset Manager");

  void *it = nullptr;
  ImGuiID id;
  while (m_selection.GetNextSelectedItem(&it, &id)) {
    if (!m_store.scene().assetValid(id))
      m_selection.SetItemSelected(id, false);
  }

  /*
   * Filters and settings
   */
  ImGui::AlignTextToFramePadding();
  ImGui::Text("Show");
  ImGui::SameLine();
  ImGui::Checkbox("Textures", &m_showTextures);
  ImGui::SameLine();
  ImGui::Checkbox("Materials", &m_showMaterials);
  ImGui::SameLine();
  ImGui::Checkbox("Meshes", &m_showMeshes);

  ImGui::Spacing();

  /*
   * Main panels
   */
  ImGui::PushStyleColor(ImGuiCol_TableBorderLight, 0);
  if (ImGui::BeginTable("AMLayout", 2, ImGuiTableFlags_Resizable)) {
    ImGui::TableSetupColumn("Assets", ImGuiTableColumnFlags_WidthStretch, 1);
    ImGui::TableSetupColumn("Properties", ImGuiTableColumnFlags_WidthFixed,
                            250);

    ImGui::TableNextColumn();
    renderAssetsPanel();
    ImGui::TableNextColumn();
    renderPropertiesPanel();

    ImGui::EndTable();
  }
  ImGui::PopStyleColor();

  ImGui::End();
}

void AssetManager::updateLayoutSizes(float availableWidth) {
  m_layoutItemSpacing = float(m_spacing);
  m_layoutSelectableSpacing =
      max(0.0f, m_layoutItemSpacing - float(m_hitSpacing));
  m_layoutItemSize = {float(m_iconSize), float(m_iconSize)};
  m_layoutItemStep = {m_layoutItemSize.x + m_layoutItemSpacing,
                      m_layoutItemSize.y + m_layoutItemSpacing};

  m_layoutColumnCount = MAX(1u, uint32_t(availableWidth / m_layoutItemStep.x));
  m_layoutRowCount =
      (uint32_t(m_assetCount) + m_layoutColumnCount - 1) / m_layoutColumnCount;

  m_layoutOuterPadding = float(m_spacing) * 0.5f;
}

void AssetManager::renderAssetsPanel() {
  auto *theme = theme::Theme::currentTheme;

  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
  bool showChild = ImGui::BeginChild(
      "Assets", {0, 0}, ImGuiChildFlags_FrameStyle, ImGuiWindowFlags_NoMove);
  ImGui::PopStyleVar();

  if (showChild) {
    float availableWidth = ImGui::GetContentRegionAvail().x;
    updateLayoutSizes(availableWidth);

    auto *imguiDrawList = ImGui::GetWindowDrawList();

    // Calculate grid start position
    auto startPos = ImGui::GetCursorScreenPos();
    startPos = {startPos.x + m_layoutOuterPadding,
                startPos.y + m_layoutOuterPadding};
    ImGui::SetCursorScreenPos(startPos);

    // Multi select
    ImGuiMultiSelectFlags msFlags = ImGuiMultiSelectFlags_ClearOnEscape |
                                    ImGuiMultiSelectFlags_ClearOnClickVoid |
                                    ImGuiMultiSelectFlags_BoxSelect2d;
    auto *msIo =
        ImGui::BeginMultiSelect(msFlags, m_selection.Size, int(m_assetCount));

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        {m_layoutSelectableSpacing, m_layoutSelectableSpacing});

    // Draw grid
    ImGuiListClipper clipper;
    clipper.Begin(int(m_layoutRowCount), m_layoutItemStep.y);

    while (clipper.Step()) {
      for (uint32_t rowIdx = clipper.DisplayStart; rowIdx < clipper.DisplayEnd;
           rowIdx++) {
        const uint32_t rowBegin = rowIdx * m_layoutColumnCount;
        const uint32_t rowEnd =
            MIN((rowIdx + 1) * m_layoutColumnCount, uint32_t(m_assetCount));
        for (uint32_t itemIdx = rowBegin; itemIdx < rowEnd; itemIdx++) {
          auto asset = m_assets[itemIdx];

          // Push ImGui ID to avoid name conflicts
          ImGui::PushID(int(asset.id));

          // Calculate item position in grid
          ImVec2 pos(startPos.x + m_layoutItemStep.x *
                                      float(itemIdx % m_layoutColumnCount),
                     startPos.y + m_layoutItemStep.y * float(rowIdx));
          ImGui::SetCursorScreenPos(pos);

          // Draw the actual selectable
          ImGui::SetNextItemSelectionUserData(ImGuiSelectionUserData(asset.id));
          bool isSelected = m_selection.Contains(ImGuiID(asset.id));
          bool isVisible = ImGui::IsRectVisible(m_layoutItemSize);

          ImGui::PushStyleColor(ImGuiCol_Header,
                                theme::imguiRGBA(theme->primary));
          ImGui::PushStyleColor(
              ImGuiCol_HeaderHovered,
              theme::imguiRGBA(
                  mix(theme->bgObject, theme->primary, float3(0.5))));
          ImGui::PushStyleColor(ImGuiCol_NavCursor, 0);
          ImGui::Selectable("", isSelected, ImGuiSelectableFlags_None,
                            m_layoutItemSize);
          ImGui::PopStyleColor(3);

          // Update selection
          if (ImGui::IsItemToggledSelection())
            isSelected = !isSelected;

          // TODO: drag-drop source

          // Draw item
          if (isVisible) {
            ImVec2 boxMin = pos;
            ImVec2 boxMax(pos.x + m_layoutItemSize.x,
                          pos.y + m_layoutItemSize.y);

            // Asset content
            std::visit(
                [&](const auto &asset) {
                  using T = std::decay_t<decltype(asset)>;

                  imguiDrawList->AddRectFilled(
                      boxMin, boxMax, ImGui::GetColorU32(ImGuiCol_WindowBg), 2);
                  if constexpr (std::is_same_v<T, Texture *>) {
                    imguiDrawList->AddImageRounded(
                        (ImTextureID)asset->texture(), boxMin, boxMax, {0, 0},
                        {1, 1}, ImGui::GetColorU32({1, 1, 1, 1}), 2);
                  } else if constexpr (std::is_same_v<T, Material *>) {
                    float4 col = asset->baseColor;
                    imguiDrawList->AddRectFilled({boxMin.x + float(m_padding),
                                                  boxMin.y + float(m_padding)},
                                                 {boxMax.x - float(m_padding),
                                                  boxMax.y - float(m_padding)},
                                                 theme::imguiU32(col.xyz), 2);
                  }
                },
                asset.asset);

            // Type indicator
            // TODO: replace this with an icon
            std::visit(
                [&](const auto &asset) {
                  using T = std::decay_t<decltype(asset)>;

                  ImU32 color;
                  if constexpr (std::is_same_v<T, Texture *>) {
                    color = theme::imguiU32(theme::sRGB(theme->viewportAxisZ));
                  } else if constexpr (std::is_same_v<T, Material *>) {
                    color = theme::imguiU32(theme::sRGB(theme->viewportAxisY));
                  } else if constexpr (std::is_same_v<T, Mesh *>) {
                    color = theme::imguiU32(theme::sRGB(theme->viewportAxisX));
                  }
                  imguiDrawList->AddRectFilled(
                      {boxMax.x - float(m_padding) - 8,
                       boxMin.y + float(m_padding)},
                      {boxMax.x - float(m_padding),
                       boxMin.y + float(m_padding) + 8},
                      color, 2);
                },
                asset.asset);

            // Retain indicator
            // TODO: replace this with an icon
            if (m_store.scene().assetRetained(asset.id)) {
              imguiDrawList->AddRectFilled(
                  {boxMin.x + float(m_padding), boxMin.y + float(m_padding)},
                  {boxMin.x + float(m_padding) + 8,
                   boxMin.y + float(m_padding) + 8},
                  theme::imguiU32(theme->primary), 2);
            }

            // Asset ID
            auto labelColor = ImGui::GetColorU32(
                isSelected ? ImGuiCol_Text : ImGuiCol_TextDisabled);
            auto label = std::format("{}", asset.id);
            imguiDrawList->AddText(
                {boxMin.x + float(m_padding),
                 boxMax.y - float(m_padding) - ImGui::GetFontSize()},
                labelColor, label.data());
          }

          ImGui::PopID();
        }
      }
    }

    ImGui::PopStyleVar();

    // Apply selection changes
    msIo = ImGui::EndMultiSelect();
    m_selection.ApplyRequests(msIo);
  }
  ImGui::EndChild();
}

void AssetManager::renderPropertiesPanel() {
  if (ImGui::BeginChild("Properties", {0, 0}, ImGuiChildFlags_None,
                        ImGuiWindowFlags_NoMove)) {
    if (m_selection.Size == 0) {
      ImGui::Text("[No assets selected]");
    } else if (m_selection.Size > 1) {
      ImGui::Text("[%d assets selected]", m_selection.Size);

      bool allSelectedAssetsRetained = true;
      void *it = nullptr;
      ImGuiID id;
      while (allSelectedAssetsRetained &&
             m_selection.GetNextSelectedItem(&it, &id)) {
        if (!m_store.scene().assetRetained(id))
          allSelectedAssetsRetained = false;
      }

      if (ImGui::Checkbox("Retain assets", &allSelectedAssetsRetained)) {
        it = nullptr;
        while (m_selection.GetNextSelectedItem(&it, &id)) {
          m_store.scene().assetRetained(id) = allSelectedAssetsRetained;
        }
      }
    } else {
      void *it = nullptr;
      ImGuiID id;
      if (m_selection.GetNextSelectedItem(&it, &id)) {
        auto asset = m_store.scene().getAsset(id);

        if (std::holds_alternative<Texture *>(asset)) {
          renderTextureProperties(asset, id);
        } else if (std::holds_alternative<Material *>(asset)) {
          renderMaterialProperties(asset, id);
        } else {
          renderMeshProperties(asset, id);
        }
      }
    }
  }

  ImGui::EndChild();
}

void AssetManager::renderTextureProperties(Scene::AnyAsset &texture,
                                           Scene::AssetID id) {
  Texture *asset = std::get<Texture *>(texture);

  assetPropertiesHeader("Texture", id);

  ImGui::Text("%s", getTextureFormatName(asset->texture()));
  auto size = std::format("{}x{}", asset->texture()->width(),
                          asset->texture()->height());
  ImGui::SameLine(ImGui::GetContentRegionAvail().x -
                  ImGui::CalcTextSize(size.c_str()).x);
  ImGui::Text("%s", size.c_str());

  ImGui::Spacing();

  const float width = ImGui::GetContentRegionAvail().x;
  ImGui::PushStyleColor(ImGuiCol_ChildBg,
                        (ImVec4)ImColor::HSV(0.0f, 0.0f, 0.8f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
  ImGui::BeginChild("TextureView",
                    {width, width * float(asset->texture()->height()) /
                                float(asset->texture()->width())},
                    ImGuiChildFlags_Borders,
                    ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();

  ImGui::Image((ImTextureID)asset->texture(),
               {width, width * float(asset->texture()->height()) /
                           float(asset->texture()->width())});

  ImGui::EndChild();
}

void AssetManager::renderMaterialProperties(Scene::AnyAsset &material,
                                            Scene::AssetID id) {
  Material *asset = std::get<Material *>(material);

  assetPropertiesHeader("Material", id);
  materialProperties(m_store.scene(), asset, id);
}

void AssetManager::renderMeshProperties(Scene::AnyAsset &mesh,
                                        Scene::AssetID id) {
  Mesh *asset = std::get<Mesh *>(mesh);

  assetPropertiesHeader("Mesh", id);

  ImGui::Text("%lu vertices", asset->vertexCount());
  ImGui::Text("%lu triangles", asset->indexCount() / 3);
}

void AssetManager::assetPropertiesHeader(const char *assetTypeName,
                                         Scene::AssetID id) {
  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s [id: %llu]", assetTypeName, id);

  auto users = std::format("{} users", m_store.scene().getAssetRc(id));
  auto availableWidth = ImGui::GetContentRegionAvail().x;
  ImGui::SameLine(availableWidth - ImGui::CalcTextSize(users.c_str()).x);
  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s", users.c_str());

  ImGui::Checkbox("Retain asset", &m_store.scene().assetRetained(id));

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();
}

} // namespace pt::frontend::windows
