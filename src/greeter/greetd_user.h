#pragma once

#include <optional>
#include <string>

namespace greeter {

  inline constexpr const char* kGreeterUserEnv = "GREETER_USER";
  inline constexpr const char* kDefaultGreeterUser = "greeter";

  // Greetd session user for install/setup (logs, greeter.conf).
  [[nodiscard]] std::optional<std::string> resolveGreeterAccountName();

} // namespace greeter
