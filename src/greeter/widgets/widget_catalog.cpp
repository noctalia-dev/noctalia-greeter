#include "greeter/widgets/widget_catalog.h"

#include <algorithm>
#include <array>
#include <string>

namespace greeter {
  namespace {
    constexpr std::array kClockProviders{WidgetProvider::LocalTime};
    constexpr std::array kStickerProviders{WidgetProvider::ManagedImages};
    constexpr std::array kSysmonProviders{WidgetProvider::LocalMetrics};
    constexpr std::array kButtonProviders{WidgetProvider::GreeterActions};

    constexpr std::array<WidgetTypeDefinition, 5> kBuiltinWidgets{{
        {"clock", WidgetKind::Clock, kClockProviders},
        {"label", WidgetKind::Label, {}},
        {"sticker", WidgetKind::Sticker, kStickerProviders},
        {"sysmon", WidgetKind::Sysmon, kSysmonProviders},
        {"button", WidgetKind::Button, kButtonProviders},
    }};

    constexpr std::array<WidgetActionDefinition, 8> kActions{{
        {"none", WidgetAction::None},
        {"power", WidgetAction::Power},
        {"appearance", WidgetAction::Appearance},
        {"sessions", WidgetAction::Sessions, true},
        {"suspend", WidgetAction::Suspend},
        {"reboot", WidgetAction::Reboot},
        {"shutdown", WidgetAction::Shutdown},
        {"firmware", WidgetAction::Firmware},
    }};
  } // namespace

  std::span<const WidgetTypeDefinition> builtinWidgetDefinitions() { return kBuiltinWidgets; }

  const WidgetTypeDefinition* findBuiltinWidget(std::string_view type) {
    const auto* const found = std::ranges::find(kBuiltinWidgets, type, &WidgetTypeDefinition::type);
    return found == kBuiltinWidgets.end() ? nullptr : found;
  }

  std::span<const WidgetActionDefinition> widgetActionDefinitions() { return kActions; }

  std::optional<WidgetAction> parseWidgetAction(std::string_view name) {
    const auto* const found = std::ranges::find(kActions, name, &WidgetActionDefinition::name);
    return found == kActions.end() ? std::nullopt : std::make_optional(found->action);
  }

  std::vector<WidgetModelIssue> validateWidgetAdmission(const WidgetInstance& widget) {
    std::vector<WidgetModelIssue> issues;
    const std::string path = "widgets.widget." + widget.id;
    const auto* definition = findBuiltinWidget(widget.type);
    if (definition == nullptr) {
      issues.push_back({path + ".type", "type is unavailable in the greeter catalog: " + widget.type});
      return issues;
    }
    if (widget.settings.contains("command"))
      issues.push_back({path + ".settings.command", "desktop commands are unavailable in greeter widgets"});
    if (const auto action = widget.settings.find("action"); action != widget.settings.end()) {
      if (definition->kind != WidgetKind::Button) {
        issues.push_back({path + ".settings.action", "only button widgets accept an action"});
      } else if (const auto* name = std::get_if<std::string>(&action->second); name == nullptr) {
        issues.push_back({path + ".settings.action", "expected an action name string"});
      } else if (!parseWidgetAction(*name)) {
        issues.push_back({path + ".settings.action", "unsupported greeter widget action: " + *name});
      }
    }
    return issues;
  }

} // namespace greeter
