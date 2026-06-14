#pragma once

#include "config/config_types.h"

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace greeter::appearance {

  inline constexpr int kManifestVersion = 1;
  // Root-owned synced appearance (manifest + wallpaper). UI prefs: greeter.conf
  // in same dir.
  inline constexpr const char* kDefaultSyncedDataDir = "/var/lib/noctalia-greeter";
  inline constexpr const char* kManifestFileName = "appearance.json";
  inline constexpr const char* kGreeterConfFileName = "greeter.conf";
  inline constexpr const char* kWallpaperBaseName = "wallpaper";
  inline constexpr const char* kSyncedSchemeDisplayName = "Synced";
  inline constexpr const char* kSyncedDataDirEnv = "NOCTALIA_GREETER_STATE_DIR";

  [[nodiscard]] std::filesystem::path syncedDataDirectory();
  [[nodiscard]] std::filesystem::path packageConfPath();
  [[nodiscard]] std::filesystem::path manifestPath();
  [[nodiscard]] std::filesystem::path stagingManifestPath(const std::filesystem::path& stagingDirectory);
  [[nodiscard]] bool syncedAppearanceInstalled();

  [[nodiscard]] const std::vector<std::string_view>& requiredPaletteKeys();

  [[nodiscard]] std::optional<WallpaperFillMode> parseFillMode(std::string_view value);

  [[nodiscard]] bool validateStagingManifest(const std::filesystem::path& stagingDirectory, std::string& errorOut);

  // Root-owned install into syncedDataDirectory(). No chown, no prefs.
  [[nodiscard]] bool installFromStaging(const std::filesystem::path& stagingDirectory, std::string& errorOut);

} // namespace greeter::appearance
