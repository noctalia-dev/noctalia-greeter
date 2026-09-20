#include "greeter/greeter_sessions.h"

#include <cstddef>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

namespace {

  [[nodiscard]] bool expectIndex(
      const std::vector<greeter::SessionOption>& sessions, const std::string_view query,
      const std::optional<std::size_t> expected
  ) {
    const std::optional<std::size_t> actual = greeter::findSessionIndex(sessions, query);
    if (actual == expected) {
      return true;
    }
    std::cerr << "findSessionIndex(\"" << query << "\"): expected ";
    if (expected) {
      std::cerr << *expected;
    } else {
      std::cerr << "nullopt";
    }
    std::cerr << ", got ";
    if (actual) {
      std::cerr << *actual;
    } else {
      std::cerr << "nullopt";
    }
    std::cerr << '\n';
    return false;
  }

} // namespace

int main() {
  const std::vector<greeter::SessionOption> sessions = {
      {.name = "Hyprland (uwsm-managed)",
       .desktopId = "hyprland-uwsm",
       .command = "uwsm start hyprland",
       .desktopNames = "Hyprland"},
      // Name= of this entry collides with the filename of the next one.
      {.name = "sway", .desktopId = "sway-custom", .command = "sway-custom", .desktopNames = "sway"},
      {.name = "Sway (custom)", .desktopId = "sway", .command = "sway", .desktopNames = "sway"},
      {.name = "Shell", .desktopId = {}, .command = "/bin/sh", .desktopNames = {}, .sessionType = "tty"},
  };

  bool ok = true;
  ok &= expectIndex(sessions, "Hyprland (uwsm-managed)", 0);
  ok &= expectIndex(sessions, "hyprland-uwsm", 0);
  ok &= expectIndex(sessions, "HYPRLAND-UWSM", 0);
  ok &= expectIndex(sessions, "sway", 1);
  ok &= expectIndex(sessions, "sway-custom", 1);
  ok &= expectIndex(sessions, "Sway (custom)", 2);
  ok &= expectIndex(sessions, "plasma", std::nullopt);
  ok &= expectIndex(sessions, "hyprland-uwsm.desktop", std::nullopt);

  return ok ? 0 : 1;
}
