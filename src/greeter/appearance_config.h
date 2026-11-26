#pragma once

#include "config/config_types.h"
#include "ui/palette.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

struct Color;

struct GreeterOutputWallpaper {
  std::string path;
  WallpaperFillMode fillMode = WallpaperFillMode::Crop;
  Color fillColor = rgba(0.0f, 0.0f, 0.0f, 0.0f);
  // Intensities in [0, 1]; 0 disables the effect.
  float blur = 0.0f;
  float tint = 0.0f;
};

struct GreeterWallpaperAppearance {
  std::optional<GreeterOutputWallpaper> wallpaper;
  std::unordered_map<std::string, GreeterOutputWallpaper> wallpapersByOutput;

  [[nodiscard]] std::optional<GreeterOutputWallpaper> wallpaperForOutput(std::string_view outputName) const {
    if (!outputName.empty()) {
      const auto it = wallpapersByOutput.find(std::string(outputName));
      if (it != wallpapersByOutput.end()) {
        return it->second;
      }
    }
    return wallpaper;
  }
};

struct GreeterSyncedAppearance {
  Palette palette{};
  std::string themeMode;
  float cornerRadiusScale = 1.0f;
  // From appearance.json "font_family"; empty means leave the greeter default.
  std::string fontFamily;
};

// Legacy Sync appearance.json path; used only for the one-shot migration into sync.toml.
[[nodiscard]] std::filesystem::path greeterAppearanceConfigPath();

// Synced scheme source, in order: greeter.toml embedded appearance (if its palette is
// complete), else sync.toml [appearance] (Sync-owned, if its palette is complete), else the
// legacy live appearance.json — migrated into sync.toml once when found.
[[nodiscard]] std::optional<GreeterSyncedAppearance> loadGreeterSyncedAppearance();

// Wallpaper source is independent of the selected color scheme. Declarative
// greeter.toml values override matching Sync-owned sync.toml values.
[[nodiscard]] std::optional<GreeterWallpaperAppearance> loadGreeterWallpaperAppearance();
