#pragma once

#include "config/config_types.h"
#include "greeter/greeter_config_store.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace greeter::appearance {

  inline constexpr int kManifestVersion = 1;
  // State dir (/var/lib/noctalia-greeter by default):
  //   greeter.toml     — full declarative config (Nix-safe C+ overwrite)
  //   sync.toml        — Sync + UI mutable: palette/wallpaper/theme/font, session power/menu,
  //                      last session/scheme, synced layout/transforms/scales
  //   appearance.json  — legacy live file only (migrated into sync.toml on first load).
  // Staging: shell writes sync.toml (+ wallpapers, optional layout/transforms/scales text files).
  // Legacy shells may still stage appearance.json; apply accepts either.
  // Precedence: greeter.toml wins over sync.toml when both set.
  inline constexpr const char* kDefaultSyncedDataDir = "/var/lib/noctalia-greeter";
  inline constexpr const char* kManifestFileName = "appearance.json";
  inline constexpr const char* kOutputLayoutFileName = "output_layout";
  inline constexpr const char* kOutputTransformsFileName = "output_transforms";
  inline constexpr const char* kOutputScalesFileName = "output_scales";
  inline constexpr const char* kGreeterTomlFileName = "greeter.toml";
  inline constexpr const char* kSyncTomlFileName = "sync.toml";
  inline constexpr const char* kWallpaperBaseName = "wallpaper";
  inline constexpr const char* kSyncedSchemeDisplayName = "Synced";
  inline constexpr const char* kSyncedDataDirEnv = "NOCTALIA_GREETER_STATE_DIR";

  [[nodiscard]] std::filesystem::path syncedDataDirectory();
  [[nodiscard]] std::filesystem::path packageConfPath();
  [[nodiscard]] std::filesystem::path syncConfPath();
  // Legacy live manifest path. Only read for one-shot migration into sync.toml.
  [[nodiscard]] std::filesystem::path manifestPath();
  [[nodiscard]] std::filesystem::path stagingSyncTomlPath(const std::filesystem::path& stagingDirectory);
  // Legacy staged appearance.json path (older shell builds).
  [[nodiscard]] std::filesystem::path stagingManifestPath(const std::filesystem::path& stagingDirectory);
  [[nodiscard]] bool syncedAppearanceInstalled();

  // Inline: shared by translation units (e.g. the compositor) that don't link appearance_sync.cpp.
  [[nodiscard]] inline const std::vector<std::string_view>& requiredPaletteKeys() {
    static const std::vector<std::string_view> keys = {
        "primary", "on_primary", "secondary", "on_secondary", "tertiary",        "on_tertiary",
        "error",   "on_error",   "surface",   "on_surface",   "surface_variant", "on_surface_variant",
        "outline", "shadow",     "hover",     "on_hover",
    };
    return keys;
  }

  [[nodiscard]] std::optional<WallpaperFillMode> parseFillMode(std::string_view value);

  [[nodiscard]] bool validateStagingManifest(const std::filesystem::path& stagingDirectory, std::string& errorOut);

  // Installs wallpaper images into syncedDataDirectory() (no live appearance.json).
  [[nodiscard]] bool installFromStaging(const std::filesystem::path& stagingDirectory, std::string& errorOut);

  // Merges staged sync.toml (or legacy appearance.json) + optional layout/transforms/scales into live sync.toml.
  // The constrained Polkit path leaves privileged session command configuration unchanged.
  [[nodiscard]] bool applySyncedGreeterPreferences(
      const std::filesystem::path& stagingDirectory, bool includeSessionCommands, std::string& errorOut
  );

  // Root only: chown synced state dir, sync.toml, and wallpaper files to the greetd session user.
  [[nodiscard]] bool ensureSyncedDataOwnedByGreeter(std::string& errorOut);

  // Palette/wallpaper/theme + session power/menu data, ready to merge into a GreeterSyncFile.
  struct ManifestSyncPayload {
    config::GreeterTomlAppearance appearance;
    std::optional<std::string> sessionPowerSuspend;
    std::optional<std::string> sessionPowerReboot;
    std::optional<std::string> sessionPowerShutdown;
    std::vector<config::GreeterSyncFile::SyncSessionAction> sessionActions;
  };

  // Legacy JSON parser for old live/staged appearance.json.
  [[nodiscard]] std::optional<ManifestSyncPayload> parseManifestForSync(const std::filesystem::path& path);

} // namespace greeter::appearance
