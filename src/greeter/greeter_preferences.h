#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace greeter {

struct GreeterPreferences {
  std::optional<std::string> defaultSession;
  std::optional<std::string> session;
  std::optional<std::string> scheme;
  std::optional<std::string> output;
  // Manual UI scale; unset or invalid → auto from display geometry.
  std::optional<float> scale;
};

[[nodiscard]] std::filesystem::path greeterConfPath();

[[nodiscard]] GreeterPreferences loadGreeterPreferences();
[[nodiscard]] bool saveGreeterPreferences(const GreeterPreferences &prefs);

// greetd/CLI default (--session / --cmd); overrides greeter.conf
// default_session.
void setCliDefaultSession(std::optional<std::string> session);

// CLI default → default_session → session (last used).
[[nodiscard]] std::optional<std::string>
resolveInitialSessionName(const GreeterPreferences &prefs);

// Root only: synced data dir, greeter.conf, chown conf to greeterUser.
[[nodiscard]] bool installGreeterSystemLayout(std::string_view greeterUser,
                                              std::string &errorOut);

} // namespace greeter
