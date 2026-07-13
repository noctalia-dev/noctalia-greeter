#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace greeter::config {

  struct GreeterConfigFile {
    std::optional<std::string> sessionDefault;
    std::optional<std::string> sessionLast;

    std::optional<std::string> userDefault;

    std::optional<std::string> appearanceScheme;
    std::optional<std::string> appearancePasswordStyle;
    std::optional<bool> appearanceHideLogo;

    std::optional<std::string> outputName;
    std::optional<std::string> outputLayout;
    std::optional<float> outputScale;
    std::optional<int> outputModeWidth;
    std::optional<int> outputModeHeight;
    // Per-output DRM transforms: "NAME:90; NAME2:180" (see compositor parse).
    std::optional<std::string> outputTransforms;

    std::optional<int> idleTimeoutSec;

    std::optional<std::string> cursorTheme;
    std::optional<int> cursorSize;
    std::optional<std::string> cursorPath;

    std::optional<std::string> keyboardLayout;
    std::optional<std::string> keyboardVariant;
    std::optional<std::string> keyboardOptions;
    std::optional<bool> keyboardNumlock;

    std::optional<bool> authAllowEmptyPassword;
  };

  [[nodiscard]] GreeterConfigFile loadConfig(const std::filesystem::path& path);
  [[nodiscard]] bool writeConfig(const std::filesystem::path& path, const GreeterConfigFile& config);

} // namespace greeter::config
