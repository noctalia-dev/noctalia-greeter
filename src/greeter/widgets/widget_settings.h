#pragma once

#include "greeter/widgets/widget_model.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace greeter {

  enum class WidgetSettingType : std::uint8_t { Bool, Int, Double, String, Color, Enum };

  struct WidgetSettingDefinition {
    std::string_view key;
    WidgetSettingType type;
    WidgetSettingValue defaultValue;
    std::optional<double> minValue = std::nullopt;
    std::optional<double> maxValue = std::nullopt;
    // Editor increment; floating-point values are not quantized during validation.
    std::optional<double> step = std::nullopt;
    std::span<const std::string_view> enumValues;
  };

  // Background fields for applicable types. Button owns its own presentation fields.
  std::span<const WidgetSettingDefinition> commonWidgetSettingDefinitions();
  // Unset means the per-type schema is not implemented, not an empty valid schema.
  std::optional<std::span<const WidgetSettingDefinition>> widgetSettingDefinitions(std::string_view type);

  // Admission and complete per-type settings checks; does not mutate values or fill defaults.
  // Structural geometry/identity checks remain in validateWidgetStructure().
  // Image references receive string checks only; asset provenance and contents require separate validation.
  std::vector<WidgetModelIssue> validateWidgetSettings(const WidgetInstance& widget);

} // namespace greeter
