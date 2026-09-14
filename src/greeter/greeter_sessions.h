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
    // Desktop Entry Exec= with field codes stripped (may contain multiple argv tokens).
    std::string command;
    // From DesktopNames= (desktop-entry ;-list). Empty when unset (synthetic Shell).
    std::string desktopNames;
    // "wayland" for wayland-sessions entries, "x11" for xsessions entries, "tty" for the
    // synthetic Shell fallback.
    std::string sessionType = "wayland";
  };

  [[nodiscard]] std::vector<SessionOption> discoverSessions();

  // Match Wayland .desktop Name=; comparison is case-insensitive.
  [[nodiscard]] std::optional<std::size_t>
  findSessionIndex(const std::vector<SessionOption>& sessions, std::string_view name);

  // Env for greetd start_session (must be set before PAM opens the session).
  [[nodiscard]] std::vector<GreetdEnvironmentEntry> sessionStartEnvironment(const SessionOption& session);

  // Full argv to execute for this session: the split Exec= tokens for wayland/tty
  // sessions, or the same tokens wrapped in noctalia-greeter-xsession for x11 sessions
  // (which need Xorg bootstrapped first). Empty when Exec= is empty or whitespace-only.
  [[nodiscard]] std::vector<std::string> sessionArgv(const SessionOption& session);

} // namespace greeter
