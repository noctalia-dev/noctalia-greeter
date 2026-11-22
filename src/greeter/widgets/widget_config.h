#pragma once

#include "greeter/widgets/widget_model.h"

#include <expected>
#include <optional>
#include <toml++/toml.hpp>

namespace greeter {

  // Table conversion only: no files, precedence resolution or runtime activation.
  // Absence leaves widgets unmanaged; an empty table requests the default collection.
  std::expected<std::optional<WidgetCollection>, std::vector<WidgetModelIssue>>
  readWidgetCollection(const toml::table& root);

  // Replaces/removes only [widgets]. Invalid input leaves the entire table untouched.
  std::expected<void, std::vector<WidgetModelIssue>>
  writeWidgetCollection(toml::table& root, const std::optional<WidgetCollection>& collection);

} // namespace greeter
