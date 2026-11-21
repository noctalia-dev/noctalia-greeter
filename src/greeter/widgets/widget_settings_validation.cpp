#include "greeter/widgets/widget_catalog.h"
#include "greeter/widgets/widget_settings.h"
#include "render/core/color.h"
#include "ui/palette.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>

namespace greeter {
  namespace {
    std::optional<double> number(const WidgetSettingValue& value) {
      if (const auto* floating = std::get_if<double>(&value))
        return *floating;
      if (const auto* integer = std::get_if<std::int64_t>(&value))
        return static_cast<double>(*integer);
      return std::nullopt;
    }

    const char* settingError(const WidgetSettingDefinition& definition, const WidgetSettingValue& value) {
      switch (definition.type) {
      case WidgetSettingType::Bool:
        return std::holds_alternative<bool>(value) ? nullptr : "expected a boolean";
      case WidgetSettingType::Int:
        if (!std::holds_alternative<std::int64_t>(value))
          return "expected an integer";
        [[fallthrough]];
      case WidgetSettingType::Double: {
        const auto numeric = number(value);
        if (!numeric
            || !std::isfinite(*numeric)
            || (definition.minValue && *numeric < *definition.minValue)
            || (definition.maxValue && *numeric > *definition.maxValue))
          return "expected a finite number within the setting's bounds";
        return nullptr;
      }
      case WidgetSettingType::String:
        return std::holds_alternative<std::string>(value) ? nullptr : "expected a string";
      case WidgetSettingType::Color: {
        const auto* text = std::get_if<std::string>(&value);
        if (text == nullptr)
          return "expected a color string";
        const auto* defaultColor = std::get_if<std::string>(&definition.defaultValue);
        if (text->empty() && defaultColor != nullptr && defaultColor->empty())
          return nullptr;
        Color color;
        if (tryParseHexColor(*text, color) || colorRoleFromToken(*text))
          return nullptr;
        return "expected a palette role or hex color";
      }
      }
      return "unsupported setting type";
    }
  } // namespace

  std::vector<WidgetModelIssue> validateWidgetSettings(const WidgetInstance& widget) {
    auto issues = validateWidgetAdmission(widget);
    if (!issues.empty())
      return issues;
    const std::string path = "widgets.widget." + widget.id + ".settings";
    const auto definitions = widgetSettingDefinitions(widget.type);
    if (!definitions)
      return {{path, "settings schema is not implemented for this widget type"}};
    const auto issue = [&](std::string_view key, const char* message) {
      std::string fieldPath = path;
      fieldPath += '.';
      fieldPath += key;
      issues.push_back({std::move(fieldPath), message});
    };
    for (const auto& [key, value] : widget.settings) {
      const auto definition = std::ranges::find(*definitions, key, &WidgetSettingDefinition::key);
      if (definition == definitions->end()) {
        issue(key, "setting is unavailable for this greeter widget");
      } else if (const char* message = settingError(*definition, value)) {
        issue(key, message);
      }
    }
    return issues;
  }

} // namespace greeter
