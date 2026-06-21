#pragma once

#include <optional>
#include <string>

namespace greeter {

  inline constexpr const char* kGreeterUserEnv = "GREETER_USER";
  inline constexpr const char* kDefaultGreeterUser = "greeter";

  // Greetd session user for install/setup (from greetd config or GREETER_USER).
  [[nodiscard]] std::optional<std::string> resolveGreeterAccountName();

} // namespace greeter
