#pragma once

#include "greetd/greetd_client.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace greeter {

  struct SessionOption {
    std::string name;
    // .desktop filename without the extension. Empty for the synthetic Shell fallback.
    std::string desktopId;
    // Desktop Entry Exec= with field codes stripped (may contain multiple argv tokens).
    std::string command;
    // From DesktopNames= (desktop-entry ;-list). Empty when unset (synthetic Shell).
    std::string desktopNames;
    // "wayland" for wayland-sessions entries; "tty" for the synthetic Shell fallback.
    std::string sessionType = "wayland";
  };

  [[nodiscard]] std::vector<SessionOption> discoverSessions();

  // Match Wayland .desktop Name= first, then fall back to the .desktop filename stem
  // (SessionOption::desktopId). Both comparisons are case-insensitive.
  [[nodiscard]] std::optional<std::size_t>
  findSessionIndex(const std::vector<SessionOption>& sessions, std::string_view name);

  // Env for greetd start_session (must be set before PAM opens the session).
  [[nodiscard]] std::vector<GreetdEnvironmentEntry> sessionStartEnvironment(const SessionOption& session);

} // namespace greeter
