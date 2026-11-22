#pragma once

#include "greeter/greeter_config_store.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace greeter::secure_sync {
  // Filename-only check, also used before privilege drop. No image parsing.
  bool isWidgetImageFileName(std::string_view name);

  struct CustomizationUpdate {
    std::optional<GreeterLayoutConfig> layout;
    std::optional<WidgetCollection> widgets;
    std::vector<std::string> imageHashes;
    bool present() const { return layout.has_value() || widgets.has_value(); }
  };

  // Only on the private, post-drop snapshot. Validates and normalizes staged assets.
  bool prepareCustomization(const std::filesystem::path& staging, CustomizationUpdate& update, std::string& errorOut);
  bool installCustomization(
      const std::filesystem::path& staging, CustomizationUpdate update,
      const std::unordered_set<std::string>& wallpaperFiles, std::string& errorOut
  );
} // namespace greeter::secure_sync
