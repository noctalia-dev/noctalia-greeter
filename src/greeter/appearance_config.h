#pragma once

#include "config/config_types.h"
#include "ui/palette.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

struct Color;

struct GreeterOutputWallpaper {
  std::string path;
  WallpaperFillMode fillMode = WallpaperFillMode::Crop;
  Color fillColor = rgba(0.0f, 0.0f, 0.0f, 0.0f);
};

struct GreeterSyncedAppearance {
  Palette palette{};
  std::string themeMode;
  std::string wallpaperPath;
  WallpaperFillMode wallpaperFillMode = WallpaperFillMode::Crop;
  Color wallpaperFillColor = rgba(0.0f, 0.0f, 0.0f, 0.0f);
  // connector name -> wallpaper (from appearance.json "wallpapers")
  std::unordered_map<std::string, GreeterOutputWallpaper> wallpapersByOutput;
  float cornerRadiusScale = 1.0f;

  [[nodiscard]] GreeterOutputWallpaper wallpaperForOutput(std::string_view outputName) const {
    if (!outputName.empty()) {
      const auto it = wallpapersByOutput.find(std::string(outputName));
      if (it != wallpapersByOutput.end() && !it->second.path.empty()) {
        return it->second;
      }
    }
    GreeterOutputWallpaper fallback;
    fallback.path = wallpaperPath;
    fallback.fillMode = wallpaperFillMode;
    fallback.fillColor = wallpaperFillColor;
    return fallback;
  }
};

[[nodiscard]] std::filesystem::path greeterAppearanceConfigPath();
[[nodiscard]] std::optional<GreeterSyncedAppearance> loadGreeterSyncedAppearance();
