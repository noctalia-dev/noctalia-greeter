#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace greeter::config {

  struct ConfigDiagnostic {
    std::filesystem::path path;
    std::size_t line = 0;
    std::size_t column = 0;
    std::string message;
  };

  struct GreeterTomlWallpaper {
    std::optional<std::string> path;
    std::optional<std::string> fillMode;
    std::optional<std::string> fillColor;
  };

  // Declarative Synced appearance under [appearance] in greeter.toml.
  struct GreeterTomlAppearance {
    std::optional<std::string> themeMode;
    std::optional<float> cornerRadiusScale;
    std::optional<std::string> fontFamily;
    // Hex strings keyed like appearance.json ("primary", "on_primary", …).
    std::unordered_map<std::string, std::string> palette;
    std::optional<GreeterTomlWallpaper> wallpaper;
    std::unordered_map<std::string, GreeterTomlWallpaper> wallpapers;

    [[nodiscard]] bool hasCompletePalette() const;
  };

  // Full declarative greeter.toml. UI and Sync never write this file.
  struct GreeterConfigFile {
    std::optional<std::string> sessionDefault;
    // Legacy only: migrated into sync.toml; stripped on greeter.toml write.
    std::optional<std::string> sessionLast;

    std::optional<std::string> userDefault;

    // Declarative scheme (overrides sync.toml last scheme when set).
    std::optional<std::string> appearanceScheme;
    std::optional<std::string> appearancePasswordStyle;
    std::optional<bool> appearanceHideLogo;
    // UI element positioning: "hidden", "bottom-left", "bottom-right", "top-left", "top-right"
    std::optional<std::string> appearancePowerButtonsPosition;
    std::optional<std::string> appearanceSchemeSelectorPosition;
    // Optional palette/wallpaper/font; wins over Sync sync.toml when complete.
    GreeterTomlAppearance appearance;

    std::optional<std::string> outputName;
    std::optional<std::string> outputLayout;
    std::optional<float> outputScale;
    std::optional<int> outputModeWidth;
    std::optional<int> outputModeHeight;
    std::optional<std::string> outputTransforms;
    // Per-connector scales (NAME:1.25; ...). Distinct from global outputScale.
    std::optional<std::string> outputScales;

    std::optional<int> idleTimeoutSec;

    std::optional<std::string> cursorTheme;
    std::optional<int> cursorSize;
    std::optional<std::string> cursorPath;

    std::optional<std::string> keyboardLayout;
    std::optional<std::string> keyboardVariant;
    std::optional<std::string> keyboardOptions;
    std::optional<bool> keyboardNumlock;

    std::optional<bool> authAllowEmptyPassword;
    std::optional<bool> authAutoLogin;
  };

  // Sync + UI mutable file (sync.toml). Never managed by Nix. Loses to greeter.toml.
  struct GreeterSyncFile {
    std::optional<std::string> sessionLast;
    std::optional<std::string> appearanceScheme;
    std::optional<std::string> outputLayout;
    std::optional<std::string> outputTransforms;
    std::optional<std::string> outputScales;

    // Sync-owned appearance (palette/wallpaper/theme/font); migrated from legacy appearance.json.
    GreeterTomlAppearance appearance;

    // Sync-owned session power actions; migrated from legacy appearance.json "session.power".
    std::optional<std::string> sessionPowerSuspend;
    std::optional<std::string> sessionPowerReboot;
    std::optional<std::string> sessionPowerShutdown;

    // Sync-owned session menu entries; migrated from legacy appearance.json "session.actions".
    struct SyncSessionAction {
      std::string action;
      std::optional<std::string> command;
      std::optional<std::string> label;
      std::optional<std::string> glyph;
    };
    std::vector<SyncSessionAction> sessionActions;
  };

  [[nodiscard]] GreeterConfigFile loadConfig(const std::filesystem::path& path);
  // Writes declarative keys only; never persists session.last.
  [[nodiscard]] bool writeConfig(const std::filesystem::path& path, const GreeterConfigFile& config);

  [[nodiscard]] GreeterSyncFile loadSync(const std::filesystem::path& path);
  [[nodiscard]] bool writeSync(const std::filesystem::path& path, const GreeterSyncFile& sync);

  // Parse failures are retained for the greeter UI, which can show a useful
  // on-screen diagnostic instead of silently falling back to defaults.
  void clearConfigDiagnostics();
  [[nodiscard]] const std::vector<ConfigDiagnostic>& configDiagnostics();

} // namespace greeter::config
