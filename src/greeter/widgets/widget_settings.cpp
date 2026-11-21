#include "greeter/widgets/widget_settings.h"

#include "greeter/widgets/widget_catalog.h"

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
    constexpr std::array<std::string_view, 6> kButtonVariants{"default", "primary", "secondary",
                                                              "outline", "ghost",   "destructive"};
    constexpr std::array<std::string_view, 10> kSysmonStatsWithNone{"",         "cpu_usage",     "cpu_temp",
                                                                    "cpu_freq", "gpu_temp",      "gpu_usage",
                                                                    "gpu_vram", "gpu_vram_used", "ram_pct",
                                                                    "swap_pct"};
    constexpr std::array<std::string_view, 2> kSysmonDisplays{"graph", "gauge"};
    constexpr std::array<std::string_view, 2> kSysmonGaugeLayouts{"horizontal", "vertical"};
    const std::array<WidgetSettingDefinition, 5> kCommonSettings{{
        setting("background", WidgetSettingType::Bool, true),
        setting("background_color", WidgetSettingType::Color, std::string("surface")),
        setting("background_opacity", WidgetSettingType::Double, 0.8, 0, 1, 0.01),
        setting("background_radius", WidgetSettingType::Int, std::int64_t{12}, 0, 32, 1),
        setting("background_padding", WidgetSettingType::Int, std::int64_t{10}, 0, 32, 1),
    }};

    std::span<const WidgetSettingDefinition> buttonSettings() {
      static const auto actions = [] {
        std::vector<std::string_view> result;
        for (const auto& action : widgetActionDefinitions())
          result.push_back(action.name);
        return result;
      }();
      static const std::array<WidgetSettingDefinition, 8> fields{{
          setting("background", WidgetSettingType::Bool, true),
          setting("glyph", WidgetSettingType::String, std::string("heart")),
          setting("label", WidgetSettingType::String, std::string{}),
          setting("variant", WidgetSettingType::Enum, std::string("default"), {}, {}, {}, kButtonVariants),
          setting("color", WidgetSettingType::Color, std::string{}),
          setting("hover_background", WidgetSettingType::Color, std::string("hover")),
          setting("font_family", WidgetSettingType::String, std::string{}),
          setting("action", WidgetSettingType::Enum, std::string("none"), {}, {}, {}, actions),
      }};
      return fields;
    }

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

    std::span<const WidgetSettingDefinition> stickerSettings() {
      static const auto fields = [] {
        std::vector<WidgetSettingDefinition> result{
            setting("image_path", WidgetSettingType::String, std::string{}),
            setting("opacity", WidgetSettingType::Double, 1.0, 0, 1, 0.01),
        };
        result.insert(result.end(), kCommonSettings.begin(), kCommonSettings.end());
        return result;
      }();
      return fields;
    }

    std::span<const WidgetSettingDefinition> sysmonSettings() {
      static const auto fields = [] {
        std::vector<WidgetSettingDefinition> result{
            setting(
                "stat", WidgetSettingType::Enum, std::string("cpu_usage"), {}, {}, {},
                std::span{kSysmonStatsWithNone}.subspan(1)
            ),
            setting("stat2", WidgetSettingType::Enum, std::string{}, {}, {}, {}, kSysmonStatsWithNone),
            setting("display", WidgetSettingType::Enum, std::string("graph"), {}, {}, {}, kSysmonDisplays),
            setting(
                "gauge_layout", WidgetSettingType::Enum, std::string("horizontal"), {}, {}, {}, kSysmonGaugeLayouts
            ),
            setting("color", WidgetSettingType::Color, std::string("primary")),
            setting("color2", WidgetSettingType::Color, std::string("secondary")),
            setting("highlight_color", WidgetSettingType::Color, std::string("error")),
            setting("font_family", WidgetSettingType::String, std::string{}),
            setting("show_label", WidgetSettingType::Bool, true),
            setting("label_min_width", WidgetSettingType::Int, std::int64_t{0}, 0, 200, 1),
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
    if (type == "sticker")
      return stickerSettings();
    if (type == "sysmon")
      return sysmonSettings();
    if (type == "button")
      return buttonSettings();
    return std::nullopt;
  }

} // namespace greeter
