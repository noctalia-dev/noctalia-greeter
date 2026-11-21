#pragma once

#include "greeter/widgets/widget_model.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace greeter {

  enum class WidgetKind : std::uint8_t { Clock, Label, Sticker, Sysmon, Button };
  enum class WidgetProvider : std::uint8_t { LocalTime, ManagedImages, LocalMetrics, GreeterActions };
  enum class WidgetAction : std::uint8_t { None, Power, Appearance, Sessions, Suspend, Reboot, Shutdown, Firmware };

  struct WidgetTypeDefinition {
    std::string_view type;
    WidgetKind kind;
    std::span<const WidgetProvider> providers;
  };

  struct WidgetActionDefinition {
    std::string_view name;
    WidgetAction action;
    bool needsSessionPicker = false;
  };

  // Target definitions do not advertise installed support. Renderer registration,
  // complete setting schemas and provider availability are separate requirements.
  std::span<const WidgetTypeDefinition> builtinWidgetDefinitions();
  const WidgetTypeDefinition* findBuiltinWidget(std::string_view type);
  std::span<const WidgetActionDefinition> widgetActionDefinitions();
  std::optional<WidgetAction> parseWidgetAction(std::string_view name);

  // Type/action admission only; callers also need structural and per-setting validation.
  // No commands are executed and missing button actions use the explicit "none" default.
  std::vector<WidgetModelIssue> validateWidgetAdmission(const WidgetInstance& widget);

} // namespace greeter
