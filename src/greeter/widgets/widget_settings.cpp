#include "greeter/widgets/widget_settings.h"

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace greeter {
  namespace {
    WidgetSettingDefinition setting(
        std::string_view key, WidgetSettingType type, WidgetSettingValue defaultValue,
        std::optional<double> min = std::nullopt, std::optional<double> max = std::nullopt,
        std::optional<double> step = std::nullopt, std::span<const std::string_view> enumValues = {}
    ) {
      return {key, type, std::move(defaultValue), min, max, step, enumValues};
    }

    constexpr std::array<std::string_view, 2> kClockStyles{"digital", "analog"};
    const std::array<WidgetSettingDefinition, 5> kCommonSettings{{
        setting("background", WidgetSettingType::Bool, true),
        setting("background_color", WidgetSettingType::Color, std::string("surface")),
        setting("background_opacity", WidgetSettingType::Double, 0.8, 0, 1, 0.01),
        setting("background_radius", WidgetSettingType::Int, std::int64_t{12}, 0, 32, 1),
        setting("background_padding", WidgetSettingType::Int, std::int64_t{10}, 0, 32, 1),
    }};

    std::span<const WidgetSettingDefinition> labelSettings() {
      static const auto fields = [] {
        std::vector<WidgetSettingDefinition> result{
            setting("title", WidgetSettingType::String, std::string("Title")),
            setting("description", WidgetSettingType::String, std::string{}),
            setting("color", WidgetSettingType::Color, std::string("on_surface")),
            setting("opacity", WidgetSettingType::Double, 1.0, 0, 1, 0.01),
            setting("font_family", WidgetSettingType::String, std::string{}),
            setting("shadow", WidgetSettingType::Bool, true),
        };
        result.insert(result.end(), kCommonSettings.begin(), kCommonSettings.end());
        return result;
      }();
      return fields;
    }

    std::span<const WidgetSettingDefinition> clockSettings() {
      static const auto fields = [] {
        std::vector<WidgetSettingDefinition> result{
            setting("clock_style", WidgetSettingType::Enum, std::string("digital"), {}, {}, {}, kClockStyles),
            setting("format", WidgetSettingType::String, std::string("{:%H:%M}")),
            setting("center_text", WidgetSettingType::Bool, true),
            setting("timezone", WidgetSettingType::String, std::string{}),
            setting("color", WidgetSettingType::Color, std::string("on_surface")),
            setting("font_family", WidgetSettingType::String, std::string{}),
            setting("shadow", WidgetSettingType::Bool, true),
            setting("circle", WidgetSettingType::Bool, true),
            setting("show_seconds", WidgetSettingType::Bool, true),
        };
        result.insert(result.end(), kCommonSettings.begin(), kCommonSettings.end());
        return result;
      }();
      return fields;
    }
  } // namespace

  std::span<const WidgetSettingDefinition> commonWidgetSettingDefinitions() { return kCommonSettings; }

  std::optional<std::span<const WidgetSettingDefinition>> widgetSettingDefinitions(std::string_view type) {
    if (type == "label")
      return labelSettings();
    if (type == "clock")
      return clockSettings();
    return std::nullopt;
  }

} // namespace greeter
