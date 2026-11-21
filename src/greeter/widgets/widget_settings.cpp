#include "greeter/widgets/widget_settings.h"

#include <array>
#include <string>
#include <vector>

namespace greeter {
  namespace {
    const std::array<WidgetSettingDefinition, 5> kCommonSettings{{
        {"background", WidgetSettingType::Bool, true},
        {"background_color", WidgetSettingType::Color, std::string("surface")},
        {"background_opacity", WidgetSettingType::Double, 0.8, 0, 1, 0.01},
        {"background_radius", WidgetSettingType::Int, std::int64_t{12}, 0, 32, 1},
        {"background_padding", WidgetSettingType::Int, std::int64_t{10}, 0, 32, 1},
    }};

    std::span<const WidgetSettingDefinition> labelSettings() {
      static const auto fields = [] {
        std::vector<WidgetSettingDefinition> result{
            {"title", WidgetSettingType::String, std::string("Title")},
            {"description", WidgetSettingType::String, std::string{}},
            {"color", WidgetSettingType::Color, std::string("on_surface")},
            {"opacity", WidgetSettingType::Double, 1.0, 0, 1, 0.01},
            {"font_family", WidgetSettingType::String, std::string{}},
            {"shadow", WidgetSettingType::Bool, true},
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
    return std::nullopt;
  }

} // namespace greeter
