#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace greeter {

  struct SessionOption {
    std::string name;
    std::string command;
  };

  [[nodiscard]] std::vector<SessionOption> discoverSessions();

  // Match Wayland .desktop Name=; comparison is case-insensitive.
  [[nodiscard]] std::optional<std::size_t>
  findSessionIndex(const std::vector<SessionOption>& sessions, std::string_view name);

} // namespace greeter
