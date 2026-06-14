#pragma once

#include "config/config_types.h"
#include "ui/palette.h"

#include <filesystem>
#include <optional>
#include <string>

struct Color;

struct GreeterSyncedAppearance {
  Palette palette{};
  std::string themeMode;
  std::string wallpaperPath;
  WallpaperFillMode wallpaperFillMode = WallpaperFillMode::Crop;
  Color wallpaperFillColor = rgba(0.0f, 0.0f, 0.0f, 0.0f);
  float cornerRadiusScale = 1.0f;
};

[[nodiscard]] std::filesystem::path greeterAppearanceConfigPath();
[[nodiscard]] std::optional<GreeterSyncedAppearance> loadGreeterSyncedAppearance();
