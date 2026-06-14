#include "greeter/appearance_config.h"

#include "core/log.h"
#include "greeter/appearance_sync.h"
#include "render/core/color.h"

#include <fstream>
#include <json.hpp>

namespace {
  constexpr Logger kLog("greeter-appearance");

  bool parsePaletteColor(const nlohmann::json& palette, std::string_view key, Color& out) {
    const auto it = palette.find(std::string(key));
    if (it == palette.end() || !it->is_string()) {
      return false;
    }
    return tryParseHexColor(it->get<std::string>(), out);
  }

  bool parseColorWallpaperPath(std::string_view path, Color& out) {
    constexpr std::string_view kPrefix = "color:";
    if (!path.starts_with(kPrefix)) {
      return false;
    }
    return tryParseHexColor(path.substr(kPrefix.size()), out);
  }

} // namespace

std::filesystem::path greeterAppearanceConfigPath() { return greeter::appearance::manifestPath(); }

std::optional<GreeterSyncedAppearance> loadGreeterSyncedAppearance() {
  const auto path = greeterAppearanceConfigPath();
  std::error_code ec;
  if (!std::filesystem::exists(path, ec) || ec) {
    return std::nullopt;
  }

  std::ifstream in(path);
  if (!in.is_open()) {
    kLog.warn("failed to open synced appearance '{}'", path.string());
    return std::nullopt;
  }

  try {
    const auto root = nlohmann::json::parse(in);
    if (!root.is_object()) {
      return std::nullopt;
    }

    const int version = root.value("version", 0);
    if (version != greeter::appearance::kManifestVersion) {
      kLog.warn("unsupported synced appearance version {} in '{}'", version, path.string());
      return std::nullopt;
    }

    GreeterSyncedAppearance appearance;
    appearance.themeMode = root.value("theme_mode", "dark");
    appearance.cornerRadiusScale = root.value("corner_radius_scale", 1.0f);

    const auto paletteIt = root.find("palette");
    if (paletteIt == root.end() || !paletteIt->is_object()) {
      kLog.warn("synced appearance '{}' is missing palette", path.string());
      return std::nullopt;
    }

    const auto& paletteJson = *paletteIt;
    if (!parsePaletteColor(paletteJson, "primary", appearance.palette.primary)
        || !parsePaletteColor(paletteJson, "on_primary", appearance.palette.onPrimary)
        || !parsePaletteColor(paletteJson, "secondary", appearance.palette.secondary)
        || !parsePaletteColor(paletteJson, "on_secondary", appearance.palette.onSecondary)
        || !parsePaletteColor(paletteJson, "tertiary", appearance.palette.tertiary)
        || !parsePaletteColor(paletteJson, "on_tertiary", appearance.palette.onTertiary)
        || !parsePaletteColor(paletteJson, "error", appearance.palette.error)
        || !parsePaletteColor(paletteJson, "on_error", appearance.palette.onError)
        || !parsePaletteColor(paletteJson, "surface", appearance.palette.surface)
        || !parsePaletteColor(paletteJson, "on_surface", appearance.palette.onSurface)
        || !parsePaletteColor(paletteJson, "surface_variant", appearance.palette.surfaceVariant)
        || !parsePaletteColor(paletteJson, "on_surface_variant", appearance.palette.onSurfaceVariant)
        || !parsePaletteColor(paletteJson, "outline", appearance.palette.outline)
        || !parsePaletteColor(paletteJson, "shadow", appearance.palette.shadow)
        || !parsePaletteColor(paletteJson, "hover", appearance.palette.hover)
        || !parsePaletteColor(paletteJson, "on_hover", appearance.palette.onHover)) {
      kLog.warn("synced appearance '{}' has incomplete palette", path.string());
      return std::nullopt;
    }

    const auto wallpaperIt = root.find("wallpaper");
    if (wallpaperIt != root.end() && wallpaperIt->is_object()) {
      const auto& wallpaper = *wallpaperIt;
      if (const auto pathValue = wallpaper.find("path"); pathValue != wallpaper.end() && pathValue->is_string()) {
        appearance.wallpaperPath = pathValue->get<std::string>();
      }
      if (const auto fillMode = wallpaper.find("fill_mode"); fillMode != wallpaper.end() && fillMode->is_string()) {
        if (const auto parsed = greeter::appearance::parseFillMode(fillMode->get<std::string>())) {
          appearance.wallpaperFillMode = *parsed;
        }
      }
      if (const auto fillColor = wallpaper.find("fill_color"); fillColor != wallpaper.end() && fillColor->is_string()) {
        Color color;
        if (tryParseHexColor(fillColor->get<std::string>(), color)) {
          appearance.wallpaperFillColor = color;
        }
      } else if (!appearance.wallpaperPath.empty()) {
        Color color;
        if (parseColorWallpaperPath(appearance.wallpaperPath, color)) {
          appearance.wallpaperFillColor = color;
        }
      }
    }

    return appearance;
  } catch (const std::exception& e) {
    kLog.warn("failed to parse synced appearance '{}': {}", path.string(), e.what());
    return std::nullopt;
  }
}
