#pragma once

#include "greeter/greeter_config_store.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace greeter {

  struct GreeterOutputPlacement {
    std::string name;
    int32_t x = 0;
    int32_t y = 0;
  };

  enum class PasswordMaskStyle : std::uint8_t {
    Default,
    RandomIcons,
  };

  struct GreeterPreferences {
    std::optional<std::string> defaultSession;
    std::optional<std::string> defaultUser;
    std::optional<std::string> session;
    std::optional<std::string> scheme;
    std::optional<std::string> output;
    // Manual UI scale; unset or invalid → auto from display geometry.
    std::optional<float> scale;
    PasswordMaskStyle passwordMaskStyle = PasswordMaskStyle::Default;
    bool allowEmptyPassword = false;
    int authRequestTimeoutSec = 60;
    bool hideLogo = false;
    // UI element positioning: "hidden", "bottom-left", "bottom-right", "top-left", "top-right"
    std::optional<std::string> powerButtonsPosition;
    std::optional<std::string> schemeSelectorPosition;
  };

  [[nodiscard]] std::filesystem::path greeterConfPath();
  [[nodiscard]] std::filesystem::path greeterSyncPath();

  [[nodiscard]] GreeterPreferences loadGreeterPreferences();
  // Persists [session].last and [appearance].scheme to sync.toml only.
  [[nodiscard]] bool saveGreeterPreferences(const GreeterPreferences& prefs);

  // Declarative greeter.toml layout if set, else sync.toml (Sync).
  [[nodiscard]] std::vector<GreeterOutputPlacement> loadGreeterOutputLayout();

  // Sync-owned appearance + session power/menu payload to merge into sync.toml, replacing any
  // previously synced values wholesale (a staged sync.toml / legacy appearance.json is a full snapshot).
  struct GreeterSyncAppearanceUpdate {
    config::GreeterTomlAppearance appearance;
    bool replaceSession = true;
    std::optional<std::string> sessionPowerSuspend;
    std::optional<std::string> sessionPowerReboot;
    std::optional<std::string> sessionPowerShutdown;
    std::vector<config::GreeterSyncFile::SyncSessionAction> sessionActions;
  };

  // Sets sync.toml scheme to Synced; updates layout/transforms/scales only when staged; replaces the
  // Sync-owned appearance when `appearanceUpdate` is set. Session power/menu data is replaced
  // only when `appearanceUpdate.replaceSession` is true.
  [[nodiscard]] bool applyAppearanceSyncGreeterConf(
      const std::optional<std::string>& stagedOutputLayout, const std::optional<std::string>& stagedOutputTransforms,
      const std::optional<std::string>& stagedOutputScales,
      const std::optional<GreeterSyncAppearanceUpdate>& appearanceUpdate
  );

  // greetd/CLI default (--session / --cmd); overrides greeter.toml default.
  void setCliDefaultSession(std::optional<std::string> session);

  // greetd/CLI default (--user); overrides greeter.toml default_user.
  void setCliDefaultUser(std::optional<std::string> user);

  // CLI default → default_session → session (last used).
  [[nodiscard]] std::optional<std::string> resolveInitialSessionName(const GreeterPreferences& prefs);

  // CLI default → default_user.
  [[nodiscard]] std::optional<std::string> resolveInitialUserName(const GreeterPreferences& prefs);

  // Root only: state dir, greeter.toml + sync.toml, chown to greeterUser.
  [[nodiscard]] bool installGreeterSystemLayout(std::string_view greeterUser, std::string& errorOut);

} // namespace greeter
