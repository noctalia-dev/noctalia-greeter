#pragma once

#include <filesystem>
#include <string_view>

namespace greeter::passwordless_sync::detail {

  [[nodiscard]] bool
  validateConstrainedPolicyXml(std::string_view contents, const std::filesystem::path& expectedHelper);

} // namespace greeter::passwordless_sync::detail
