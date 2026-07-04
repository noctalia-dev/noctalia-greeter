#pragma once

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
    bool hideLogo = false;
  };

  [[nodiscard]] std::filesystem::path greeterConfPath();

  [[nodiscard]] GreeterPreferences loadGreeterPreferences();
  [[nodiscard]] bool saveGreeterPreferences(const GreeterPreferences& prefs);

  // [output].layout in greeter.toml: "NAME:X,Y; ..." (logical pixels, compositor + client).
  [[nodiscard]] std::vector<GreeterOutputPlacement> loadGreeterOutputLayout();

  // Sets [appearance].scheme to Synced; updates [output].layout only when stagedLayout is set.
  [[nodiscard]] bool applyAppearanceSyncGreeterConf(const std::optional<std::string>& stagedOutputLayout);

  // greetd/CLI default (--session / --cmd); overrides greeter.toml
  // default_session.
  void setCliDefaultSession(std::optional<std::string> session);

  // greetd/CLI default (--user); overrides greeter.toml default_user.
  void setCliDefaultUser(std::optional<std::string> user);

  // CLI default → default_session → session (last used).
  [[nodiscard]] std::optional<std::string> resolveInitialSessionName(const GreeterPreferences& prefs);

  // CLI default → default_user.
  [[nodiscard]] std::optional<std::string> resolveInitialUserName(const GreeterPreferences& prefs);

  // Root only: synced data dir, greeter.toml, chown conf to greeterUser.
  [[nodiscard]] bool installGreeterSystemLayout(std::string_view greeterUser, std::string& errorOut);

} // namespace greeter
